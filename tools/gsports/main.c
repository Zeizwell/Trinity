// Тест портов GS на ПК: обмен Z80 по FULGSCOM.TXT, звук сверяется с прямым запуском модуля
// gsports файл.mod
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../engine/gs/gs_core.h"
#include "../../engine/gs/gs_dev_gs.h"
#include "../../engine/gs/gs_mod.h"
#include "../../engine/gs/gs_fx.h"
#include "../../engine/gs/gs_chan.h"

extern const gs_dev_t gs_dev_gs, gs_dev_opl4;

#define RAM_SIZE (4u << 20)
#define BLOCK 1024

static uint8_t *ram;
static int fails;

static void check(const char *what, int got, int want) {
    int ok = got == want;
    printf("%-34s %6d (надо %d) %s\n", what, got, want, ok ? "ok" : "ОШИБКА");
    if (!ok) fails++;
}

static uint8_t st(void) { return gs_dev_gs.port_read(GS_PORT_BB); }

// SC + WC: команда и ожидание бита команды, ядро 0 в это время работает
static void sc(uint8_t c) {
    gs_dev_gs.port_write(GS_PORT_BB, c);
    for (int i = 0; st() & 1; i++) {
        if (i > 16) { printf("команда %02X зависла\n", c); fails++; return; }
        gs_dev_gs.work();
    }
}

static void sd(uint8_t v) { gs_dev_gs.port_write(GS_PORT_B3, v); }

// WD: Спектрум ждёт, пока GS прочтёт данные. Здесь их читает команда, ждать нечего

// WN + GD: ждём данные и читаем
static int gd(void) {
    for (int i = 0; !(st() & 0x80); i++) {
        if (i > 16) { printf("нет данных\n"); fails++; return -1; }
        gs_dev_gs.work();
    }
    return gs_dev_gs.port_read(GS_PORT_B3);
}

static int gd3(void) { int a = gd(), b = gd(), c = gd(); return a | b << 8 | c << 16; }

// Как пример в FULGSCOM: #30, #D1, байты, #D2. Ядро 0 работает, только когда Z80 ждёт: самый плохой случай для кольца
static int load(const uint8_t *m, int size) {
    sc(0x30);
    int h = gs_dev_gs.port_read(GS_PORT_B3);
    sc(0xD1);
    for (int i = 0; i < size; i++) {
        for (int w = 0; st() & 0x80; w++) {      // WD: кольцо почти полно - Z80 ждёт
            if (w > 16) { printf("бит данных завис\n"); fails++; break; }
            gs_dev_gs.work();
        }
        sd(m[i]);
    }
    sc(0xD2);
    return h;
}

