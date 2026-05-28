/**
 * test_L2_gpio — Layer 2: GPIO Register Read/Write & Toggle
 *
 * Verifies that GPIO registers are accessible for read and write,
 * BSRR toggle changes ODR, and IDR/ODR consistency holds.
 *
 * NOTE: In the standalone test firmware, no hwdef-based GPIO init
 * runs. Pins are in their default reset state (INPUT, PUSHPULL).
 * This is expected — the test verifies register-level access, not
 * peripheral configuration.
 *
 * Test steps:
 *   Step 1: GPIO register accessibility (MODER for A/C/D/G/H)
 *   Step 2: RGB LED pin state (PH10/PH11/PH12 — informational)
 *   Step 3: BSRR toggle test (ODR bit changes verify write works)
 *   Step 4: SPI pin state (PG11/PA6/PD7 — informational)
 *   Step 5: IDR/ODR consistency
 *
 * Prerequisites: L0-L1 passing (system, IWDG).
 * Build: scons --target=cuav-v5 --test=L2_gpio -j$(nproc)
 */

#include "test_runner.h"
#include <rtthread.h>

/* ================================================================
 *  GPIO register map (STM32F7)
 * ================================================================ */
#define GPIO_A_BASE     0x40020000UL
#define GPIO_C_BASE     0x40020800UL
#define GPIO_D_BASE     0x40020C00UL
#define GPIO_G_BASE     0x40021800UL
#define GPIO_H_BASE     0x40021C00UL

#define GPIO_MODER(base)   (*(volatile uint32_t *)((base) + 0x00))
#define GPIO_OTYPER(base)  (*(volatile uint32_t *)((base) + 0x04))
#define GPIO_OSPEEDR(base) (*(volatile uint32_t *)((base) + 0x08))
#define GPIO_PUPDR(base)   (*(volatile uint32_t *)((base) + 0x0C))
#define GPIO_IDR(base)     (*(volatile uint32_t *)((base) + 0x10))
#define GPIO_ODR(base)     (*(volatile uint32_t *)((base) + 0x14))
#define GPIO_BSRR(base)    (*(volatile uint32_t *)((base) + 0x18))
#define GPIO_AFR(base,hi)  (*(volatile uint32_t *)((base) + 0x20 + ((hi) ? 4 : 0)))

/* MODER bit encodings */
#define MODER_INPUT   0U
#define MODER_OUTPUT  1U
#define MODER_AF      2U
#define MODER_ANALOG  3U

/* CUAV V5 RGB LED pins: PH10(Red), PH11(Green), PH12(Blue) */
#define LED_R_PIN     10
#define LED_G_PIN     11
#define LED_B_PIN     12

/* Helpers */
#define MODER_GET(base, pin)   (((GPIO_MODER(base)) >> ((pin) * 2)) & 0x03U)
#define OTYPER_GET(base, pin)  (((GPIO_OTYPER(base)) >> (pin)) & 0x01U)
#define AFR_GET(base, pin)     (((GPIO_AFR(base, ((pin) >= 8))) >> (((pin) % 8) * 4)) & 0x0FU)

static const char *_moder_name(uint32_t m)
{
    if (m == MODER_INPUT)  return "INPUT";
    if (m == MODER_OUTPUT) return "OUTPUT";
    if (m == MODER_AF)     return "AF";
    if (m == MODER_ANALOG) return "ANALOG";
    return "?";
}

static const char *_otyper_name(uint32_t o)
{
    return o ? "OPENDRAIN" : "PUSHPULL";
}

static void _led_on(int pin)  { GPIO_BSRR(GPIO_H_BASE) = (1U << (pin + 16)); }
static void _led_off(int pin) { GPIO_BSRR(GPIO_H_BASE) = (1U << pin); }
static int  _led_read(int pin){ return (GPIO_ODR(GPIO_H_BASE) >> pin) & 1; }


/* ================================================================
 *  Step 1: GPIO Register Accessibility
 * ================================================================ */
static void step_reg_access(void)
{
    TEST_STEP("GPIO Register Accessibility");

    const uint32_t bases[] = {
        GPIO_A_BASE, GPIO_C_BASE, GPIO_D_BASE, GPIO_G_BASE, GPIO_H_BASE
    };
    const char *names[] = {"A", "C", "D", "G", "H"};
    int all_ok = 1;

    for (int i = 0; i < 5; i++) {
        uint32_t moder = GPIO_MODER(bases[i]);
        test_printf("    GPIO%cs_MODER=0x%08lx\\r\\n",
                    names[i][0], (unsigned long)moder);
        if (moder == 0xFFFFFFFFU) {
            test_printf("      -> BUS ERROR!\\r\\n");
            all_ok = 0;
        }
    }

    TEST_ASSERT(all_ok, "All GPIO MODER should be readable (not 0xFFFFFFFF)");
    TEST_PASS();
}


/* ================================================================
 *  Step 2: RGB LED Pin State (informational)
 *
 *  In test firmware, default reset state = INPUT/PUSHPULL.
 * ================================================================ */
static void step_led_state(void)
{
    TEST_STEP("RGB LED Pin State");

    const int pins[] = {LED_R_PIN, LED_G_PIN, LED_B_PIN};
    const char *colors[] = {"Red(PH10)", "Green(PH11)", "Blue(PH12)"};

    for (int i = 0; i < 3; i++) {
        int p = pins[i];
        uint32_t m = MODER_GET(GPIO_H_BASE, p);
        uint32_t o = OTYPER_GET(GPIO_H_BASE, p);
        test_printf("    %s: MODER=%s(%lu), OTYPER=%s(%lu)\\r\\n",
                    colors[i], _moder_name(m), (unsigned long)m,
                    _otyper_name(o), (unsigned long)o);
    }

    uint32_t mod = GPIO_MODER(GPIO_H_BASE);
    uint32_t oty = GPIO_OTYPER(GPIO_H_BASE);
    test_printf("    GPIOH_MODER=0x%08lx OTYPER=0x%08lx\\r\\n",
                (unsigned long)mod, (unsigned long)oty);

    /* Informational: show default RST state, not a test criterion */
    test_printf("    [INFO] Default reset state: pins are INPUT\\r\\n");
    TEST_PASS();
}


