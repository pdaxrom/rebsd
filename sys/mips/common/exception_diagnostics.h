/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * Byte offsets shared by the MIPS exception assembly and C diagnostics.
 * Sequence is published last, after the rest of each snapshot is complete.
 */
#ifndef _MIPS_COMMON_EXCEPTION_DIAGNOSTICS_H_
#define _MIPS_COMMON_EXCEPTION_DIAGNOSTICS_H_

#define MIPS_REFILL_DIAG_SEQUENCE       0
#define MIPS_REFILL_DIAG_EPC            4
#define MIPS_REFILL_DIAG_BADVADDR       8
#define MIPS_REFILL_DIAG_AT             12
#define MIPS_REFILL_DIAG_V0             16
#define MIPS_REFILL_DIAG_A0             20
#define MIPS_REFILL_DIAG_CAUSE          24
#define MIPS_REFILL_DIAG_STATUS         28
#define MIPS_REFILL_DIAG_ENTRYHI        32
#define MIPS_REFILL_DIAG_ENTRYLO0       36
#define MIPS_REFILL_DIAG_ENTRYLO1       40
#define MIPS_REFILL_DIAG_INDEX          44
#define MIPS_REFILL_DIAG_RANDOM         48
#define MIPS_REFILL_DIAG_COUNT          52
#define MIPS_REFILL_DIAG_PTE_PAIR       56
#define MIPS_REFILL_DIAG_DIRECTORY      60
#define MIPS_REFILL_DIAG_PTE0           64
#define MIPS_REFILL_DIAG_PTE1           68
#define MIPS_REFILL_DIAG_AT_HIGH        72
#define MIPS_REFILL_DIAG_AT_LOW_COPY    76
#define MIPS_REFILL_DIAG_V0_HIGH        80
#define MIPS_REFILL_DIAG_V0_LOW_COPY    84
#define MIPS_REFILL_DIAG_A0_HIGH        88
#define MIPS_REFILL_DIAG_A0_LOW_COPY    92
#define MIPS_REFILL_DIAG_BYTES          96
#define MIPS_REFILL_DIAG_WORDS          (MIPS_REFILL_DIAG_BYTES / 4)

#define MIPS_RESTORE_DIAG_SEQUENCE      0
#define MIPS_RESTORE_DIAG_FRAME         4
#define MIPS_RESTORE_DIAG_EPC           8
#define MIPS_RESTORE_DIAG_STATUS        12
#define MIPS_RESTORE_DIAG_AT            16
#define MIPS_RESTORE_DIAG_V0            20
#define MIPS_RESTORE_DIAG_A0            24
#define MIPS_RESTORE_DIAG_SP            28
#define MIPS_RESTORE_DIAG_RA            32
#define MIPS_RESTORE_DIAG_COUNT         36
#define MIPS_RESTORE_DIAG_AT_HIGH       40
#define MIPS_RESTORE_DIAG_AT_LOW_COPY   44
#define MIPS_RESTORE_DIAG_V0_HIGH       48
#define MIPS_RESTORE_DIAG_V0_LOW_COPY   52
#define MIPS_RESTORE_DIAG_A0_HIGH       56
#define MIPS_RESTORE_DIAG_A0_LOW_COPY   60
#define MIPS_RESTORE_DIAG_SP_HIGH       64
#define MIPS_RESTORE_DIAG_SP_LOW_COPY   68
#define MIPS_RESTORE_DIAG_RA_HIGH       72
#define MIPS_RESTORE_DIAG_RA_LOW_COPY   76
#define MIPS_RESTORE_DIAG_BYTES         80
#define MIPS_RESTORE_DIAG_WORDS         (MIPS_RESTORE_DIAG_BYTES / 4)

#define MIPS_CP0_DIAG_INDEX             0
#define MIPS_CP0_DIAG_RANDOM            4
#define MIPS_CP0_DIAG_ENTRYLO0          8
#define MIPS_CP0_DIAG_ENTRYLO1          12
#define MIPS_CP0_DIAG_PAGEMASK          16
#define MIPS_CP0_DIAG_WIRED             20
#define MIPS_CP0_DIAG_COUNT             24
#define MIPS_CP0_DIAG_ENTRYHI           28
#define MIPS_CP0_DIAG_COMPARE           32
#define MIPS_CP0_DIAG_STATUS            36
#define MIPS_CP0_DIAG_CAUSE             40
#define MIPS_CP0_DIAG_EPC               44
#define MIPS_CP0_DIAG_CONFIG            48
#define MIPS_CP0_DIAG_WATCHLO           52
#define MIPS_CP0_DIAG_WATCHHI           56
#define MIPS_CP0_DIAG_ERROREPC          60
#define MIPS_CP0_DIAG_CONTEXT           64
#define MIPS_CP0_DIAG_BADVADDR          68
#define MIPS_CP0_DIAG_PRID              72
#define MIPS_CP0_DIAG_LLADDR            76
#define MIPS_CP0_DIAG_XCONTEXT          80
#define MIPS_CP0_DIAG_ECC               84
#define MIPS_CP0_DIAG_CACHEERR          88
#define MIPS_CP0_DIAG_TAGLO             92
#define MIPS_CP0_DIAG_TAGHI             96
#define MIPS_CP0_DIAG_BYTES             100
#define MIPS_CP0_DIAG_WORDS             (MIPS_CP0_DIAG_BYTES / 4)

#endif
