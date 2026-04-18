/*
 * Minimal newlib syscall stubs for bare-metal RT-Thread builds.
 *
 * ArduPilot links against arm-none-eabi newlib which expects these
 * symbols.  RT-Thread's own syscalls (components/libc/compilers/newlib/)
 * are only compiled when RT_USING_NEWLIB is enabled in .config, which
 * is not the case for the CUAV V5 BSP.  Provide thin stubs here so the
 * waf link succeeds.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <rtthread.h>

/* sbrk — heap extension (required by malloc) */
extern char __HeapLimit;  /* linker-defined end of RAM */
static char *_sbrk_cur = NULL;

void *_sbrk(ptrdiff_t incr)
{
    if (_sbrk_cur == NULL) {
        _sbrk_cur = &_sbrk_cur;  /* will be set by linker script via _end */
        /* Try to get _end from linker symbols */
        extern char _end;
        _sbrk_cur = &_end;
    }
    char *prev = _sbrk_cur;
    if (_sbrk_cur + incr > &__HeapLimit) {
        return (void *)-1;
    }
    _sbrk_cur += incr;
    return prev;
}

int _write(int fd, const void *buf, size_t nbytes)
{
    (void)fd;
    (void)buf;
    (void)nbytes;
    return -1;
}

int _read(int fd, void *buf, size_t nbytes)
{
    (void)fd;
    (void)buf;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _fstat(int fd, struct stat *pstat)
{
    (void)fd;
    if (pstat) {
        pstat->st_mode = S_IFCHR;
    }
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

off_t _lseek(int fd, off_t pos, int whence)
{
    (void)fd;
    (void)pos;
    (void)whence;
    return -1;
}

int _gettimeofday(struct timeval *__tp, void *__tzp)
{
    (void)__tzp;
    if (__tp) {
        __tp->tv_sec  = (long)(rt_tick_get() / RT_TICK_PER_SECOND);
        __tp->tv_usec = 0;
    }
    return 0;
}

void _exit(int status)
{
    (void)status;
    while (1) { __ASM volatile("bkpt #0"); }
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

int _getpid(void)
{
    return 0;
}
