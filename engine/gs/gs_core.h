// Коре RP-GS (арбитр, doc/trinity-config.md, п.23-38): таблица устройств, кто звучит, микшер.
// Про GS не знает. Новое устройство - файл с таблицей gs_dev_t и строка в списке (gs_core.c).
#ifndef GS_CORE_H
#define GS_CORE_H

#include <stdint.h>

typedef struct {
    const char *name;
    int  (*enabled)(void);                  // включено в настройках
    void (*reset)(void);                    // порты живут, звука нет
    void (*sound_stop)(void);               // замолчать: его вытеснил другой
    void (*port_write)(uint8_t code, uint8_t v);    // шина: только быстро
    uint8_t (*port_read)(uint8_t code);     // шина: только быстро
    void (*work)(void);                     // ядро 0: команды, загрузка
    void (*audio)(float *out, int n);       // ядро 1: добавить звук к буферу
} gs_dev_t;

void gs_core_init(void);
void gs_core_claim(const gs_dev_t *dev);    // устройство начало играть: последний выключает мешающего (п.37)
const gs_dev_t *gs_core_active(void);       // кто звучит, NULL - никто
void gs_core_audio(float *out, int n);      // ядро 1: буфер L, R, L, R...: звук, цепочка, громкость

#endif
