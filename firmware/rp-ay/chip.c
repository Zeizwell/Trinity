// Чип: очередь записей "регистр + данные" и писатель (chip.h). Пишет ядро ножками напрямую (SIO) - тем же способом,
// что тест чтения (на железе проверен), но шагами по времени: каждое состояние ножек держится до своей отметки,
// между шагами ядро свободно (прерывание шины обслуживается). Время - счётчик тактов RP (SysTick ядра 1), паузы
// точные до такта. PIO для записи не используем: на макете молчал.
#include "chip.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

chip_stat_t chip_stat;

static uint16_t q[CHIP_Q_SIZE];           // регистр << 8 | данные
static volatile uint32_t q_head, q_tail;  // head - писать сюда, tail - брать отсюда
static uint8_t  step;                     // какое состояние записи q_tail ставим следующим (0..7)
static uint32_t t_start, t_dur;           // SysTick: когда поставлено текущее состояние, сколько тактов держать
static uint32_t c_setup, c_pulse, c_hold, c_addr, c_ssg, c_fm;   // паузы, такты RP

#define CHIP_PINS_MASK  ((0xFFull << PIN_CHIP_D0) | (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_A0))

// ---- Счётчик тактов RP: SysTick, 24 бита, считает ВНИЗ; разница - не больше 67 мс на 250 МГц
#ifndef SIM_HOST
#include "hardware/structs/systick.h"
void     cyc_init(void) { systick_hw->rvr = 0xFFFFFF; systick_hw->cvr = 0; systick_hw->csr = 5; }
uint32_t cyc_now(void)  { return systick_hw->cvr; }
#else
void     cyc_init(void) {}
uint32_t cyc_now(void)  { time_us_32(); return (uint32_t)(0xFFFFFF - sim_now_ns() * 150 / 1000) & 0xFFFFFF; }
#endif
static uint32_t cyc_since(uint32_t t0) { return (t0 - cyc_now()) & 0xFFFFFF; }

static uint32_t ns_to_cyc(uint32_t ns) {
    return (uint32_t)((uint64_t)clock_get_hz(clk_sys) * ns / 1000000000u) + 1;
}
static uint32_t ht_to_cyc(uint32_t half_t) {          // полутакты Z80 -> такты RP (вверх)
    return (uint32_t)(((uint64_t)half_t * clock_get_hz(clk_sys) + 2u * cfg.z80_hz - 1) / (2u * cfg.z80_hz));
}
static uint32_t clk_to_cyc(uint32_t chip_clk) {      // такты чипа -> такты RP (вверх)
    return (uint32_t)(((uint64_t)chip_clk * clock_get_hz(clk_sys) + cfg.phim_hz - 1) / cfg.phim_hz);
}

// Состояние ножек чипа: данные, A0, /CS, /WR
static uint64_t pins(uint8_t d, bool a0, bool cs, bool wr) {
    return ((uint64_t)d << PIN_CHIP_D0) | ((uint64_t)a0 << PIN_CHIP_A0) | ((uint64_t)cs << PIN_CHIP_CS) | ((uint64_t)wr << PIN_CHIP_WR);
}

void chip_init(void) {
    for (uint p = PIN_CHIP_D0; p <= PIN_CHIP_A0; p++) gpio_init(p);
    gpio_init(PIN_CHIP_RD);                                     // /RD чипа - вверх (чтение только в тесте)
    gpio_put(PIN_CHIP_RD, 1);
    gpio_set_dir(PIN_CHIP_RD, GPIO_OUT);
    gpio_put_masked64(CHIP_PINS_MASK, pins(0, false, true, true));   // покой: /CS и /WR вверх
    gpio_set_dir_masked64(CHIP_PINS_MASK, CHIP_PINS_MASK);
    chip_timing_cfg();
    chip_reset_state();
}

void chip_reset_state(void) {
    q_head = q_tail = 0;
    step = 0;
    t_dur = 0;
}

