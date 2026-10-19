// Движок таймерных эффектов AY FX (ZX Trinity) - переносимый C, без железа. Один на всех: прошивка RP-AY
// и (потом) Unreal. Протокол - FT2 doc/ay-ports-map.md п.5а, строка FX - doc/fx-row.md, эффекты физически -
// doc/bitphase-fx.md. Логика перенесена из Unreal (sndchip.cpp, trin_*), но на выходе - записи в регистры AY
// (а не правка микшера): SID - громкость R8..R10, Sync - R13, FM - тон R0..R5, Env FM - R11 / R12.
//
// Один экземпляр (ayfx_t) - один чип. Хозяин:
//   ayfx_port_write / read - записи и чтения регистров #C0 / #C1 (протокол);
//   ayfx_reg_write         - запись Спектрума в регистр AY 0..15: тень; true - регистр сейчас у эффекта
//                            (в чип не пропускать - движок сам запишет с поправкой);
//   ayfx_tick (n)          - прошло n тактов тона чипа (такт AY / 8);
//   ayfx_frame             - раз в кадр (50 Гц): свип ШИМ.
// Выход - функция out(user, reg, val): записать в чип регистр reg (0..15).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AYFX_ROW    14          // строка FX в пакете ROW
#define AYFX_TABS   256         // таблиц
#define AYFX_STEPS  16          // шагов в таблице
#define AYFX_PKT    19          // самый длинный пакет (ROW)
#define AYFX_VER    2           // версия протокола (INFO)

enum { AYFX_P_ROW = 1, AYFX_P_TABLE = 2, AYFX_P_RESET = 3, AYFX_P_INFO = 4 };   // коды пакетов (как в FT2 sdk)

typedef void (*ayfx_out_fn)(void *user, uint8_t reg, uint8_t val);

typedef struct {                // один таймер: Mix (SID / Sync), FM, Env FM
    uint32_t cnt, dur;          // тактов в текущем шаге, длина шага
    uint8_t  step;              // шаг таблицы
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
    uint8_t   sel;              // выбранный регистр протокола (#C0 / #C1 / прочие)
    uint8_t   on, key;          // открыт ли поток; сколько букв ключа "TRIN" пришло
    uint8_t   pkt[AYFX_PKT], pos, need;   // разбор потока
    uint8_t   info;             // ответ INFO: какой байт следующий
    ayfx_out_fn out;
    void     *user;
} ayfx_t;

void ayfx_init(ayfx_t *a, ayfx_out_fn out, void *user);
void ayfx_reset(ayfx_t *a);                                   // всё выкл, таблицы - нули
void ayfx_select(ayfx_t *a, uint8_t reg);                     // Спектрум выбрал регистр (запись в #FFFD)
void ayfx_off(ayfx_t *a);                                     // все эффекты выкл, регистры - как у Спектрума
bool ayfx_port_write(ayfx_t *a, uint8_t reg, uint8_t val);    // reg #C0 / #C1; false - не наш регистр
bool ayfx_port_read(ayfx_t *a, uint8_t reg, uint8_t *val);    // false - не наш (отвечает тень AY)
bool ayfx_port_peek(const ayfx_t *a, uint8_t reg, uint8_t *val); // ответ без сдвига (RP: заранее в PIO)
void ayfx_port_read_done(ayfx_t *a, uint8_t reg);             // чтение было - INFO на следующий байт
bool ayfx_reg_write(ayfx_t *a, uint8_t reg, uint8_t val);     // AY 0..15 от Спектрума
void ayfx_tick(ayfx_t *a, uint32_t n);
void ayfx_frame(ayfx_t *a);
bool ayfx_active(const ayfx_t *a);                            // хоть один эффект включён
