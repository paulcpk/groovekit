/* LAME's build configuration for GrooveKit: the encoder only (no decoder, no frontend), on
   macOS with a standard C library. */
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_LIMITS_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define STDC_HEADERS 1
#define HAVE_MEMCPY 1
#define HAVE_STRCHR 1
#define USE_FAST_LOG 1
#define ieee754_float32_t float
#define PACKAGE "lame"
#define VERSION "3.100"
