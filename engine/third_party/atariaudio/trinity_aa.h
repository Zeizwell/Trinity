// Trinity: наши правки AtariAudio (SOURCE.txt)
#pragma once
#include <stddef.h>

// Холодные таблицы (база длительностей, синтез YM, плеер .ym) - на RP во флеше, не в ОЗУ
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TRINITY_COLD __attribute__((section(".flashdata.atariaudio")))
#else
#define TRINITY_COLD
#endif

// Большие блоки (память Atari 4 МБ, файл, распаковка) - через крючок: на RP - PSRAM (engine/sndh), иначе malloc / free
extern void *(*g_aaBigAlloc)(size_t size);
extern void (*g_aaBigFree)(void *p);

#ifdef __cplusplus
#include <new>
// Объект машины (SndhRenderer ~6 К) - тоже большой блок: куча RP (5.5 К) его не вмещает - panic, ядро 0 встаёт.
// Создают / удаляют сами Create / Destroy (конструкторы закрыты): g_aaBigAlloc + new (место), деструктор + g_aaBigFree
#endif
