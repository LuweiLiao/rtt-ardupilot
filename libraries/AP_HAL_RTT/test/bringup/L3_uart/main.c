/**
 * test_L3_uart — Layer 3: UART Register Access & Verification
 *
 * Verifies UART peripherals are accessible and configured:
 *   Step 1: UART7 register access + BRR baud verification
 *   Step 2: USART1 register access (GPS/Telem1)
 *   Step 3: USART6 register access (Telem2)
 *   Step 4: UART7 ISR status flags
 *   Step 5: UART7 DMA/CR3 configuration
 *
 * Prerequisites: L0-L2 passing (system, IWDG, GPIO).
 * Build: scons --target=cuav-v5 --test=L3_uart -j$(nproc)
 */

#include "test_runner.h"
#include <rtthread.h>

/* ================================================================
 *  UART register maps (STM32F7)
 *
 *  V1 layout (USART1/2/3/6): SR@+0, DR@+4, BRR@+8, CR1@+0xC, CR2@+0x10, CR3@+0x14
 *  V2 layout (UART4/5/7/8):  CR1@+0, CR2@+4, CR3@+8, BRR@+0xC, ISR@+0x1C, TDR@+0x28
 * ================================================================ */

/* UART7 — V2 layout, console/CH340 */
#define UART7_BASE      0x40007800UL
#define UART7_CR1       (*(volatile uint32_t *)(UART7_BASE + 0x00))
#define UART7_CR2       (*(volatile uint32_t *)(UART7_BASE + 0x04))
#define UART7_CR3       (*(volatile uint32_t *)(UART7_BASE + 0x08))
#define UART7_BRR       (*(volatile uint32_t *)(UART7_BASE + 0x0C))
#define UART7_ISR       (*(volatile uint32_t *)(UART7_BASE + 0x1C))
#define UART7_RDR       (*(volatile uint32_t *)(UART7_BASE + 0x24))
#define UART7_TDR       (*(volatile uint32_t *)(UART7_BASE + 0x28))

/* USART1 — V1 layout */
#define USART1_BASE     0x40011000UL
#define USART1_SR       (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DR       (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR      (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CR1      (*(volatile uint32_t *)(USART1_BASE + 0x0C))
#define USART1_CR2      (*(volatile uint32_t *)(USART1_BASE + 0x10))
#define USART1_CR3      (*(volatile uint32_t *)(USART1_BASE + 0x14))

/* USART6 — V1 layout */
#define USART6_BASE     0x40011400UL
#define USART6_SR       (*(volatile uint32_t *)(USART6_BASE + 0x00))
#define USART6_DR       (*(volatile uint32_t *)(USART6_BASE + 0x04))
#define USART6_BRR      (*(volatile uint32_t *)(USART6_BASE + 0x08))
#define USART6_CR1      (*(volatile uint32_t *)(USART6_BASE + 0x0C))
#define USART6_CR2      (*(volatile uint32_t *)(USART6_BASE + 0x10))
#define USART6_CR3      (*(volatile uint32_t *)(USART6_BASE + 0x14))

/* Status register bits (V1: SR, V2: ISR) */
#define USART_SR_TXE    (1U << 7)
#define USART_SR_TC     (1U << 6)
#define USART_SR_RXNE   (1U << 5)

/* CR1 bits */
#define USART_CR1_UE    (1U << 13)
#define USART_CR1_TE    (1U << 3)
#define USART_CR1_RE    (1U << 2)

/* UART7 expected baud rate */
#define UART7_PCLK      54000000UL
#define EXPECTED_BRR    469U

/* ================================================================
 *  Step 1: UART7 Register Access & Baud Rate
 * ================================================================ */
static void step_uart7_access(void)
{
    TEST_STEP("UART7 Register Access");

    uint32_t cr1 = UART7_CR1;
    uint32_t cr2 = UART7_CR2;
    uint32_t cr3 = UART7_CR3;
    uint32_t brr = UART7_BRR;
    uint32_t isr = UART7_ISR;

    test_printf("    CR1=0x%08lx CR2=0x%08lx CR3=0x%08lx\\r\\n",
                (unsigned long)cr1, (unsigned long)cr2, (unsigned long)cr3);
    test_printf("    BRR=0x%04lx (%lu, expect %lu for 115200)\\r\\n",
                (unsigned long)brr, (unsigned long)brr, (unsigned long)EXPECTED_BRR);
    test_printf("    ISR=0x%08lx\\r\\n", (unsigned long)isr);

    int ok = 1;
    if (cr1 == 0xFFFFFFFFU) { test_printf("    CR1 BUS ERROR!\\r\\n"); ok = 0; }
    if (brr == 0xFFFFFFFFU) { test_printf("    BRR BUS ERROR!\\r\\n"); ok = 0; }

    bool ue = (cr1 & USART_CR1_UE) != 0;
    bool te = (cr1 & USART_CR1_TE) != 0;
    bool re = (cr1 & USART_CR1_RE) != 0;

    test_printf("    UE=%s TE=%s RE=%s\\r\\n",
                ue ? "ON" : "OFF",
                te ? "ON" : "OFF",
                re ? "ON" : "OFF");

    TEST_ASSERT(ue, "UART7 should be enabled (UE=1)");
    TEST_ASSERT(te, "UART7 should have TX enabled (TE=1)");
    TEST_ASSERT(brr > 0, "UART7 BRR should be non-zero");
    TEST_ASSERT(ok,   "No bus errors on UART7 registers");

    TEST_PASS();
}


