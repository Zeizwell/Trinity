// Коре RP-GS (арбитр, doc/trinity-config.md, п.23-38): таблица устройств, кто звучит, микшер.
// Про GS не знает. Новое устройство - файл с таблицей gs_dev_t и строка в списке (gs_core.c).
#ifndef GS_CORE_H
#define GS_CORE_H

#include <stdint.h>

// Код порта от CPLD (п.12). Новый порт - код здесь и строка в таблице (gs_core.c)
enum { GS_CODE_B3, GS_CODE_BB, OPL4_CODE_C4, OPL4_CODE_C5, OPL4_CODE_C6, OPL4_CODE_C7, GS_CODE_COUNT };

typedef struct {
    const char *name;
    int  (*enabled)(void);                  // включено в настройках
    void (*reset)(void);                    // порты живут, звука нет
    void (*sound_stop)(void);               // замолчать: его вытеснил другой
    void (*port_write)(uint8_t code, uint8_t v);    // шина: только быстро
    uint8_t (*port_read)(uint8_t code);     // шина: только быстро
    uint8_t (*port_peek)(uint8_t code);     // что отдаст чтение, без последствий: для образа в PIO
    void (*work)(void);                     // ядро 0: команды, загрузка
    void (*audio)(float *out, int n);       // ядро 1: добавить звук к буферу
} gs_dev_t;

void gs_core_init(void);
// Замок ядра 0 (меняет голоса, модуль) и ядра 1 (считает звук). Шина его не берёт. На ПК - нет
void gs_core_set_lock(void (*lock)(void), void (*unlock)(void));
void gs_core_lock(void);
void gs_core_unlock(void);
void gs_core_claim(const gs_dev_t *dev);    // устройство начало играть: последний выключает мешающего (п.37)
const gs_dev_t *gs_core_active(void);       // кто звучит, NULL - никто
void gs_core_work(void);                     // ядро 0: команды всех устройств
void gs_core_audio(float *out, int n);      // ядро 1: буфер L, R, L, R...: звук, цепочка, громкость

// Шина: по коду порта - устройство. Только быстро
void gs_core_port_write(uint8_t code, uint8_t v);
uint8_t gs_core_port_read(uint8_t code);
uint8_t gs_core_port_peek(uint8_t code);

// Настройки на лету с ядра 0: под замком, звук не застанет их наполовину
void gs_core_set_mode(int mode);

#endif
