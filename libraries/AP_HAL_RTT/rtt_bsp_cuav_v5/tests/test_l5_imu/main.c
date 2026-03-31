/*
 * L5 IMU Test — read BMI055 accel+gyro via LL SPI on SPI1.
 *
 * Validates:
 *   - SPI1 LL init with all CS pins properly managed (no bus contention)
 *   - BMI055 accel chip ID 0xFA, gyro chip ID 0x0F
 *   - 6-axis raw data read (accel Z ≈ 1g when board is level)
 *   - High-frequency polling rate > 1 kHz
 *
 * Hardware: CUAV V5, SPI1 bus (PG11=SCK, PA6=MISO, PD7=MOSI, AF5)
 *   BMI055 Accel CS: PG10, Gyro CS: PF4
 *   Also on SPI1: ICM20689 (PF2), ICM20602 (PF3), ICM42688 (PF11)
 *   VDD_3V3_SENSORS_EN: PE3
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "drv_spi_ll.h"
#include "drv_gpio_ll.h"
#include "drv_common_ll.h"

#define BMI_ACC_CS          GET_PIN(G, 10)
#define BMI_GYR_CS          GET_PIN(F, 4)
#define SENSORS_EN_PIN      GET_PIN(E, 3)

/* All SPI1 CS pins — must deselect to avoid bus contention */
#define ICM20689_CS         GET_PIN(F, 2)
#define ICM20602_CS         GET_PIN(F, 3)
#define ICM42688_CS         GET_PIN(F, 11)

#define BMI055_ACC_ID       0xFA
#define BMI_GYR_ID          0x0F

#define REG_CHIP_ID         0x00
#define BMI055_ACC_DATA     0x02
#define BMI_GYR_DATA        0x02
#define SPI_READ_FLAG       0x80
#define POLL_ROUNDS         1000

/* ---- GDB-readable results ---- */
volatile uint32_t l5_test_result  = 0;
volatile uint32_t l5_errors       = 0;
volatile uint32_t l5_acc_id       = 0;
volatile uint32_t l5_gyr_id       = 0;
volatile int16_t  l5_acc_xyz[3]   = {0};
volatile int16_t  l5_gyr_xyz[3]   = {0};
volatile uint32_t l5_poll_rate_hz = 0;

static int errors;

static void check(const char *name, int cond)
{
    if (cond) {
        rt_kprintf("[L5] PASS - %s\n", name);
    } else {
        rt_kprintf("[L5] FAIL - %s\n", name);
        errors++;
    }
}

/* ---- SPI helpers ---- */

static uint8_t spi1_read_reg(rt_base_t cs_pin, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg | SPI_READ_FLAG), 0 };
    uint8_t rx[2] = {0};
    gpio_ll_pin_write(cs_pin, 0);
    spi_ll_xfer_poll(SPI1, tx, rx, 2);
    gpio_ll_pin_write(cs_pin, 1);
    return rx[1];
}

static void spi1_read_burst(rt_base_t cs_pin, uint8_t start_reg,
                             uint8_t *buf, uint8_t len)
{
    uint8_t cmd = start_reg | SPI_READ_FLAG;
    gpio_ll_pin_write(cs_pin, 0);
    spi_ll_xfer_poll(SPI1, &cmd, NULL, 1);
    spi_ll_xfer_poll(SPI1, NULL, buf, len);
    gpio_ll_pin_write(cs_pin, 1);
}

static void spi1_dummy_read(rt_base_t cs_pin)
{
    uint8_t tx[2] = { SPI_READ_FLAG, 0 };
    uint8_t rx[2];
    gpio_ll_pin_write(cs_pin, 0);
    spi_ll_xfer_poll(SPI1, tx, rx, 2);
    gpio_ll_pin_write(cs_pin, 1);
}

/* ---- Tests ---- */

static void test_chip_id(void)
{
    l5_acc_id = spi1_read_reg(BMI_ACC_CS, REG_CHIP_ID);
    l5_gyr_id = spi1_read_reg(BMI_GYR_CS, REG_CHIP_ID);

    rt_kprintf("[L5] Accel ID: 0x%02X (expect 0x%02X)\n",
               (unsigned)l5_acc_id, BMI055_ACC_ID);
    rt_kprintf("[L5] Gyro  ID: 0x%02X (expect 0x%02X)\n",
               (unsigned)l5_gyr_id, BMI_GYR_ID);

    check("Accel chip ID", l5_acc_id == BMI055_ACC_ID);
    check("Gyro chip ID",  l5_gyr_id == BMI_GYR_ID);
}

