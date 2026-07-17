/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _VM_VM_TYPES_H_
#define _VM_VM_TYPES_H_

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#include <sys/types.h>
#else
#include <stddef.h>
#endif
#include <stdint.h>

/*
 * All currently supported kernels and user ABIs are 32-bit.  Malta64 and
 * N64 use 64-bit-capable CPUs, but both kernels are built with -mabi=32.
 * Keep user addresses distinct so a future kernel ABI change does not make
 * the current user ABI implicit.
 */
#define VM_KVA_BITS             32
#define VM_UVA_BITS             32
#define VM_PA_BITS              32

typedef uint32_t vm_vaddr_t;
typedef uint32_t vm_uaddr_t;
typedef uint32_t vm_paddr_t;
typedef uint32_t vm_size_t;
typedef uint32_t vm_vpn_t;
typedef uint32_t vm_pfn_t;
typedef uint32_t vm_prot_t;
typedef uint64_t vm_ooffset_t;

#define VM_VADDR_MAX            UINT32_MAX
#define VM_UADDR_MAX            UINT32_MAX
#define VM_PADDR_MAX            UINT32_MAX
#define VM_SIZE_MAX             UINT32_MAX
#define VM_VPN_MAX              UINT32_MAX
#define VM_PFN_MAX              UINT32_MAX

typedef char vm_assert_uint32_is_4[(sizeof(uint32_t) == 4) ? 1 : -1];
typedef char vm_assert_uint64_is_8[(sizeof(uint64_t) == 8) ? 1 : -1];
typedef char vm_assert_vaddr_is_4[(sizeof(vm_vaddr_t) == 4) ? 1 : -1];
typedef char vm_assert_uaddr_is_4[(sizeof(vm_uaddr_t) == 4) ? 1 : -1];
typedef char vm_assert_paddr_is_4[(sizeof(vm_paddr_t) == 4) ? 1 : -1];

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
typedef char vm_assert_pointer_is_4[(sizeof(void *) == 4) ? 1 : -1];
typedef char vm_assert_size_is_4[(sizeof(size_t) == 4) ? 1 : -1];
typedef char vm_assert_uintptr_is_4[(sizeof(uintptr_t) == 4) ? 1 : -1];
#endif

#endif /* _VM_VM_TYPES_H_ */
