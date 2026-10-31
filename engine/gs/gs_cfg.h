// Настройки RP-GS - одна структура переменных (doc/trinity-config.md, п.46). Режим - только набор значений этих
// переменных (gs_cfg_mode), любую можно поменять отдельно. Менять можно на лету.
#ifndef GS_CFG_H
#define GS_CFG_H

#include <stdint.h>

typedef enum { GS_MODE_GS, GS_MODE_A500, GS_MODE_A1200, GS_MODE_HIFI, GS_MODE_CUSTOM } gs_mode_t;
typedef enum { GS_RS_STEP, GS_RS_BLEP, GS_RS_INTERP, GS_RS_ROM, GS_RS_CUBIC } gs_resample_t;     // пересчёт: ступеньки / BLEP / линейная / как ROM GS / кубическая
typedef enum { GS_LP_OFF, GS_LP_A500, GS_LP_A1200 } gs_lp_t;             // ФНЧ платы
typedef enum { GS_LED_OFF, GS_LED_MODULE, GS_LED_ON } gs_led_t;          // LED-фильтр: выкл / по команде E0x / всегда

typedef struct {
    // --- железо
    uint32_t sys_khz;           // такт RP, кГц
    uint32_t gs_ram_kb;         // память GS в PSRAM, КБ (п.60 не решён, рабочее значение)
    // --- устройства
    uint8_t  gs_on;             // GS отвечает на порты и звучит
    // --- вывод
    uint32_t rate_hz;           // частота вывода, Гц (I2S)
    uint8_t  oversample;        // смешивание: 1 - на частоте вывода, 2 - вдвое выше + понижение FIR (как pt2-clone). Действует с gs_core_init
    uint8_t  voices;            // каналов в микшере
    uint8_t  master_pct;        // общая громкость, %
    // --- звук (цепочка: пересчёт -> громкость -> сведение -> фильтры)
    gs_mode_t     mode;         // какой режим выставлен последним (GS_MODE_CUSTOM - руками)
    gs_resample_t resample;     // пересчёт
    uint32_t hold_hz;           // GS_RS_STEP, GS_RS_ROM: частота тиков, Гц (настоящий GS - 37500, прерывание его Z80)
    uint8_t  pwm;               // GS_RS_BLEP: громкость ШИМ, как Paula (0 - умножение)
    uint8_t  ramp;              // плавность громкости 1-2 мс: 0 / 1
    uint8_t  stereo_pct;        // ширина стерео, %: 100 - жёстко L R R L, 0 - моно
    gs_lp_t  lp;                // ФНЧ платы
    gs_led_t led;               // LED-фильтр
    uint8_t  hp;                // ФВЧ ~5 Гц: 0 / 1
    uint8_t  rc16k;             // RC после ЦАП GS (1 кОм + 10 нФ, схема MultiSound): 0 / 1
    // --- общие ручки: на все режимы, смена режима их не меняет. Сумма с окраской режима - в пределах -12..12 дБ
    int8_t   bass_db;           // НЧ, -12..12 дБ, 0 - не трогать
    int8_t   treble_db;         // ВЧ, -12..12 дБ, 0 - не трогать
    // --- окраска режима (HiFi), остальные режимы - 0
    int8_t   color_bass_db;
    int8_t   color_treble_db;
    uint8_t  comp;              // компрессор и мягкий ограничитель на выходе: 0 / 1
} gs_cfg_t;

extern gs_cfg_t gs_cfg;

void gs_cfg_defaults(void);
uint32_t gs_cfg_mix_hz(void);            // частота смешивания
void gs_cfg_mode(gs_mode_t mode);       // выставить переменные режима
const char *gs_cfg_mode_name(gs_mode_t mode);

#endif