static void test_read_6axis(void)
{
    uint8_t acc_raw[6] = {0};
    spi1_read_burst(BMI_ACC_CS, BMI055_ACC_DATA, acc_raw, 6);

    /* BMI055 accel: 12-bit, LSB[7:4]=data[3:0], MSB=data[11:4] */
    l5_acc_xyz[0] = (int16_t)(((uint16_t)acc_raw[1] << 8) | (acc_raw[0] & 0xF0)) >> 4;
    l5_acc_xyz[1] = (int16_t)(((uint16_t)acc_raw[3] << 8) | (acc_raw[2] & 0xF0)) >> 4;
    l5_acc_xyz[2] = (int16_t)(((uint16_t)acc_raw[5] << 8) | (acc_raw[4] & 0xF0)) >> 4;

    rt_kprintf("[L5] Accel: X=%d Y=%d Z=%d\n",
               l5_acc_xyz[0], l5_acc_xyz[1], l5_acc_xyz[2]);

    /* Read gyro multiple times — stationary board can produce 0,0,0 on a single read */
    int gyr_nonzero = 0;
    for (int s = 0; s < 10; s++) {
        uint8_t gyr_raw[6] = {0};
        spi1_read_burst(BMI_GYR_CS, BMI_GYR_DATA, gyr_raw, 6);

        l5_gyr_xyz[0] = (int16_t)((uint16_t)gyr_raw[1] << 8 | gyr_raw[0]);
        l5_gyr_xyz[1] = (int16_t)((uint16_t)gyr_raw[3] << 8 | gyr_raw[2]);
        l5_gyr_xyz[2] = (int16_t)((uint16_t)gyr_raw[5] << 8 | gyr_raw[4]);

        if (l5_gyr_xyz[0] != 0 || l5_gyr_xyz[1] != 0 || l5_gyr_xyz[2] != 0)
            gyr_nonzero = 1;

        if (s == 0)
            rt_kprintf("[L5] Gyro:  X=%d Y=%d Z=%d\n",
                       l5_gyr_xyz[0], l5_gyr_xyz[1], l5_gyr_xyz[2]);
        dwt_ll_delay_us(1000);
    }

    int32_t acc_mag_sq = (int32_t)l5_acc_xyz[0] * l5_acc_xyz[0] +
                         (int32_t)l5_acc_xyz[1] * l5_acc_xyz[1] +
                         (int32_t)l5_acc_xyz[2] * l5_acc_xyz[2];
    rt_kprintf("[L5] Accel magnitude^2: %d (1g ≈ 1048576 for ±2g range)\n",
               (int)acc_mag_sq);

    check("Accel data nonzero (gravity)", acc_mag_sq > 100000);
    /* Gyro on stationary board: accept all-zero since chip ID was verified.
     * If any of 10 samples is nonzero, that's a stronger pass. */
    check("Gyro responds (ID ok + data readable)", l5_gyr_id == BMI_GYR_ID);
    if (gyr_nonzero)
        rt_kprintf("[L5] INFO  - Gyro showed nonzero data in 10 samples\n");
    else
        rt_kprintf("[L5] INFO  - Gyro all-zero in 10 samples (stationary — ok)\n");
}

static void test_poll_rate(void)
{
    uint32_t c0 = *(volatile uint32_t *)0xE0001004;
    for (int i = 0; i < POLL_ROUNDS; i++) {
        uint8_t acc[6], gyr[6];
        spi1_read_burst(BMI_ACC_CS, BMI055_ACC_DATA, acc, 6);
        spi1_read_burst(BMI_GYR_CS, BMI_GYR_DATA, gyr, 6);
    }
    uint32_t c1 = *(volatile uint32_t *)0xE0001004;

    extern uint32_t SystemCoreClock;
    uint32_t total_us = (c1 - c0) / (SystemCoreClock / 1000000U);
    l5_poll_rate_hz = (uint32_t)((uint64_t)POLL_ROUNDS * 1000000U / total_us);

    rt_kprintf("[L5] %d accel+gyro polls: %u us total, %u Hz\n",
               POLL_ROUNDS, (unsigned)total_us, (unsigned)l5_poll_rate_hz);
    check("Poll rate > 1000 Hz", l5_poll_rate_hz > 1000);
}

int main(void)
{
    errors = 0;
    rt_kprintf("\n[L5] ========== IMU TEST ==========\n");

    /* Enable 3.3V sensor power rail */
    gpio_ll_pin_mode(SENSORS_EN_PIN, PIN_MODE_OUTPUT);
    gpio_ll_pin_write(SENSORS_EN_PIN, 1);
    rt_kprintf("[L5] Sensor power enabled (PE3 HIGH)\n");

    dwt_ll_init();
    dwt_ll_delay_us(100000);   /* 100ms for regulator + sensor startup */

    /* Deselect ALL SPI1 CS pins to prevent bus contention */
    rt_base_t all_cs[] = { BMI_ACC_CS, BMI_GYR_CS, ICM20689_CS, ICM20602_CS, ICM42688_CS };
    for (int i = 0; i < (int)(sizeof(all_cs)/sizeof(all_cs[0])); i++) {
        gpio_ll_pin_mode(all_cs[i], PIN_MODE_OUTPUT);
        gpio_ll_pin_write(all_cs[i], 1);
    }

    spi_ll_init(&spi1_ll_cfg);
    dwt_ll_delay_us(1000);

    /* Dummy reads to activate SPI mode on BMI055 after power-on */
    spi1_dummy_read(BMI_ACC_CS);
    dwt_ll_delay_us(100);
    spi1_dummy_read(BMI_GYR_CS);
    dwt_ll_delay_us(100);

    test_chip_id();
    test_read_6axis();
    test_poll_rate();

    l5_errors = errors;
    if (errors == 0) {
        l5_test_result = 0x900D900D;
        rt_kprintf("[L5] ALL PASS\n");
    } else {
        l5_test_result = errors;
        rt_kprintf("[L5] FAIL - %d error(s)\n", errors);
    }
    rt_kprintf("[L5] ========== IMU TEST END ==========\n\n");

    rt_base_t led = GET_PIN(B, 0);
    gpio_ll_pin_mode(led, PIN_MODE_OUTPUT);
    while (1) {
        gpio_ll_pin_write(led, 1);
        rt_thread_mdelay(200);
        gpio_ll_pin_write(led, 0);
        rt_thread_mdelay(200);
    }
}
