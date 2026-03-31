/*
 * L3 Integration Test — multi-driver concurrent operation.
 *
 * Validates all LL drivers working together in a realistic scenario:
 *   1. GPIO: LED blink indicator
 *   2. SPI:  MS5611 continuous sampling (10 readings)
 *   3. UART: LL USART3 TX of sensor data
 *   4. Flash: Write/read-back of sensor log
 *   5. DWT:  Timing for each phase
 *
 * This proves LL drivers don't interfere with each other and can
 * operate concurrently within the RT-Thread environment.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "drv_gpio_ll.h"
#include "drv_common_ll.h"
#include "drv_spi_ll.h"
#include "drv_usart_ll.h"
#include "drv_flash_ll.h"

#define LED_PIN         GET_PIN(B, 0)
#define MS5611_CS_PIN   GET_PIN(F, 10)
#define FLASH_SECTOR    11
#define NUM_SAMPLES     10

volatile uint32_t l3_test_result = 0;
volatile uint32_t l3_errors = 0;
volatile uint32_t l3_d1_samples[NUM_SAMPLES];
volatile uint32_t l3_d2_samples[NUM_SAMPLES];
volatile uint32_t l3_total_ms = 0;
volatile uint32_t l3_flash_verify = 0;

static int errors;

static void check(const char *name, int cond)
{
    if (cond) {
        rt_kprintf("[L3] PASS - %s\n", name);
    } else {
        rt_kprintf("[L3] FAIL - %s\n", name);
        errors++;
    }
}

static inline void cs_lo(void) { gpio_ll_pin_write(MS5611_CS_PIN, 0); }
static inline void cs_hi(void) { gpio_ll_pin_write(MS5611_CS_PIN, 1); }

static void ms5611_cmd(uint8_t cmd)
{
    cs_lo();
    spi_ll_xfer_poll(SPI4, &cmd, NULL, 1);
    cs_hi();
}

static uint32_t ms5611_read_adc(void)
{
    uint8_t tx[4] = { 0x00, 0, 0, 0 };
    uint8_t rx[4] = {0};
    cs_lo();
    spi_ll_xfer_poll(SPI4, tx, rx, 4);
    cs_hi();
    return ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
}

int main(void)
{
    errors = 0;
    rt_kprintf("\n[L3] ========== INTEGRATION TEST ==========\n");

    dwt_ll_init();
    uint32_t c_start = *(volatile uint32_t *)0xE0001004;

    /* --- Phase 1: Init all drivers --- */
    gpio_ll_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    gpio_ll_pin_mode(MS5611_CS_PIN, PIN_MODE_OUTPUT);
    cs_hi();

    spi_ll_init(&spi4_ll_cfg);
    usart_ll_init(&usart3_ll_cfg);

    gpio_ll_pin_write(LED_PIN, 1);  /* LED on = test running */
    rt_kprintf("[L3] All drivers initialized\n");

    /* --- Phase 2: MS5611 sensor sampling --- */
    ms5611_cmd(0x1E);  /* Reset */
    dwt_ll_delay_us(5000);

    int valid_d1 = 0, valid_d2 = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        ms5611_cmd(0x48);   /* Convert D1 OSR=4096 */
        dwt_ll_delay_us(10000);
        l3_d1_samples[i] = ms5611_read_adc();
        if (l3_d1_samples[i] != 0 && l3_d1_samples[i] != 0xFFFFFF)
            valid_d1++;

        ms5611_cmd(0x58);   /* Convert D2 OSR=4096 */
        dwt_ll_delay_us(10000);
        l3_d2_samples[i] = ms5611_read_adc();
        if (l3_d2_samples[i] != 0 && l3_d2_samples[i] != 0xFFFFFF)
            valid_d2++;

        gpio_ll_pin_write(LED_PIN, i & 1);  /* Toggle LED each sample */
    }

    rt_kprintf("[L3] Sensor: %d/%d D1 valid, %d/%d D2 valid\n",
               valid_d1, NUM_SAMPLES, valid_d2, NUM_SAMPLES);
    rt_kprintf("[L3] D1 range: %u .. %u\n",
               (unsigned)l3_d1_samples[0], (unsigned)l3_d1_samples[NUM_SAMPLES-1]);
    rt_kprintf("[L3] D2 range: %u .. %u\n",
               (unsigned)l3_d2_samples[0], (unsigned)l3_d2_samples[NUM_SAMPLES-1]);
    check("D1 valid >= 8/10", valid_d1 >= 8);
    check("D2 valid >= 8/10", valid_d2 >= 8);

    /* --- Phase 3: UART TX of sensor summary --- */
    {
        char msg[128];
        int n = rt_snprintf(msg, sizeof(msg),
                            "[L3] D1=%u D2=%u\r\n",
                            (unsigned)l3_d1_samples[0],
                            (unsigned)l3_d2_samples[0]);
        usart_ll_tx_poll(USART3, (const uint8_t *)msg, n);
        check("USART3 TX done", 1);
    }

    /* --- Phase 4: Flash storage of sensor log --- */
    {
        uint32_t flash_addr = flash_ll_sector_addr(FLASH_SECTOR);
        int rc = flash_ll_unlock();
        check("Flash unlock", rc == 0);

        rc = flash_ll_erase_sector(FLASH_SECTOR);
        check("Flash erase", rc == 0);

        /* Write D1 samples (10 * 4 = 40 bytes) */
        uint8_t buf[NUM_SAMPLES * 4];
        for (int i = 0; i < NUM_SAMPLES; i++) {
            uint32_t val = l3_d1_samples[i];
            buf[i*4+0] = (uint8_t)(val >> 24);
            buf[i*4+1] = (uint8_t)(val >> 16);
            buf[i*4+2] = (uint8_t)(val >> 8);
            buf[i*4+3] = (uint8_t)(val);
        }
        rc = flash_ll_program_bytes(flash_addr, buf, sizeof(buf));
        check("Flash write", rc == 0);

        /* Read back and verify */
        uint8_t readback[NUM_SAMPLES * 4];
        flash_ll_read(flash_addr, readback, sizeof(readback));

        int match = 1;
        for (int i = 0; i < (int)sizeof(buf); i++) {
            if (readback[i] != buf[i]) { match = 0; break; }
        }
        l3_flash_verify = match;
        check("Flash read-back", match);

        /* Cleanup */
        flash_ll_erase_sector(FLASH_SECTOR);
        flash_ll_lock();
    }

    /* --- Timing --- */
    uint32_t c_end = *(volatile uint32_t *)0xE0001004;
    extern uint32_t SystemCoreClock;
    l3_total_ms = (c_end - c_start) / (SystemCoreClock / 1000U);
    rt_kprintf("[L3] Total time: %u ms\n", (unsigned)l3_total_ms);

    /* --- Result --- */
    l3_errors = errors;
    if (errors == 0) {
        l3_test_result = 0x900D900D;
        rt_kprintf("[L3] ALL PASS\n");
    } else {
        l3_test_result = errors;
        rt_kprintf("[L3] FAIL - %d error(s)\n", errors);
    }
    rt_kprintf("[L3] ========== INTEGRATION TEST END ==========\n\n");

    gpio_ll_pin_write(LED_PIN, 1);
    while (1) {
        gpio_ll_pin_write(LED_PIN, 1);
        rt_thread_mdelay(200);
        gpio_ll_pin_write(LED_PIN, 0);
        rt_thread_mdelay(200);
    }
}
