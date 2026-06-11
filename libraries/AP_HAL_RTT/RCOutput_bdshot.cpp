/*
 * AP_HAL_RTT — bidirectional DShot placement (matrix parity with ChibiOS).
 *
 * ChibiOS: libraries/AP_HAL_ChibiOS/RCOutput_bdshot.cpp — FMU TIM+DMA bdshot
 * telemetry, depends on Shared_DMA.
 *
 * CUAV v5 / current RTT baseline:
 *   - Motor PWM/DShot to ESCs goes through IOMCU (RCOutput.cpp + AP_IOMCU).
 *   - FMU bdshot is not implemented; AP_HAL::RCOutput::set_bidir_dshot_mask()
 *     uses the empty base default.
 *
 * Future FMU bdshot work belongs in this file: port state machine from
 * ChibiOS RCOutput_bdshot.cpp and wire RTT::Shared_DMA for DMA streams.
 */

#include "RCOutput.h"

#if HAL_WITH_IO_MCU
#include <AP_IOMCU/AP_IOMCU.h>
extern AP_IOMCU iomcu;
#endif

using namespace RTT;

void RCOutput::set_bidir_dshot_mask(uint32_t mask)
{
#if HAL_WITH_IO_MCU_BIDIR_DSHOT
    const uint32_t iomcu_mask = ((1U << chan_offset) - 1U);
    if (iomcu_dshot && (mask & iomcu_mask)) {
        iomcu.set_bidir_dshot_mask(mask & iomcu_mask);
    }
#else
    (void)mask;
#endif
}

#if defined(HAL_WITH_BIDIR_DSHOT) && !HAL_WITH_IO_MCU
#error "FMU bidirectional DShot on RTT is not implemented — extend RCOutput_bdshot.cpp"
#endif
