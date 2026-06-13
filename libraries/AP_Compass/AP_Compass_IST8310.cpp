/*
 * Copyright (C) 2016  Emlid Ltd. All rights reserved.
 *
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Driver by Georgii Staroselskii, Sep 2016
 */
#include "AP_Compass_IST8310.h"

#if AP_COMPASS_IST8310_ENABLED

#include <stdio.h>
#include <utility>

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include <AP_HAL/utility/sparse-endian.h>
#include <AP_Math/AP_Math.h>
#include <AP_BoardConfig/AP_BoardConfig.h>

#define WAI_REG 0x0
#define DEVICE_ID 0x10

#define OUTPUT_X_L_REG 0x3
#define OUTPUT_X_H_REG 0x4
#define OUTPUT_Y_L_REG 0x5
#define OUTPUT_Y_H_REG 0x6
#define OUTPUT_Z_L_REG 0x7
#define OUTPUT_Z_H_REG 0x8

#define CNTL1_REG 0xA
#define CNTL1_VAL_SINGLE_MEASUREMENT_MODE 0x1

#define CNTL2_REG 0xB
#define CNTL2_VAL_SRST 1

#define AVGCNTL_REG 0x41
#define AVGCNTL_VAL_XZ_0  (0)
#define AVGCNTL_VAL_XZ_2  (1)
#define AVGCNTL_VAL_XZ_4  (2)
#define AVGCNTL_VAL_XZ_8  (3)
#define AVGCNTL_VAL_XZ_16 (4)
#define AVGCNTL_VAL_Y_0  (0 << 3)
#define AVGCNTL_VAL_Y_2  (1 << 3)
#define AVGCNTL_VAL_Y_4  (2 << 3)
#define AVGCNTL_VAL_Y_8  (3 << 3)
#define AVGCNTL_VAL_Y_16 (4 << 3)

#define PDCNTL_REG 0x42
#define PDCNTL_VAL_PULSE_DURATION_NORMAL 0xC0

#define SAMPLING_PERIOD_USEC (10 * AP_USEC_PER_MSEC)

#if CONFIG_HAL_BOARD == HAL_BOARD_RTT
#define IST8310_WHOAMI_ATTEMPTS 30
#define IST8310_WHOAMI_RETRY_DELAY_MS 25
#else
#define IST8310_WHOAMI_ATTEMPTS 5
#define IST8310_WHOAMI_RETRY_DELAY_MS 20
#endif

/*
 * FSR:
 *   x, y: +- 1600 µT
 *   z:    +- 2500 µT
 *
 * Resolution according to datasheet is 0.3µT/LSB
 */
#define IST8310_RESOLUTION 0.3

static const int16_t IST8310_MAX_VAL_XY = (1600 / IST8310_RESOLUTION) + 1;
static const int16_t IST8310_MIN_VAL_XY = -IST8310_MAX_VAL_XY;
static const int16_t IST8310_MAX_VAL_Z  = (2500 / IST8310_RESOLUTION) + 1;
static const int16_t IST8310_MIN_VAL_Z  = -IST8310_MAX_VAL_Z;


extern const AP_HAL::HAL &hal;

#define RTT_IST8310_DBG_DTCM_BSS __attribute__((section(".dtcm_bss.rtt_dbg"), used))

enum {
    RTT_IST8310_FAIL_NONE = 0,
    RTT_IST8310_FAIL_NO_DEV = 1,
    RTT_IST8310_FAIL_ALLOC_OR_INIT = 2,
    RTT_IST8310_FAIL_WHOAMI_READ = 3,
    RTT_IST8310_FAIL_WHOAMI_MISMATCH = 4,
    RTT_IST8310_FAIL_RESET_STUCK = 5,
    RTT_IST8310_FAIL_SETUP_WRITE = 6,
    RTT_IST8310_FAIL_REGISTER_COMPASS = 7,
};

volatile uint32_t rtt_dbg_ist8310_probe_calls RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_probe_null_dev RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_probe_init_fail RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_probe_success RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_init_calls RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_init_success RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_fail_reason RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_fail_counts[8] RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_initial_reset_write_ok RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_whoami_read_ok RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_whoami_attempts RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_last_whoami RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_reset_attempts RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_reset_write_fail RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_reset_read_ok RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_last_cntl2 RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_setup_avgcntl_ok RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_setup_pdcntl_ok RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_start_conversion_calls RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_start_conversion_fail RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_timer_calls RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_timer_read_fail RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_timer_outlier RTT_IST8310_DBG_DTCM_BSS;
volatile uint32_t rtt_dbg_ist8310_timer_accumulate RTT_IST8310_DBG_DTCM_BSS;

