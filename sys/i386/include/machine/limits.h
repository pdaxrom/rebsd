#ifndef _I386_MACHINE_LIMITS_H_
#define _I386_MACHINE_LIMITS_H_

#define CHAR_BIT        8

#define SCHAR_MAX       127
#define SCHAR_MIN       (-128)
#define UCHAR_MAX       255
#define CHAR_MAX        127
#define CHAR_MIN        (-128)

#define USHRT_MAX       65535
#define SHRT_MAX        32767
#define SHRT_MIN        (-32768)

#define UINT_MAX        0xffffffffU
#define INT_MAX         2147483647
#define INT_MIN         (-2147483647-1)

#define ULONG_MAX       0xffffffffUL
#define LONG_MAX        2147483647L
#define LONG_MIN        (-2147483647L-1L)

#define SSIZE_MAX       INT_MAX
#define SIZE_T_MAX      UINT_MAX

#endif
