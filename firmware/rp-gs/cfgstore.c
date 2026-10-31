#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "gs/gs_cfg.h"
#include "cfgstore.h"

#define STORE_OFF   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)     // последний сектор
#define STORE_MAGIC 0x53474654u
#define STORE_VER   2u

typedef struct {
    uint32_t magic, ver, size, sum;
    gs_cfg_t cfg;
} store_t;

static uint32_t sum_of(const gs_cfg_t *c) {
    const uint8_t *p = (const uint8_t *)c;
    uint32_t s = 0;
    for (uint32_t i = 0; i < sizeof *c; i++) s = s * 31u + p[i];
    return s;
}

int cfgstore_load(void) {
    const store_t *st = (const store_t *)(XIP_BASE + STORE_OFF);
    if (st->magic != STORE_MAGIC || st->ver != STORE_VER || st->size != sizeof st->cfg) return 0;
    if (st->sum != sum_of(&st->cfg)) return 0;
    gs_cfg = st->cfg;
    return 1;
}

int cfgstore_save(void) {
    static uint8_t page[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
    store_t *st = (store_t *)page;
    memset(page, 0xFF, sizeof page);
    st->magic = STORE_MAGIC, st->ver = STORE_VER, st->size = sizeof st->cfg;
    st->cfg = gs_cfg;
    st->sum = sum_of(&st->cfg);
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(STORE_OFF, FLASH_SECTOR_SIZE);
    flash_range_program(STORE_OFF, page, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
    return memcmp((const void *)(XIP_BASE + STORE_OFF), page, sizeof(store_t)) == 0;
}
