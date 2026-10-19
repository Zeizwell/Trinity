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
#define PIN_RGB         20  // светодиод WS2812 на Spotpear

// ---- Режимы (doc 14а). Порядок - как в таблице переходов modes.c
typedef enum { MODE_AY, MODE_FX, MODE_ATARI, MODE_UPDATE, MODE_TEST, MODE_COUNT } mode_t;

typedef struct {
    // --- чип: тайминги записи (YM2203 на макете; AY-3-8910 / YM2149 на плате - другие числа)
    uint32_t phim_hz;           // такт чипа, Гц (для пауз в тактах чипа)
    uint16_t wr_pulse_ns;       // длительность /WR при записи в чип
    uint16_t setup_ns;          // данные и A0 -> /WR вниз
    uint16_t wait_addr_clk;     // после записи адреса - пауза, тактов чипа (YM2203: 17)
    uint16_t wait_ssg_clk;      // после данных в регистр SSG 0..15 (YM2203: 0, берём с запасом)
    uint16_t wait_fm_clk;       // после данных в регистр FM (YM2203: 83)
    // --- ответ на чтение со стороны Спектрума
    uint8_t  status_answer;     // чтение A0 = 0 (статус YM2203): 0 - никогда не занят
    // --- режимы
    mode_t   mode_after_reset;  // режим после сброса (/IC) и включения
    bool     silence_on_reset;  // после сброса - тишина в чип (громкости 0)
    uint16_t fx_timeout_s;      // FX: нет пакетов и записей столько секунд - эффекты выкл (0 - никогда)
    uint8_t  chips;             // чипов (TurboSound - 2; на макете второй "в никуда")
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
