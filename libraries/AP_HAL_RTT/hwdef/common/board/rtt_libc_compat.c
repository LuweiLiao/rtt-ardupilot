/*
 * Polyfills for GNU libc extensions missing in newlib bare-metal.
 * Provides: asprintf, vasprintf, memmem
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _GNU_SOURCE

int vasprintf(char **strp, const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0) {
        *strp = NULL;
        return -1;
    }
    *strp = (char *)malloc((size_t)len + 1);
    if (!*strp)
        return -1;
    return vsnprintf(*strp, (size_t)len + 1, fmt, ap);
}

int asprintf(char **strp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}

void *memmem(const void *haystack, size_t haystacklen,
             const void *needle, size_t needlelen)
{
    if (needlelen == 0)
        return (void *)haystack;
    if (haystacklen < needlelen)
        return NULL;
    const char *h = (const char *)haystack;
    const char *end = h + haystacklen - needlelen;
    for (; h <= end; h++) {
        if (memcmp(h, needle, needlelen) == 0)
            return (void *)h;
    }
    return NULL;
}

#endif /* _GNU_SOURCE */
