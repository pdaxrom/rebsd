/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * JZ4780 clock-generation helpers shared by Ci20 device attachments.
 */

#include <sys/param.h>

#include "cgu.h"

#define CI20_CGU_BASE           0xb0000000u
#define CI20_CGU_CPCCR          0x00u
#define CI20_CGU_APLL           0x10u
#define CI20_CGU_MPLL           0x14u
#define CI20_CGU_EXCLK_KHZ      48000u
#define CI20_CGU_RTCLK_KHZ      32u

#define CI20_PLL_M_SHIFT        19u
#define CI20_PLL_M_MASK         0x1fffu
#define CI20_PLL_N_SHIFT        13u
#define CI20_PLL_N_MASK         0x3fu
#define CI20_PLL_OD_SHIFT       9u
#define CI20_PLL_OD_MASK        0x0fu
#define CI20_PLL_ENABLE         0x01u

static volatile unsigned *
ci20_cgu_reg(unsigned reg)
{
    return (volatile unsigned *)(CI20_CGU_BASE + reg);
}

static unsigned
ci20_cgu_read(unsigned reg)
{
    return *ci20_cgu_reg(reg);
}

static void
ci20_cgu_write(unsigned reg, unsigned value)
{
    *ci20_cgu_reg(reg) = value;
}

void
ci20_cgu_gate_enable(unsigned reg, unsigned mask)
{
    ci20_cgu_write(reg, ci20_cgu_read(reg) & ~mask);
}

unsigned
ci20_cgu_pll_rate_khz(unsigned reg)
{
    unsigned divider;
    unsigned multiplier;
    unsigned value;

    value = ci20_cgu_read(reg);
    if ((value & CI20_PLL_ENABLE) == 0)
        return 0;
    multiplier = ((value >> CI20_PLL_M_SHIFT) & CI20_PLL_M_MASK) + 1u;
    divider = (((value >> CI20_PLL_N_SHIFT) & CI20_PLL_N_MASK) + 1u) *
        (((value >> CI20_PLL_OD_SHIFT) & CI20_PLL_OD_MASK) + 1u);
    if (divider == 0 ||
        multiplier > (unsigned)-1 / CI20_CGU_EXCLK_KHZ)
        return 0;
    return CI20_CGU_EXCLK_KHZ * multiplier / divider;
}

static unsigned
ci20_cgu_sclka_rate_khz(unsigned cpccr)
{
    switch ((cpccr >> 30) & 3u) {
    case 1:
        return ci20_cgu_pll_rate_khz(CI20_CGU_APLL);
    case 2:
        return CI20_CGU_EXCLK_KHZ;
    case 3:
        return CI20_CGU_RTCLK_KHZ;
    default:
        return 0;
    }
}

unsigned
ci20_cgu_pclk_rate_khz(void)
{
    unsigned cpccr;
    unsigned divider;
    unsigned parent;

    cpccr = ci20_cgu_read(CI20_CGU_CPCCR);
    switch ((cpccr >> 24) & 3u) {
    case 1:
        parent = ci20_cgu_sclka_rate_khz(cpccr);
        break;
    case 2:
        parent = ci20_cgu_pll_rate_khz(CI20_CGU_MPLL);
        break;
    case 3:
        parent = CI20_CGU_RTCLK_KHZ;
        break;
    default:
        parent = 0;
        break;
    }
    divider = ((cpccr >> 16) & 0x0fu) + 1u;
    return parent / divider;
}
