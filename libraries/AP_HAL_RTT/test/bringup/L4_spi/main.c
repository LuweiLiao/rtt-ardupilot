/**
 * test_L4_spi — Layer 4: SPI1 Register Access & IMU WHO_AM_I
 *
 * Configures SPI1 in polled mode, reads ICM20689 WHO_AM_I register:
 *   Step 1: SPI1 register accessibility
 *   Step 2: SPI1 GPIO pin config (PG11/PA6/PD7 -> AF5)
 *   Step 3: SPI1 clock enable + CR1/CR2 polled mode config
 *   Step 4: SPI1 send/receive test
 *   Step 5: ICM20689 WHO_AM_I read (CS=PF2, reg=0x75 -> expect 0x98)
 *
 * Prerequisites: L0-L3 passing.
 * Build: scons --target=cuav-v5 --test=L4_spi -j$(nproc)
 */

#include "test_runner.h"
#include <rtthread.h>

/* RCC register for SPI1 clock enable */
#define RCC_APB2ENR     (*(volatile uint32_t *)0x40023844UL)

/* SPI1 registers (base 0x40013000) */
#define SPI1_BASE       0x40013000UL
#define SPI1_CR1        (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI1_CR2        (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI1_SR         (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_DR         (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

/* CR1 bits */
#define SPI_CR1_CPHA    (1U << 0)
#define SPI_CR1_CPOL    (1U << 1)
#define SPI_CR1_MSTR    (1U << 2)
#define SPI_CR1_SPE     (1U << 6)
#define SPI_CR1_SSI     (1U << 8)
#define SPI_CR1_SSM     (1U << 9)

/* CR2 bits */
#define SPI_CR2_SSOE    (1U << 2)
#define SPI_CR2_FRXTH   (1U << 12)
#define SPI_CR2_DS_8BIT (7U << 8)

/* SR bits */
#define SPI_SR_RXNE     (1U << 0)
#define SPI_SR_TXE      (1U << 1)
#define SPI_SR_BSY      (1U << 7)

/* GPIO for SPI1 */
#define GPIO_A_BASE     0x40020000UL
#define GPIO_D_BASE     0x40020C00UL
#define GPIO_F_BASE     0x40021400UL
#define GPIO_G_BASE     0x40021800UL

#define GPIO_MODER(b)   (*(volatile uint32_t *)((b) + 0x00))
#define GPIO_AFR(b,h)   (*(volatile uint32_t *)((b) + 0x20 + ((h) ? 4 : 0)))
#define GPIO_BSRR(b)    (*(volatile uint32_t *)((b) + 0x18))
#define GPIO_OSPEEDR(b) (*(volatile uint32_t *)((b) + 0x08))

/* ICM20689 CS: PF2 */
#define CS_PORT      GPIO_F_BASE
#define CS_PIN       2
#define CS_LOW()     do { GPIO_BSRR(CS_PORT) = (1U << (CS_PIN + 16)); __asm volatile("dsb" ::: "memory"); } while(0)
#define CS_HIGH()    do { GPIO_BSRR(CS_PORT) = (1U << CS_PIN);       __asm volatile("dsb" ::: "memory"); } while(0)

/* ICM20689 registers */
#define ICM20689_WHO_AM_I   0x75
#define ICM20689_WHOAMI_VAL 0x98


static inline void _gpio_set_af(uint32_t base, uint32_t pin, uint32_t af)
{
    uint32_t mask = 3U << (pin * 2);
    GPIO_MODER(base) = (GPIO_MODER(base) & ~mask) | (2U << (pin * 2));
    if (pin < 8)
        GPIO_AFR(base, 0) = (GPIO_AFR(base, 0) & ~(0xFU << (pin * 4))) | (af << (pin * 4));
    else
        GPIO_AFR(base, 1) = (GPIO_AFR(base, 1) & ~(0xFU << ((pin - 8) * 4))) | (af << ((pin - 8) * 4));
    GPIO_OSPEEDR(base) = (GPIO_OSPEEDR(base) & ~mask) | (2U << (pin * 2));
    __asm volatile("dsb" ::: "memory");
}


/* Step 1: SPI1 Register Accessibility */
static void step_spi1_access(void)
{
    TEST_STEP("SPI1 Register Accessibility");
    uint32_t cr1 = SPI1_CR1, cr2 = SPI1_CR2, sr = SPI1_SR;
    test_printf("    CR1=0x%08lx CR2=0x%08lx SR=0x%08lx\\r\\n",
                (unsigned long)cr1, (unsigned long)cr2, (unsigned long)sr);
    TEST_ASSERT(cr1 != 0xFFFFFFFFU, "SPI1 CR1 readable");
    TEST_ASSERT(cr2 != 0xFFFFFFFFU, "SPI1 CR2 readable");
    TEST_ASSERT(sr  != 0xFFFFFFFFU, "SPI1 SR readable");
    TEST_PASS();
}


