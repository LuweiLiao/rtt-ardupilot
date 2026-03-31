/*
 * L2 SPI Test — verify LL SPI driver with MS5611 barometer on SPI4.
 *
 * Validates:
 *   - SPI4 peripheral init (register values)
 *   - Polled SPI transfer (TXE/RXNE flag path)
 *   - MS5611 PROM read (real hardware communication)
 *   - SPI transfer throughput benchmark
 *
 * Hardware: CUAV V5 — MS5611 on SPI4, CS = PF10 (spi41)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "drv_spi_ll.h"
#include "drv_gpio_ll.h"
#include "drv_common_ll.h"

#define MS5611_CS_PIN   GET_PIN(F, 10)

/* MS5611 commands */
#define MS5611_CMD_RESET      0x1E
#define MS5611_CMD_PROM_BASE  0xA0
#define MS5611_CMD_CONV_D1    0x48   /* OSR=4096 */
#define MS5611_CMD_CONV_D2    0x58   /* OSR=4096 */
#define MS5611_CMD_ADC_READ   0x00

volatile uint32_t l2s_test_result = 0;
volatile uint32_t l2s_errors = 0;
volatile uint32_t l2s_prom[8] = {0};
volatile uint32_t l2s_adc_d1 = 0;
volatile uint32_t l2s_adc_d2 = 0;
volatile uint32_t l2s_xfer_128_us = 0;
volatile uint32_t l2s_xfer_kbps = 0;

static int errors;

static void check(const char *name, int cond)
{
    if (cond) {
        rt_kprintf("[L2S] PASS - %s\n", name);
    } else {
        rt_kprintf("[L2S] FAIL - %s\n", name);
        errors++;
    }
}

static inline void cs_assert(void) {
    gpio_ll_pin_write(MS5611_CS_PIN, 0);
}
static inline void cs_release(void) {
    gpio_ll_pin_write(MS5611_CS_PIN, 1);
}

static void ms5611_cmd(SPI_TypeDef *spi, uint8_t cmd)
{
    cs_assert();
    spi_ll_xfer_poll(spi, &cmd, NULL, 1);
    cs_release();
}

static uint16_t ms5611_read_prom(SPI_TypeDef *spi, uint8_t addr_idx)
{
    uint8_t tx[3] = { (uint8_t)(MS5611_CMD_PROM_BASE | (addr_idx << 1)), 0, 0 };
    uint8_t rx[3] = {0};
    cs_assert();
    spi_ll_xfer_poll(spi, tx, rx, 3);
    cs_release();
    return ((uint16_t)rx[1] << 8) | rx[2];
}

static uint32_t ms5611_read_adc(SPI_TypeDef *spi)
{
    uint8_t tx[4] = { MS5611_CMD_ADC_READ, 0, 0, 0 };
    uint8_t rx[4] = {0};
    cs_assert();
    spi_ll_xfer_poll(spi, tx, rx, 4);
    cs_release();
    return ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
}

static void test_spi4_init(void)
{
    /* CS pin as output, default high (released) */
    gpio_ll_pin_mode(MS5611_CS_PIN, PIN_MODE_OUTPUT);
    cs_release();

    spi_ll_init(&spi4_ll_cfg);

    /* Verify SPI4 CR1 */
    uint32_t cr1 = SPI4->CR1;
    check("SPI4 MSTR", cr1 & SPI_CR1_MSTR);
    check("SPI4 SSM", cr1 & SPI_CR1_SSM);
    check("SPI4 CPOL=1", cr1 & SPI_CR1_CPOL);
    check("SPI4 CPHA=1", cr1 & SPI_CR1_CPHA);
    check("SPI4 SPE", cr1 & SPI_CR1_SPE);

    /* Verify SPI4 CR2 */
    uint32_t cr2 = SPI4->CR2;
    check("SPI4 FRXTH", cr2 & SPI_CR2_FRXTH);

    uint32_t pclk = spi_ll_get_pclk(SPI4);
    uint32_t actual_freq = pclk >> (spi4_ll_cfg.prescaler + 1);
    rt_kprintf("[L2S] SPI4: pclk=%u prescaler=/%u freq=%u Hz\n",
               (unsigned)pclk, 2U << spi4_ll_cfg.prescaler, (unsigned)actual_freq);
}

