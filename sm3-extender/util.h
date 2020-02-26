#pragma once

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define bitsof(x) (CHAR_BIT * sizeof(x))

#define rol(x, n) (((x) << (n)) | ((x) >> ((bitsof(x) - (n)))))
#define ror(x, n) (((x) >> (n)) | ((x) << ((bitsof(x) - (n)))))

#define byte_swap16 __builtin_bswap16
#define byte_swap32 __builtin_bswap32
#define byte_swap64 __builtin_bswap64

#define to_bigendian16 byte_swap16
#define to_bigendian32 byte_swap32
#define to_bigendian64 byte_swap64