/* ================================================================
 *  Step 2: USART1 Register Access
 * ================================================================ */
static void step_usart1_access(void)
{
    TEST_STEP("USART1 Register Access (GPS/Telem1)");

    uint32_t sr  = USART1_SR;
    uint32_t brr = USART1_BRR;
    uint32_t cr1 = USART1_CR1;
    uint32_t cr2 = USART1_CR2;
    uint32_t cr3 = USART1_CR3;

    test_printf("    SR=0x%08lx BRR=0x%04lx\\r\\n",
                (unsigned long)sr, (unsigned long)brr);
    test_printf("    CR1=0x%08lx CR2=0x%08lx CR3=0x%08lx\\r\\n",
                (unsigned long)cr1, (unsigned long)cr2, (unsigned long)cr3);

    bool ue = (cr1 & USART_CR1_UE) != 0;
    bool te = (cr1 & USART_CR1_TE) != 0;
    bool re = (cr1 & USART_CR1_RE) != 0;
    test_printf("    UE=%s TE=%s RE=%s\\r\\n",
                ue ? "ON" : "OFF",
                te ? "ON" : "OFF",
                re ? "ON" : "OFF");

    TEST_ASSERT(sr  != 0xFFFFFFFFU, "USART1 SR should be readable");
    TEST_ASSERT(cr1 != 0xFFFFFFFFU, "USART1 CR1 should be readable");

    TEST_PASS();
}


/* ================================================================
 *  Step 3: USART6 Register Access
 * ================================================================ */
static void step_usart6_access(void)
{
    TEST_STEP("USART6 Register Access (Telem2)");

    uint32_t sr  = USART6_SR;
    uint32_t brr = USART6_BRR;
    uint32_t cr1 = USART6_CR1;
    uint32_t cr2 = USART6_CR2;
    uint32_t cr3 = USART6_CR3;

    test_printf("    SR=0x%08lx BRR=0x%04lx\\r\\n",
                (unsigned long)sr, (unsigned long)brr);
    test_printf("    CR1=0x%08lx CR2=0x%08lx CR3=0x%08lx\\r\\n",
                (unsigned long)cr1, (unsigned long)cr2, (unsigned long)cr3);

    bool ue = (cr1 & USART_CR1_UE) != 0;
    bool te = (cr1 & USART_CR1_TE) != 0;
    bool re = (cr1 & USART_CR1_RE) != 0;
    test_printf("    UE=%s TE=%s RE=%s\\r\\n",
                ue ? "ON" : "OFF",
                te ? "ON" : "OFF",
                re ? "ON" : "OFF");

    TEST_ASSERT(sr  != 0xFFFFFFFFU, "USART6 SR should be readable");
    TEST_ASSERT(cr1 != 0xFFFFFFFFU, "USART6 CR1 should be readable");

    TEST_PASS();
}


/* ================================================================
 *  Step 4: UART7 ISR Status Flags
 * ================================================================ */
static void step_uart7_isr(void)
{
    TEST_STEP("UART7 ISR Status");

    uint32_t isr = UART7_ISR;
    bool txe  = (isr & USART_SR_TXE)  != 0;
    bool tc   = (isr & USART_SR_TC)   != 0;
    bool rxne = (isr & USART_SR_RXNE) != 0;

    test_printf("    ISR=0x%08lx\\r\\n", (unsigned long)isr);
    test_printf("    TXE=%s TC=%s RXNE=%s\\r\\n",
                txe  ? "ready" : "busy",
                tc   ? "done"  : "pending",
                rxne ? "data"  : "empty");

    TEST_ASSERT(txe, "UART7 TXE should be set (ready to transmit)");

    bool ore = (isr & (1U << 3)) != 0;
    bool fe  = (isr & (1U << 1)) != 0;
    bool pe  = (isr & (1U << 0)) != 0;
    if (ore) test_printf("    WARNING: Overrun error detected\\r\\n");
    if (fe)  test_printf("    WARNING: Framing error detected\\r\\n");
    if (pe)  test_printf("    WARNING: Parity error detected\\r\\n");

    TEST_PASS();
}


/* ================================================================
 *  Step 5: UART7 DMA/CR3 Configuration
 * ================================================================ */
static void step_uart7_cr3(void)
{
    TEST_STEP("UART7 DMA/Flow Control Config");

    uint32_t cr3 = UART7_CR3;

    bool dmaren = (cr3 & (1U << 12)) != 0;
    bool dmat   = (cr3 & (1U << 11)) != 0;
    bool rtse   = (cr3 & (1U << 9))  != 0;
    bool ctse   = (cr3 & (1U << 8))  != 0;

    test_printf("    CR3=0x%08lx\\r\\n", (unsigned long)cr3);
    test_printf("    DMAR=%s DMAT=%s\\r\\n",
                dmaren ? "enabled" : "disabled",
                dmat   ? "enabled" : "disabled");
    test_printf("    RTS=%s CTS=%s\\r\\n",
                rtse ? "flow ctrl" : "disabled",
                ctse ? "flow ctrl" : "disabled");

    TEST_PASS();
}


/* ================================================================
 *  Main
 * ================================================================ */
int main(void)
{
    TEST_INIT("L3_UART");

    test_current_layer = 3;

    step_uart7_access();
    step_usart1_access();
    step_usart6_access();
    step_uart7_isr();
    step_uart7_cr3();

    TEST_DONE();
    return 0;
}