static void rtt_ist8310_fail(uint32_t reason)
{
    rtt_dbg_ist8310_fail_reason = reason;
    if (reason < sizeof(rtt_dbg_ist8310_fail_counts) / sizeof(rtt_dbg_ist8310_fail_counts[0])) {
        rtt_dbg_ist8310_fail_counts[reason]++;
    }
}

AP_Compass_Backend *AP_Compass_IST8310::probe(AP_HAL::OwnPtr<AP_HAL::I2CDevice> dev,
                                              bool force_external,
                                              enum Rotation rotation)
{
    rtt_dbg_ist8310_probe_calls++;
    if (!dev) {
        rtt_dbg_ist8310_probe_null_dev++;
        rtt_ist8310_fail(RTT_IST8310_FAIL_NO_DEV);
        return nullptr;
    }

    AP_Compass_IST8310 *sensor = NEW_NOTHROW AP_Compass_IST8310(std::move(dev), force_external, rotation);
    if (!sensor) {
        rtt_dbg_ist8310_probe_init_fail++;
        rtt_ist8310_fail(RTT_IST8310_FAIL_ALLOC_OR_INIT);
        return nullptr;
    }
    if (!sensor->init()) {
        rtt_dbg_ist8310_probe_init_fail++;
        delete sensor;
        return nullptr;
    }

    rtt_dbg_ist8310_probe_success++;
    return sensor;
}

AP_Compass_IST8310::AP_Compass_IST8310(AP_HAL::OwnPtr<AP_HAL::Device> dev,
                                       bool force_external,
                                       enum Rotation rotation)
    : _dev(std::move(dev))
    , _rotation(rotation)
    , _force_external(force_external)
{
}

bool AP_Compass_IST8310::init()
{
    rtt_dbg_ist8310_init_calls++;
    uint8_t reset_count = 0;

    _dev->get_semaphore()->take_blocking();

    // high retries for init
    _dev->set_retries(10);
    _dev->set_speed(AP_HAL::Device::SPEED_LOW);

    /*
      unfortunately the IST8310 employs the novel concept of a
      writeable WHOAMI register. The register can become corrupt due
      to bus noise, and what is worse it persists the corruption even
      across a power cycle. If you power it off for 30s or more then
      it will reset to the default of 0x10, but for less than that the
      value of WAI is unreliable.

      To avoid this issue we do a reset of the device before we probe
      the WAI register. This is nasty as we don't yet know we've found
      a real IST8310, but it is the best we can do given the bad
      hardware design of the sensor
     */
    uint8_t whoami = 0;
    bool whoami_read_ok = false;
    for (uint8_t attempt = 0; attempt < IST8310_WHOAMI_ATTEMPTS; attempt++) {
        rtt_dbg_ist8310_whoami_attempts = attempt + 1U;
        rtt_dbg_ist8310_initial_reset_write_ok =
            _dev->write_register(CNTL2_REG, CNTL2_VAL_SRST) ? 1U : 0U;
        if (!rtt_dbg_ist8310_initial_reset_write_ok) {
            rtt_dbg_ist8310_reset_write_fail++;
            hal.scheduler->delay(IST8310_WHOAMI_RETRY_DELAY_MS);
            continue;
        }

        hal.scheduler->delay(IST8310_WHOAMI_RETRY_DELAY_MS);

        whoami_read_ok = _dev->read_registers(WAI_REG, &whoami, 1);
        rtt_dbg_ist8310_whoami_read_ok = whoami_read_ok ? 1U : 0U;
        rtt_dbg_ist8310_last_whoami = whoami;
        if (whoami_read_ok && whoami == DEVICE_ID) {
            break;
        }
        hal.scheduler->delay(IST8310_WHOAMI_RETRY_DELAY_MS);
    }

    // [Cybernetics Ch.9] Noise tolerance: early RTT I2C startup can miss the first WAI read.
    if (!whoami_read_ok) {
        rtt_ist8310_fail(RTT_IST8310_FAIL_WHOAMI_READ);
        goto fail;
    }
    if (whoami != DEVICE_ID) {
        // not an IST8310
        rtt_ist8310_fail(RTT_IST8310_FAIL_WHOAMI_MISMATCH);
        goto fail;
    }

    for (; reset_count < 5; reset_count++) {
        rtt_dbg_ist8310_reset_attempts = reset_count + 1U;
        if (!_dev->write_register(CNTL2_REG, CNTL2_VAL_SRST)) {
            rtt_dbg_ist8310_reset_write_fail++;
            hal.scheduler->delay(10);
            continue;
        }

        hal.scheduler->delay(10);

        uint8_t cntl2 = 0xFF;
        bool cntl2_read_ok = _dev->read_registers(CNTL2_REG, &cntl2, 1);
        rtt_dbg_ist8310_last_cntl2 = cntl2;
        if (cntl2_read_ok) {
            rtt_dbg_ist8310_reset_read_ok++;
        }
        if (cntl2_read_ok && (cntl2 & 0x01) == 0) {
            break;
        }
    }

    if (reset_count == 5) {
        rtt_ist8310_fail(RTT_IST8310_FAIL_RESET_STUCK);
        printf("IST8310: failed to reset device\n");
        goto fail;
    }

    rtt_dbg_ist8310_setup_avgcntl_ok =
        _dev->write_register(AVGCNTL_REG, AVGCNTL_VAL_Y_16 | AVGCNTL_VAL_XZ_16) ? 1U : 0U;
    rtt_dbg_ist8310_setup_pdcntl_ok =
        _dev->write_register(PDCNTL_REG, PDCNTL_VAL_PULSE_DURATION_NORMAL) ? 1U : 0U;
    if (!rtt_dbg_ist8310_setup_avgcntl_ok || !rtt_dbg_ist8310_setup_pdcntl_ok) {
        rtt_ist8310_fail(RTT_IST8310_FAIL_SETUP_WRITE);
        printf("IST8310: found device but could not set it up\n");
        goto fail;
    }

    // lower retries for run
    _dev->set_retries(3);
    _dev->set_speed(AP_HAL::Device::SPEED_LOW);

    // start state machine: request a sample
    start_conversion();

    _dev->get_semaphore()->give();

    // register compass instance
    _dev->set_device_type(DEVTYPE_IST8310);
    if (!register_compass(_dev->get_bus_id(), _instance)) {
        rtt_ist8310_fail(RTT_IST8310_FAIL_REGISTER_COMPASS);
        return false;
    }
    set_dev_id(_instance, _dev->get_bus_id());

    printf("%s found on bus %u id %u address 0x%02x\n", name,
           _dev->bus_num(), unsigned(_dev->get_bus_id()), _dev->get_bus_address());

    set_rotation(_instance, _rotation);

    if (_force_external) {
        set_external(_instance, true);
    }
    
    _periodic_handle = _dev->register_periodic_callback(SAMPLING_PERIOD_USEC,
        FUNCTOR_BIND_MEMBER(&AP_Compass_IST8310::timer, void));

    rtt_dbg_ist8310_init_success++;
    rtt_ist8310_fail(RTT_IST8310_FAIL_NONE);
    return true;

fail:
    _dev->get_semaphore()->give();
    return false;
}

