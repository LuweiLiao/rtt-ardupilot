/*
 * L1 GPIO Test — verify LL GPIO driver against RT-Thread pin API.
 *
 * Validates:
 *   - gpio_ll_clk_enable enables port clocks
 *   - gpio_ll_pin_mode configures output/input correctly
 *   - gpio_ll_pin_write / gpio_ll_pin_read match rt_pin_write / rt_pin_read
 *   - GPIO toggle speed benchmark (LL vs RT-Thread API)
 *   - DWT microsecond timing accuracy
 *
 * Test pins: PB0 (LED1, safe to toggle), PE3 (VDD_3V3_Sensor_EN)
 * Read-only pin: PC0 (available as input test)
 *
 * Expected output on UART7 (115200):
 *   [L1] ========== GPIO TEST ==========
 *   [L1] PASS / FAIL for each sub-test
 *   [L1] ========== GPIO TEST END ==========
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "drv_gpio_ll.h"
#include "drv_common_ll.h"

#define TEST_PIN_OUT    GET_PIN(B, 0)   /* PB0 = LED1, safe for output test */
#define TEST_PIN_IN     GET_PIN(C, 0)   /* PC0, read-only */
#define BENCH_CYCLES    10000

volatile uint32_t l1_test_result = 0;
volatile uint32_t l1_errors = 0;
volatile uint32_t l1_ll_toggle_ns = 0;
volatile uint32_t l1_rt_toggle_ns = 0;
volatile uint32_t l1_dwt_10ms_us = 0;

static int errors;

static void check(const char *name, int cond)
{
    if (cond) {
        rt_kprintf("[L1] PASS — %s\n", name);
    } else {
        rt_kprintf("[L1] FAIL — %s\n", name);
        errors++;
    }
}

static void test_clock_enable(void)
{
    gpio_ll_init_all_clocks();
    uint32_t ahb1enr = RCC->AHB1ENR;
    check("GPIOA clk", ahb1enr & RCC_AHB1ENR_GPIOAEN);
    check("GPIOB clk", ahb1enr & RCC_AHB1ENR_GPIOBEN);
    check("GPIOC clk", ahb1enr & RCC_AHB1ENR_GPIOCEN);
    check("GPIOE clk", ahb1enr & RCC_AHB1ENR_GPIOEEN);
    check("GPIOI clk", ahb1enr & RCC_AHB1ENR_GPIOIEN);
}

static void test_output_mode(void)
{
    gpio_ll_pin_mode(TEST_PIN_OUT, PIN_MODE_OUTPUT);

    GPIO_TypeDef *port = gpio_ll_port(GPIO_LL_PORT_IDX(TEST_PIN_OUT));
    uint8_t pin_no = GPIO_LL_PIN_NO(TEST_PIN_OUT);
    uint32_t moder = (port->MODER >> (pin_no * 2)) & 0x3;
    check("PB0 output MODER=01", moder == 0x01);

    gpio_ll_pin_write(TEST_PIN_OUT, 1);
    uint32_t odr = (port->ODR >> pin_no) & 1;
    check("PB0 write HIGH ODR=1", odr == 1);

    gpio_ll_pin_write(TEST_PIN_OUT, 0);
    odr = (port->ODR >> pin_no) & 1;
    check("PB0 write LOW ODR=0", odr == 0);
}

static void test_read_back(void)
{
    gpio_ll_pin_mode(TEST_PIN_OUT, PIN_MODE_OUTPUT);

    gpio_ll_pin_write(TEST_PIN_OUT, 1);
    rt_ssize_t val_ll = gpio_ll_pin_read(TEST_PIN_OUT);
    int val_rt = rt_pin_read(TEST_PIN_OUT);
    check("LL read HIGH==1", val_ll == PIN_HIGH);
    check("RT read HIGH==1", val_rt == PIN_HIGH);
    check("LL==RT HIGH", val_ll == val_rt);

    gpio_ll_pin_write(TEST_PIN_OUT, 0);
    val_ll = gpio_ll_pin_read(TEST_PIN_OUT);
    val_rt = rt_pin_read(TEST_PIN_OUT);
    check("LL read LOW==0", val_ll == PIN_LOW);
    check("RT read LOW==0", val_rt == PIN_LOW);
    check("LL==RT LOW", val_ll == val_rt);
}

