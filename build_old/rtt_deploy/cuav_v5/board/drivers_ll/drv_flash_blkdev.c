/*
 * Flash block device driver for RT-Thread DFS.
 *
 * Uses STM32F767 internal Flash sector 11 (256KB) as a block device
 * for elmfat filesystem. Sector 10 is reserved for ArduPilot parameter
 * storage (HAL_Storage / STORAGE_FLASH_PAGE).
 *
 * STM32F767 sector map (single-bank 2MB):
 *   Sectors 0-3:  32KB each
 *   Sector 4:     128KB
 *   Sectors 5-11: 256KB each
 *
 * Sector 11: 0x081C0000, 256KB — used exclusively by this driver.
 *
 * Erase strategy:
 *   Flash can only be erased in whole sectors (256KB). This is a hardware
 *   limitation — we cannot erase smaller units.
 *
 *   Key optimization: before erasing, check if the target block is already
 *   erased (all 0xFF). If so, write directly without erasing the sector.
 *   This preserves all previously written data in the sector.
 *
 *   For sequential logging (writing fresh blocks), this means the sector is
 *   only erased once (on format/mount), then data accumulates without any
 *   further erases until the sector is full.
 *
 *   When rewriting a previously-written block (e.g., FAT table update), an
 *   erase is still required, which destroys all data in the sector. This is
 *   an inherent limitation of STM32 Flash without a second scratch sector.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "drv_flash_ll.h"

#define FLASH_BLKDEV_NAME    "flash0"
#define FLASH_BLKDEV_SECTOR  11
#define FLASH_BLKDEV_BLKSIZE 4096   /* elmfat block size (must be power of 2) */

/* Sector info from drv_flash_ll */
#define _BASE_ADDR  flash_ll_sector_addr(FLASH_BLKDEV_SECTOR)
#define _TOTAL_SIZE flash_ll_sector_size(FLASH_BLKDEV_SECTOR)

static struct rt_device _blkdev;
static struct rt_device_blk_geometry _geom;

/* Small buffer to check if a block is already erased */
static uint8_t _chk_buf[FLASH_BLKDEV_BLKSIZE];

/* Check if a range of blocks [start, start+count) is all 0xFF (erased).
 * Returns 1 if all erased (safe to write directly), 0 if erase needed. */
static int _blocks_are_erased(uint32_t start, uint32_t count)
{
    for (uint32_t b = start; b < start + count; b++) {
        uint32_t addr = _BASE_ADDR + b * FLASH_BLKDEV_BLKSIZE;
        if (flash_ll_read(addr, _chk_buf, FLASH_BLKDEV_BLKSIZE) != 0)
            return 0;
        /* Fast check: first 4 bytes (aligned read) */
        uint32_t w;
        memcpy(&w, _chk_buf, 4);
        if (w != 0xFFFFFFFF) return 0;
    }
    return 1;
}

static rt_err_t _blk_init(rt_device_t dev)
{
    _geom.bytes_per_sector = FLASH_BLKDEV_BLKSIZE;
    _geom.block_size       = FLASH_BLKDEV_BLKSIZE;
    _geom.sector_count     = _TOTAL_SIZE / FLASH_BLKDEV_BLKSIZE;
    return RT_EOK;
}

static rt_err_t _blk_open(rt_device_t dev, uint16_t oflag)
{
    return RT_EOK;
}

static rt_err_t _blk_close(rt_device_t dev)
{
    return RT_EOK;
}

static rt_ssize_t _blk_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    uint32_t off = pos * FLASH_BLKDEV_BLKSIZE;
    if (off + size * FLASH_BLKDEV_BLKSIZE > _TOTAL_SIZE) return 0;
    return flash_ll_read(_BASE_ADDR + off, (uint8_t *)buffer, size * FLASH_BLKDEV_BLKSIZE)
           / FLASH_BLKDEV_BLKSIZE;
}

static rt_ssize_t _blk_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    uint32_t off = pos * FLASH_BLKDEV_BLKSIZE;
    uint32_t bytes = size * FLASH_BLKDEV_BLKSIZE;
    if (off + bytes > _TOTAL_SIZE) return 0;

    /* Only erase the sector if target blocks are not already erased.
     * This preserves previously written data for sequential logging. */
    if (!_blocks_are_erased(pos, size)) {
        if (flash_ll_unlock() != 0) return 0;
        if (flash_ll_erase_sector(FLASH_BLKDEV_SECTOR) != 0) {
            flash_ll_lock();
            return 0;
        }
        flash_ll_lock();
    }

    if (flash_ll_unlock() != 0) return 0;
    int rc = flash_ll_program_bytes(_BASE_ADDR + off, (const uint8_t *)buffer, bytes);
    flash_ll_lock();

    return (rc == 0) ? size : 0;
}

static rt_err_t _blk_control(rt_device_t dev, int cmd, void *args)
{
    if (cmd == RT_DEVICE_CTRL_BLK_GETGEOME) {
        struct rt_device_blk_geometry *geo = (struct rt_device_blk_geometry *)args;
        if (geo) *geo = _geom;
        return RT_EOK;
    }
    if (cmd == RT_DEVICE_CTRL_BLK_ERASE) {
        /* No-op: erase is done lazily on write */
        return RT_EOK;
    }
    return -RT_ENOSYS;
}

int rt_hw_flash_blkdev_init(void)
{
    _blkdev.type    = RT_Device_Class_Block;
    _blkdev.init    = _blk_init;
    _blkdev.open    = _blk_open;
    _blkdev.close   = _blk_close;
    _blkdev.read    = _blk_read;
    _blkdev.write   = _blk_write;
    _blkdev.control = _blk_control;

    /* Pre-fill geometry */
    _geom.bytes_per_sector = FLASH_BLKDEV_BLKSIZE;
    _geom.block_size       = FLASH_BLKDEV_BLKSIZE;
    _geom.sector_count     = _TOTAL_SIZE / FLASH_BLKDEV_BLKSIZE;

    rt_err_t rc = rt_device_register(&_blkdev, FLASH_BLKDEV_NAME,
                                     RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE);
    if (rc != RT_EOK) {
        rt_kprintf("[flash0] register failed: %d\n", rc);
        return rc;
    }

    rt_kprintf("[flash0] registered: sector %d, %u KB, block %u bytes\n",
               FLASH_BLKDEV_SECTOR, _TOTAL_SIZE / 1024, FLASH_BLKDEV_BLKSIZE);
    return 0;
}
