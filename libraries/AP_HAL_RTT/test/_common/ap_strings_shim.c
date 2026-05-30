/* libc string compare wrappers for D_uart_hal HAL smoke (AP_Param). */
#include <rtthread.h>

int strcasecmp(const char *a, const char *b)
{
    return rt_strcasecmp(a, b);
}

int strncasecmp(const char *a, const char *b, unsigned int n)
{
    return rt_strncasecmp(a, b, n);
}
