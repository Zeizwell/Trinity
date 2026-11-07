// RP-AY (doc/zx-trinity.md, 14а и п.15): режимы AY и FX - записи из слота -> тень -> чип, эффекты AY FX
// (движок engine/ayfx.c), чтение из слота - ответ RP из тени. Каркас режимов и настроек - под ATARI, обновление.
// Ядро 1: шина - на макете ответ железом (bus_hw.c, doc 14е), на рев А - прерыванием PIO (doc 14б); в цикле -
// «Звук»: события, эффекты, писатель в чип, сброс.
// Ядро 0: USB-консоль, светодиод.
#include "wd.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "config.h"
#include "modes.h"
#include "chip.h"
#include "card.h"
#include "psram.h"
#include "ws2812.pio.h"
#include "bus_dma.h"
#include "bus_hw.h"
#ifdef BOARD_REV_A
#include "bus_ay.pio.h"
#endif

#define IC_PULSE_US 100                 // /IC вниз при старте и сбросе: у YM2203 от 72 тактов, это 20 мкс на 3.5 МГц
#ifdef BOARD_REV_A
static PIO  bus_pio = pio1;                     // рев А: шина от CPLD, ответ - прерыванием (ниже)
static uint sm_wr, sm_rd;
#endif

// Тест чтения чипа (режим TEST): просит ядро 0 (клавиша t в консоли), делает ядро 1
static volatile bool test_req, test_done;
static volatile bool hold_req;
static volatile bool tune_req, tune_done;      // клавиша p: тест пауз (chip_tune)
static chip_tune_t   tune_res;                  // клавиша h: замер напряжений на ножках чипа (chip_hold)
static volatile int  boot_result = -1;          // тест при старте: -1 ещё нет, 0 - OK, 1 - ошибки (boot_res)
static volatile int  boot_stage;                // шаг старта ядра 1 (светодиод): 0 - не начал, 1 - писк, 2 - тест
static volatile bool svc_force;                     // консоль j: журнал служебных пакетов напечатать
static chip_test_t   test_res, boot_res;
static uint16_t      boot_pins;                // ножки RP к чипу с замыканием (chip_pins_check)
static uint8_t       boot_probe[3];            // до теста: R14, R15, статус (chip_probe)

static void test_run(void) {                      // ядро 1: дождаться очереди, тест, вернуть тень, режим AY
    if (!chip_idle()) return;                     //   (пока очередь не пуста - в следующий раз)
    busy_wait_us_32(100);                         //   чип закончил последнюю запись
    chip_test(0, &test_res);
    card_resend();
    test_req = false;
    test_done = true;
    mode_set(MODE_AY);
}

#ifdef BOARD_REV_A
// Ответ на чтение (рев А): данные - от карты
static void answer_update(void) { bus_ay_rd_set(bus_pio, sm_rd, card_answer()); }

// ---- Уровень «Шина» (doc 14б): прерывание PIO, высший приоритет, ядро 1. Всё, от чего зависит ответ Спектруму,
// считается здесь, ответ выставляется сразу после записи, которая его меняет. Нельзя: ждать, USB / printf, флеш.
// Такты обработчика - счётчиком SysTick (такт RP), самый долгий - в консоль (бюджет: до следующего обращения
// Спектрума, ~0.85 мкс на 14 МГц)
#ifndef SIM_HOST
#include "hardware/irq.h"
#endif
static volatile uint32_t irq_max_cyc;           // самый долгий обработчик шины, такты RP

