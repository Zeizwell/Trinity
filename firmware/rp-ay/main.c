// RP-AY (FT2 doc/zx-trinity.md, 14а и п.15): режимы AY и FX - записи из слота -> тень -> чип, эффекты AY FX
// (движок engine/ayfx.c), чтение из слота - ответ RP из тени. Каркас режимов и настроек - под ATARI, обновление.
// Ядро 1: шина - прерыванием PIO (уровень «Шина», doc 14б), в цикле - «Звук»: события, эффекты, писатель в чип, сброс.
// Ядро 0: USB-консоль, светодиод.
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
#include "ws2812.pio.h"
#include "bus.pio.h"

static PIO  bus_pio = pio1;
static uint sm_wr, sm_rd;

// Тест чтения чипа (режим TEST): просит ядро 0 (клавиша t в консоли), делает ядро 1
static volatile bool test_req, test_done;
static volatile bool hold_req;
static volatile bool tune_req, tune_done;      // клавиша p: тест пауз (chip_tune)
static chip_tune_t   tune_res;                  // клавиша h: замер напряжений на ножках чипа (chip_hold)
static volatile int  boot_result = -1;          // тест при старте: -1 ещё нет, 0 - OK, 1 - ошибки (boot_res)
static volatile int  boot_stage;                // шаг старта ядра 1 (светодиод): 0 - не начал, 1 - писк, 2 - тест
static chip_test_t   test_res, boot_res;
static uint16_t      boot_pins;                // ножки RP к чипу с замыканием (chip_pins_check)
static uint8_t       boot_probe[3];            // до теста: R14, R15, статус (chip_probe)

static void test_run(void) {                      // ядро 1: дождаться очереди, тест, вернуть тень, режим AY
    if (!chip_idle()) return;                     //   (пока очередь не пуста - в следующий раз)
    busy_wait_us_32(100);                         //   чип закончил последнюю запись
    chip_test(&test_res);
    card_resend();
    test_req = false;
    test_done = true;
    mode_set(MODE_AY);
}

// Ответ на чтение: данные - от карты, статус - из cfg
static void answer_update(void) {
    bus_rd_set(bus_pio, sm_rd, card_answer(), cfg.status_answer);
}

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
        uint32_t w = pio_sm_get(bus_pio, sm_wr);    // снимок ножек от PIN_SLOT_D0 (раскладка - config.h)
        if (w >> (PIN_SLOT_CS - PIN_SLOT_D0) & 1) continue;      // /CS = 1 - не наш чип
        card_slot_write(w >> (PIN_SLOT_A0 - PIN_SLOT_D0) & 1, (uint8_t)w);
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
    cyc_init();                                     // счётчик тактов: писатель в чип и замер обработчика
    pio_set_irq0_source_enabled(bus_pio, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + sm_wr), true);
    pio_set_irq0_source_enabled(bus_pio, (enum pio_interrupt_source)(pis_sm0_rx_fifo_not_empty + sm_rd), true);
    irq_set_exclusive_handler(PIO1_IRQ_0, bus_irq);
    irq_set_priority(PIO1_IRQ_0, 0);                // высший
    irq_set_enabled(PIO1_IRQ_0, true);              // на ядре 1: обработчик - здесь
}

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

static void sound_on(void) {                    // на ядре 1: прерывание будильника - здесь
    snd_alarm = hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback((uint)snd_alarm, snd_alarm_cb);
    irq_set_priority(hardware_alarm_get_irq_num((uint)snd_alarm), 0x40);   // ниже шины (0)
    hardware_alarm_force_irq((uint)snd_alarm);
}
#else
void sound_kick(void) {}
static void sound_on(void) {}
#endif

static void do_reset(void) {                      // /IC: сбросить всё (doc 14а); шина в это время не отвечает
    uint32_t s = save_and_disable_interrupts();
    mode_reset();
    card_reset();
    answer_update();
    restore_interrupts(s);
    chip_stat.resets++;
}

// ---- Ядро 1: «Шина» - прерыванием, «Звук» - здесь, в цикле
// Проверка RP -> чип при включении: нота в канале A на cfg.boot_beep_ms, потом тишина
static void boot_beep(void) {
    if (!cfg.boot_beep_ms) return;
    boot_stage = 1;
    static const uint8_t on[][2] = { {0, 0xF8}, {1, 0x00}, {7, 0x3E}, {8, 15} };   // период #0F8: ~440 Гц при такте AY 1.75 МГц
    for (unsigned i = 0; i < sizeof on / sizeof on[0]; i++) chip_put(on[i][0], on[i][1]);
    uint32_t t0 = time_us_32();
    while (time_us_32() - t0 < cfg.boot_beep_ms * 1000u) chip_poll();
    chip_put(8, 0);
    chip_put(7, 0x3F);
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
    boot_pins = chip_pins_check();
    chip_probe(boot_probe);
    chip_test(&boot_res);
    uint8_t out[6] = { (uint8_t)boot_res.errors, (uint8_t)(boot_res.errors >> 8), boot_res.bad_bits,
                       boot_res.bad_reg, boot_res.bad_wrote, boot_res.bad_read };
    card_test_bytes(out, 6);
    card_resend();                                // в чип - тень (после старта - нули, тишина)
    boot_result = boot_res.errors ? 1 : 0;
}

