// Плеер модулей как плагин: gs_mod выбирает первый, кто взял файл (как устройства в коре, gs_dev_t). Новый формат - новая таблица
#ifndef GS_PLAYER_H
#define GS_PLAYER_H

#include <stdint.h>

typedef struct {
    int (*accepts)(const void *data, int size);     // 1 - формат мой
    int (*load)(const void *data, int size, uint8_t pos, int writable);    // 1 - готов играть с позиции pos
    void (*rate_sync)(void);                        // частота смешивания могла смениться: темп и высота те же
    int (*audio)(float *out, int n);                // добавить звук; 0 - песня остановилась сама, блок всё равно заполнен
    void (*set_speed)(uint8_t v);                   // 1..#1F скорость, #20..#FF темп
    uint8_t (*song_pos)(void);
    uint8_t (*pat_pos)(void);
    uint8_t (*speed)(void);
    uint8_t (*tempo)(void);
    int (*period)(int chan);                        // период ноты, 0 - нет ноты
    uint8_t (*vol)(int chan);                       // 0..64
} gs_player_t;

extern const gs_player_t gs_player_pt;              // ProTracker: 4 канала, 31 сэмпл
extern const gs_player_t gs_player_pocket;          // остальные MOD

int gs_mod_gain(int vol);                           // громкость канала с учётом общей и модуля

#endif