// ---- Паузы: ритм Z80 (cfg, полутакты) * % набора
const char *const chip_tm_name[CHIP_TM_N] = { "setup", "/WR", "hold", "addr gap", "data gap" };
static const uint8_t *tm_ht(int i) {
    const uint8_t *h[CHIP_TM_N] = { &cfg.setup_ht, &cfg.pulse_ht, &cfg.hold_ht, &cfg.gap_addr_ht, &cfg.gap_ssg_ht };
    return h[i];
}
uint32_t chip_tm_base_ns(int i) { return (uint32_t)((uint64_t)*tm_ht(i) * 500000000u / cfg.z80_hz); }

void chip_timing_pct(const uint16_t pct[CHIP_TM_N]) {
    uint32_t c[CHIP_TM_N];
    for (int i = 0; i < CHIP_TM_N; i++) {
        c[i] = (uint32_t)((uint64_t)ht_to_cyc(*tm_ht(i)) * pct[i] / 100);
        if (!c[i]) c[i] = 1;
    }
    c_setup = c[0], c_pulse = c[1], c_hold = c[2], c_addr = c[3], c_ssg = c[4];
    c_fm = clk_to_cyc(cfg.wait_fm_clk);
}

void chip_timing_cfg(void) {
    static const uint16_t z80[CHIP_TM_N] = { 100, 100, 100, 100, 100 };
    chip_timing_pct(cfg.timing_preset ? cfg.tuned_pct : z80);
}

bool chip_put(uint8_t reg, uint8_t v) {
    uint32_t h = q_head;
    if (((h + 1) & (CHIP_Q_SIZE - 1)) == q_tail) { chip_stat.q_lost++; return false; }
    q[h] = (uint16_t)(reg << 8 | v);
    q_head = (h + 1) & (CHIP_Q_SIZE - 1);
    return true;
}

// Восемь состояний на запись, как цикл Z80: регистр (установка, /WR вниз, /WR вверх при /CS внизу, /CS вверх и
// пауза адреса), данные (то же, в конце - пауза занятости). Чип защёлкивает по подъёму /WR, /CS должен быть ещё внизу.
// Ставит состояние s, возвращает, сколько тактов RP его держать
static uint32_t step_set(uint8_t reg, uint8_t v, uint8_t s) {
    bool data = s >= 4;
    uint8_t d = data ? v : reg;
    switch (s & 3) {
    case 0:  gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, false, true));  return c_setup;   // данные, A0, /CS вниз
    case 1:  gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, false, false)); return c_pulse;   // /WR вниз
    case 2:  gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, false, true));  return c_hold;    // /WR вверх, /CS ещё внизу
    default: gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, true, true));                     // /CS вверх, пауза
             return !data ? c_addr : (reg < 16 ? c_ssg : c_fm);
    }
}

// Следующий шаг записи, если пауза прошлого состояния кончилась (ядро не ждёт)
void chip_poll(void) {
    if (t_dur && cyc_since(t_start) < t_dur) return;
    t_dur = 0;                                                  // пауза прошла (дальше счётчик может обернуться)
    if (q_tail == q_head) return;
    uint16_t e = q[q_tail];
    t_start = cyc_now();
    t_dur = step_set((uint8_t)(e >> 8), (uint8_t)e, step);
    if (++step < 8) return;
    step = 0;
    q_tail = (q_tail + 1) & (CHIP_Q_SIZE - 1);
    chip_stat.chip_wr += 2;                                     // циклы: адрес и данные
}

// Всё записано: очередь пуста и последняя пауза прошла
bool chip_idle(void) {
    if (t_dur && cyc_since(t_start) >= t_dur) t_dur = 0;
    return q_tail == q_head && step == 0 && !t_dur;
}

// ---- Тест чтения: пишем и читаем сразу (с ожиданием), паузы - те же из cfg
#define CHIP_D_MASK  (0xFFull << PIN_CHIP_D0)

static void wait_cyc(uint32_t cyc) { busy_wait_at_least_cycles(cyc); }