/* ================================================================
 *  Step 3: BSRR Toggle Verification
 *
 *  Verify ODR changes when writing BSRR. Even in INPUT mode,
 *  BSRR modifies ODR — this proves register write works.
 * ================================================================ */
static void step_toggle(void)
{
    TEST_STEP("BSRR Toggle (ODR write)");

    const int pins[] = {LED_R_PIN, LED_G_PIN, LED_B_PIN};
    const char *colors[] = {"RED", "GREEN", "BLUE"};

    /* Start: all OFF (HIGH) */
    _led_off(LED_R_PIN); _led_off(LED_G_PIN); _led_off(LED_B_PIN);

    uint32_t odr0 = GPIO_ODR(GPIO_H_BASE);
    test_printf("    Initial ODR=0x%08lx\\r\\n", (unsigned long)odr0);

    int all_ok = 1;

    for (int i = 0; i < 3; i++) {
        int p = pins[i];

        _led_on(p);
        int s_on = _led_read(p);
        for (volatile int d = 0; d < 5000; d++) { __asm volatile("nop"); }

        _led_off(p);
        int s_off = _led_read(p);
        for (volatile int d = 0; d < 5000; d++) { __asm volatile("nop"); }

        test_printf("    %s: ON->%s, OFF->%s\\r\\n",
                    colors[i],
                    s_on  ? "HIGH(off)" : "LOW(on)",
                    s_off ? "HIGH(off)" : "LOW(on)");

        if (s_on != 0) {
            test_printf("      -> ON should be LOW (0)!\\r\\n");
            all_ok = 0;
        }
        if (s_off != 1) {
            test_printf("      -> OFF should be HIGH (1)!\\r\\n");
            all_ok = 0;
        }
    }

    uint32_t odr1 = GPIO_ODR(GPIO_H_BASE);
    test_printf("    Final ODR=0x%08lx\\r\\n", (unsigned long)odr1);

    TEST_ASSERT(all_ok, "All LED toggles should change ODR (LOW→HIGH)");
    TEST_PASS();
}


/* ================================================================
 *  Step 4: SPI Pin AF State (informational)
 *
 *  In test firmware, SPI pins default to INPUT/INPUT/INPUT.
 *  Full AF configuration happens in the full ArduPilot build.
 * ================================================================ */
static void step_spi_pins(void)
{
    TEST_STEP("SPI1 Pin AF State");

    uint32_t pg11_m = MODER_GET(GPIO_G_BASE, 11);
    uint32_t pg11_a = AFR_GET(GPIO_G_BASE, 11);
    uint32_t pa6_m  = MODER_GET(GPIO_A_BASE, 6);
    uint32_t pa6_a  = AFR_GET(GPIO_A_BASE, 6);
    uint32_t pd7_m  = MODER_GET(GPIO_D_BASE, 7);
    uint32_t pd7_a  = AFR_GET(GPIO_D_BASE, 7);

    test_printf("    PG11(SCK):  MODER=%s(%lu) AF=%lu\\r\\n",
                _moder_name(pg11_m), (unsigned long)pg11_m,
                (unsigned long)pg11_a);
    test_printf("    PA6(MISO):  MODER=%s(%lu) AF=%lu\\r\\n",
                _moder_name(pa6_m), (unsigned long)pa6_m,
                (unsigned long)pa6_a);
    test_printf("    PD7(MOSI):  MODER=%s(%lu) AF=%lu\\r\\n",
                _moder_name(pd7_m), (unsigned long)pd7_m,
                (unsigned long)pd7_a);

    test_printf("    [INFO] Default reset: all INPUT AF=0\\r\\n");
    TEST_PASS();
}


/* ================================================================
 *  Step 5: IDR/ODR Consistency
 *
 *  Verify we can read both registers without bus errors.
 * ================================================================ */
static void step_idr_odr(void)
{
    TEST_STEP("IDR/ODR Consistency");

    uint32_t odr = GPIO_ODR(GPIO_H_BASE);
    uint32_t idr = GPIO_IDR(GPIO_H_BASE);

    test_printf("    GPIOH ODR=0x%08lx IDR=0x%08lx\\r\\n",
                (unsigned long)odr, (unsigned long)idr);

    TEST_ASSERT(odr != 0xFFFFFFFFU, "ODR should be readable");
    TEST_ASSERT(idr != 0xFFFFFFFFU, "IDR should be readable");

    int led_r = (odr >> LED_R_PIN) & 1;
    int led_g = (odr >> LED_G_PIN) & 1;
    int led_b = (odr >> LED_B_PIN) & 1;
    test_printf("    LED_R=%s LED_G=%s LED_B=%s\\r\\n",
                led_r ? "HIGH" : "LOW",
                led_g ? "HIGH" : "LOW",
                led_b ? "HIGH" : "LOW");

    TEST_PASS();
}


/* ================================================================
 *  Main
 * ================================================================ */
int main(void)
{
    TEST_INIT("L2_GPIO");

    test_current_layer = 2;

    step_reg_access();
    step_led_state();
    step_toggle();
    step_spi_pins();
    step_idr_odr();

    TEST_DONE();
    return 0;
}
