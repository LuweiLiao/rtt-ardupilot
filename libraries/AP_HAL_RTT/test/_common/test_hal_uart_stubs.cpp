/*
 * Link stubs for D_uart_hal HAL smoke — symbols pulled by HAL_RTT / AP_Param
 * without linking the full vehicle stack.
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_InternalError/AP_InternalError.h>

const AP_HAL::HAL &hal = AP_HAL::get_HAL();

namespace AP {

AP_InternalError &internalerror()
{
    static AP_InternalError ie;
    return ie;
}

} // namespace AP
