// Шаг 0: проверка сборки и прошивки. Раз в секунду: строка в USB-консоль и вспышка
// RGB-светодиода (зелёный). Ножки шины не трогаем - все входы (безопасно в слоте).
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

#define RGB_PIN 20                  // WS2812 на плате Spotpear

// Цвет точки: G, R, B по 8 бит (порядок WS2812), выравнивание влево на 32 бита
static void rgb(PIO pio, uint sm, uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(pio, sm, ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8));
}

int main(void) {
    stdio_init_all();
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, RGB_PIN, 800000);

    unsigned n = 0;
    while (true) {
        printf("Trinity rp-ay: alive %u\n", n++);
        rgb(pio, sm, 0, 16, 0);     // зелёный, неярко
        sleep_ms(100);
        rgb(pio, sm, 0, 0, 0);      // погасить
        sleep_ms(900);
    }
}
