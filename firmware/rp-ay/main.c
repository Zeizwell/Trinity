// Шаг 0: проверка сборки и прошивки. RP раз в секунду пишет в USB-консоль, что жива.
// Ножки шины пока не трогаем - все входы (безопасно в слоте).
#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    unsigned n = 0;
    while (true) {
        printf("Trinity rp-ay: alive %u\n", n++);
        sleep_ms(1000);
    }
}
