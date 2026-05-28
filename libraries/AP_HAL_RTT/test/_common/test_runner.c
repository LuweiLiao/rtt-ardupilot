/**
 * test_runner.c — Shared test framework implementation.
 *
 * Provides UART7 CMSIS direct output + banner printing + pass/fail tracking.
 * All output goes to UART7 so test results are visible even when USB/SPI/I2C are broken.
 */

#include "test_runner.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Forward declaration of IWDG kick — each test provides its own */
extern void ap_rtt_iwdg_kick(void);

/* ================================================================
 *  Cortex-M7 System Control
 * ================================================================ */
#define SCTLR           (*(volatile uint32_t *)0xE000ED24)
#define SCTLR_C_BIT     2          /* D-Cache enable bit */
#define SCTLR_I_BIT     12         /* I-Cache enable bit */

/* ================================================================
 *  CMSIS UART7 registers (V2 USART — UART4/5/7/8 use new layout)
 *  Old layout: SR=0x00 DR=0x04 BRR=0x08 CR1=0x0C
 *  New layout: CR1=0x00 CR2=0x04 CR3=0x08 BRR=0x0C ISR=0x1C TDR=0x28
 * ================================================================ */
#define UART7_BASE      0x40007800UL
#define UART7_CR1       (*(volatile uint32_t *)(UART7_BASE + 0x00))
#define UART7_CR2       (*(volatile uint32_t *)(UART7_BASE + 0x04))
#define UART7_CR3       (*(volatile uint32_t *)(UART7_BASE + 0x08))
#define UART7_BRR       (*(volatile uint32_t *)(UART7_BASE + 0x0C))
#define UART7_GTPR      (*(volatile uint32_t *)(UART7_BASE + 0x10))  /* Guard time & prescaler */
#define UART7_RTOR      (*(volatile uint32_t *)(UART7_BASE + 0x14))  /* Receiver timeout */
#define UART7_RQR       (*(volatile uint32_t *)(UART7_BASE + 0x18))  /* Request register */
#define UART7_ISR       (*(volatile uint32_t *)(UART7_BASE + 0x1C))  /* Interrupt & status */
#define UART7_ICR       (*(volatile uint32_t *)(UART7_BASE + 0x20))  /* Interrupt clear */
#define UART7_RDR       (*(volatile uint32_t *)(UART7_BASE + 0x24))  /* Receive data */
#define UART7_TDR       (*(volatile uint32_t *)(UART7_BASE + 0x28))  /* Transmit data */

/* UART7 pins (CUAV V5): PE8=TX, PF6=RX, AF8 */
#define GPIOE_BASE      0x40021000UL
#define GPIOE_MODER     (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_AFR1      (*(volatile uint32_t *)(GPIOE_BASE + 0x20))  /* AFRL (PE0-PE7) */
#define GPIOE_AFR2      (*(volatile uint32_t *)(GPIOE_BASE + 0x24))  /* AFRH (PE8-PE15) */
#define GPIOE_OSPEEDR   (*(volatile uint32_t *)(GPIOE_BASE + 0x08))  /* Output speed */

#define GPIOF_BASE      0x40021400UL
#define GPIOF_MODER     (*(volatile uint32_t *)(GPIOF_BASE + 0x00))
#define GPIOF_AFRL      (*(volatile uint32_t *)(GPIOF_BASE + 0x20))  /* AFRL (PF0-PF7) */
#define GPIOF_OSPEEDR   (*(volatile uint32_t *)(GPIOF_BASE + 0x08))  /* Output speed */

/* RCC registers for clock enable */
#define RCC_BASE        0x40023800UL
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_APB1RSTR    (*(volatile uint32_t *)(RCC_BASE + 0x20))

#define RCC_APB1ENR_UART7EN  (1U << 30)   /* Bit 30: UART7 clock enable */
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_AHB1ENR_GPIOEEN (1U << 4)     /* Bit 4: GPIOE clock enable */

/* USART V2 ISR bits (same semantics as old SR) */
#define USART_ISR_TXE    (1U << 7)          /* Transmit data register empty */
#define USART_ISR_TC     (1U << 6)          /* Transmission complete */
#define USART_ISR_RXNE   (1U << 5)          /* Read data register not empty */
#define USART_ISR_ORE    (1U << 3)          /* Overrun error */
#define USART_ISR_FE     (1U << 1)          /* Framing error */
#define USART_ISR_PE     (1U << 0)          /* Parity error */

