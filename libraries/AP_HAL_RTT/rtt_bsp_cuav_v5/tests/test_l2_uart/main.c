/*
 * L2 UART Test — verify LL USART driver.
 *
 * Test strategy:
 *   - Console (UART7) is initialized by HAL in rt_hw_board_init() for rt_kprintf.
 *   - LL driver re-initializes USART3 (PD8 TX / PD9 RX) at 115200 baud.
 *   - Verifies TX polling, BRR register, flag behavior, throughput.
 *   - If TX/RX pins are externally looped, also verifies RX.
 *
 * Hardware: CUAV V5 (STM32F767)
 *   USART3: PD8 = TX, PD9 = RX (Telem1 port)
 *   UART7:  PE8 = TX, PF6 = RX (debug console via CH343)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "drv_usart_ll.h"
#include "drv_common_ll.h"
#include "drv_gpio_ll.h"

volatile uint32_t l2_test_result = 0;
volatile uint32_t l2_errors = 0;
volatile uint32_t l2_brr_value = 0;
volatile uint32_t l2_tx_1k_us = 0;
volatile uint32_t l2_tx_bps = 0;
volatile uint32_t l2_loopback_ok = 0;

static int errors;

static void check(const char *name, int cond)
{
    if (cond) {
        rt_kprintf("[L2] PASS - %s\n", name);
    } else {
        rt_kprintf("[L2] FAIL - %s\n", name);
        errors++;
    }
}

static void test_usart3_init(void)
{
    usart_ll_init(&usart3_ll_cfg);

    /* Verify BRR: 54MHz / 115200 ≈ 469 */
    l2_brr_value = USART3->BRR;
    uint32_t pclk = usart_ll_get_pclk(USART3);
    uint32_t expected_brr = (pclk + 115200U / 2U) / 115200U;
    rt_kprintf("[L2] USART3 BRR = %u (expected %u, pclk=%u)\n",
               (unsigned)l2_brr_value, (unsigned)expected_brr, (unsigned)pclk);
    check("BRR value", l2_brr_value == expected_brr);

    /* Verify UE, TE, RE bits are set */
    uint32_t cr1 = USART3->CR1;
    check("CR1.UE set", cr1 & USART_CR1_UE);
    check("CR1.TE set", cr1 & USART_CR1_TE);
    check("CR1.RE set", cr1 & USART_CR1_RE);

    /* Verify TXE (transmit data register empty) is initially set */
    check("ISR.TXE ready", USART3->ISR & USART_ISR_TXE);
}

static void test_tx_single(void)
{
    usart_ll_putc(USART3, 'L');
    usart_ll_putc(USART3, '2');
    usart_ll_putc(USART3, '\r');
    usart_ll_putc(USART3, '\n');

    /* Wait TC (transmission complete) */
    while (!(USART3->ISR & USART_ISR_TC)) { }
    check("TX 4 bytes TC", USART3->ISR & USART_ISR_TC);
}

static void test_tx_throughput(void)
{
    uint8_t buf[128];
    for (int i = 0; i < 128; i++)
        buf[i] = 'A' + (i % 26);

    dwt_ll_init();

    /* TX 1KB (8 × 128B) and measure time */
    uint32_t c0 = *(volatile uint32_t *)0xE0001004;
    for (int round = 0; round < 8; round++)
        usart_ll_tx_poll(USART3, buf, 128);
    uint32_t c1 = *(volatile uint32_t *)0xE0001004;

    extern uint32_t SystemCoreClock;
    uint32_t cyc = c1 - c0;
    l2_tx_1k_us = cyc / (SystemCoreClock / 1000000U);

    /* Expected: 1024 bytes at 115200 8N1 = ~89ms (11 bits/byte) */
    uint32_t expected_us = (1024U * 11U * 1000000U) / 115200U;
    l2_tx_bps = (uint32_t)((uint64_t)1024U * 8U * 1000000U / l2_tx_1k_us);

    rt_kprintf("[L2] TX 1KB: %u us (expected ~%u us), %u bps\n",
               (unsigned)l2_tx_1k_us, (unsigned)expected_us, (unsigned)l2_tx_bps);

    /* Polled TX at 115200 should take ~89ms; allow 50-200ms */
    check("TX 1KB time 50-200ms", l2_tx_1k_us > 50000 && l2_tx_1k_us < 200000);
}

static void test_loopback(void)
{
    /* Clear any pending RX data */
    while (USART3->ISR & USART_ISR_RXNE)
        (void)USART3->RDR;

    /* Send test pattern and try to read back (requires external TX->RX wire) */
    const uint8_t pattern[] = { 0x55, 0xAA, 0x5A, 0xA5 };
    for (int i = 0; i < 4; i++)
        usart_ll_putc(USART3, pattern[i]);

    /* Wait for last byte to finish transmitting */
    while (!(USART3->ISR & USART_ISR_TC)) { }

    /* Small delay for byte propagation on loopback wire */
    dwt_ll_delay_us(500);

    int match = 0;
    for (int i = 0; i < 4; i++) {
        int ch = usart_ll_getc_nb(USART3);
        if (ch == (int)pattern[i])
            match++;
    }

    l2_loopback_ok = (uint32_t)match;
    if (match == 4) {
        rt_kprintf("[L2] Loopback: 4/4 bytes matched\n");
        check("Loopback 4/4", 1);
    } else {
        rt_kprintf("[L2] Loopback: %d/4 matched (no external wire = expected)\n", match);
        /* Don't count as error — loopback requires external wiring */
    }
}

int main(void)
{
    errors = 0;
    rt_kprintf("\n[L2] ========== UART TEST ==========\n");

    test_usart3_init();
    test_tx_single();
    test_tx_throughput();
    test_loopback();

    l2_errors = errors;
    if (errors == 0) {
        l2_test_result = 0x900D900D;
        rt_kprintf("[L2] ALL PASS\n");
    } else {
        l2_test_result = errors;
        rt_kprintf("[L2] FAIL - %d error(s)\n", errors);
    }
    rt_kprintf("[L2] ========== UART TEST END ==========\n\n");

    /* Idle blink */
    rt_base_t led = GET_PIN(B, 0);
    gpio_ll_pin_mode(led, PIN_MODE_OUTPUT);
    while (1) {
        gpio_ll_pin_write(led, 1);
        rt_thread_mdelay(500);
        gpio_ll_pin_write(led, 0);
        rt_thread_mdelay(500);
    }
}
