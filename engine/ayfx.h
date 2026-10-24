// Движок таймерных эффектов AY FX (ZX Trinity) - переносимый C, без железа. Один на всех: прошивка RP-AY
// и (потом) Unreal. Протокол - FT2 doc/ay-ports-map.md п.5а, строка FX - doc/fx-row.md, эффекты физически -
// doc/bitphase-fx.md. Логика перенесена из Unreal (sndchip.cpp, trin_*), но на выходе - записи в регистры AY
// (а не правка микшера): SID - громкость R8..R10, Sync - R13, FM - тон R0..R5, Env FM - R11 / R12.
//
// Две части с чёткой границей (FT2 doc/zx-trinity.md 14б), обе - на один чип:
//   ayfx_bus_t - уровень «Шина» (на RP - прерывание): выбор регистра, ключ #C0, кадры пакетов #C1, ответы на чтение.
//                Быстро, без эффектов. На запись данных отдаёт событие: регистр AY, открыть, закрыть, пакет готов.
//   ayfx_t     - уровень «Звук» (фон): события -> эффекты, записи в чип.
//     ayfx_packet    - готовый пакет (ROW / TABLE / RESET);
//     ayfx_reg_write - запись Спектрума в регистр AY 0..15: тень; true - регистр сейчас у эффекта
//                      (в чип не пропускать - движок сам запишет с поправкой);
//     ayfx_off       - все эффекты выкл (таймаут, сброс);
//     ayfx_tick (n)  - прошло n тактов тона чипа (такт AY / 8);  ayfx_frame - раз в кадр (50 Гц): свип ШИМ.
//   Выход - функция out(user, reg, val): записать в чип регистр reg (0..15).
// Шаги (ayfx_tick / ayfx_next / ayfx_active) могут идти в прерывании поверх всего остального: остальное готовит план
// в запасном буфере и подменяет его, в AY сразу не пишет, а просит шаги (restart / kick).
// Эмулятор зовёт обе части подряд: ayfx_bus_write -> по событию ayfx_reg_write / ayfx_packet, потом ayfx_tick. Закрытие потока
// эффекты не выключает; выключить - ROW нулей (плеер на стопе), ayfx_off - таймаут и сброс.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AYFX_ROW    14          // строка FX в пакете ROW
#define AYFX_TABS   256         // таблиц
#define AYFX_STEPS  16          // шагов в таблице
#define AYFX_PKT    19          // самый длинный пакет (ROW)
#define AYFX_VER    2           // версия протокола (INFO)

enum { AYFX_P_ROW = 1, AYFX_P_TABLE = 2, AYFX_P_RESET = 3, AYFX_P_INFO = 4 };   // коды пакетов (как в FT2 sdk)

// ---- Шина
typedef struct {
    uint8_t sel;                // выбранный регистр (запись в #FFFD)
    uint8_t on, key;            // открыт ли поток; сколько букв ключа "TRIN" пришло
    uint8_t pkt[AYFX_PKT], pos, need;   // кадр пакета
    uint8_t info;               // ответ INFO: какой байт следующий (0 - нет)
} ayfx_bus_t;

enum {                          // событие записи данных - для уровня «Звук»
    AYFX_EV_NONE = 0,           // ничего (байт ключа, середина пакета, INFO, поток закрыт)
    AYFX_EV_REG,                // запись в регистр AY (не #C0 / #C1): регистр sel
    AYFX_EV_OPEN,               // ключ "TRIN": поток открыт
    AYFX_EV_CLOSE,              // 0 в #C0: поток закрыт; эффекты ИГРАЮТ дальше (ay-ports-map.md п.4) - звуку нечего
    AYFX_EV_PACKET,             // пакет целиком в pkt (ROW / TABLE / RESET)
};

void ayfx_bus_reset(ayfx_bus_t *b);
void ayfx_bus_select(ayfx_bus_t *b, uint8_t reg);             // запись в #FFFD
int  ayfx_bus_write(ayfx_bus_t *b, uint8_t val);              // запись в #BFFD (в выбранный регистр) -> AYFX_EV_*
bool ayfx_bus_peek(const ayfx_bus_t *b, uint8_t *val);        // ответ на чтение выбранного: false - не наш (тень AY)
void ayfx_bus_read_done(ayfx_bus_t *b);                       // чтение было - INFO на следующий байт

// ---- Звук
typedef void (*ayfx_out_fn)(void *user, uint8_t reg, uint8_t val);

typedef struct {                // готовый план таймера: шаг - только из него
    uint8_t  on;                // таймер включён
    uint8_t  len;               // длина таблицы (1..16)
    uint8_t  reg, two;          // регистр AY; two - пишем пару (reg - мл., reg + 1 - ст.: FM, Env FM)
    uint8_t  pwm;               // ШИМ: шаг 0 длится d0, шаг 1 - d1 (иначе все шаги - d0)
    uint32_t d0, d1;            // длины шагов, такты тона
    uint16_t val[16];           // готовое значение регистра(ов) на каждый шаг
} ayfx_plan_t;

typedef struct {                // один таймер: Mix (SID / Sync), FM, Env FM
    ayfx_plan_t pl[2];          // два плана: шаги идут по pl[cur], новый считается в другом и подменяется одной
    volatile uint8_t cur;       //   записью cur - шаги (прерывание) никогда не ждут и не видят полплана
    volatile uint8_t restart, kick;   // просьбы к шагам: restart - с шага 0, kick - текущий шаг заново в AY
    uint32_t cnt, dur;          // ШАГИ (только ayfx_tick): тактов в текущем шаге, длина текущего шага
    uint8_t  step;              //   шаг таблицы
} ayfx_tmr_t;

typedef struct {
    uint8_t   row[AYFX_ROW];    // строка FX: FLAGS, PWM, SWEEP, 3 x (SEMI, RAW, TAB), LEN1, LEN2
    ayfx_tmr_t t[3];
    uint16_t  sweep;            // фаза свипа ШИМ 0..65535
    uint8_t   shape, start, keep;
} ayfx_ch_t;

typedef struct {
    ayfx_ch_t ch[3];
    uint8_t   tab[AYFX_TABS][AYFX_STEPS];
    uint8_t   regs[16];         // тень: что Спектрум записал в AY
    ayfx_out_fn out;
    void     *user;
} ayfx_t;

void ayfx_init(ayfx_t *a, ayfx_out_fn out, void *user);
void ayfx_reset(ayfx_t *a);                                   // всё выкл, таблицы - нули
void ayfx_off(ayfx_t *a);                                     // все эффекты выкл, регистры - как у Спектрума
void ayfx_packet(ayfx_t *a, const uint8_t *pkt);              // пакет от шины (AYFX_EV_PACKET)
bool ayfx_reg_write(ayfx_t *a, uint8_t reg, uint8_t val);     // AY 0..15 от Спектрума
void ayfx_tick(ayfx_t *a, uint32_t n);                         // ЛЁГКАЯ (прерывание): только шаги по готовому плану
                                                              //   (n = 0 - только просьбы: restart / kick)
void ayfx_frame(ayfx_t *a);
bool ayfx_active(const ayfx_t *a);                            // хоть один эффект включён
uint32_t ayfx_next(const ayfx_t *a);                          // тактов тона до ближайшего шага (UINT32_MAX - нет)
