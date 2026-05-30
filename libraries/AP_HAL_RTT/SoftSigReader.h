/*
 * AP_HAL_RTT — SoftSigReader (ChibiOS API compatibility).
 *
 * ChibiOS SoftSigReader uses ICU + DMA bounce buffers (HAL_USE_ICU).
 * RT-Thread port uses interrupt-driven SoftSigReaderInt only.
 *
 * RCInput on RTT calls SoftSigReaderInt::init() directly when HAL_RCININT_*
 * is defined. This header exists so code searching for SoftSigReader finds
 * the RTT interrupt path and does not assume ICU/DMA is available.
 */

#pragma once

#include "SoftSigReaderInt.h"
#include "HAL_RTT_Namespace.h"

namespace RTT
{

class SoftSigReader
{
public:
    /*
     * ChibiOS: attach ICU + DMA capture. Not supported on RTT — use
     * SoftSigReaderInt::init(tim, chan, irq_n) from board RCININT macros.
     */
    bool attach_capture_timer(void *icu_drv, uint8_t chan,
                              uint8_t dma_stream, uint32_t dma_channel)
    {
        (void)icu_drv;
        (void)chan;
        (void)dma_stream;
        (void)dma_channel;
        return false;
    }

    void disable(void)
    {
        SoftSigReaderInt *reader = SoftSigReaderInt::get_singleton();
        if (reader != nullptr) {
            reader->disable();
        }
    }

    bool read(uint32_t &widths0, uint32_t &widths1)
    {
        SoftSigReaderInt *reader = SoftSigReaderInt::get_singleton();
        if (reader == nullptr) {
            return false;
        }
        return reader->read(widths0, widths1);
    }
};

} // namespace RTT
