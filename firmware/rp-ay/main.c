// RP-AY (FT2 doc/zx-trinity.md, 14а и п.15): режимы AY и FX - записи из слота -> тень -> чип, эффекты AY FX
// (движок engine/ayfx.c), чтение из слота - ответ RP из тени. Каркас режимов и настроек - под ATARI, обновление.
// Ядро 1: шина (разбор PIO), карта (card.c), писатель в чип, сброс. Ядро 0: USB-консоль, светодиод.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
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
static volatile int  boot_result = -1;          // тест при старте: -1 ещё нет, 0 - OK, 1 - ошибки
static volatile int  boot_stage;                // шаг старта ядра 1 (светодиод): 0 - не начал, 1 - писк, 2 - тест
static chip_test_t   test_res;

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

static void do_reset(void) {                      // /IC: сбросить всё (doc 14а)
    mode_reset();
    card_reset();
    answer_update();
    chip_stat.resets++;
}

// ---- Ядро 1: реальное время
// Проверка RP -> чип при включении: нота в канале A на cfg.boot_beep_ms, потом тишина (записи слота в это время теряются)
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

// Тест чипа при старте (cfg.boot_test): записать и прочитать регистры, итог - светодиодом (ядро 0)
static void boot_check(void) {
    if (!cfg.boot_test) { boot_result = 0; return; }
    boot_stage = 2;
    while (!chip_idle()) chip_poll();
    chip_test(&test_res);
    card_resend();                                // в чип - тень (после старта - нули, тишина)
    boot_result = test_res.errors ? 1 : 0;
}

static void core1_main(void) {
    boot_beep();
    boot_check();
    bool ic_was = gpio_get(PIN_SLOT_IC);
    while (true) {
        if (pio_sm_is_rx_fifo_full(bus_pio, sm_wr)) chip_stat.fifo_full++;
        while (!pio_sm_is_rx_fifo_empty(bus_pio, sm_wr)) {
            uint32_t w = pio_sm_get(bus_pio, sm_wr);    // D0..D7, /CS (8), /WR (9), /RD (10), A0 (11)
            if (w & (1u << 8)) continue;                // /CS = 1 - не наш чип
            card_slot_write((w >> 11) & 1, (uint8_t)w);
            answer_update();
        }
        while (!pio_sm_is_rx_fifo_empty(bus_pio, sm_rd)) {   // метки PIO: данные прочитали
            pio_sm_get(bus_pio, sm_rd);
            card_read_done();
            answer_update();
        }
        card_poll();
        chip_poll();
        if (test_req) test_run();
        bool ic = gpio_get(PIN_SLOT_IC);
        if (!ic && ic_was) do_reset();                  // /IC вниз
        ic_was = ic;
    }
}

// ---- Светодиод: состояние, не вспышки (CHECK.md)
static PIO  led_pio = pio0;
static uint led_sm;
static void rgb(uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(led_pio, led_sm, ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8));
}

int main(void) {
    cfg_defaults();
    stdio_init_all();

    pio_set_gpio_base(led_pio, 16);                    // pio0 видит GP16..47: светодиод (20) и запись в чип (28..38)
    led_sm = 0;
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
    bus_wr_init(bus_pio, sm_wr, pio_add_program(bus_pio, &bus_wr_program));
    bus_rd_init(bus_pio, sm_rd, pio_add_program(bus_pio, &bus_rd_program));
    answer_update();

    multicore_launch_core1(core1_main);
    while (boot_result < 0) {                                      // шаг старта ядра 1 - цветом (где застрял)
        if (boot_stage == 1) rgb(0, 0, 64);                        //   синий - писк
        else if (boot_stage == 2) rgb(64, 48, 0);                  //   жёлтый - тест чипа
        sleep_ms(10);                                              //   белый - ядро 1 ещё не начало
    }
    if (cfg.boot_test) {
        if (boot_result) rgb(64, 0, 0); else rgb(0, 64, 0);        // красный - ошибки, зелёный - OK
        sleep_ms(5000);
    }

    uint32_t last_wr = 0, last_cw = 0, t_log = 0;
    while (true) {
        sleep_ms(100);
        int key = getchar_timeout_us(0);                              // консоль: t - тест чтения чипа
        if (key == 't' && !test_req) {
            if (mode_set(MODE_TEST) == MODE_OK) test_req = true;
            else printf("test: нельзя из режима %s\n", mode_name[mode]);
        }
        if (test_done) {
            test_done = false;
            if (!test_res.errors) printf("test: OK, %u сравнений\n", test_res.checks);
            else printf("test: %u ошибок из %u, первая: R%u записали %02X прочитали %02X\n",
                        test_res.errors, test_res.checks, test_res.bad_reg, test_res.bad_wrote, test_res.bad_read);
        }
        uint32_t wr = chip_stat.slot_wr, cw = chip_stat.chip_wr;
        uint8_t k = cfg.led_bright;
        if (chip_stat.q_lost || chip_stat.fifo_full) rgb(k, 0, 0);       // красный - теряли записи
        else if (wr != last_wr && cw != last_cw) rgb(0, k, 0);            // зелёный - записи идут в чип (за эти 100 мс)
        else if (wr != last_wr) rgb(0, 0, k);                             // синий - приходят, в чип не уходят
        else rgb(k / 2, k / 2, k / 2);                                    // белый (слабее цветов) - тишина
        last_wr = wr, last_cw = cw;
        if (cfg.log_period_ms && (t_log += 100) >= cfg.log_period_ms) {
            t_log = 0;
            printf("rp-ay %s: slot %lu  chip %lu  lost %lu  fifo %lu  resets %lu  sel %02X\n",
                   mode_name[mode], (unsigned long)chip_stat.slot_wr, (unsigned long)chip_stat.chip_wr,
                   (unsigned long)chip_stat.q_lost, (unsigned long)chip_stat.fifo_full,
                   (unsigned long)chip_stat.resets, card_sel());
        }
    }
}
