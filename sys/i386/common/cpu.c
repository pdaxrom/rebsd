#include <sys/errno.h>

#include "cpu.h"

static struct i386_cpu_info cpu_info;

static void
i386_cpuid(i386_u32 leaf, i386_u32 *eax, i386_u32 *ebx,
    i386_u32 *ecx, i386_u32 *edx)
{
    __asm__ volatile ("cpuid"
        : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
        : "a" (leaf));
}

static void
i386_cpu_vendor_word(unsigned offset, i386_u32 word)
{
    cpu_info.ci_vendor[offset] = (char)word;
    cpu_info.ci_vendor[offset + 1u] = (char)(word >> 8);
    cpu_info.ci_vendor[offset + 2u] = (char)(word >> 16);
    cpu_info.ci_vendor[offset + 3u] = (char)(word >> 24);
}

int
i386_cpu_init(void)
{
    i386_u32 eax;
    i386_u32 ebx;
    i386_u32 ecx;
    i386_u32 edx;
    unsigned base_family;
    unsigned base_model;

    i386_cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax < 1u)
        return ENODEV;
    i386_cpu_vendor_word(0, ebx);
    i386_cpu_vendor_word(4, edx);
    i386_cpu_vendor_word(8, ecx);
    cpu_info.ci_vendor[12] = '\0';

    i386_cpuid(1, &eax, &ebx, &ecx, &edx);
    cpu_info.ci_features = edx;
    cpu_info.ci_stepping = eax & 0x0fu;
    base_model = (eax >> 4) & 0x0fu;
    base_family = (eax >> 8) & 0x0fu;
    cpu_info.ci_family = base_family;
    if (base_family == 0x0fu)
        cpu_info.ci_family += (eax >> 20) & 0xffu;
    cpu_info.ci_model = base_model;
    if (base_family == 0x06u || base_family == 0x0fu)
        cpu_info.ci_model |= ((eax >> 16) & 0x0fu) << 4;

    if (cpu_info.ci_family < 6u ||
        (edx & (I386_CPUID_FEATURE_CX8 | I386_CPUID_FEATURE_CMOV)) !=
        (I386_CPUID_FEATURE_CX8 | I386_CPUID_FEATURE_CMOV))
        return ENODEV;
    return 0;
}

const struct i386_cpu_info *
i386_cpu_info(void)
{
    return &cpu_info;
}
