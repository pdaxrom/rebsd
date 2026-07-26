/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _LINUX_TYPES_H_
#define _LINUX_TYPES_H_

/*
 * Linux UAPI-compatible fixed-width scalar names.  ReBSD currently exposes
 * this compatibility namespace to 32-bit MIPS userland.
 */
typedef signed char __s8;
typedef unsigned char __u8;
typedef signed short __s16;
typedef unsigned short __u16;
typedef signed int __s32;
typedef unsigned int __u32;
typedef signed long long __s64;
typedef unsigned long long __u64;

#endif