/* CR1 bits */
#define USART_CR1_UE    (1U << 13)         /* USART enable */
#define USART_CR1_TE    (1U << 3)          /* Transmitter enable */
#define USART_CR1_RE    (1U << 2)          /* Receiver enable */

/* ================================================================
 *  Debug Markers — BSS section (auto-zeroed)
 * ================================================================ */
volatile uint32_t test_runner_initialized __attribute__((section(".bss"))) = 0;
volatile uint32_t test_current_layer      __attribute__((section(".bss"))) = 0;
volatile uint32_t test_status             __attribute__((section(".bss"))) = 0;
volatile uint32_t test_step_index         __attribute__((section(".bss"))) = 0;
volatile uint32_t test_fail_count         __attribute__((section(".bss"))) = 0;

/* ================================================================
 *  UART7 low-level init (pure CMSIS, no driver dependency)
 * ================================================================ */
static bool _uart7_initialized = false;

void test_uart7_init(void)
{
    if (_uart7_initialized) return;

    /* Ensure D-Cache write buffer is drained before touching peripherals */
    __asm__ volatile("dsb" : : : "memory");
    __asm__ volatile("isb" : : : "memory");

    /* Check if UART7 is already configured by bootloader or RTT console HAL */
    uint32_t cr1 = UART7_CR1;
    if (cr1 & USART_CR1_UE) {
        /* UART already enabled — just bump speed and mark ready */
        GPIOE_OSPEEDR |= (2U << 16);  /* High speed for 115200 */
        __asm__ volatile("dsb" : : : "memory");
        _uart7_initialized = true;
        return;
    }

    /* UE=0 (observed after scheduler starts) — just force UE on, no GPIO reconfig */
    GPIOE_OSPEEDR |= (2U << 16);
    UART7_CR1 |= USART_CR1_UE;
    __asm__ volatile("dsb" : : : "memory");
    _uart7_initialized = true;
    return;

    /* UART not initialized yet — do full CMSIS setup (ChibiOS usart_start style) */
    /* Enable clocks */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
    __asm__ volatile("dsb" : : : "memory");
    RCC_AHB1ENR |= (RCC_AHB1ENR_GPIOEEN << 1); /* GPIOFEN is bit 5 */
    __asm__ volatile("dsb" : : : "memory");
    RCC_APB1ENR |= RCC_APB1ENR_UART7EN;
    __asm__ volatile("dsb" : : : "memory");

    /* Small delay for clock stabilization */
    volatile int i;
    for (i = 0; i < 100; i++) {
        __asm__ volatile("nop");
    }

    /* Configure PE8 as AF8 (UART7_TX) with High speed */
    GPIOE_MODER   &= ~(3U << 16);     /* Clear MODER[8] bits */
    GPIOE_MODER   |=  (2U << 16);     /* Alternate function mode */
    GPIOE_OSPEEDR &= ~(3U << 16);     /* Clear speed bits */
    GPIOE_OSPEEDR |=  (2U << 16);     /* High speed (50MHz) for reliable 115200 */
    GPIOE_AFR2    &= ~(0xFU << 0);    /* Clear AFRH[3:0] (PE8) */
    GPIOE_AFR2    |=  (8U << 0);      /* AF8 = UART7 */

    /* Configure PF6 as AF8 (UART7_RX) */
    GPIOF_MODER   &= ~(3U << 12);     /* Clear MODER[6] bits */
    GPIOF_MODER   |=  (2U << 12);     /* Alternate function mode */
    GPIOF_AFRL    &= ~(0xFU << 24);   /* Clear AFRL[27:24] (PF6) */
    GPIOF_AFRL    |=  (8U << 24);     /* AF8 = UART7 */

    /* Force GPIO config through with barrier */
    __asm__ volatile("dsb" : : : "memory");
    (void)GPIOE_MODER;  /* Read-back barrier */
    (void)GPIOF_MODER;

    /* UART7 config: ChibiOS-style register sequence */
    /* BRR = PCLK / baud = 54000000 / 115200 = 468.75 ≈ 469 = 0x1D5 */
    UART7_CR1 = 0;                   /* Disable UART before changing config */
    __asm__ volatile("dsb" : : : "memory");
    UART7_BRR = 469;
    UART7_ICR = 0xFFFFFFFFU;         /* Clear ALL status flags (ChibiOS) */
    UART7_CR2 = 0x0000;              /* 1 stop bit, no LIN */
    UART7_CR3 = 0x0000;              /* No flow control, no DMA */
    __asm__ volatile("dsb" : : : "memory");

    /* Read-back barrier: verify BRR was written */
    (void)UART7_BRR;

    /* Enable UART with TX + RX (ChibiOS: UE|TE|RE|PEIE) */
    UART7_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
    __asm__ volatile("dsb" : : : "memory");
    __asm__ volatile("isb" : : : "memory");

    /* Verify CR1 was written */
    (void)UART7_CR1;

    /* Small delay for UART to stabilize */
    for (i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }

    _uart7_initialized = true;
}

