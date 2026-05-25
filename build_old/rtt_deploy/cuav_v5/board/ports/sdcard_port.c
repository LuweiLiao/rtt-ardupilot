/*
 * Copyright (c) 2025 ArduPilot RTT Port Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * SD card initialization for CUAV v5 (STM32F767) ArduPilot RTT port.
 *
 * This file serves two purposes:
 *   1. Provides auto-init SD card mount via RTT INIT_APP_EXPORT, ensuring
 *      the SDIO driver (drv_sdio.o) and DFS filesystem are linked into the
 *      final ELF even with --gc-sections.
 *   2. Powers on the SD card (PG7 = VDD_3V3_SD_CARD_EN), initialises SDIO,
 *      creates /sdcard mount point, mounts elmfat, and prepares /sdcard/APM
 *      for ArduPilot's AP_Filesystem.
 *
 * SDMMC1 pins: PC8(D0) PC9(D1) PC10(D2) PC11(D3) PC12(CLK) PD2(CMD) — all AF12
 * SD card power: PG7 (HIGH = enabled). RTT pin number = 6*16+7 = 103.
 */

#include <rtthread.h>
#include <dfs_fs.h>
#include <dfs_file.h>

#ifdef RT_USING_DFS
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef BSP_USING_SDIO

/* PG7 — VDD_3V3_SD_CARD_EN on CUAV v5 */
#define SD_CARD_POWER_PIN    103   /* GPIOG pin 7: 6*16+7 = 103 */

static int sdcard_mount(void)
{
    int ret;

    /* 1. Power on the SD card */
    rt_pin_mode(SD_CARD_POWER_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(SD_CARD_POWER_PIN, PIN_HIGH);
    rt_kprintf("[sdcard] SD card power enabled (PG7 HIGH)\n");

    /* 2. Initialise the SDIO block device driver.
     *    rt_hw_sdio_init() is also INIT_DEVICE_EXPORT'd in drv_sdio.c,
     *    but calling it explicitly ensures the symbol is referenced so
     *    the linker keeps drv_sdio.o even under --gc-sections. */
    ret = rt_hw_sdio_init();
    if (ret != 0)
    {
        rt_kprintf("[sdcard] rt_hw_sdio_init() failed: %d\n", ret);
        /* Don't fail boot — SD card may not be inserted */
        return 0;
    }

    /* 3. Wait for SD card detection and stabilisation */
    rt_thread_mdelay(500);

    /* 4. Create mount point directory */
#ifdef RT_USING_DFS
    mkdir("/sdcard", 0x777);
#endif

    /* 5. Mount the filesystem */
    ret = dfs_mount("sd0", "/sdcard", "elm", 0, 0);
    if (ret == 0)
    {
        rt_kprintf("[sdcard] SD card mounted at /sdcard (elmfat)\n");
    }
    else
    {
        rt_kprintf("[sdcard] dfs_mount(\"sd0\",\"/sdcard\",\"elm\") failed: %d\n", ret);
        /* Non-fatal — board continues without SD */
    }

    return 0;
}
/* Auto-initialise during RTT apps init phase (after device/board init) */
INIT_APP_EXPORT(sdcard_mount);

/* Second-stage init: create /sdcard/APM directory tree for ArduPilot.
 *
 * NOTE: DFS V1 + elmfat does not support symbolic links, so we create
 * /sdcard/APM as a regular directory.  ArduPilot's AP_Filesystem uses
 * paths like /sdcard/APM/LOGS/, /sdcard/APM/TERRAIN/, etc.
 */
static int sdcard_remount(void)
{
#ifdef RT_USING_DFS
    /* Ensure the APM base directory exists on the SD card */
    mkdir("/sdcard/APM", 0x777);
    mkdir("/sdcard/APM/LOGS", 0x777);
    mkdir("/sdcard/APM/TERRAIN", 0x777);
    rt_kprintf("[sdcard] /sdcard/APM directories ready\n");
#endif
    return 0;
}
INIT_APP_EXPORT(sdcard_remount);

#endif /* BSP_USING_SDIO */