static void bus_irq(void) {
    uint32_t t0 = cyc_now();
    while (!pio_sm_is_rx_fifo_empty(bus_pio, sm_wr)) {
        uint32_t w = pio_sm_get(bus_pio, sm_wr);    // снимок ножек от PIN_AY_D0 (раскладка - config.h)
        uint code = w >> (PIN_AY_C0 - PIN_AY_D0) & 3;       // cpld/rtl/top.v: 1 - #FFFD, 2 - #BFFD
        if (code == 1 || code == 2) card_slot_write(code == 2, (uint8_t)w);
        answer_update();
    }
    while (!pio_sm_is_rx_fifo_empty(bus_pio, sm_rd)) {   // метки PIO: данные прочитали
        uint32_t r = pio_sm_get(bus_pio, sm_rd);    // метка = байт, что был на шине
        if (chip_stat.reads < 4)                    // консоль: первые 4 чтения - регистр и что получил Спектрум
            chip_stat.rd_sel[chip_stat.reads] = card_sel(), chip_stat.rd_ans[chip_stat.reads] = (uint8_t)r;
        chip_stat.reads++;
        card_read_done();
        answer_update();
    }
    uint32_t d = (t0 - cyc_now()) & 0xFFFFFF;
    if (d > irq_max_cyc) irq_max_cyc = d;
}

static void bus_irq_on(void) {
    pio_set_irq0_source_enabled(bus_pio, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + sm_wr), true);
    pio_set_irq0_source_enabled(bus_pio, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + sm_rd), true);
    irq_set_exclusive_handler(PIO1_IRQ_0, bus_irq);
    irq_set_priority(PIO1_IRQ_0, 0);                // высший
    irq_set_enabled(PIO1_IRQ_0, true);              // на ядре 1: обработчик - здесь
}
#else
static void answer_update(void) {}               // макет: отвечает железо (bus_hw.c), ядро только сообщает ему
#endif

// ---- Уровень «Звук» (doc 14б): прерывание будильника RP, ниже шины. Будильник - на ближайший шаг эффекта
// (card_sound говорит, через сколько мкс); шина будит его сразу, когда кладёт событие (sound_kick)
#ifndef SIM_HOST
#include "hardware/timer.h"
static int snd_alarm = -1;

static void snd_alarm_cb(uint alarm) {
    uint32_t us = card_sound();
    if (hardware_alarm_set_target(alarm, delayed_by_us(get_absolute_time(), us)))
        hardware_alarm_force_irq(alarm);                        // срок уже прошёл - сразу снова
}

void sound_kick(void)   { if (snd_alarm >= 0) hardware_alarm_force_irq((uint)snd_alarm); }

// Такт чипов φM: на плате рев А его даёт RP (GP25, CLK_GPOUT3), на макете - слот MultiSound (не трогаем)
void phim_set(uint32_t hz) {
#ifdef BOARD_REV_A
    if (!hz) hz = cfg.phim_hz;
    clock_gpio_init(PIN_PHIM, CLOCKS_CLK_GPOUT3_CTRL_AUXSRC_VALUE_CLK_SYS, (float)clock_get_hz(clk_sys) / (float)hz);
#else
    (void)hz;
#endif
}

static void sound_on(void) {                    // на ядре 1: прерывание будильника - здесь
    snd_alarm = hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback((uint)snd_alarm, snd_alarm_cb);
    irq_set_priority(hardware_alarm_get_irq_num((uint)snd_alarm), 0x40);   // ниже шины (0)
    hardware_alarm_force_irq((uint)snd_alarm);
}
#else
void sound_kick(void) {}
void phim_set(uint32_t hz) { sim_phim_hz = hz; }
static void sound_on(void) {}
#endif

static void do_reset(void) {                      // /IC: сбросить всё (doc 14а); шина в это время не отвечает
    uint32_t s = save_and_disable_interrupts();
    mode_reset();
    card_reset();
    answer_update();
    restore_interrupts(s);
    chip_stat.resets++;
#ifdef BOARD_REV_A
    gpio_put(PIN_CHIP_IC, 0);                       // сброс и самих чипов
    busy_wait_us_32(IC_PULSE_US);
    gpio_put(PIN_CHIP_IC, 1);
#endif
}

