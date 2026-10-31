// Устройство GS (лицо на портах). Команды GS - следующий шаг; пока модуль запускают напрямую
#ifndef GS_DEV_GS_H
#define GS_DEV_GS_H

int  gs_gs_play_mod(const void *data, int size);    // 1 - играет
void gs_gs_stop(void);

#endif
