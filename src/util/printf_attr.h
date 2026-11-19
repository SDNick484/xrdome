#ifndef XRD_PRINTF_ATTR_H
#define XRD_PRINTF_ATTR_H

/* printf-style format checking for a function's (format, first argument)
 * parameters. MinGW-w64 gcc checks the "printf" archetype by the old MSVCRT
 * rules (no %zu, no %jd); xrdome links the UCRT, whose printf takes C99
 * formats, so it checks gnu_printf there. */
#if defined(__MINGW32__)
#define XRD_PRINTF(fmt, first) __attribute__((format(gnu_printf, fmt, first)))
#else
#define XRD_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#endif

#endif