// Один цикл записи ножками: A0, данные, /CS и /WR - импульс по cfg
static void sio_cycle(bool a0, uint8_t v) {
    uint64_t val = ((uint64_t)v << PIN_CHIP_D0) | (a0 ? (1ull << PIN_CHIP_A0) : 0) | (1ull << PIN_CHIP_WR);
    gpio_put_masked64(CHIP_PINS_MASK, val);                     // данные, A0, /CS = 0, /WR = 1
    wait_cyc(c_setup);
    gpio_put_masked64(1ull << PIN_CHIP_WR, 0);                  // /WR вниз
    wait_cyc(c_pulse);
    gpio_put(PIN_CHIP_WR, 1);                                   // /WR вверх, /CS ещё внизу
    wait_cyc(c_hold);
    gpio_put(PIN_CHIP_CS, 1);                                   // /CS вверх
}

// Чтение: регистр, потом на шину - ОБРАТНОЕ ожидаемому (не чип - так и прочитается: ошибка, а не ложный OK; шина
// без хозяина держит последнее), данные - вход, /CS и /RD вниз при A0 = 1, читаем, всё вверх, данные снова выход
// a0 = 0 - чтение статуса YM2203 (не регистр)
static uint8_t read_a0(uint8_t reg, uint8_t expect, bool a0) {
    sio_cycle(false, reg); wait_cyc(c_addr);
    gpio_put_masked64(CHIP_D_MASK, (uint64_t)(uint8_t)~expect << PIN_CHIP_D0);
    wait_cyc(c_setup);
    gpio_set_dir_masked64(CHIP_D_MASK, 0);                      // отпустить данные
    gpio_put_masked64((1ull << PIN_CHIP_A0) | (1ull << PIN_CHIP_CS), a0 ? 1ull << PIN_CHIP_A0 : 0);   // A0, /CS = 0
    wait_cyc(c_setup);
    gpio_put(PIN_CHIP_RD, 0);
    wait_cyc(ns_to_cyc(cfg.rd_pulse_ns));
    uint8_t v = (uint8_t)(gpio_get_all64() >> PIN_CHIP_D0);
    gpio_put(PIN_CHIP_RD, 1);
    gpio_put(PIN_CHIP_CS, 1);
    wait_cyc(c_pulse);                                          // чип отпустил шину
    gpio_set_dir_masked64(CHIP_D_MASK, CHIP_D_MASK);            // данные - снова выход
    return v;
}
static uint8_t read_now(uint8_t reg, uint8_t expect) { return read_a0(reg, expect, true); }

// Какая линия: R14 / R15 - порты чипа на вход (их ножки, без записи), статус - чтение при A0 = 0.
// Ожидание 5A: прочитали A5 - чип шину не выдал. R14 = 00 и статус = 00 - похоже, A0 до чипа не доходит
// Ножки RP к чипу сами по себе: по одной ставим 1 (остальные 0), читаем ножку обратно. Не та - замкнута на землю,
// на соседа или на 5 В. -> маска плохих: бит 0..7 - D0..D7, 8 /CS, 9 /WR, 10 A0, 11 /RD
uint16_t chip_pins_check(void) {
    static const uint8_t pin[12] = { PIN_CHIP_D0, PIN_CHIP_D0 + 1, PIN_CHIP_D0 + 2, PIN_CHIP_D0 + 3, PIN_CHIP_D0 + 4,
        PIN_CHIP_D0 + 5, PIN_CHIP_D0 + 6, PIN_CHIP_D0 + 7, PIN_CHIP_CS, PIN_CHIP_WR, PIN_CHIP_A0, PIN_CHIP_RD };
    uint16_t bad = 0;
    for (int i = 0; i < 12; i++) {
        gpio_put_masked64(CHIP_PINS_MASK, pins(0, false, true, true));   // покой: /CS, /WR вверх, данные 0
        gpio_put(PIN_CHIP_RD, 1);
        gpio_put(pin[i], 1); wait_cyc(ns_to_cyc(2000));
        bool hi = gpio_get(pin[i]);
        gpio_put(pin[i], 0); wait_cyc(ns_to_cyc(2000));
        bool lo = gpio_get(pin[i]);
        if (!hi || lo) bad |= 1u << i;
    }
    gpio_put_masked64(CHIP_PINS_MASK, pins(0, false, true, true));
    gpio_put(PIN_CHIP_RD, 1);
    return bad;
}