static void test_ms5611_prom(void)
{
    /* Reset MS5611 */
    ms5611_cmd(SPI4, MS5611_CMD_RESET);
    rt_thread_mdelay(5);

    /* Read 8 PROM words */
    int valid = 0;
    for (int i = 0; i < 8; i++) {
        uint16_t w = ms5611_read_prom(SPI4, i);
        l2s_prom[i] = w;
        rt_kprintf("[L2S] PROM[%d] = 0x%04X (%u)\n", i, w, w);
        if (w != 0x0000 && w != 0xFFFF)
            valid++;
    }

    /* PROM[0] is factory/manufacturer, PROM[7] is CRC.
     * PROM[1..6] are calibration coefficients — should be nonzero. */
    check("PROM valid words >= 4", valid >= 4);
    check("C1 (PROM[1]) nonzero", l2s_prom[1] != 0 && l2s_prom[1] != 0xFFFF);
    check("C2 (PROM[2]) nonzero", l2s_prom[2] != 0 && l2s_prom[2] != 0xFFFF);
}

static void test_ms5611_adc(void)
{
    dwt_ll_init();

    /* Convert D1 (pressure) — OSR4096 needs ~9ms */
    ms5611_cmd(SPI4, MS5611_CMD_CONV_D1);
    dwt_ll_delay_us(15000);
    l2s_adc_d1 = ms5611_read_adc(SPI4);
    rt_kprintf("[L2S] D1 (pressure)   = %u\n", (unsigned)l2s_adc_d1);
    check("D1 nonzero", l2s_adc_d1 != 0 && l2s_adc_d1 != 0xFFFFFF);

    /* Convert D2 (temperature) */
    ms5611_cmd(SPI4, MS5611_CMD_CONV_D2);
    dwt_ll_delay_us(15000);
    l2s_adc_d2 = ms5611_read_adc(SPI4);
    rt_kprintf("[L2S] D2 (temperature) = %u\n", (unsigned)l2s_adc_d2);
    check("D2 nonzero", l2s_adc_d2 != 0 && l2s_adc_d2 != 0xFFFFFF);
}

static void test_throughput(void)
{
    dwt_ll_init();

    uint8_t tx[128], rx[128];
    for (int i = 0; i < 128; i++)
        tx[i] = (uint8_t)i;

    cs_assert();
    uint32_t c0 = *(volatile uint32_t *)0xE0001004;
    for (int round = 0; round < 8; round++)
        spi_ll_xfer_poll(SPI4, tx, rx, 128);
    uint32_t c1 = *(volatile uint32_t *)0xE0001004;
    cs_release();

    extern uint32_t SystemCoreClock;
    uint32_t cyc = c1 - c0;
    l2s_xfer_128_us = cyc / (SystemCoreClock / 1000000U);
    l2s_xfer_kbps = (uint32_t)((uint64_t)1024U * 8U * 1000U / l2s_xfer_128_us);

    uint32_t freq = spi_ll_get_pclk(SPI4) >> (spi4_ll_cfg.prescaler + 1);
    uint32_t theoretical_us = (1024U * 8U * 1000000U) / freq;

    rt_kprintf("[L2S] 1KB polled: %u us (theoretical %u us), %u kbps\n",
               (unsigned)l2s_xfer_128_us, (unsigned)theoretical_us,
               (unsigned)l2s_xfer_kbps);
    check("SPI throughput > 1000 kbps", l2s_xfer_kbps > 1000);
}

int main(void)
{
    errors = 0;
    rt_kprintf("\n[L2S] ========== SPI TEST ==========\n");

    test_spi4_init();
    test_ms5611_prom();
    test_ms5611_adc();
    test_throughput();

    l2s_errors = errors;
    if (errors == 0) {
        l2s_test_result = 0x900D900D;
        rt_kprintf("[L2S] ALL PASS\n");
    } else {
        l2s_test_result = errors;
        rt_kprintf("[L2S] FAIL - %d error(s)\n", errors);
    }
    rt_kprintf("[L2S] ========== SPI TEST END ==========\n\n");

    rt_base_t led = GET_PIN(B, 0);
    gpio_ll_pin_mode(led, PIN_MODE_OUTPUT);
    while (1) {
        gpio_ll_pin_write(led, 1);
        rt_thread_mdelay(500);
        gpio_ll_pin_write(led, 0);
        rt_thread_mdelay(500);
    }
}
