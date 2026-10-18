// RP-AY, шаг 1 (FT2 doc/zx-trinity.md, 14а и п.15): режим AY - записи из слота -> тень -> чип,
// чтение из слота - ответ RP из тени. Каркас режимов и настроек - под FX, ATARI, обновление.
// Ядро 1: шина (разбор записей PIO), писатель в чип, сброс. Ядро 0: USB-консоль, светодиод.
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "config.h"
#include "modes.h"
#include "chip.h"
#include "ws2812.pio.h"
#include "bus.pio.h"

static PIO  bus_pio = pio1;
static uint sm_wr, sm_rd;

// Ответ на чтение: тень выбранного регистра, статус - из cfg
static void answer_update(void) {
    bus_rd_set(bus_pio, sm_rd, chip_regs[chip_sel], cfg.status_answer);
}

static void do_reset(void) {                      // /IC: сбросить всё (doc 14а)
    chip_reset_state();
    mode_reset();
    answer_update();
    chip_stat.resets++;
}

// ---- Ядро 1: реальное время
static void core1_main(void) {
    bool ic_was = gpio_get(PIN_SLOT_IC);
    while (true) {
        if (pio_sm_is_rx_fifo_full(bus_pio, sm_wr)) chip_stat.fifo_full++;
        while (!pio_sm_is_rx_fifo_empty(bus_pio, sm_wr)) {
            uint32_t w = pio_sm_get(bus_pio, sm_wr);    // D0..D7, /CS (8), /WR (9), /RD (10), A0 (11)
            if (w & (1u << 8)) continue;                // /CS = 1 - не наш чип
            chip_slot_write((w >> 11) & 1, (uint8_t)w);
            answer_update();
        }
        chip_poll();
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

    led_sm = 0;
    uint off = pio_add_program(led_pio, &ws2812_program);
    ws2812_program_init(led_pio, led_sm, off, PIN_RGB, 800000);

    for (uint p = PIN_SLOT_CS; p <= PIN_SLOT_A0; p++) {   // стробы слота - входы (у RP2350 ножка до gpio_init
        gpio_init(p);                                      //   изолирована, PIO её не видит)
        gpio_set_dir(p, GPIO_IN);
    }
    gpio_init(PIN_SLOT_IC);   gpio_set_dir(PIN_SLOT_IC, GPIO_IN);
    gpio_init(PIN_SLOT_PHIM); gpio_set_dir(PIN_SLOT_PHIM, GPIO_IN);
    chip_init();
    mode_reset();

    sm_wr = pio_claim_unused_sm(bus_pio, true);
    sm_rd = pio_claim_unused_sm(bus_pio, true);
    bus_wr_init(bus_pio, sm_wr, pio_add_program(bus_pio, &bus_wr_program));
    bus_rd_init(bus_pio, sm_rd, pio_add_program(bus_pio, &bus_rd_program));
    answer_update();

    multicore_launch_core1(core1_main);

    uint32_t last_wr = 0, t_log = 0;
    while (true) {
        sleep_ms(100);
        uint32_t wr = chip_stat.slot_wr, cw = chip_stat.chip_wr;
        uint8_t k = cfg.led_bright;
        if (chip_stat.q_lost || chip_stat.fifo_full) rgb(k, 0, 0);       // красный - теряли записи
        else if (wr != last_wr && cw) rgb(0, k, 0);                       // зелёный - записи идут в чип
        else if (wr != last_wr) rgb(0, 0, k);                             // синий - приходят, в чип не уходят
        else rgb(k / 4, k / 4, k / 4);                                    // тускло-белый - тишина
        last_wr = wr;
        if (cfg.log_period_ms && (t_log += 100) >= cfg.log_period_ms) {
            t_log = 0;
            printf("rp-ay %s: slot %lu  chip %lu  lost %lu  fifo %lu  resets %lu  sel %02X\n",
                   mode_name[mode], (unsigned long)chip_stat.slot_wr, (unsigned long)chip_stat.chip_wr,
                   (unsigned long)chip_stat.q_lost, (unsigned long)chip_stat.fifo_full,
                   (unsigned long)chip_stat.resets, chip_sel);
        }
    }
}
