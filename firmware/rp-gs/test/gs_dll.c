// RP-GS для эмулятора (Unreal, GSType=TRIN): тот же движок, что в прошивке, порты #BB / #B3.
// Ядро 0 работает на каждом обращении Спектрума к портам (он всё равно опрашивает статус в цикле).
// Диск шлюза секторов - файл-образ trinity_sd.img рядом с dll (нет файла - SD нет). Звука пока нет.
//   tgs_init  - старт, dir - папка dll (с \ в конце)
//   tgs_out   - запись порта #BB / #B3
//   tgs_in    - чтение порта #BB / #B3
//   tgs_reset - сброс (кнопка, #33 бит 7)
// Проверка тестов шины: TGS_NOISE=N в окружении - каждое N-е чтение #B3 с битом 2 наоборот, каждое 3N-е - лишнее
// чтение перед ним (байт пропал, как на макетке RP-AY)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs/gs_cfg.h"
#include "gs/gs_core.h"
#include "gs/gs_dev_gs.h"
#include "gs/gs_ctl.h"

#define DLL __declspec(dllexport)
#define RAM_SIZE (4u << 20)
#define WORK_N 2                    // проходов ядра 0 на обращение

static FILE *img;
static long noise, reads;

static int d_init(void) { return img != 0; }
static int d_read(uint32_t s, uint8_t *b) {
    if (!img || _fseeki64(img, (__int64)s * 512, SEEK_SET)) return 0;
    memset(b, 0, 512);
    fread(b, 1, 512, img);
    return 1;
}
static int d_write(uint32_t s, const uint8_t *b) {
    if (!img || _fseeki64(img, (__int64)s * 512, SEEK_SET)) return 0;
    int ok = fwrite(b, 1, 512, img) == 512;
    fflush(img);
    return ok;
}
static const gs_disk_t file_disk = { d_init, d_read, d_write, 0 };

static void work(void) { for (int i = 0; i < WORK_N; i++) gs_core_work(); }

static uint8_t code_of(int port) { return (port & 0xFF) == 0xBB ? GS_CODE_BB : GS_CODE_B3; }

DLL int tgs_init(const char *dir) {
    char path[1024];
    gs_cfg_defaults();
    gs_core_init();
    gs_gs_set_ram(malloc(RAM_SIZE), RAM_SIZE);
    gs_ctl_init(0, 0);
    snprintf(path, sizeof path, "%strinity_sd.img", dir);
    img = fopen(path, "r+b");
    gs_gs_set_disk(&file_disk);
    const char *n = getenv("TGS_NOISE");
    noise = n ? atol(n) : 0;
    return 1;
}

DLL void tgs_out(int port, int v) {
    gs_core_port_write(code_of(port), (uint8_t)v);
    work();
}

// Статус - без последствий (как образ в PIO), данные - чтение снимает бит данных
DLL int tgs_in(int port) {
    work();
    uint8_t c = code_of(port);
    if (c == GS_CODE_BB) return gs_core_port_peek(c);
    reads++;
    if (noise && reads % (noise * 3) == noise / 2) { gs_core_port_read(c); work(); }  // лишнее чтение: байт пропал (не там, где помеха)
    uint8_t v = gs_core_port_read(c);
    if (noise && reads % noise == 0) v ^= 0x04;     // помеха на D2
    return v;
}

DLL void tgs_reset(void) { gs_core_init(); }