// ---- Ядро 1: «Шина» - прерыванием, «Звук» - здесь, в цикле
// Проверка RP -> чип при включении: нота в канале A на cfg.boot_beep_ms, потом тишина
static void boot_beep(void) {
    if (!cfg.boot_beep_ms) return;
    boot_stage = 1;
    static const uint8_t on[][2] = { {0, 0xF8}, {1, 0x00}, {7, 0x3E}, {8, 15} };   // период #0F8: ~440 Гц при такте AY 1.75 МГц
    for (unsigned i = 0; i < sizeof on / sizeof on[0]; i++) chip_put(0, on[i][0], on[i][1]);
    uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < cfg.boot_beep_ms * 1000u) chip_poll();
    chip_put(0, 8, 0);
    chip_put(0, 7, 0x3F);
    while (!chip_idle()) chip_poll();
}

// Тест чипа при старте (cfg.boot_test): 256 случайных строк R0..R13 тем же путём, что музыка, и чтение обратно.
// Итог - светодиод (ядро 0: зелёный 3 с - OK, красный насовсем - ошибки), консоль, #E0..#E5 тени
// (ошибок мл / ст, плохие биты, первая ошибка: регистр, записали, прочитали). Шина в это время отвечает (прерывание),
// её события ждут в очереди
static void boot_check(void) {
    if (!cfg.boot_test) { boot_result = 0; return; }
    boot_stage = 2;
    while (!chip_idle()) chip_poll();
    boot_pins = chip_pins_check(0);
    chip_probe(0, boot_probe);
    chip_test(0, &boot_res);
    for (int c = 1; c < CHIP_N && !boot_res.errors; c++) {      // остальные чипы - тот же тест
        boot_pins |= chip_pins_check(c);
        chip_test(c, &boot_res);
    }
    uint8_t out[6] = { (uint8_t)boot_res.errors, (uint8_t)(boot_res.errors >> 8), boot_res.bad_bits,
                       boot_res.bad_reg, boot_res.bad_wrote, boot_res.bad_read };
    card_test_bytes(out, 6);
    card_resend();                                // в чип - тень (после старта - нули, тишина)
    boot_result = boot_res.errors ? 1 : 0;
}

// Сброс со стороны шины: линия вверху - работаем
#ifdef BOARD_REV_A
static bool bus_ic_high(void) { return !gpio_get(PIN_AY_RST); }
#else
static bool bus_ic_high(void) { return gpio_get(PIN_SLOT_IC); }
#endif

static void core1_main(void) {
    cyc_init();                                     // счётчик тактов: писатель в чип и замеры
#ifdef BOARD_REV_A
    bus_irq_on();
#else
    bus_hw_init();                                  // автоматы и цепочки DMA - на ядре 1 (прерывание кольца - здесь)
#endif
    boot_check();
    boot_beep();
    sound_on();                                     // после теста: эффекты пишут в чип только теперь
    bool ic_was = bus_ic_high();
    while (true) {
#ifdef SIM_HOST
        sim_core1_loop();                               // модель на ПК: старт ядра 1 закончен (test/mock/sim.c)
#endif
#ifdef BOARD_REV_A
        if (pio_sm_is_rx_fifo_full(bus_pio, sm_wr)) chip_stat.fifo_full++;   // прерывание не успело
#endif
        WD_BEAT1(), WD_AT1(1);
        card_poll();                                    // события шины, планы эффектов, кадр, таймаут
        WD_AT1(2);
        chip_poll();                                    // писатель в чип: эффекты - первыми, потом очередь
        WD_AT1(3);
        if (test_req) test_run();
#ifndef SIM_HOST
        if (bus_dma_load) bus_dma_hammer();              // замер цепочек DMA под нагрузкой (bus_dma.c)
#endif
        bus_hw_poll();                                  // журнал шины (bus_hw.c)
        WD_AT1(4);
        if (hold_req && chip_idle()) { chip_hold(30000); hold_req = false; }
        if (tune_req && chip_idle()) {                   // тест пауз, потом tuned - в работу (до перезагрузки)
            mode_set(MODE_TEST);                         //   эффекты стоят, Спектрум в чип не пишет
            chip_tune(&tune_res);
            for (int i = 0; i < CHIP_TM_N; i++) cfg.tuned_pct[i] = tune_res.work_pct[i];
            cfg.timing_preset = 1;
            chip_timing_cfg();
            card_resend();
            mode_set(MODE_AY);
            tune_req = false, tune_done = true;
        }
        bool ic = bus_ic_high();
        if (!ic && ic_was) do_reset();                  // /IC вниз
        ic_was = ic;
    }
}