// Сумма всего звука и пик: для сравнения прогонов и проверки тишины
static double render(float *dst, int blocks, double *peak) {
    static float b[BLOCK * 2];
    double sum = 0;
    *peak = 0;
    for (int k = 0; k < blocks; k++) {
        memset(b, 0, sizeof b);
        gs_core_audio(b, BLOCK);
        gs_dev_gs.work();
        for (int i = 0; i < BLOCK * 2; i++) {
            double a = b[i] < 0 ? -b[i] : b[i];
            sum += a;
            if (a > *peak) *peak = a;
        }
        if (dst) memcpy(dst + k * BLOCK * 2, b, sizeof b);
    }
    return sum;
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("gsports файл.mod\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("нет файла\n"); return 1; }
    fseek(f, 0, SEEK_END);
    int size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *mod = malloc(size);
    fread(mod, 1, size, f);
    fclose(f);
    ram = malloc(RAM_SIZE);
    enum { BLOCKS = 900 };      // ~10 с на 96 кГц
    float *a = malloc(BLOCKS * BLOCK * 2 * sizeof(float)), *b = malloc(BLOCKS * BLOCK * 2 * sizeof(float));
    double peak;

    // Эталон: прямой запуск
    gs_core_init();
    gs_gs_set_ram(ram, RAM_SIZE);
    gs_gs_play_mod(mod, size);
    render(a, BLOCKS, &peak);
    printf("эталон: пик %.3f\n", peak);
    gs_gs_stop();

    // Флаги
    gs_core_init();
    gs_gs_set_ram(ram, RAM_SIZE);
    check("старт: бит 0 и бит 7", st() & 0x81, 0);
    sd(0x55);
    check("данные записаны: бит 7", st() >> 7, 1);
    sc(0x00);
    check("#00 сбросил данные", st() & 0x81, 0);
    gs_dev_gs.port_write(GS_PORT_BB, 0x36);
    check("команда записана: бит 0", st() & 1, 1);
    gs_dev_gs.work();
    check("команда выполнена: бит 0", st() & 1, 0);
    check("#36 данные есть", st() >> 7, 1);
    check("#36 значение", gs_dev_gs.port_read(GS_PORT_B3), 0xFF);
    check("прочли: бит 7 сброшен", st() >> 7, 0);

    // Память
    sc(0x20);
    check("#20 всего", gd3(), RAM_SIZE);
    sc(0x21);
    check("#21 свободно", gd3(), RAM_SIZE);
    sc(0x23);
    check("#23 страниц", gd(), 127);

    // Загрузка
    check("#30 номер модуля", load(mod, size), 1);
    check("в памяти то же", memcmp(ram, mod, size), 0);
    sc(0x21);
    check("#21 после загрузки", gd3(), RAM_SIZE - size);

    // Запуск командой: звук до бита как прямой
    sd(0);
    sc(0x31);
    check("#31 ответ", gs_dev_gs.port_read(GS_PORT_B3), 1);
    check("играет", gs_mod_playing(), 1);
    check("бит 7 после чтения", st() >> 7, 0);
    render(b, BLOCKS, &peak);
    check("звук совпал с прямым (memcmp)", memcmp(a, b, BLOCKS * BLOCK * 2 * sizeof(float)), 0);

    // Позиция и состояние
    sc(0x60);
    int song = gd();
    sc(0x61);
    int row = gd();
    sc(0x62);
    check("#62 смешанная", gd(), (song << 6 | (row & 0x3F)) & 0xFF);
    printf("позиция %d, строка %d\n", song, row);
    sc(0x67);
    printf("скорость %d, ", gd());
    sc(0x68);
    printf("темп %d\n", gd());
    sc(0x63);
    int n0 = gd(), n1 = gd(), n2 = gd(), n3 = gd();
    printf("ноты %02X %02X %02X %02X\n", n0, n1, n2, n3);
    sc(0x63);
    n0 = gd(), n1 = gd(), n2 = gd(), n3 = gd();
    check("#63 снова: бит 7 везде", n0 & n1 & n2 & n3 & 0x80, 0x80);
    sc(0x64);
    printf("громкости %02X %02X %02X %02X\n", gd(), gd(), gd(), gd());

    // Новая команда обрывает недосланный ответ
    sc(0x20);
    gd();
    sc(0x36);
    check("ответ оборван новой командой", gd(), 0xFF);

    // Пауза, тишина, продолжение
    sc(0x32);
    check("#32 бит 7", st() >> 7, 0);
    render(0, 20, &peak);                           // хвост фильтров
    printf("хвост после #32: %.4f\n", peak);
    render(0, 100, &peak);
    printf("потом: %.6f\n", peak);
    check("после #32 тишина", peak < 0.001, 1);
    sc(0x33);
    render(0, 200, &peak);
    check("после #33 звук", peak > 0.01, 1);

    // Громкость: 0 - тишина, ответ - старое значение
    sd(0);
    sc(0x2A);
    check("#2A старая", gs_dev_gs.port_read(GS_PORT_B3), 0x40);
    check("#2A бит 7", st() >> 7, 0);
    render(0, 20, &peak);
    printf("хвост после громкости 0: %.4f\n", peak);
    render(0, 100, &peak);
    printf("потом: %.6f\n", peak);
    check("громкость 0: тишина", peak < 0.001, 1);
    sd(0xFF);
    sc(0x2A);
    check("#2A старая 0", gs_dev_gs.port_read(GS_PORT_B3), 0);
    render(0, 200, &peak);
    check("вернули: звук", peak > 0.01, 1);

    // Скорость, темп
    sd(3);
    sc(0x66);
    sc(0x67);
    check("#66 скорость", gd(), 3);
    sd(150);
    sc(0x66);
    sc(0x68);
    check("#66 темп", gd(), 150);

    // Переход
    sd(1);
    sc(0x65);
    check("#65 ответ", gs_dev_gs.port_read(GS_PORT_B3), 1);
    render(0, 1, &peak);
    sc(0x60);
    check("#65 позиция", gd(), 1);

    // Не модуль: #31 отвечает 0
    {
        static uint8_t junk[100];
        load(junk, sizeof junk);
        sd(0);
        sc(0x31);
        check("мусор: #31 ответ", gs_dev_gs.port_read(GS_PORT_B3), 0);
        check("мусор: не играет", gs_mod_playing(), 0);
    }

    // Перезапуск
    load(mod, size);
    sd(1);
    sc(0x31);
    sc(0xF3);
    check("#F3 не играет", gs_mod_playing(), 0);
    sc(0x21);
    check("#F3 память свободна", gd3(), RAM_SIZE);
    sd(1);
    sc(0x31);
    check("#31 без модуля", gs_dev_gs.port_read(GS_PORT_B3), 0);

    // Эффекты: память свободна после #F3
    {
        static uint8_t tone[8000];
        for (int i = 0; i < (int)sizeof tone; i++) tone[i] = (i % 8) < 4 ? 0xC0 : 0x40;   // беззнаковый меандр
        static float w[BLOCK * 2 * 40];
        sc(0x38);
        check("#38 номер", gs_dev_gs.port_read(GS_PORT_B3), 1);
        sc(0xD1);
        for (int i = 0; i < (int)sizeof tone; i++) sd(tone[i]);
        sc(0xD2);
        check("в памяти со знаком", (int8_t)ram[0], 0x40);
        // Нота C-2 Амиги: 8287 отсчётов/с, меандр из 8 - 1036 Гц
        sd(48); sc(0x40);
        sd(1); sc(0x39);
        check("#39 ответ", gs_dev_gs.port_read(GS_PORT_B3), 0);
        check("канал 0 занят", gs_voice[4].data != 0, 1);
        check("арбитр: звучит GS", gs_core_active() == &gs_dev_gs, 1);
        render(w, 40, &peak);
        check("эффект звучит без модуля", peak > 0.05, 1);
        int cross = 0, skip = 2000;
        for (int i = skip; i + 2 < 20000; i += 2) if ((w[i] < 0) != (w[i + 2] < 0)) cross++;
        double hz = cross / 2.0 / ((20000 - skip) / 2 / 48000.0);
        printf("частота меандра %.0f Гц\n", hz);
        check("частота 1036 +-2%", hz > 1015 && hz < 1057, 1);
        render(0, 100, &peak);
        check("без цикла: кончился", gs_voice[4].on, 0);

        // Цикл #48, #49
        sc(0x38); sc(0xD1);
        for (int i = 0; i < (int)sizeof tone; i++) sd(tone[i]);
        sc(0xD2);
        sd(0); sc(0x48); sd(0); gs_dev_gs.work(); sd(0); gs_dev_gs.work();
        sd(0x40); sc(0x49); sd(0); gs_dev_gs.work(); sd(0); gs_dev_gs.work();
        check("#48 начало", gs_fx_get(2)->loop_begin, 0);
        check("#49 конец", gs_fx_get(2)->loop_end, 0x40);
        sd(2); sc(0x39);
        render(0, 300, &peak);
        check("с циклом: звучит дальше", gs_voice[4].on, 1);

        // #3A: бит 7 - канал 0
        sd(0x80); sc(0x3A);
        check("#3A канал 0 снят", gs_voice[4].data != 0, 0);

        // Прямой запуск #9A: канал 2, нота, громкость
        sd(2); sc(0x9A); sd(60); gs_dev_gs.work(); sd(0x20); gs_dev_gs.work();
        check("#9A канал 2", gs_voice[6].data != 0, 1);
        check("#9A громкость", gs_voice[6].vol, 0x20);

        // Приоритет: все заняты, младший вытесняется, равный нет
        gs_fx_stop(0x0F);
        for (int c = 0; c < 4; c++) gs_fx_play_chan(2, c, 60, 0x40);
        gs_fx_t *f1 = gs_fx_get(1);
        f1->prio = 0x80;
        gs_fx_play(1);
        check("равный приоритет не вытесняет", gs_voice[4].data == gs_fx_get(2)->data, 1);
        f1->prio = 0x90;
        gs_fx_play(1);
        check("старший вытесняет", gs_voice[4].data == f1->data, 1);

        // Модуль после эффектов, потом второй модуль стирает эффекты
        check("#30 первый модуль", load(mod, size), 1);
        check("эффекты живы", gs_fx_count(), 2);
        sd(1); sc(0x31);
        check("#31 ответ", gs_dev_gs.port_read(GS_PORT_B3), 1);
        render(0, 200, &peak);
        check("модуль и эффекты звучат", peak > 0.01, 1);
        load(mod, size);
        check("второй #30 стёр эффекты", gs_fx_count(), 0);

        // Арбитр: OPL4 начал - GS замолчал
        sd(1); sc(0x31);
        gs_core_claim(&gs_dev_opl4);
        check("арбитр: GS вытеснен", gs_mod_playing(), 0);
        check("арбитр: звучит OPL4", gs_core_active() == &gs_dev_opl4, 1);
        check("порты OPL4 живут", gs_dev_opl4.port_read(0), 0xFF);
    }

    printf(fails ? "ОШИБОК: %d\n" : "ВСЁ OK\n", fails);
    return fails != 0;
}