static void core1_main(void) {
    bus_irq_on();
    boot_check();
    boot_beep();
    sound_on();                                     // после теста: эффекты пишут в чип только теперь
    bool ic_was = gpio_get(PIN_SLOT_IC);
    while (true) {
        if (pio_sm_is_rx_fifo_full(bus_pio, sm_wr)) chip_stat.fifo_full++;   // прерывание не успело
        card_poll();                                    // события шины, планы эффектов, кадр, таймаут
        chip_poll();                                    // писатель в чип: эффекты - первыми, потом очередь
        if (test_req) test_run();
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
        bool ic = gpio_get(PIN_SLOT_IC);
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

int main(void) {
    cfg_defaults();
    vreg_set_voltage(cfg.vreg);                        // разгон: ответ на чтение сразу за записью (14 МГц) успевает
    sleep_ms(2);
    set_sys_clock_khz(cfg.sys_khz, true);
    stdio_init_all();

    pio_set_gpio_base(led_pio, PIN_RGB >= 32 ? 16 : 0);  // окно PIO - 32 ножки: светодиод (config.h) должен попасть
    led_sm = pio_claim_unused_sm(led_pio, true);         // занять: иначе запись в чип (chip.c) получит ту же машину
    uint off = pio_add_program(led_pio, &ws2812_program);
    ws2812_program_init(led_pio, led_sm, off, PIN_RGB, 800000);
    rgb(64, 64, 64);                                   // прошивка жива: яркий белый 0.5 с
    sleep_ms(500);

    for (uint p = PIN_SLOT_CS; p <= PIN_SLOT_A0; p++) {   // стробы слота - входы (у RP2350 ножка до gpio_init
        gpio_init(p);                                      //   изолирована, PIO её не видит)
        gpio_set_dir(p, GPIO_IN);
    }
    gpio_init(PIN_SLOT_IC);   gpio_set_dir(PIN_SLOT_IC, GPIO_IN);
    gpio_init(PIN_SLOT_PHIM); gpio_set_dir(PIN_SLOT_PHIM, GPIO_IN);
    chip_init();
    mode_reset();
    card_reset();

    sm_wr = pio_claim_unused_sm(bus_pio, true);
    sm_rd = pio_claim_unused_sm(bus_pio, true);
    bus_wr_init(bus_pio, sm_wr, bus_add_program(bus_pio, &bus_wr_program, PIN_SLOT_WR));   // ножки - config.h
    bus_rd_init(bus_pio, sm_rd, bus_add_program(bus_pio, &bus_rd_program, PIN_SLOT_RD));
    answer_update();

    multicore_launch_core1(core1_main);
    while (boot_result < 0) {                                      // тест чипа - жёлтый
        if (boot_stage == 2) rgb(64, 48, 0);                       //   белый - ядро 1 ещё не начало
        sleep_ms(10);
    }
    if (boot_result == 0) {                                        // тест OK - зелёный 3 с, дальше обычная работа
        rgb(0, 64, 0);
        sleep_ms(3000);
    }

    uint32_t last_wr = 0, last_cw = 0, t_log = 0;
    while (true) {
        sleep_ms(100);
        int key = getchar_timeout_us(0);                              // консоль: t - тест чтения чипа
        if (key == 'p' && !tune_req && mode == MODE_AY) { tune_req = true; printf("pause test: ~1 min...\n"); }
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
        if (cfg.log_period_ms && (t_log += 100) >= cfg.log_period_ms) {
            t_log = 0;
            if (!boot_result) printf("rp-ay v%02d  chip test OK (%u)\n", FW_VERSION, boot_res.checks);
            else printf("rp-ay v%02d  chip test FAIL: %u of %u, bad bits %02X, first R%u wrote %02X read %02X\n",
                        FW_VERSION, boot_res.errors, boot_res.checks, boot_res.bad_bits,
                        boot_res.bad_reg, boot_res.bad_wrote, boot_res.bad_read);
            printf("  test errors: read 00 %u, not driven %u, old value %u, other reg %u; by reg:", boot_res.got_zero,
                   boot_res.got_float, boot_res.got_old, boot_res.got_other);
            for (int i = 0; i < 14; i++) printf(" %u", boot_res.reg_err[i]);
            printf("\n  phiM %lu kHz, /IC %d; before test R14 %02X R15 %02X status %02X; shorted pins %03X\n",
                   (unsigned long)phim_khz(), gpio_get(PIN_SLOT_IC), boot_probe[0], boot_probe[1], boot_probe[2], boot_pins);
            printf("  bus irq max %lu cyc (%lu ns), events lost %lu, packets ROW %lu TABLE %lu RESET %lu\n",
                   (unsigned long)irq_max_cyc, (unsigned long)((uint64_t)irq_max_cyc * 1000000000u / clock_get_hz(clk_sys)),
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
        }
    }
}