/* Step 2: SPI1 GPIO Configuration */
static void step_spi1_gpio(void)
{
    TEST_STEP("SPI1 GPIO Configuration");
    _gpio_set_af(GPIO_G_BASE, 11, 5);
    _gpio_set_af(GPIO_A_BASE, 6,  5);
    _gpio_set_af(GPIO_D_BASE, 7,  5);
    GPIO_MODER(CS_PORT) = (GPIO_MODER(CS_PORT) & ~(3U << (CS_PIN*2))) | (1U << (CS_PIN*2));
    GPIO_OSPEEDR(CS_PORT) |= (2U << (CS_PIN*2));
    CS_HIGH();
    __asm volatile("dsb" ::: "memory");

    uint32_t pg11 = (GPIO_MODER(GPIO_G_BASE) >> 22) & 3;
    uint32_t pa6  = (GPIO_MODER(GPIO_A_BASE) >> 12) & 3;
    uint32_t pd7  = (GPIO_MODER(GPIO_D_BASE) >> 14) & 3;
    uint32_t pf2  = (GPIO_MODER(CS_PORT) >> (CS_PIN*2)) & 3;

    test_printf("    PG11(SCK)=MODER(%lu) PA6(MISO)=MODER(%lu)\\r\\n",
                (unsigned long)pg11, (unsigned long)pa6);
    test_printf("    PD7(MOSI)=MODER(%lu) PF2(CS)=MODER(%lu)\\r\\n",
                (unsigned long)pd7, (unsigned long)pf2);

    int ok = 1;
    if (pg11 != 2) { test_printf("    ! PG11 not AF!\\r\\n"); ok = 0; }
    if (pa6  != 2) { test_printf("    ! PA6 not AF!\\r\\n");  ok = 0; }
    if (pd7  != 2) { test_printf("    ! PD7 not AF!\\r\\n");  ok = 0; }
    if (pf2  != 1) { test_printf("    ! PF2 not OUTPUT!\\r\\n"); ok = 0; }
    TEST_ASSERT(ok, "SPI1 pins configured");
    TEST_PASS();
}


/* Step 3: SPI1 Clock Enable + CR1/CR2 Config */
static void step_spi1_config(void)
{
    TEST_STEP("SPI1 Clock + CR1/CR2 Config");

    /* Enable SPI1 clock: APB2 bit 12 */
    volatile uint32_t *rcc = (volatile uint32_t *)0x40023844UL;
    *rcc = *rcc | (1U << 12);
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
    uint32_t apb2 = *rcc;
    test_printf("    RCC_APB2ENR=0x%08lx (SPI1EN=%s)\\r\\n",
                (unsigned long)apb2,
                (apb2 & (1U << 12)) ? "ON" : "OFF");
    TEST_ASSERT(apb2 & (1U << 12), "SPI1 clock enabled");

    /* Disable SPI then configure */
    SPI1_CR1 &= ~SPI_CR1_SPE;
    __asm volatile("dsb" ::: "memory");
    (void)SPI1_CR1;

    SPI1_CR2 = SPI_CR2_DS_8BIT | SPI_CR2_FRXTH | SPI_CR2_SSOE;
    __asm volatile("dsb" ::: "memory");

    SPI1_CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
               SPI_CR1_CPOL | SPI_CR1_CPHA | (3U << 3);
    __asm volatile("dsb" ::: "memory");

    SPI1_CR1 |= SPI_CR1_SPE;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");

    uint32_t cr1 = SPI1_CR1, cr2 = SPI1_CR2;
    test_printf("    CR1=0x%04lx CR2=0x%04lx\\r\\n",
                (unsigned long)cr1, (unsigned long)cr2);

    TEST_PASS();
}


/* Step 4: SPI Transfer Test */
static uint8_t _spi_xfer(uint8_t tx)
{
    uint32_t tmo = 100000;
    while (!(SPI1_SR & SPI_SR_TXE) && --tmo) {}
    SPI1_DR = tx;
    __asm volatile("dsb" ::: "memory");
    tmo = 100000;
    while (!(SPI1_SR & SPI_SR_RXNE) && --tmo) {}
    return (uint8_t)(SPI1_DR & 0xFF);
}

static void step_spi1_xfer(void)
{
    TEST_STEP("SPI1 Transfer Test");
    uint8_t rx = _spi_xfer(0xA5);
    uint32_t sr = SPI1_SR;
    test_printf("    RX=0x%02x SR=0x%08lx TXE=%s RXNE=%s\\r\\n",
                rx, (unsigned long)sr,
                (sr & SPI_SR_TXE) ? "ready" : "busy",
                (sr & SPI_SR_RXNE) ? "data" : "empty");
    TEST_PASS();
}


/* Step 5: ICM20689 WHO_AM_I */
static void step_icm_whoami(void)
{
    TEST_STEP("ICM20689 WHO_AM_I");
    uint8_t reg = ICM20689_WHO_AM_I | 0x80;  /* read flag */

    CS_LOW();
    volatile int d;
    for (d = 0; d < 100; d++) __asm volatile("nop");
    _spi_xfer(reg);
    for (d = 0; d < 50; d++) __asm volatile("nop");
    uint8_t whoami = _spi_xfer(0x00);
    for (d = 0; d < 100; d++) __asm volatile("nop");
    CS_HIGH();

    test_printf("    WHO_AM_I(0x%02x)=0x%02x (expect 0x%02x ICM20689)\\r\\n",
                ICM20689_WHO_AM_I, whoami, ICM20689_WHOAMI_VAL);

    if (whoami == ICM20689_WHOAMI_VAL)
        test_printf("    -> ICM20689 DETECTED!\\r\\n");
    else
        test_printf("    -> MISMATCH (got 0x%02x)\\r\\n", whoami);

    TEST_ASSERT(whoami != 0xFF, "WHO_AM_I not bus error");
    TEST_PASS();
}


/* Main */
int main(void)
{
    TEST_INIT("L4_SPI");
    test_current_layer = 4;
    step_spi1_access();
    step_spi1_gpio();
    step_spi1_config();
    step_spi1_xfer();
    step_icm_whoami();
    TEST_DONE();
    return 0;
}
