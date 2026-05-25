/*
 * stdio wrappers for AP_HAL_RTT
 *
 * Mirrors ChibiOS hwdef/common/stdio.h — declares __wrap_* functions
 * that redirect printf/vprintf/snprintf/fprintf/scanf via linker wrapping.
 *
 * Relies on -Wl,--wrap,<func> linker flags set in SConscript.
 * The wrapping functions forward to vsnprintf + rt_kprintf or console output.
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SEEK_SET/SEEK_CUR/SEEK_END needed by DFS/POSIX components (msh_file.c etc.) */
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/* printf family wrappers */
int __wrap_snprintf(char *str, size_t size, const char *fmt, ...);
int __wrap_vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int __wrap_vasprintf(char **strp, const char *fmt, va_list ap);
int __wrap_asprintf(char **strp, const char *fmt, ...);
int __wrap_vprintf(const char *fmt, va_list arg);
int __wrap_printf(const char *fmt, ...);
int __wrap_scanf(const char *fmt, ...);
int __wrap_sscanf(const char *buf, const char *fmt, ...);
int __wrap_fprintf(void *f, const char *fmt, ...);

/* Forward declarations so callers using the unwrapped names also resolve.
   These are never used directly — the __wrap_ variants replace them at link time. */
int snprintf(char *str, size_t size, const char *fmt, ...);   // replaced by __wrap_snprintf
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap); // replaced by __wrap_vsnprintf
int vasprintf(char **strp, const char *fmt, va_list ap);       // replaced by __wrap_vasprintf
int asprintf(char **strp, const char *fmt, ...);               // replaced by __wrap_asprintf
int vprintf(const char *fmt, va_list arg);                     // replaced by __wrap_vprintf
int printf(const char *fmt, ...);                              // replaced by __wrap_printf

/* Hook for redirecting console output — can be replaced at runtime.
   Default implementation writes via rt_kprintf (RT-Thread console).
   HAL can override this to redirect to USB CDC or UART. */
extern int (*vprintf_console_hook)(const char *fmt, va_list arg);

void malloc_check(const void *ptr);

#ifdef __cplusplus
}
#endif

/* Include the system stdio.h to get standard declarations like rename(), fopen(), etc. */
#include_next <stdio.h>