/* ================================================================
 *  UART7 character output (blocking poll — ChibiOS/HAL stm32_putc style)
 *  Sequence: clear TC flag → write TDR → wait for TC (byte fully shifted out)
 * ================================================================ */
static void _uart7_putc(char c)
{
    /* Step 1: Feed IWDG before each character (IWDG timeout may be very short) */
    *(volatile uint32_t *)0x40002800 = 0x0000AAAA;
    __asm__ volatile("dsb" : : : "memory");

    /* Step 2: Clear Transmission Complete flag (matches HAL stm32_putc) */
    UART7_ICR = USART_ISR_TC;   /* Write bit 6 to ICR to clear TC */
    __asm__ volatile("dsb" : : : "memory");

    /* Step 2: Write data to TDR */
    UART7_TDR = (uint32_t)(unsigned char)c;
    __asm__ volatile("dsb" : : : "memory");

    /* Step 3: Wait for Transmission Complete (byte fully shifted out) */
    {
        uint32_t isr;
        do {
            __asm__ volatile("dsb" : : : "memory");
            isr = UART7_ISR;
        } while (!(isr & USART_ISR_TC));
    }

    /* If newline, also send carriage return */
    if (c == '\n') {
        UART7_ICR = USART_ISR_TC;
        __asm__ volatile("dsb" : : : "memory");
        UART7_TDR = '\r';
        __asm__ volatile("dsb" : : : "memory");
        do {
            __asm__ volatile("dsb" : : : "memory");
        } while (!(UART7_ISR & USART_ISR_TC));
    }
}

void test_uart7_write(const char *s)
{
    if (!_uart7_initialized) {
        test_uart7_init();
    }
    while (*s) {
        _uart7_putc(*s++);
    }
}

/* ================================================================
 *  Formatted print via UART7 (no stdio dependency — pure CMSIS)
 * ================================================================ */

void test_printf(const char *fmt, ...)
{
    /* Pre-format into local buffer using sprintf-like */
    /* But we avoid vsnprintf entirely — write character by character */
    va_list args;
    va_start(args, fmt);

    char buf[128];
    int n = 0;
    const char *p = fmt;
    while (*p && n < (int)sizeof(buf) - 1) {
        if (*p == '%') {
            p++;
            switch (*p) {
            case 's': {
                const char *s = va_arg(args, const char *);
                while (*s && n < (int)sizeof(buf) - 1)
                    buf[n++] = *s++;
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                if (val < 0) { buf[n++] = '-'; val = -val; }
                /* Convert to string (reversed), then copy */
                char tmp[16];
                int ti = 0;
                do { tmp[ti++] = '0' + (val % 10); val /= 10; } while (val > 0);
                while (ti > 0) buf[n++] = tmp[--ti];
                break;
            }
            case 'c': {
                buf[n++] = (char)va_arg(args, int);
                break;
            }
            case 'l': {
                p++; /* consume 'l' but handle same as 'd'/'u' for long */
                if (*p == 'u' || *p == 'd') {
                    unsigned long val = va_arg(args, unsigned long);
                    char tmp[24];
                    int ti = 0;
                    do { tmp[ti++] = '0' + (val % 10); val /= 10; } while (val > 0);
                    while (ti > 0) buf[n++] = tmp[--ti];
                }
                break;
            }
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                char tmp[16];
                int ti = 0;
                do {
                    int d = val % 16;
                    tmp[ti++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
                    val /= 16;
                } while (val > 0);
                while (ti > 0) buf[n++] = tmp[--ti];
                break;
            }
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9': {
                /* Skip width/precision — just read the format char */
                while (*p >= '0' && *p <= '9') p++;
                if (*p == 'l') p++;
                if (*p == 'x') {
                    unsigned int val = va_arg(args, unsigned int);
                    char tmp[16];
                    int ti = 0;
                    do {
                        int d = val % 16;
                        tmp[ti++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
                        val /= 16;
                    } while (val > 0);
                    while (ti > 0) buf[n++] = tmp[--ti];
                    break;
                }
                if (*p == 'u' || *p == 'd') {
                    unsigned long val = va_arg(args, unsigned long);
                    char tmp[24];
                    int ti = 0;
                    do { tmp[ti++] = '0' + (val % 10); val /= 10; } while (val > 0);
                    while (ti > 0) buf[n++] = tmp[--ti];
                }
                break;
            }
            default:
                buf[n++] = '%';
                if (*p) buf[n++] = *p;
                break;
            }
            p++;
        } else {
            buf[n++] = *p++;
        }
    }
    va_end(args);
    buf[n] = '\0';

    /* Dual path: CMSIS direct (always works) + rt_kprintf (primary console) */
    test_uart7_write(buf);
    rt_kprintf("%s", buf);
}