static void test_input_mode(void)
{
    gpio_ll_pin_mode(TEST_PIN_IN, PIN_MODE_INPUT_PULLUP);

    GPIO_TypeDef *port = gpio_ll_port(GPIO_LL_PORT_IDX(TEST_PIN_IN));
    uint8_t pin_no = GPIO_LL_PIN_NO(TEST_PIN_IN);
    uint32_t moder = (port->MODER >> (pin_no * 2)) & 0x3;
    uint32_t pupdr = (port->PUPDR >> (pin_no * 2)) & 0x3;
    check("PC0 input MODER=00", moder == 0x00);
    check("PC0 pullup PUPDR=01", pupdr == 0x01);

    rt_ssize_t val = gpio_ll_pin_read(TEST_PIN_IN);
    check("PC0 pullup reads HIGH", val == PIN_HIGH);
}

static void test_toggle_benchmark(void)
{
    gpio_ll_pin_mode(TEST_PIN_OUT, PIN_MODE_OUTPUT);

    dwt_ll_init();

    /* Benchmark LL GPIO toggle */
    uint32_t t0 = dwt_ll_get_us();
    for (int i = 0; i < BENCH_CYCLES; i++) {
        gpio_ll_pin_write(TEST_PIN_OUT, 1);
        gpio_ll_pin_write(TEST_PIN_OUT, 0);
    }
    uint32_t t1 = dwt_ll_get_us();
    uint32_t ll_us = t1 - t0;
    l1_ll_toggle_ns = (ll_us * 1000U) / BENCH_CYCLES;

    /* Benchmark RT-Thread GPIO toggle */
    t0 = dwt_ll_get_us();
    for (int i = 0; i < BENCH_CYCLES; i++) {
        rt_pin_write(TEST_PIN_OUT, PIN_HIGH);
        rt_pin_write(TEST_PIN_OUT, PIN_LOW);
    }
    t1 = dwt_ll_get_us();
    uint32_t rt_us = t1 - t0;
    l1_rt_toggle_ns = (rt_us * 1000U) / BENCH_CYCLES;

    rt_kprintf("[L1] GPIO toggle %d cycles:\n", BENCH_CYCLES);
    rt_kprintf("[L1]   LL:  %u us total, %u ns/toggle\n",
               (unsigned)ll_us, (unsigned)l1_ll_toggle_ns);
    rt_kprintf("[L1]   RT:  %u us total, %u ns/toggle\n",
               (unsigned)rt_us, (unsigned)l1_rt_toggle_ns);
    check("LL toggle < 100ns/op", l1_ll_toggle_ns < 100);
}

static void test_dwt_accuracy(void)
{
    extern uint32_t SystemCoreClock;
    uint32_t target_cyc = (SystemCoreClock / 1000U) * 10;  /* 10ms */
    uint32_t c0 = *(volatile uint32_t *)0xE0001004;
    while ((*(volatile uint32_t *)0xE0001004 - c0) < target_cyc) { }
    uint32_t c1 = *(volatile uint32_t *)0xE0001004;
    l1_dwt_10ms_us = (c1 - c0) / (SystemCoreClock / 1000000U);
    rt_kprintf("[L1] DWT 10ms busy-wait: %u us (expect ~10000)\n",
               (unsigned)l1_dwt_10ms_us);
    int32_t err = (int32_t)l1_dwt_10ms_us - 10000;
    check("DWT 10ms within ±500us", err > -500 && err < 500);
}

int main(void)
{
    errors = 0;
    rt_kprintf("\n[L1] ========== GPIO TEST ==========\n");

    test_clock_enable();
    test_output_mode();
    test_read_back();
    test_input_mode();
    test_toggle_benchmark();
    test_dwt_accuracy();

    l1_errors = errors;
    if (errors == 0) {
        l1_test_result = 0x900D900D;
        rt_kprintf("[L1] ALL PASS — %d tests OK\n", 0);
    } else {
        l1_test_result = errors;
        rt_kprintf("[L1] FAIL — %d error(s)\n", errors);
    }
    rt_kprintf("[L1] ========== GPIO TEST END ==========\n\n");

    while (1) {
        gpio_ll_pin_write(TEST_PIN_OUT, 1);
        rt_thread_mdelay(100);
        gpio_ll_pin_write(TEST_PIN_OUT, 0);
        rt_thread_mdelay(100);
    }
}
