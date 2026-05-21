/*
 * stdio wrappers for AP_HAL_RTT
 *
 * Mirrors ChibiOS AP_HAL_ChibiOS/stdio.cpp — provides __wrap_* functions
 * that redirect printf/vprintf/snprintf/fprintf/scanf via linker wrapping.
 *
 * Relies on -Wl,--wrap,<func> linker flags set in SConscript.
 *
 * The __wrap_snprintf / __wrap_vsnprintf variants delegate to
 * hal.util->vsnprintf() (ArduPilot's own print_vprintf).
 * For bootloader builds, use RT-Thread's rt_vsnprintf directly.
 *
 * The __wrap_vprintf / __wrap_printf / __wrap_fprintf chain goes through
 * the vprintf_console_hook function pointer, defaulting to a function
 * that formats via vsnprintf and emits via rt_kprintf.  The hook can be
 * replaced at runtime to redirect to USB CDC or UART output.
 */

#include <string.h>
#include <rtthread.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/statfs.h>
#include "hwdef/common/stdio.h"
#include <AP_HAL/AP_HAL.h>

#ifndef HAL_BOOTLOADER_BUILD
extern const AP_HAL::HAL& hal;
#endif

/* ----------------------------------------------------------------
 * Default console vprintf — formats to a stack buffer, outputs via
 * rt_kprintf.  Works from early boot onward; no dependency on hal.
 * ---------------------------------------------------------------- */
static int _rtt_console_vprintf(const char *fmt, va_list arg)
{
    char buf[256];
    int ret = vsnprintf(buf, sizeof(buf), fmt, arg);
    if (ret > 0) {
        rt_kprintf("%s", buf);
    }
    return ret;
}

/* Hook pointer — allows HAL to redirect console output at runtime */
int (*vprintf_console_hook)(const char *fmt, va_list arg) = _rtt_console_vprintf;

/* ----------------------------------------------------------------
 * __wrap_snprintf / __wrap_vsnprintf
 *
 * Use hal.util->vsnprintf() when available (ArduPilot's own
 * print_vprintf), fall back to newlib's __real_vsnprintf.
 *
 * ChibiOS reference: stdio.cpp:41-64
 * ---------------------------------------------------------------- */
int __wrap_snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list arg;
    int done;

    va_start(arg, fmt);
#ifdef HAL_BOOTLOADER_BUILD
    done = rt_vsnprintf(str, size, fmt, arg);
#else
    done = hal.util->vsnprintf(str, size, fmt, arg);
#endif
    va_end(arg);

    return done;
}

int __wrap_vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
#ifdef HAL_BOOTLOADER_BUILD
    return rt_vsnprintf(str, size, fmt, ap);
#else
    return hal.util->vsnprintf(str, size, fmt, ap);
#endif
}

/* ----------------------------------------------------------------
 * __wrap_vasprintf / __wrap_asprintf
 *
 * Allocate and format.  ChibiOS reference: stdio.cpp:66-88
 * ---------------------------------------------------------------- */
int __wrap_vasprintf(char **strp, const char *fmt, va_list ap)
{
    int len = __wrap_vsnprintf(NULL, 0, fmt, ap);
    if (len <= 0) {
        return -1;
    }
    char *buf = (char*)calloc(len + 1, 1);
    if (!buf) {
        return -1;
    }
    __wrap_vsnprintf(buf, len + 1, fmt, ap);
    *strp = buf;
    return len;
}

int __wrap_asprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = __wrap_vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}

/* ----------------------------------------------------------------
 * __wrap_vprintf — core output function
 *
 * ChibiOS reference: stdio.cpp:90-101
 * In ChibiOS this writes to HAL_STDOUT_SERIAL or SDU1 (USB CDC).
 * In RTT we go through the vprintf_console_hook, which by default
 * formats via vsnprintf and emits via rt_kprintf.
 *
 * The hook can be overridden by HAL to redirect to hal.console
 * (USB CDC) for user-facing output, or to a debug UART.
 * ---------------------------------------------------------------- */
int __wrap_vprintf(const char *fmt, va_list arg)
{
    return vprintf_console_hook(fmt, arg);
}

/* ----------------------------------------------------------------
 * __wrap_printf — public API entry point
 *
 * ChibiOS reference: stdio.cpp:106-121
 * ---------------------------------------------------------------- */
int __wrap_printf(const char *fmt, ...)
{
#ifndef HAL_NO_PRINTF
    va_list arg;
    int done;

    va_start(arg, fmt);
    done = vprintf_console_hook(fmt, arg);
    va_end(arg);

    return done;
#else
    (void)fmt;
    return 0;
#endif
}

/* ----------------------------------------------------------------
 * __wrap_fprintf — stdout/stderr (ignore FILE*)
 *
 * ChibiOS reference: stdio.cpp:127-142
 * ---------------------------------------------------------------- */
int __wrap_fprintf(void *f, const char *fmt, ...)
{
#ifndef HAL_NO_PRINTF
    va_list arg;
    int done;

    va_start(arg, fmt);
    done = vprintf_console_hook(fmt, arg);
    va_end(arg);

    return done;
#else
    (void)f;
    (void)fmt;
    return 0;
#endif
}

/* ----------------------------------------------------------------
 * __wrap_scanf / __wrap_sscanf — stubs
 *
 * ChibiOS reference: stdio.cpp:145-149
 * ---------------------------------------------------------------- */
int __wrap_scanf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

int __wrap_sscanf(const char *buf, const char *fmt, ...)
{
    /* Use the real sscanf via __real_ prefix */
    extern int __real_sscanf(const char *, const char *, ...);
    va_list ap;
    va_start(ap, fmt);
    int ret = __real_sscanf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

/* ----------------------------------------------------------------
 * __wrap_fiprintf — stub, saves flash for unused code path
 *
 * ChibiOS reference: stdio.cpp:151-155
 * ---------------------------------------------------------------- */
extern "C" {
    int __wrap_fiprintf(const char *fmt, ...);
    int __wrap_fiprintf(const char *fmt, ...) { (void)fmt; return -1; }
}

/* ----------------------------------------------------------------
 * statfs / fstatfs — stubs for RT-Thread DFS compatibility
 *
 * RT-Thread's VFS/SD layer uses <sys/statfs.h> from
 * AP_HAL_RTT/include/sys/statfs.h, which declares statfs() and
 * fstatfs().  Provide -1 stubs since DFS is not compiled in.
 * ---------------------------------------------------------------- */
extern "C" {
int statfs(const char *path, struct statfs *buf)
{
    (void)path;
    (void)buf;
    return -1;
}

int fstatfs(int fd, struct statfs *buf)
{
    (void)fd;
    (void)buf;
    return -1;
}
}
