// Эффекты GS: сэмплы с параметрами и каналы 0..3 (голоса 4..7 движка). Правила - FULGSCOM.TXT, команды #38..#49, #80..#9B
#ifndef GS_FX_H
#define GS_FX_H

#include <stdint.h>

#define GS_FX_MAX 60            // как в ROM
#define GS_FX_CHANS 4
#define GS_FX_NO_LOOP 0xFF0000u // старший байт #FF - без цикла

typedef struct {
    const int8_t *data;
    uint32_t len;
    uint32_t loop_begin, loop_end;
    uint8_t note, vol, finetune, prio, seek_first, seek_last;
} gs_fx_t;

void gs_fx_reset(void);                 // сэмплов нет, каналы молчат
void gs_fx_stop(uint8_t chan_mask);     // бит 0 - канал 0
gs_fx_t *gs_fx_get(int handle);         // 1..количество, иначе NULL
int  gs_fx_new(const void *data);       // номер нового сэмпла с умолчаниями, 0 - нет места
void gs_fx_end(int handle, uint32_t len);
int  gs_fx_count(void);
void gs_fx_play(int handle);            // поиск канала по SeekFirst, SeekLast, приоритету
void gs_fx_play_chan(int handle, int chan, int note, int vol);  // note, vol < 0 - из параметров сэмпла
void gs_fx_set_master(uint8_t vol);     // 0..64
int  gs_fx_active(void);                // 1 - какой-то канал звучит

#endif
