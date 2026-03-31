/*
 * L2 Flash Test — verify LL Flash driver with sector 11 (safe, unused).
 *
 * Validates:
 *   - Flash unlock/lock
 *   - Sector erase (verify 0xFF)
 *   - Word and byte programming
 *   - Read-back verification
 *   - Erase/write timing
 *
 * Uses Sector 11 (0x081C0000, 256KB) which is unused by test firmware.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "drv_flash_ll.h"
#include "drv_gpio_ll.h"
#include "drv_common_ll.h"

#define TEST_SECTOR     11
#define TEST_SIZE       256     /* bytes to test */

volatile uint32_t l2f_test_result = 0;
volatile uint32_t l2f_errors = 0;
volatile uint32_t l2f_erase_ms = 0;
volatile uint32_t l2f_write_us = 0;
volatile uint32_t l2f_sector_addr = 0;

static int errors;

static void check(const char *name, int cond)
{
    if (cond) {
        rt_kprintf("[L2F] PASS - %s\n", name);
    } else {
        rt_kprintf("[L2F] FAIL - %s\n", name);
        errors++;
    }
}

static void test_sector_info(void)
{
    l2f_sector_addr = flash_ll_sector_addr(TEST_SECTOR);
    uint32_t size = flash_ll_sector_size(TEST_SECTOR);
    rt_kprintf("[L2F] Sector %d: addr=0x%08X size=%u KB\n",
               TEST_SECTOR, (unsigned)l2f_sector_addr, (unsigned)(size / 1024));
    check("Sector addr valid", l2f_sector_addr == 0x081C0000);
    check("Sector size 256KB", size == 256 * 1024);
}

static void test_unlock_lock(void)
{
    int rc = flash_ll_unlock();
    check("Flash unlock", rc == 0);
    flash_ll_lock();
    check("Flash lock", FLASH->CR & FLASH_CR_LOCK);
}

static void test_erase(void)
{
    dwt_ll_init();

    int rc = flash_ll_unlock();
    check("Unlock for erase", rc == 0);

    uint32_t c0 = *(volatile uint32_t *)0xE0001004;
    rc = flash_ll_erase_sector(TEST_SECTOR);
    uint32_t c1 = *(volatile uint32_t *)0xE0001004;

    extern uint32_t SystemCoreClock;
    l2f_erase_ms = (c1 - c0) / (SystemCoreClock / 1000U);
    rt_kprintf("[L2F] Erase sector %d: rc=%d, %u ms\n",
               TEST_SECTOR, rc, (unsigned)l2f_erase_ms);
    check("Erase success", rc == 0);

    /* Verify first TEST_SIZE bytes are 0xFF */
    int erased_ok = 1;
    for (uint32_t i = 0; i < TEST_SIZE; i++) {
        if (*(volatile uint8_t *)(l2f_sector_addr + i) != 0xFF) {
            erased_ok = 0;
            break;
        }
    }
    check("Erased to 0xFF", erased_ok);

    flash_ll_lock();
}

static void test_write_read(void)
{
    uint8_t pattern[TEST_SIZE];
    for (int i = 0; i < TEST_SIZE; i++)
        pattern[i] = (uint8_t)(i ^ 0xA5);

    int rc = flash_ll_unlock();
    check("Unlock for write", rc == 0);

    dwt_ll_init();
    uint32_t c0 = *(volatile uint32_t *)0xE0001004;
    rc = flash_ll_program_bytes(l2f_sector_addr, pattern, TEST_SIZE);
    uint32_t c1 = *(volatile uint32_t *)0xE0001004;

    extern uint32_t SystemCoreClock;
    l2f_write_us = (c1 - c0) / (SystemCoreClock / 1000000U);
    rt_kprintf("[L2F] Write %d bytes: rc=%d, %u us\n",
               TEST_SIZE, rc, (unsigned)l2f_write_us);
    check("Write success", rc == 0);

    flash_ll_lock();

    /* Read back and verify */
    uint8_t readbuf[TEST_SIZE];
    flash_ll_read(l2f_sector_addr, readbuf, TEST_SIZE);

    int match = 1;
    for (int i = 0; i < TEST_SIZE; i++) {
        if (readbuf[i] != pattern[i]) {
            rt_kprintf("[L2F] Mismatch at %d: wrote 0x%02X read 0x%02X\n",
                       i, pattern[i], readbuf[i]);
            match = 0;
            break;
        }
    }
    check("Read-back match", match);
}

static void test_cleanup_erase(void)
{
    int rc = flash_ll_unlock();
    if (rc == 0) {
        flash_ll_erase_sector(TEST_SECTOR);
        flash_ll_lock();
    }
    rt_kprintf("[L2F] Cleanup erase done\n");
}

int main(void)
{
    errors = 0;
    rt_kprintf("\n[L2F] ========== FLASH TEST ==========\n");

    test_sector_info();
    test_unlock_lock();
    test_erase();
    test_write_read();
    test_cleanup_erase();

    l2f_errors = errors;
    if (errors == 0) {
        l2f_test_result = 0x900D900D;
        rt_kprintf("[L2F] ALL PASS\n");
    } else {
        l2f_test_result = errors;
        rt_kprintf("[L2F] FAIL - %d error(s)\n", errors);
    }
    rt_kprintf("[L2F] ========== FLASH TEST END ==========\n\n");

    rt_base_t led = GET_PIN(B, 0);
    gpio_ll_pin_mode(led, PIN_MODE_OUTPUT);
    while (1) {
        gpio_ll_pin_write(led, 1);
        rt_thread_mdelay(500);
        gpio_ll_pin_write(led, 0);
        rt_thread_mdelay(500);
    }
}
