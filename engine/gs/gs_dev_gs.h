// Устройство GS (лицо на портах). Модуль запускают командами или напрямую (gsplay)
#ifndef GS_DEV_GS_H
#define GS_DEV_GS_H

#include <stdint.h>
#include "gs_core.h"
#include "gs_mod.h"

extern const gs_dev_t gs_dev_gs;

// Коды портов для port_write / port_read
#define GS_PORT_B3 0    // запись: данные, чтение: вывод
#define GS_PORT_BB 1    // запись: команда, чтение: статус (бит 7 данные, бит 0 команда)

// Диск шлюза секторов (SD карты; потом - диск ПК по USB). Сектор 512 байт, 1 - получилось
typedef struct {
    int (*init)(void);
    int (*read)(uint32_t sector, uint8_t *buf);
    int (*write)(uint32_t sector, const uint8_t *buf);
    void (*release)(void);                          // Спектрум ушёл из шлюза: прошивке перечитать FAT
} gs_disk_t;

void gs_gs_set_ram(void *mem, uint32_t size);       // память под модули
void gs_gs_set_disk(const gs_disk_t *disk);         // 0 - диска нет: init отвечает 1
int  gs_gs_sd_gate(int test);                       // пакет GS_CTL_SD: в шлюз секторов (test - диск в ОЗУ), 1 - есть диск
int  gs_gs_play_mod(const void *data, int size, gs_mod_mem_t mem);    // 1 - играет
void gs_gs_stop(void);

#endif
