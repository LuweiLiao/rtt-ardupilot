/**
 * test_E_sdcard — External module: microSD via SDMMC + RT-Thread DFS (elm FAT)
 *
 * Layer: E* (mount point check, POSIX create / write / read / unlink)
 * Status: SD/FS smoke — uses board INIT_APP_EXPORT background mount + POSIX on /APM.
 *
 * Mount layout (cuav_v5): SDMMC1 → dfs_mount at "/" (not /sd); ArduPilot dirs under /APM.
 * Test file: /APM/.rtt_e_sdcard_smoke (hidden, removed on success; does not touch logs/params).
 *
 * No card / mount failure: TEST_FAIL with rtt_sd_mount_* diagnostics (no fake PASS).
 *
 * Prerequisites: BSP_USING_SDIO + drv_sdio (HAL_Drivers); L6_sdmmc optional.
 * Reference: modules/rt-thread/examples/test/fs_test.c; AP_Filesystem/examples/File_IO
 *
 * Build: scons --target=cuav_v5 --test=E_sdcard -j$(nproc)
 * Hardware: microSD present on CUAV v5 SDMMC1 slot (power PG7 via board init).
 */

#include "test_runner.h"
#include <rtthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

/* Board SD mount telemetry (rt_board_init.c, BSP_USING_SDIO) */
extern volatile int rtt_sd_mount_stage;
extern volatile int rtt_sd_mount_result;

extern void ap_rtt_iwdg_kick(void);

#define SD_MOUNT_DIR       "/APM"
#define SD_TEST_FILE       "/APM/.rtt_e_sdcard_smoke"
#define SD_POLL_MS         500
#define SD_WAIT_MAX_MS     30000U

#define SD_TEST_PATTERN_LEN  32U

static const uint8_t sd_test_pattern[SD_TEST_PATTERN_LEN] = {
    0xE5, 0xDC, 0x41, 0x52, 0xD0, 0x0F, 0x1A, 0x2B,
    0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81, 0x92, 0xA3,
    0xB4, 0xC5, 0xD6, 0xE7, 0xF8, 0x09, 0x1A, 0x2B,
    0x3C, 0x4D, 0x5E, 0x6F, 0x70, 0x81, 0x92, 0xA3,
};

static int sd_mount_point_ready(void)
{
    struct stat st;

    if (rtt_sd_mount_result == 0) {
        return 1;
    }

    if (stat(SD_MOUNT_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 1;
    }

    return 0;
}

static void sd_wait_for_mount(void)
{
    uint32_t elapsed_ms = 0;

    test_printf("    mount_point=%s (root FS at \"/\")\r\n", SD_MOUNT_DIR);
    test_printf("    waiting up to %u ms for SD mount (background sdmnt thread)\r\n",
                (unsigned)SD_WAIT_MAX_MS);

    while (elapsed_ms < SD_WAIT_MAX_MS) {
        ap_rtt_iwdg_kick();

        if (sd_mount_point_ready()) {
            test_printf("    mount ok: stage=%d result=%d\r\n",
                        (int)rtt_sd_mount_stage,
                        (int)rtt_sd_mount_result);
            return;
        }

        if (rtt_sd_mount_result == -4) {
            test_printf("    mount failed: stage=%d result=%d (no card or FS error)\r\n",
                        (int)rtt_sd_mount_stage,
                        (int)rtt_sd_mount_result);
            TEST_FAIL("SD mount failed — check SDIO power/DFS (see stage/result)");
        }

        rt_thread_mdelay(SD_POLL_MS);
        elapsed_ms += SD_POLL_MS;
    }

    test_printf("    mount timeout: stage=%d result=%d errno=%d\r\n",
                (int)rtt_sd_mount_stage,
                (int)rtt_sd_mount_result,
                (int)errno);
    TEST_FAIL("SD mount timeout — SDIO or dfs_mount not ready");
}

static void sd_smoke_file_rw(void)
{
    int fd;
    ssize_t n;
    uint8_t readbuf[SD_TEST_PATTERN_LEN];

    TEST_STEP("POSIX file create / write / read / unlink");

    fd = open(SD_TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) {
        test_printf("    open(%s, WR) failed errno=%d\r\n", SD_TEST_FILE, errno);
        TEST_FAIL("cannot create test file on SD");
    }

    n = write(fd, sd_test_pattern, SD_TEST_PATTERN_LEN);
    if (n != (ssize_t)SD_TEST_PATTERN_LEN) {
        test_printf("    write returned %d (expected %u) errno=%d\r\n",
                    (int)n, (unsigned)SD_TEST_PATTERN_LEN, errno);
        close(fd);
        TEST_FAIL("SD write length mismatch");
    }

    if (close(fd) != 0) {
        test_printf("    close after write failed errno=%d\r\n", errno);
        TEST_FAIL("close after write failed");
    }

    fd = open(SD_TEST_FILE, O_RDONLY, 0);
    if (fd < 0) {
        test_printf("    open(%s, RD) failed errno=%d\r\n", SD_TEST_FILE, errno);
        unlink(SD_TEST_FILE);
        TEST_FAIL("cannot reopen test file for read");
    }

    memset(readbuf, 0, sizeof(readbuf));
    n = read(fd, readbuf, SD_TEST_PATTERN_LEN);
    close(fd);

    if (n != (ssize_t)SD_TEST_PATTERN_LEN) {
        test_printf("    read returned %d (expected %u) errno=%d\r\n",
                    (int)n, (unsigned)SD_TEST_PATTERN_LEN, errno);
        unlink(SD_TEST_FILE);
        TEST_FAIL("SD read length mismatch");
    }

    if (memcmp(readbuf, sd_test_pattern, SD_TEST_PATTERN_LEN) != 0) {
        test_printf("    data mismatch on %s\r\n", SD_TEST_FILE);
        unlink(SD_TEST_FILE);
        TEST_FAIL("SD readback data mismatch");
    }

    if (unlink(SD_TEST_FILE) != 0) {
        test_printf("    unlink(%s) failed errno=%d\r\n", SD_TEST_FILE, errno);
        TEST_FAIL("cannot remove test file");
    }

    test_printf("    file smoke ok: %s (%u bytes)\r\n",
                SD_TEST_FILE, (unsigned)SD_TEST_PATTERN_LEN);
    TEST_PASS();
}

static void step_sd_fs_smoke(void)
{
    TEST_STEP("SD mount + /APM directory");

    sd_wait_for_mount();

    {
        struct stat st;
        if (stat(SD_MOUNT_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
            test_printf("    stat(%s) failed errno=%d\r\n", SD_MOUNT_DIR, errno);
            TEST_FAIL("APM directory not present after mount");
        }
        test_printf("    %s present (mode=0%o)\r\n", SD_MOUNT_DIR, (unsigned)st.st_mode);
    }

    TEST_PASS();

    sd_smoke_file_rw();
}

int main(void)
{
    TEST_INIT("E_SDCARD");

    step_sd_fs_smoke();

    TEST_DONE();
    return 0;
}
