/*
 * AP_HAL_RTT — GPIO driver implementation
 * Uses RT-Thread rt_pin_* API. Pin numbers follow GET_PIN() convention.
 */

#include "GPIO.h"
#include <AP_HAL/AP_HAL.h>
#include <rtthread.h>
#include <drivers/dev_pin.h>

using namespace RTT;

/* DigitalSource */

DigitalSource::DigitalSource(uint16_t pin) : _pin(pin) {}

void DigitalSource::mode(uint8_t output)
{
    rt_pin_mode(_pin, output ? PIN_MODE_OUTPUT : PIN_MODE_INPUT);
}

uint8_t DigitalSource::read()
{
    return rt_pin_read(_pin) == PIN_HIGH ? 1 : 0;
}

void DigitalSource::write(uint8_t value)
{
    rt_pin_write(_pin, value ? PIN_HIGH : PIN_LOW);
}

void DigitalSource::toggle()
{
    write(read() ^ 1);
}

/* GPIO */

void GPIO::init()
{
}

void GPIO::pinMode(uint8_t pin, uint8_t output)
{
    if (output == HAL_GPIO_INPUT) {
        rt_pin_mode(pin, PIN_MODE_INPUT);
    } else {
        rt_pin_mode(pin, PIN_MODE_OUTPUT);
    }
}

void GPIO::pinMode(uint8_t pin, uint8_t output, uint8_t alt)
{
    (void)alt;
    pinMode(pin, output);
}

uint8_t GPIO::read(uint8_t pin)
{
    return rt_pin_read(pin) == PIN_HIGH ? 1 : 0;
}

void GPIO::write(uint8_t pin, uint8_t value)
{
    rt_pin_write(pin, value ? PIN_HIGH : PIN_LOW);
}

void GPIO::toggle(uint8_t pin)
{
    write(pin, read(pin) ^ 1);
}

AP_HAL::DigitalSource* GPIO::channel(uint16_t n)
{
    return NEW_NOTHROW DigitalSource(n);
}

bool GPIO::usb_connected()
{
    /*
     * GCS_MAVLink::have_flow_control() for MAVLINK_COMM_0 uses hal.gpio->usb_connected().
     * When false, ArduPilot throttles PARAM_VALUE streaming (see GCS_Param.cpp:
     * low bw_in_bytes_per_second + max 5 params/batch without "flow control").
     * ChibiOS sets this when USB VCP is active; RTT primary GCS is USB CDC — treat as connected.
     */
    return true;
}

bool GPIO::attach_interrupt(uint8_t pin,
                            irq_handler_fn_t fn,
                            INTERRUPT_TRIGGER_TYPE mode)
{
    (void)pin;
    (void)fn;
    (void)mode;
    return false;
}

bool GPIO::attach_interrupt(uint8_t pin, AP_HAL::Proc fn, INTERRUPT_TRIGGER_TYPE mode)
{
    (void)pin;
    (void)fn;
    (void)mode;
    return false;
}