/* ================================================================
 *  Test Runner Implementation
 * ================================================================ */
static const char *_test_name = "UNKNOWN";
static uint32_t _test_start_tick;
static uint32_t _step_start_tick;

void _test_init(const char *name, int line)
{
    (void)line;
    _test_name = name;
    _test_start_tick = rt_tick_get();
    test_runner_initialized = 0x12345678;
    test_status = 0;  /* running */

    /* Init UART7 first */
    if (!_uart7_initialized) {
        test_uart7_init();
    }

    /* Init RT-Thread console via rt_kprintf as primary output */
    test_printf("\n");
    test_printf("========================================\n");
    test_printf("  RTT LAYERED TEST\n");
    test_printf("  Layer: %s\n", name);
    test_printf("  Started\n");
    test_printf("========================================\n");
    test_printf("\n");

    /* Also echo banner to UART7 hardware for fallback */
    test_uart7_write("\r\n");

    /* Also echo to rt_kprintf */
    test_printf("\n=== RTT LAYERED TEST: %s ===\n", name);
}

void _test_step(const char *desc, int line)
{
    (void)line;
    _step_start_tick = rt_tick_get();
    test_step_index++;

    test_printf("  [STEP %lu] %s ... ", (unsigned long)test_step_index, desc);
}

void _test_result(bool pass, int line, const char *msg)
{
    uint32_t elapsed = rt_tick_get() - _step_start_tick;
    uint32_t elapsed_ms = (elapsed * 1000U) / RT_TICK_PER_SECOND;

    if (pass) {
        test_printf("PASS (%lu ms)\r\n", (unsigned long)elapsed_ms);
    } else {
        test_fail_count++;
        test_status = 2;
        test_printf("FAIL (%lu ms)\r\n", (unsigned long)elapsed_ms);
        if (msg) {
            test_printf("    Reason: %s\r\n", msg);
        }
        /* Also print which line */
        test_printf("    At line: %d\r\n", line);
    }
}

void _test_done(void)
{
    uint32_t total_ticks = rt_tick_get() - _test_start_tick;
    uint32_t total_ms = (total_ticks * 1000U) / RT_TICK_PER_SECOND;

    test_uart7_write("\r\n");
    test_uart7_write("========================================\r\n");
    if (test_fail_count == 0) {
        test_status = 1;  /* PASS */
        test_printf("  [%s] RESULT: PASS\r\n", _test_name);
    } else {
        test_status = 2;  /* FAIL */
        test_printf("  [%s] RESULT: FAIL (%lu failures)\r\n",
                     _test_name, (unsigned long)test_fail_count);
    }
    test_printf("  Steps: %lu, Time: %lu ms\r\n",
                (unsigned long)test_step_index, (unsigned long)total_ms);
    test_uart7_write("========================================\r\n");
    test_uart7_write("\r\n");

    /* Also echo to RT-Thread console */
    test_printf("\n=== [%s] RESULT: %s ===\n",
               _test_name, (test_fail_count == 0) ? "PASS" : "FAIL");

    /* Infinite loop to prevent exit */
    while (1) {
        /* Feed IWDG if needed */
        rt_thread_mdelay(1000);
    }
}