// Такт чипа из слота (φM, PIN_SLOT_PHIM): фронты за 1 мс опросом (грубо, но видно: есть / нет / какой), кГц
static uint32_t phim_khz(void) {
    uint32_t n = 0, t0 = time_us_32();
    bool was = gpio_get(PIN_SLOT_PHIM);
    while (time_us_32() - t0 < 1000) {
        bool now = gpio_get(PIN_SLOT_PHIM);
        n += now && !was;
        was = now;
    }
    return n;
}

// ---- Светодиод: состояние, не вспышки (CHECK.md)
static PIO  led_pio = pio0;
static uint led_sm;
static void rgb(uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(led_pio, led_sm, ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8));
}

#ifdef BUS_SELFTEST
// ---- Самотест шины платы без CPLD: ядро 0 изображает CPLD на тех же ножках (cpld/rtl/top.v). Только на голой RP:
// на плате этими ножками правит CPLD. Ядро 1 работает как обычно: прерывание шины, карта, писатели чипов
#include "hardware/structs/systick.h"
#define ST_RUNS  100000
#define ST_LIMIT 2000                   // тактов ядра на ответ (8 мкс): дальше считаем, что RP молчит
#define ST_CODE_FFFD 1                  // коды CPLD (top.v, CODE_FFFD / CODE_BFFD)
#define ST_CODE_BFFD 2
#define ST_CHIP_US   30                 // запись дошла до ножек чипа: адрес и данные с паузами (chip.c)

static uint32_t st_top_rdy;
static const uint8_t st_chip_d0[CHIP_N] = { 0, 11 };   // D0 чипов (config.h, CHIP_PIN_TABLE)

static uint32_t st_now(void) { return systick_hw->cvr; }
static void st_code(uint code) { gpio_put_masked64(3ull << PIN_AY_C0, (uint64_t)code << PIN_AY_C0); }

// CPLD читает: AY_RD вниз, ждём RDY, берём данные, AY_RD вверх, ждём опускания RDY. -1 - не ответила
static int st_read(uint code) {
    st_code(code);
    busy_wait_at_least_cycles(20);
    uint32_t t0 = st_now();
    gpio_put(PIN_AY_RD, 0);
    while (!gpio_get(PIN_AY_RDY))
        if (((t0 - st_now()) & 0xFFFFFF) > ST_LIMIT) { gpio_put(PIN_AY_RD, 1); return -1; }
    uint32_t d = (t0 - st_now()) & 0xFFFFFF;
    if (d > st_top_rdy) st_top_rdy = d;
    int v = (int)(gpio_get_all64() >> PIN_AY_D0 & 0xFF);
    gpio_put(PIN_AY_RD, 1);
    t0 = st_now();
    while (gpio_get(PIN_AY_RDY))
        if (((t0 - st_now()) & 0xFFFFFF) > ST_LIMIT) return -2;
    return v;
}

// CPLD пишет: код и данные, строб 125 нс, данные ещё такт. Пока ядро держит AY_D, PIO на них не выходит
static void st_write(uint code, uint8_t v) {
    const uint64_t d_mask = 0xFFull << PIN_AY_D0;
    st_code(code);
    for (uint i = 0; i < 8; i++) gpio_init(PIN_AY_D0 + i);
    gpio_put_masked64(d_mask, (uint64_t)v << PIN_AY_D0);
    gpio_set_dir_masked64(d_mask, d_mask);
    busy_wait_at_least_cycles(10);
    gpio_put(PIN_AY_WR, 0);
    busy_wait_at_least_cycles(32);
    gpio_put(PIN_AY_WR, 1);
    busy_wait_at_least_cycles(8);
    gpio_set_dir_masked64(d_mask, 0);
    for (uint i = 0; i < 8; i++) pio_gpio_init(bus_pio, PIN_AY_D0 + i);
}