void chip_hold(uint32_t ms) {
    gpio_put_masked64(CHIP_PINS_MASK, pins(0, true, true, false));     // A0 = 1, /CS вверх (чип не выбран), /WR вниз
    busy_wait_us_32(ms * 1000u);
    gpio_put_masked64(CHIP_PINS_MASK, pins(0, false, true, true));     // покой
}

void chip_probe(uint8_t out[3]) {
    out[0] = read_a0(14, 0x5A, true);
    out[1] = read_a0(15, 0x5A, true);
    out[2] = read_a0(0, 0x5A, false);
}

// Сколько бит в регистрах SSG 0..13 (R7: биты портов 6, 7 не трогаем)
static const uint8_t ssg_mask[14] = { 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F };

static void cmp(chip_test_t *r, uint8_t reg, uint8_t v, uint8_t got, uint8_t old, const uint8_t *row) {
    r->checks++;
    if (((v ^ got) & ssg_mask[reg]) == 0) return;
    uint8_t m = ssg_mask[reg];
    if ((got & m) == 0) r->got_zero++;
    else if (((got ^ ~v) & m) == 0) r->got_float++;
    else if (((got ^ old) & m) == 0) r->got_old++;
    else for (int i = 0; i < 14; i++)
        if (i != reg && ((got ^ row[i]) & m) == 0) { r->got_other++; break; }
    r->reg_err[reg]++;
    r->bad_bits |= (uint8_t)((v ^ got) & ssg_mask[reg]);
    if (!r->errors) r->bad_reg = reg, r->bad_wrote = v, r->bad_read = got;
    r->errors++;
}

// 256 проходов: случайная строка R0..R13 (неиспользуемые биты - 0) пишется по порядку ТЕМ ЖЕ путём, что музыка
// (очередь chip_put + писатель chip_poll), потом все 14 читаются и сравниваются (3584 проверки).
// Видно и запись мимо регистра: прочитали значение другого регистра этой строки (got_other)
void chip_test(chip_test_t *r) {
    *r = (chip_test_t){ 0 };
    uint8_t row[14], old[14] = { 0 };
    uint32_t x = 0x2545F491;                                    // случайные числа: LCG, каждый раз те же
    for (int pass = 0; pass < 256; pass++) {
        for (int reg = 0; reg < 14; reg++) {
            x = x * 1664525u + 1013904223u;
            row[reg] = (uint8_t)(x >> 24) & ssg_mask[reg];
            chip_put((uint8_t)reg, row[reg]);
        }
        while (!chip_idle()) chip_poll();                       // писатель дописал строку
        for (int reg = 0; reg < 14; reg++)
            cmp(r, (uint8_t)reg, row[reg], read_now((uint8_t)reg, row[reg]), old[reg], row);
        for (int reg = 0; reg < 14; reg++) old[reg] = row[reg];
    }
}

// Тест пауз: каждую по очереди уменьшаем (остальные - 400 %, с запасом), на каждом шаге - chip_test; минимум без
// ошибок, рабочее = минимум + cfg.timing_margin_pct. Только когда chip_idle(); после - набор из cfg (chip_timing_cfg)
void chip_tune(chip_tune_t *t) {
    static const uint16_t steps[] = { 200, 150, 120, 100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 5 };
    chip_test_t r;
    for (int i = 0; i < CHIP_TM_N; i++) {
        uint16_t pct[CHIP_TM_N] = { 400, 400, 400, 400, 400 };
        uint16_t ok = 400;                                      // 400 % считаем проходным (как тест при старте)
        for (unsigned s = 0; s < sizeof steps / sizeof steps[0]; s++) {
            pct[i] = steps[s];
            chip_timing_pct(pct);
            chip_test(&r);
            if (r.errors) break;
            ok = steps[s];
        }
        t->min_pct[i] = ok;
        t->work_pct[i] = (uint16_t)((ok * (100u + cfg.timing_margin_pct) + 99) / 100);
    }
    chip_timing_cfg();
}
