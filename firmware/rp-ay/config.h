// Настройки RP-AY - ВСЁ, что может понадобиться подкрутить, здесь, одной структурой (cfg).
// Код читает только cfg, сам чисел не держит. Сейчас - заводские значения (cfg_defaults);
// потом - из флеша и командой со Спектрума (пакет CFG), без правки кода.
// Решения - FT2 doc/zx-trinity.md, раздел 14а.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---- Ножки макета (FT2 doc/zx-trinity.md, п.15). На плате - другие, тоже только здесь.
#define PIN_SLOT_D0     0   // слот: D0..D7 = GP0..GP7 (RP выдаёт только на чтение своего чипа)
#define PIN_SLOT_CS     8   // /CS чипа (/YM1_CS)
#define PIN_SLOT_WR     9   // /WR (/AWR)
#define PIN_SLOT_RD     10  // /RD (/ARD)
#define PIN_SLOT_A0     11  // A0 (AA0)
#define PIN_SLOT_PHIM   12  // такт чипа из слота (φM) - на макете RP его только слушает
#define PIN_SLOT_IC     13  // /IC - сброс
#define PIN_CHIP_D0     28  // в чип: D0..D7 = GP28..GP35
#define PIN_CHIP_CS     36
#define PIN_CHIP_WR     37
#define PIN_CHIP_A0     38
#define PIN_CHIP_RD     39  // /RD чипа - только для теста (режим TEST); на чипе подтяжка 10к к +5 В
#define PIN_RGB         20  // светодиод WS2812 на Spotpear

// ---- Режимы (doc 14а). Порядок - как в таблице переходов modes.c
typedef enum { MODE_AY, MODE_FX, MODE_ATARI, MODE_UPDATE, MODE_TEST, MODE_COUNT } mode_t;

typedef struct {
    // --- чип: тайминги записи - как у Z80 на 3.5 МГц (в этом ритме чипы работали всегда); в ПОЛУтактах Z80
    uint32_t z80_hz;            // такт Z80, от которого считаем (3.5 МГц: такт 286 нс)
    uint8_t  setup_ht;          // данные и A0 -> /WR вниз (Z80: ~1 такт, с T1)
    uint8_t  pulse_ht;          // длительность /WR (Z80, цикл ввода-вывода: T2 + TW + полтакта T3 = 2.5 такта)
    uint8_t  gap_addr_ht;       // после записи адреса до данных (Z80: OUT адреса -> LD B,n -> OUT данных, ~11 тактов)
    uint8_t  gap_ssg_ht;        // после данных в регистр 0..15 до следующей записи (так же)
    uint32_t phim_hz;           // такт чипа, Гц (для паузы FM - в тактах чипа)
    uint16_t wait_fm_clk;       // после данных в регистр FM (YM2203: 83 такта чипа; Z80 ждёт флаг занятости, RP - сама)
    uint16_t rd_pulse_ns;       // тест: /RD вниз -> читаем данные
    // --- ответ на чтение со стороны Спектрума
    uint8_t  status_answer;     // чтение A0 = 0 (статус YM2203): 0 - никогда не занят
    // --- режимы
    mode_t   mode_after_reset;  // режим после сброса (/IC) и включения
    bool     silence_on_reset;  // после сброса - тишина в чип (громкости 0)
    uint16_t boot_beep_ms;      // при включении RP сама играет ноту в канал A, мс (0 - нет): проверка RP -> чип
    uint16_t fx_timeout_s;      // FX: нет пакетов и записей столько секунд - эффекты выкл (0 - никогда)
    uint8_t  chips;             // чипов: 2 - RP сама выбирает чип по #FE / #FF (плата); 1 - выбор не её (макет за CPLD)
    // --- AY FX (таймерные эффекты, движок engine/ayfx.c)
    uint32_t ay_clock_hz;       // такт AY для таймеров эффектов: такт тона = ay_clock_hz / 8
    uint8_t  frame_hz;          // кадр для свипа ШИМ, Гц (0 - свипа нет)
    // --- шина слота
    uint8_t  wr_sample_delay;   // PIO: тактов PIO после /WR вниз до чтения данных (задано в bus.pio, тут - для лога)
    // --- служебное
    uint8_t  led_bright;        // яркость светодиода 0..255
    uint16_t log_period_ms;     // счётчики в USB-консоль, мс (0 - не писать)
} cfg_t;

extern cfg_t cfg;
void cfg_defaults(void);
