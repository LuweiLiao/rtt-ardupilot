/*
 * AP_HAL_RTT — RCInput (aligned with ChibiOS)
 * Uses local buffer + mutex pattern: _timer_tick() copies data from
 * AP_RCProtocol to local _rc_values[] under mutex; read() reads from
 * the local buffer. This decouples the main thread from the protocol
 * layer's internal _new_input flag.
 *
 * TIM input capture via SoftSigReaderInt provides pulse detection
 * on boards without IOMCU. Board hwdef must define HAL_RCININT_*
 * macros to enable the capture path; otherwise RC comes via serial
 * (IOMCU/SBUS/CRSF UART) through rcprot.update().
 */

#include "RCInput.h"
#include <AP_HAL/AP_HAL.h>

#include <AP_RCProtocol/AP_RCProtocol_config.h>
#if AP_RCPROTOCOL_ENABLED
#include <AP_RCProtocol/AP_RCProtocol.h>
#endif

#include <string.h>

/* CMSIS device header for GPIO and NVIC register access */
#include <stm32f7xx.h>

namespace RTT
{

void RCInput::init()
{
#if AP_RCPROTOCOL_ENABLED
    AP::RC().init();
#endif

#ifdef HAL_RCININT_TIMER
    /*
     * Board-level RCININT pin definition — configure GPIO alternate
     * function for TIM input capture. Macros are set in board hwdef:
     *   HAL_RCININT_GPIO     — GPIO port base (e.g. GPIOI for PI5)
     *   HAL_RCININT_PIN      — pin number (e.g. 5 for PI5)
     *   HAL_RCININT_AF       — AF number (e.g. 3 for TIM8 on PI5)
     *   HAL_RCININT_PULLUP   — if defined, enable pull-up
     *   HAL_RCININT_PULLDOWN — if defined, enable pull-down
     *   HAL_RCININT_RCC_ENR  — RCC enable register (e.g. RCC->APB2ENR)
     *   HAL_RCININT_RCC_BIT  — RCC enable bit mask (e.g. RCC_APB2ENR_TIM8EN)
     */

    /* Enable TIM peripheral clock (matching ChibiOS icu_lld_start RCC enable) */
#ifdef HAL_RCININT_RCC_ENR
    HAL_RCININT_RCC_ENR |= HAL_RCININT_RCC_BIT;
#endif

#ifdef HAL_RCININT_GPIO
    /* Enable GPIO port clock */
#ifdef HAL_RCININT_GPIO_RCC_ENR
    HAL_RCININT_GPIO_RCC_ENR |= HAL_RCININT_GPIO_RCC_BIT;
#endif
    /* Set pin to alternate function mode */
    HAL_RCININT_GPIO->MODER &= ~(GPIO_MODER_MODER0 << (HAL_RCININT_PIN * 2));
    HAL_RCININT_GPIO->MODER |= (GPIO_MODER_AF0 << (HAL_RCININT_PIN * 2));
    /* Set the alternate function number */
    HAL_RCININT_GPIO->AFR[HAL_RCININT_PIN >> 3] &= ~(0xF << ((HAL_RCININT_PIN & 0x7) * 4));
    HAL_RCININT_GPIO->AFR[HAL_RCININT_PIN >> 3] |= (HAL_RCININT_AF << ((HAL_RCININT_PIN & 0x7) * 4));
    /* Pull configuration */
#if defined(HAL_RCININT_PULLUP)
    HAL_RCININT_GPIO->PUPDR = (HAL_RCININT_GPIO->PUPDR & ~(GPIO_PUPDR_PUPD0 << (HAL_RCININT_PIN * 2)))
                              | (GPIO_PUPDR_PUPD0_0 << (HAL_RCININT_PIN * 2));
#elif defined(HAL_RCININT_PULLDOWN)
    HAL_RCININT_GPIO->PUPDR = (HAL_RCININT_GPIO->PUPDR & ~(GPIO_PUPDR_PUPD0 << (HAL_RCININT_PIN * 2)))
                              | (GPIO_PUPDR_PUPD0_1 << (HAL_RCININT_PIN * 2));
#else
    /* No pull */
    HAL_RCININT_GPIO->PUPDR &= ~(GPIO_PUPDR_PUPD0 << (HAL_RCININT_PIN * 2));
#endif
#endif  // HAL_RCININT_GPIO

    /* Initialise TIM input capture on the configured timer/channel */
    sig_reader.init(HAL_RCININT_TIMER, HAL_RCININT_CHANNEL, HAL_RCININT_IRQ);
    pulse_input_enabled = true;
#endif  // HAL_RCININT_TIMER

    _init = true;
}

bool RCInput::new_input()
{
    if (!_init) {
        return false;
    }
    bool valid;
    {
        WITH_SEMAPHORE(rcin_mutex);
        valid = _rcin_timestamp_last_signal != _last_read;
        _last_read = _rcin_timestamp_last_signal;
    }
    return valid;
}

uint8_t RCInput::num_channels()
{
    if (!_init) {
        return 0;
    }
    return _num_channels;
}

uint16_t RCInput::read(uint8_t ch)
{
    if (!_init || ch >= MIN(RC_INPUT_MAX_CHANNELS, _num_channels)) {
        return 0;
    }
    uint16_t v;
    {
        WITH_SEMAPHORE(rcin_mutex);
        v = _rc_values[ch];
    }
    return v;
}

uint8_t RCInput::read(uint16_t* periods, uint8_t len)
{
    if (!_init) {
        return 0;
    }
    if (len > RC_INPUT_MAX_CHANNELS) {
        len = RC_INPUT_MAX_CHANNELS;
    }
    {
        WITH_SEMAPHORE(rcin_mutex);
        memcpy(periods, _rc_values, len * sizeof(periods[0]));
    }
    return len;  // Match ChibiOS: return requested len, not actual channels
}

void RCInput::pulse_input_enable(bool enable)
{
    pulse_input_enabled = enable;
#ifdef HAL_RCININT_TIMER
    if (!enable) {
        sig_reader.disable();
    }
#endif
}

void RCInput::_timer_tick(void)
{
    if (!_init) {
        return;
    }
#if AP_RCPROTOCOL_ENABLED
    AP_RCProtocol &rcprot = AP::RC();

#ifdef HAL_RCININT_TIMER
    /*
     * Read pulse captures from TIM input capture and feed them
     * to AP_RCProtocol for decoding. This path is used on boards
     * without IOMCU (e.g. Pixhawk4-mini, MatekF405-Wing).
     * Matching ChibiOS RCInput EICU path (lines 138-145).
     */
    if (pulse_input_enabled) {
        uint32_t width_s0, width_s1;
        while (sig_reader.read(width_s0, width_s1)) {
            rcprot.process_pulse(width_s0, width_s1);
        }
    }
#endif  // HAL_RCININT_TIMER

    if (rcprot.new_input()) {
        WITH_SEMAPHORE(rcin_mutex);
        _rcin_timestamp_last_signal = AP_HAL::micros();
        _num_channels = rcprot.num_channels();
        _num_channels = MIN(_num_channels, RC_INPUT_MAX_CHANNELS);
        rcprot.read(_rc_values, _num_channels);
        _rssi = rcprot.get_RSSI();
        _rx_link_quality = rcprot.get_rx_link_quality();
    }

    // note, we rely on the vehicle code checking new_input()
    // and a timeout for the last valid input to handle failsafe
#endif  // AP_RCPROTOCOL_ENABLED
}

} // namespace RTT