void AP_Compass_IST8310::start_conversion()
{
    rtt_dbg_ist8310_start_conversion_calls++;
    if (!_dev->write_register(CNTL1_REG, CNTL1_VAL_SINGLE_MEASUREMENT_MODE)) {
        rtt_dbg_ist8310_start_conversion_fail++;
        _ignore_next_sample = true;
    }
}

void AP_Compass_IST8310::timer()
{
    rtt_dbg_ist8310_timer_calls++;
    if (_ignore_next_sample) {
        _ignore_next_sample = false;
        start_conversion();
        return;
    }

    struct PACKED {
        le16_t rx;
        le16_t ry;
        le16_t rz;
    } buffer;

    bool ret = _dev->read_registers(OUTPUT_X_L_REG, (uint8_t *) &buffer, sizeof(buffer));
    if (!ret) {
        rtt_dbg_ist8310_timer_read_fail++;
        return;
    }

    start_conversion();

    /* same period, but start counting from now */
    _dev->adjust_periodic_callback(_periodic_handle, SAMPLING_PERIOD_USEC);

    auto x = static_cast<int16_t>(le16toh(buffer.rx));
    auto y = static_cast<int16_t>(le16toh(buffer.ry));
    auto z = static_cast<int16_t>(le16toh(buffer.rz));

    /*
     * Check if value makes sense according to the FSR and Resolution of
     * this sensor, discarding outliers
     */
    if (x > IST8310_MAX_VAL_XY || x < IST8310_MIN_VAL_XY ||
        y > IST8310_MAX_VAL_XY || y < IST8310_MIN_VAL_XY ||
        z > IST8310_MAX_VAL_Z  || z < IST8310_MIN_VAL_Z) {
        rtt_dbg_ist8310_timer_outlier++;
        return;
    }

    // flip Z to conform to right-hand rule convention
    z = -z;

    /* Resolution: 0.3 µT/LSB - already convert to milligauss */
    Vector3f field = Vector3f{x * 3.0f, y * 3.0f, z * 3.0f};

    rtt_dbg_ist8310_timer_accumulate++;
    accumulate_sample(field, _instance);
}

void AP_Compass_IST8310::read()
{
    drain_accumulated_samples(_instance);
}

#endif  // AP_COMPASS_IST8310_ENABLED
