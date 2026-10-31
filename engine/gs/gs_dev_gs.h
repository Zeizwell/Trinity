// Устройство GS (лицо на портах). Модуль запускают командами или напрямую (gsplay)
#ifndef GS_DEV_GS_H
#define GS_DEV_GS_H

#include <stdint.h>

// Коды портов для port_write / port_read
#define GS_PORT_B3 0    // запись: данные, чтение: вывод
#define GS_PORT_BB 1    // запись: команда, чтение: статус (бит 7 данные, бит 0 команда)

void gs_gs_set_ram(void *mem, uint32_t size);       // память под модули
int  gs_gs_play_mod(const void *data, int size);    // 1 - играет
void gs_gs_stop(void);

#endif
