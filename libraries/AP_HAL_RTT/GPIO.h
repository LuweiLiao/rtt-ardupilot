/*
 * AP_HAL_RTT — GPIO driver
 * Uses RT-Thread rt_pin_* API for digital I/O.
 */

#pragma once

#include <AP_HAL/GPIO.h>
#include "HAL_RTT_Namespace.h"

namespace RTT
{

class DigitalSource : public AP_HAL::DigitalSource
{
public:
    DigitalSource(uint16_t pin);
    void mode(uint8_t output) override;
    uint8_t read() override;
    void write(uint8_t value) override;
    void toggle() override;
private:
    uint16_t _pin;
};

class GPIO : public AP_HAL::GPIO
{
public:
    void init() override;
    void pinMode(uint8_t pin, uint8_t output) override;
    void pinMode(uint8_t pin, uint8_t output, uint8_t alt) override;
    uint8_t read(uint8_t pin) override;
    void write(uint8_t pin, uint8_t value) override;
    void toggle(uint8_t pin) override;
    AP_HAL::DigitalSource* channel(uint16_t n) override;
    bool usb_connected() override;
    bool attach_interrupt(uint8_t pin,
                          irq_handler_fn_t fn,
                          INTERRUPT_TRIGGER_TYPE mode) override;
    bool attach_interrupt(uint8_t pin,
                          AP_HAL::Proc fn,
                          INTERRUPT_TRIGGER_TYPE mode) override;
};

} // namespace RTT
