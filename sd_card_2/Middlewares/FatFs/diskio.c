/* HAL SDIO adapter for FatFs R0.10b. Read-only; polling and aligned bounce buffer. */
#include "ff.h"
#include "diskio.h"
#include "sdio.h"
#include <string.h>
static DSTATUS status = STA_NOINIT;
static uint32_t sector_buffer[128];
static int wait_ready(void)
{
    uint32_t start = HAL_GetTick();
    do {
        if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) return 1;
    } while ((uint32_t)(HAL_GetTick() - start) < 2000U);
    return 0;
}
DSTATUS disk_initialize(BYTE drive)
{
    if (drive != 0) return STA_NOINIT;
    status = STA_NOINIT;
    /* main.c owns card identification and 4-bit configuration. */
    if (hsd.State == HAL_SD_STATE_READY && wait_ready()) status = 0;
    return status;
}
DSTATUS disk_status(BYTE drive)
{
    if (drive != 0 || hsd.State != HAL_SD_STATE_READY) return STA_NOINIT;
    return status;
}
DRESULT disk_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count)
{
    HAL_SD_CardInfoTypeDef info;
    if (drive != 0 || !buffer || !count) return RES_PARERR;
    if (disk_status(drive) & STA_NOINIT) return RES_NOTRDY;
    if (HAL_SD_GetCardInfo(&hsd, &info) != HAL_OK) return RES_ERROR;
    if (sector >= info.LogBlockNbr || count > info.LogBlockNbr - sector) return RES_PARERR;
    while (count--) {
        if (!wait_ready() || HAL_SD_ReadBlocks(&hsd, (uint8_t *)sector_buffer,
                                             sector++, 1, 2000U) != HAL_OK || !wait_ready()) {
            return RES_ERROR;
        }
        memcpy(buffer, sector_buffer, 512);
        buffer += 512;
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    HAL_SD_CardInfoTypeDef info;
    if (drive != 0) return RES_PARERR;
    if (disk_status(drive) & STA_NOINIT) return RES_NOTRDY;
    if (command == CTRL_SYNC) return wait_ready() ? RES_OK : RES_ERROR;
    if (!buffer) return RES_PARERR;
    if (HAL_SD_GetCardInfo(&hsd, &info) != HAL_OK) return RES_ERROR;
    switch (command) {
        case GET_SECTOR_COUNT: *(DWORD *)buffer = info.LogBlockNbr; return RES_OK;
        case GET_SECTOR_SIZE: *(WORD *)buffer = 512; return RES_OK;
        default: return RES_PARERR;
    }
}
