// Команды программы-драйвера к RP-GS: пакет -> ответ (doc/trinity-api.md, раздел 7). Откуда пришёл пакет, этот слой не знает:
// сейчас его несут порты GS за ключом (gs_dev_gs.c), в отладке - консоль. Только ядро 0.
// Номера и ключ - только здесь и в sdk/trinity_gs.asm: сменить номер - поменять строку тут и там, остальной код берёт имена
#ifndef GS_CTL_H
#define GS_CTL_H

#include <stdint.h>

// Канал в портах GS: команды #BB, выбраны из свободных в GS
#define GS_CTL_SIGN      "TRINITY"  // ключ, открывает канал
#define GS_CTL_CMD_KEY   0xFB       // байт ключа в #B3
#define GS_CTL_CMD_BYTE  0xFC       // байт пакета в #B3
#define GS_CTL_CMD_CLOSE 0xFD       // закрыть канал

#define GS_CTL_PROTO   1
#define GS_CTL_FW      1        // версия прошивки RP-GS
#define GS_CTL_IN_MAX  16       // данные пакета, байт
#define GS_CTL_OUT_MAX 64       // ответ, байт: больше всех ответ кривой
#define GS_CTL_POINTS  32       // точек кривой: от 20 Гц до 20 кГц, поровну по логарифму

// Коды пакетов
enum {
    GS_CTL_CFG_GET    = 0xD0,
    GS_CTL_CFG_SET    = 0xD1,
    GS_CTL_CFG_SAVE   = 0xD2,
    GS_CTL_PRESET     = 0xD3,
    GS_CTL_STAT       = 0xD4,
    GS_CTL_STAT_RESET = 0xD5,
    GS_CTL_CURVE      = 0xD6,
    GS_CTL_VERSION    = 0xD7
};

#define GS_CTL_OK  0x00
#define GS_CTL_ERR 0xFF
#define GS_CTL_CURVE_ALL 0xFF   // кривая: итог. Иначе ступень: GS_ST_*, GS_CURVE_BASS, GS_CURVE_TREBLE (gs_filter.h)

void gs_ctl_set_save(int (*save)(void));    // запись настроек: железо знает прошивка. 1 - записано
int  gs_ctl_packet(uint8_t code, const uint8_t *in, int len, uint8_t *out);    // возвращает длину ответа

#endif