static unsigned long st_ns(uint32_t cycles) {
    return (unsigned long)((uint64_t)cycles * 1000000u / (clock_get_hz(clk_sys) / 1000));
}

static void bus_selftest(void) {
    long no_answer = 0, bad_read = 0, bad_other = 0, bffd_answered = 0, bad_pins = 0, none_seen = 0;
    int mirror[CHIP_N][14];
    const uint64_t out_mask = (1ull << PIN_AY_WR) | (1ull << PIN_AY_RD) | (3ull << PIN_AY_C0);
    for (int c = 0; c < CHIP_N; c++) for (int r = 0; r < 14; r++) mirror[c][r] = -1;
    systick_hw->rvr = 0xFFFFFF;
    systick_hw->csr = 5;                                       // такт ядра 0
    st_top_rdy = 0;
    uint32_t wr0 = chip_stat.slot_wr;
    for (uint p = PIN_AY_WR; p <= PIN_AY_C0 + 1; p++) gpio_init(p);
    gpio_put_masked64(out_mask, (1ull << PIN_AY_WR) | (1ull << PIN_AY_RD));
    gpio_set_dir_masked64(out_mask, out_mask);
    for (long i = 0; i < ST_RUNS; i++) {
        int c = (int)(i & 1), r = (int)(i / 2 % 14);
        uint8_t v = (uint8_t)(i * 37 + 11);
        // Запись: выбор чипа (#FF - 0, #FE - 1), регистр, данные
        st_write(ST_CODE_FFFD, (uint8_t)(0xFF - c));
        st_write(ST_CODE_FFFD, (uint8_t)r);
        st_write(ST_CODE_BFFD, v);
        mirror[c][r] = v;
        // Чтение #FFFD: тот же регистр - то, что записали
        int got = st_read(ST_CODE_FFFD);
        if (got < 0) no_answer++;
        else if (got != v) bad_read++;
        // Другой чип: его регистр не тронут
        int o = c ^ 1;
        st_write(ST_CODE_FFFD, (uint8_t)(0xFF - o));
        st_write(ST_CODE_FFFD, (uint8_t)r);
        got = st_read(ST_CODE_FFFD);
        if (got < 0) no_answer++;
        else if (mirror[o][r] >= 0 && got != mirror[o][r]) bad_other++;
        // #BFFD на чтение молчит
        if (st_read(ST_CODE_BFFD) != -1) bffd_answered++;
        // Код 0 (не нам): запись не доходит
        uint32_t w = chip_stat.slot_wr;
        st_write(0, 0x55);
        busy_wait_us_32(1);
        if (chip_stat.slot_wr != w) none_seen++;
        // Писатель чипа выставил данные на его ножки (последнее - данные v), каждые 64 прогона
        if ((i & 63) == 0) {
            busy_wait_us_32(ST_CHIP_US);
            if ((uint8_t)(gpio_get_all64() >> st_chip_d0[c]) != v) bad_pins++;
        }
    }
    gpio_set_dir_masked64(out_mask, 0);
    printf("самотест шины RP-AY: прогонов %d, записей принято %lu (ждали %ld)\n", ST_RUNS,
           (unsigned long)(chip_stat.slot_wr - wr0), (long)ST_RUNS * 5);
    printf("нет ответа %ld, чтение не то %ld, чужой чип испорчен %ld, #BFFD ответил %ld, код 0 принят %ld, ножки чипа не те %ld\n",
           no_answer, bad_read, bad_other, bffd_answered, none_seen, bad_pins);
    printf("RDY после AY_RD: самый долгий %lu нс, обработчик шины %lu нс, записей в чипы %lu, потеряно в очередях %lu\n",
           st_ns(st_top_rdy), st_ns(irq_max_cyc), (unsigned long)chip_stat.chip_wr, (unsigned long)chip_stat.q_lost);
    printf("PIO: база %u, команда записи %u, чтения %u\n", (unsigned)pio_get_gpio_base(bus_pio),
           (unsigned)pio_sm_get_pc(bus_pio, sm_wr), (unsigned)pio_sm_get_pc(bus_pio, sm_rd));
}
#endif

