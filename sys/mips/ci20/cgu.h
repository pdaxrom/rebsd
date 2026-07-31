/*
 * Copyright (c) 2026 ReBSD contributors
 */

#ifndef _CI20_CGU_H_
#define _CI20_CGU_H_

#define CI20_CGU_CLKGR0     0x20u
#define CI20_CGU_CLKGR1     0x28u

void ci20_cgu_gate_enable(unsigned, unsigned);
unsigned ci20_cgu_pll_rate_khz(unsigned);
unsigned ci20_cgu_pclk_rate_khz(void);

#endif
