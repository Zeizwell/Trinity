// Прослойка FatFs -> sd_card. Диск один, без часов
#include "ff.h"
#include "diskio.h"
#include "sd_card.h"

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    return sd_init() ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    return pdrv || !sd_ready() ? STA_NOINIT : 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buf, LBA_t sector, UINT count) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    return sd_read((uint32_t)sector, buf, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buf, LBA_t sector, UINT count) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    return sd_write((uint32_t)sector, buf, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buf) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: return sd_sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(LBA_t *)buf = sd_sectors(); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buf = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buf = 1; return RES_OK;      // не знаем: FatFs не выравнивает
    default: return RES_PARERR;
    }
}