static bool wd_was;                                    // прошлый запуск кончил сторож: висели
static uint32_t wd_place[4];                           //   метки (wd.h) на тот момент

int main(void) {
#ifndef SIM_HOST
    wd_was = watchdog_enable_caused_reboot();
    for (int i = 0; i < 4; i++) wd_place[i] = watchdog_hw->scratch[i], watchdog_hw->scratch[i] = 0;
#endif
    cfg_defaults();
    vreg_set_voltage(cfg.vreg);                        // разгон: ответ на чтение сразу за записью (14 МГц) успевает
    sleep_ms(2);
    set_sys_clock_khz(cfg.sys_khz, true);
    stdio_init_all();
    psram_init();                                      // PSRAM: пул слотов API (config.h); такт уже выставлен

    pio_set_gpio_base(led_pio, PIN_RGB >= 32 ? 16 : 0);  // окно PIO - 32 ножки: светодиод (config.h) должен попасть
    led_sm = pio_claim_unused_sm(led_pio, true);         // занять: иначе запись в чип (chip.c) получит ту же машину
    uint off = pio_add_program(led_pio, &ws2812_program);
    ws2812_program_init(led_pio, led_sm, off, PIN_RGB, 800000);
    rgb(64, 64, 64);                                   // прошивка жива: яркий белый 0.5 с
    sleep_ms(500);

#ifdef BOARD_REV_A
    gpio_init(PIN_CHIP_IC);                                // /IC внизу, пока нет такта чипов
    gpio_put(PIN_CHIP_IC, 0);
    gpio_set_dir(PIN_CHIP_IC, GPIO_OUT);
    gpio_init(PIN_AY_RST);
    gpio_set_dir(PIN_AY_RST, GPIO_IN);
    phim_set(0);
    busy_wait_us_32(IC_PULSE_US);
    gpio_put(PIN_CHIP_IC, 1);
#else
    for (uint p = PIN_SLOT_CS; p <= PIN_SLOT_A0; p++) {   // стробы слота - входы (у RP2350 ножка по умолчанию
        gpio_init(p);                                      //   изолирована, PIO её не видит)
        gpio_set_dir(p, GPIO_IN);
    }
    gpio_init(PIN_SLOT_IC);   gpio_set_dir(PIN_SLOT_IC, GPIO_IN);
    gpio_init(PIN_SLOT_PHIM); gpio_set_dir(PIN_SLOT_PHIM, GPIO_IN);
#endif
    chip_init();
    mode_reset();
    card_reset();

#ifdef BOARD_REV_A
    sm_wr = pio_claim_unused_sm(bus_pio, true);
    sm_rd = pio_claim_unused_sm(bus_pio, true);
    pio_set_gpio_base(bus_pio, 16);                                // шина на GP28..GP40
    bus_ay_wr_init(bus_pio, sm_wr, pio_add_program(bus_pio, &bus_ay_wr_program));
    bus_ay_rd_init(bus_pio, sm_rd, pio_add_program(bus_pio, &bus_ay_rd_program));
    answer_update();
#endif                                                             // макет: шину поднимает ядро 1 (bus_hw_init)

    multicore_launch_core1(core1_main);
    while (boot_result < 0) {                                      // тест чипа - жёлтый
        if (boot_stage == 2) rgb(64, 48, 0);                       //   белый - ядро 1 ещё не начало
        sleep_ms(10);
    }
    if (boot_result == 0) {                                        // тест OK - зелёный 3 с, дальше обычная работа
        rgb(0, 64, 0);
        sleep_ms(3000);
    }

    uint32_t last_wr = 0, last_cw = 0, t_log = 0, log_mute = 0;   // log_mute - сколько отчётов ещё пропустить
#ifndef SIM_HOST
    watchdog_enable(16000, true);                                     // ядро 0 молчит 16 с (предел сторожа) - перезагрузка (метки - wd.h)
#endif
    while (true) {
        WD_AT(1);
        for (int i = 0; i < 100; i++) {                               // 100 мс: служебный API карты - каждую мс
            card_svc_poll(boot_result > 0);
            sleep_ms(1);
        }
        WD_AT(2);
        int key = getchar_timeout_us(0);                              // консоль: t - тест чтения чипа
        if (key == 'p' && !tune_req && mode == MODE_AY) { tune_req = true; printf("pause test: ~1 min...\n"); }
#ifdef BUS_SELFTEST
        if (key == 'u') { bus_selftest(); printf("готово\n"); }
#endif
#ifndef SIM_HOST
        if (key == 'd') bus_dma_test();                               // замер цепочек DMA (п.14е), только железо
#endif
        if (key == 'j') { svc_force = true; bus_hw_jr_force = true; }      // журналы пакетов и шины - напечатать
        if (key == 'b') bus_hw_bench();                               // стенд ответа шины (bus_hw.c), Ева выключена
        if (key == 'h') { hold_req = true; printf("hold 30 s: A0 (37) = 1, /WR (35) = 0, /CS (34) = 1\n"); }
        if (key == 't' && !test_req) {
            if (mode_set(MODE_TEST) == MODE_OK) test_req = true;
            else printf("test: нельзя из режима %s\n", mode_name[mode]);
        }
        if (tune_done) {                                              // табличка теста пауз
            tune_done = false;
            printf("pause      Z80 ns  min %%  min ns  work %%  work ns   (margin %u%%)\n", cfg.timing_margin_pct);
            for (int i = 0; i < CHIP_TM_N; i++) {
                uint32_t b = chip_tm_base_ns(i);
                printf("%-9s  %6lu  %5u  %6lu  %6u  %7lu\n", chip_tm_name[i], (unsigned long)b, tune_res.min_pct[i],
                       (unsigned long)(b * tune_res.min_pct[i] / 100), tune_res.work_pct[i],
                       (unsigned long)(b * tune_res.work_pct[i] / 100));
            }
            printf("tuned now on. config.c: cfg.timing_preset = 1; tuned_pct = { %u, %u, %u, %u, %u }\n",
                   tune_res.work_pct[0], tune_res.work_pct[1], tune_res.work_pct[2], tune_res.work_pct[3], tune_res.work_pct[4]);
        }
        if (test_done) {
            test_done = false;
            if (!test_res.errors) printf("test: OK, %u сравнений\n", test_res.checks);
            else printf("test: %u ошибок из %u, первая: R%u записали %02X прочитали %02X\n",
                        test_res.errors, test_res.checks, test_res.bad_reg, test_res.bad_wrote, test_res.bad_read);
        }
        uint32_t wr = chip_stat.slot_wr, cw = chip_stat.chip_wr;
        uint8_t k = cfg.led_bright;
        if (boot_result) rgb(64, 0, 0);                                   // тест при старте не прошёл - красный насовсем
        else if (chip_stat.q_lost || chip_stat.fifo_full) rgb(k, 0, 0);       // красный - теряли записи
        else if (wr != last_wr && cw != last_cw) rgb(0, k, 0);            // зелёный - записи идут в чип (за эти 100 мс)
        else if (wr != last_wr) rgb(0, 0, k);                             // синий - приходят, в чип не уходят
        else rgb(k / 2, k / 2, k / 2);                                    // белый (слабее цветов) - тишина
        last_wr = wr, last_cw = cw;
        if (cfg.log_period_ms && (t_log += 100) >= cfg.log_period_ms && (!log_mute || !--log_mute)) {
            t_log = 0;
            uint32_t log_t0 = time_us_32();
            if (wd_was) printf("  WATCHDOG reboot: core 0 at %lu (svc packet %02lX), core 1 at %lu, loops %lu\n",
                               (unsigned long)wd_place[0], (unsigned long)wd_place[1], (unsigned long)wd_place[2], (unsigned long)wd_place[3]);
            if (!boot_result) printf("rp-ay v%02d  chip test OK (%u)\n", FW_VERSION, boot_res.checks);
            else printf("rp-ay v%02d  chip test FAIL: %u of %u, bad bits %02X, first R%u wrote %02X read %02X\n",
                        FW_VERSION, boot_res.errors, boot_res.checks, boot_res.bad_bits,
                        boot_res.bad_reg, boot_res.bad_wrote, boot_res.bad_read);
            printf("  test errors: read 00 %u, not driven %u, old value %u, other reg %u; by reg:", boot_res.got_zero,
                   boot_res.got_float, boot_res.got_old, boot_res.got_other);
            for (int i = 0; i < 14; i++) printf(" %u", boot_res.reg_err[i]);
            printf("\n  phiM %lu kHz, /IC %d; before test R14 %02X R15 %02X status %02X; shorted pins %03X\n",
                   (unsigned long)phim_khz(), bus_ic_high(), boot_probe[0], boot_probe[1], boot_probe[2], boot_pins);
#ifdef BOARD_REV_A
            uint32_t irq_cyc = irq_max_cyc;
#else
            uint32_t irq_cyc = bus_hw_irq_cyc;                         // разбор кольца записей (bus_hw.c)
            printf("  bus replies with old tail left (not read whole) %lu\n", (unsigned long)bus_hw_left);
            bus_hw_print();
            // журнал служебных: код[данные 0,1]>байт ответа/длина ответа (длина - с 2 байтами длины)
            {
                static uint32_t svc_shown;
                uint32_t sn = card_svc_n;
                if (sn != svc_shown || svc_force) {
                    svc_force = false;
                    svc_shown = sn;
                    printf("  svc %lu:", (unsigned long)sn);
                    for (uint32_t k = sn > CARD_SVC_LOG ? sn - CARD_SVC_LOG : 0; k < sn; k++) {
                        uint32_t e = card_svc_log[k % CARD_SVC_LOG];
                        printf(" %02lX[%02lX,%02lX]>%02lX/%u", (unsigned long)(e >> 24), (unsigned long)(e >> 16 & 0xFF),
                               (unsigned long)(e >> 8 & 0xFF), (unsigned long)(e & 0xFF), (unsigned)card_svc_len[k % CARD_SVC_LOG]);
                        if (card_svc_rep[k % CARD_SVC_LOG] > 1) printf("x%u", (unsigned)card_svc_rep[k % CARD_SVC_LOG]);
                    }
                    printf("\n");
                }
            }
#endif
            printf("  bus irq max %lu cyc (%lu ns), events lost %lu, packets ROW %lu TABLE %lu RESET %lu\n",
                   (unsigned long)irq_cyc, (unsigned long)((uint64_t)irq_cyc * 1000000000u / clock_get_hz(clk_sys)),
                   (unsigned long)card_ev_lost, (unsigned long)card_pk[1], (unsigned long)card_pk[2], (unsigned long)card_pk[3]);
            printf("  fx steps %lu: late max %lu us, avg %lu us; fx writes overwritten (chip too slow) %lu\n",
                   (unsigned long)card_late_n, (unsigned long)card_late_max,
                   (unsigned long)(card_late_n ? card_late_sum / card_late_n : 0), (unsigned long)chip_stat.fx_over);
            printf("rp-ay %s: slot %lu  chip %lu  lost %lu  fifo %lu  resets %lu  sel %02X  reads %lu:"
                   " %02X=%02X %02X=%02X %02X=%02X %02X=%02X\n",
                   mode_name[mode], (unsigned long)chip_stat.slot_wr, (unsigned long)chip_stat.chip_wr,
                   (unsigned long)chip_stat.q_lost, (unsigned long)chip_stat.fifo_full,
                   (unsigned long)chip_stat.resets, card_sel(), (unsigned long)chip_stat.reads,
                   chip_stat.rd_sel[0], chip_stat.rd_ans[0], chip_stat.rd_sel[1], chip_stat.rd_ans[1],
                   chip_stat.rd_sel[2], chip_stat.rd_ans[2], chip_stat.rd_sel[3], chip_stat.rd_ans[3]);
            if (time_us_32() - log_t0 > 300000) log_mute = 10;             // печать шла > 0.3 с: порт открыт, но не читают -
                                                                    //   10 отчётов пропустить (ядро 0 не держать)
        }
    }
}
