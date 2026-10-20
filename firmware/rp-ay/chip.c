// Чип: очередь записей "регистр + данные" и писатель (chip.h). Пишет ядро ножками напрямую (SIO) - тем же способом,
// что тест чтения (на железе проверен), но шагами по времени: каждое состояние ножек держится до своей отметки,
// между шагами ядро свободно (ответы на чтение слота успевают). PIO для записи не используем: на макете молчал.
#include "chip.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

chip_stat_t chip_stat;

static uint16_t q[CHIP_Q_SIZE];           // регистр << 8 | данные
static volatile uint32_t q_head, q_tail;  // head - писать сюда, tail - брать отсюда
static uint8_t  step;                     // какое состояние записи q_tail ставим следующим (0..7)
static uint32_t due;                      // time_us_32, когда можно ставить следующее состояние
static uint32_t c_setup, c_pulse, c_hold, c_addr, c_ssg, c_fm;   // длительности из cfg, такты RP (тест чтения)
static uint32_t u_setup, u_pulse, u_hold, u_addr, u_ssg, u_fm;   // они же в мкс для записи шагами (с запасом)

#define CHIP_PINS_MASK  ((0xFFull << PIN_CHIP_D0) | (1ull << PIN_CHIP_CS) | (1ull << PIN_CHIP_WR) | (1ull << PIN_CHIP_A0))

static uint32_t ns_to_cyc(uint32_t ns) {
    return (uint32_t)((uint64_t)clock_get_hz(clk_sys) * ns / 1000000000u) + 1;
}
static uint32_t ht_to_cyc(uint32_t half_t) {          // полутакты Z80 -> такты RP (вверх)
    return (uint32_t)(((uint64_t)half_t * clock_get_hz(clk_sys) + 2u * cfg.z80_hz - 1) / (2u * cfg.z80_hz));
}
static uint32_t clk_to_cyc(uint32_t chip_clk) {      // такты чипа -> такты RP (вверх)
    return (uint32_t)(((uint64_t)chip_clk * clock_get_hz(clk_sys) + cfg.phim_hz - 1) / cfg.phim_hz);
}
// Такты RP -> мкс для отметок: вверх и +1 (таймер мкс тикает в любой момент - иначе можно выйти раньше)
static uint32_t cyc_to_us(uint32_t cyc) {
    uint32_t mhz = clock_get_hz(clk_sys) / 1000000u;
    return (cyc + mhz - 1) / mhz + 1;
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
    chip_timing(CHIP_TIMING_RUN);
    chip_reset_state();
}

void chip_reset_state(void) {
    q_head = q_tail = 0;
    step = 0;
    due = time_us_32();
}

void chip_timing(int set) {
    c_setup = ht_to_cyc(cfg.setup_ht);
    c_pulse = ht_to_cyc(cfg.pulse_ht);
    c_hold  = ht_to_cyc(cfg.hold_ht);
    c_addr  = ht_to_cyc(cfg.gap_addr_ht);
    c_ssg   = ht_to_cyc(cfg.gap_ssg_ht);
    c_fm    = clk_to_cyc(cfg.wait_fm_clk);
    if (set == 1) {                                             // даташит YM2203: 17 тактов чипа (3.5 МГц: 4.86 мкс)
        c_addr = clk_to_cyc(17);
        c_ssg  = clk_to_cyc(17);
    }
    if (set == 2) {                                             // всё в 4 раза медленнее
        c_setup *= 4, c_pulse *= 4, c_hold *= 4, c_addr *= 4, c_ssg *= 4;
    }
    u_setup = cyc_to_us(c_setup), u_pulse = cyc_to_us(c_pulse), u_hold = cyc_to_us(c_hold);
    u_addr = cyc_to_us(c_addr), u_ssg = cyc_to_us(c_ssg), u_fm = cyc_to_us(c_fm);
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
// Ставит состояние s, возвращает, сколько мкс его держать
static uint32_t step_set(uint8_t reg, uint8_t v, uint8_t s) {
    bool data = s >= 4;
    uint8_t d = data ? v : reg;
    switch (s & 3) {
    case 0:  gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, false, true));  return u_setup;   // данные, A0, /CS вниз
    case 1:  gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, false, false)); return u_pulse;   // /WR вниз
    case 2:  gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, false, true));  return u_hold;    // /WR вверх, /CS ещё внизу
    default: gpio_put_masked64(CHIP_PINS_MASK, pins(d, data, true, true));                     // /CS вверх, пауза
             return !data ? u_addr : (reg < 16 ? u_ssg : u_fm);
    }
}

// Следующий шаг записи, если его отметка наступила (ядро не ждёт)
void chip_poll(void) {
    if (q_tail == q_head) return;
    uint32_t now = time_us_32();
    if ((int32_t)(now - due) < 0) return;
    uint16_t e = q[q_tail];
    due = now + step_set((uint8_t)(e >> 8), (uint8_t)e, step);
    if (++step < 8) return;
    step = 0;
    q_tail = (q_tail + 1) & (CHIP_Q_SIZE - 1);
    chip_stat.chip_wr += 2;                                     // циклы: адрес и данные
}

// Всё записано: очередь пуста и последняя пауза прошла
bool chip_idle(void) {
    return q_tail == q_head && step == 0 && (int32_t)(time_us_32() - due) >= 0;
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

static void write_now(uint8_t reg, uint8_t v) {
    sio_cycle(false, reg); wait_cyc(c_addr);
    sio_cycle(true, v);    wait_cyc(c_ssg);
}

// Чтение: регистр, потом на шину - ОБРАТНОЕ ожидаемому (не чип - так и прочитается: ошибка, а не ложный OK; шина
// без хозяина держит последнее), данные - вход, /CS и /RD вниз при A0 = 1, читаем, всё вверх, данные снова выход
static uint8_t read_now(uint8_t reg, uint8_t expect) {
    sio_cycle(false, reg); wait_cyc(c_addr);
    gpio_put_masked64(CHIP_D_MASK, (uint64_t)(uint8_t)~expect << PIN_CHIP_D0);
    wait_cyc(c_setup);
    gpio_set_dir_masked64(CHIP_D_MASK, 0);                      // отпустить данные
    gpio_put_masked64((1ull << PIN_CHIP_A0) | (1ull << PIN_CHIP_CS), 1ull << PIN_CHIP_A0);   // A0 = 1, /CS = 0
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

// Сколько бит в регистрах SSG 0..13 (R7: биты портов 6, 7 не трогаем)
static const uint8_t ssg_mask[14] = { 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F };

static void cmp(chip_test_t *r, uint8_t reg, uint8_t v, uint8_t got) {
    r->checks++;
    if (((v ^ got) & ssg_mask[reg]) == 0) return;
    r->bad_bits |= (uint8_t)((v ^ got) & ssg_mask[reg]);
    if (!r->errors) r->bad_reg = reg, r->bad_wrote = v, r->bad_read = got;
    r->errors++;
}

// Каждый регистр SSG 0..13 - все значения 0..255: записать в чип и сразу прочитать обратно (3584 проверки)
void chip_test(chip_test_t *r) {
    *r = (chip_test_t){ 0 };
    for (uint8_t reg = 0; reg < 14; reg++)
        for (uint v = 0; v < 256; v++) {
            write_now(reg, (uint8_t)v & ssg_mask[reg]);
            cmp(r, reg, (uint8_t)v, read_now(reg, (uint8_t)v & ssg_mask[reg]));
        }
}
