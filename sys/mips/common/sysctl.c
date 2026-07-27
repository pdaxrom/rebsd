#include <sys/param.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/vm.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/layout.h>
#ifdef N64
#include <machine/n64.h>
#endif

#ifdef CI20_DM9000_ENABLED
extern int ci20_dm9000_stats(char *buf, int len);
#endif
extern int mips_timer_stats(char *buf, int len);

#ifdef N64
#define MIPS_SYSCTL_CPU_KHZ     N64_CPU_KHZ
#define MIPS_SYSCTL_COUNT_KHZ   N64_COUNT_KHZ
static int
mips_sysctl_ram_bytes(void)
{
    return n64_rdram_size();
}
#elif defined(CI20)
#define MIPS_SYSCTL_CPU_KHZ     CI20_CPU_KHZ
#define MIPS_SYSCTL_COUNT_KHZ   MIPS_COUNT_KHZ
static int
mips_sysctl_ram_bytes(void)
{
    return CI20_RAM_SIZE;
}
#else
#define MIPS_SYSCTL_CPU_KHZ     MALTA_CPU_KHZ
#define MIPS_SYSCTL_COUNT_KHZ   MIPS_COUNT_KHZ
static int
mips_sysctl_ram_bytes(void)
{
    return MALTA_RAM_SIZE;
}
#endif

int
md_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
    int value;
#ifdef CI20_DM9000_ENABLED
    char dmstats[1024];
#endif
    char timerstats[256];

    (void)newlen;
    switch (name[0]) {
    case CPU_FREQ_KHZ:
        if (namelen != 1)
            return ENOTDIR;
        value = MIPS_SYSCTL_CPU_KHZ;
        return sysctl_rdstruct(oldp, oldlenp, newp, &value, sizeof(value));
    case CPU_COUNT_KHZ:
        if (namelen != 1)
            return ENOTDIR;
        value = MIPS_SYSCTL_COUNT_KHZ;
        return sysctl_rdstruct(oldp, oldlenp, newp, &value, sizeof(value));
    case CPU_RAM_BYTES:
        if (namelen != 1)
            return ENOTDIR;
        value = mips_sysctl_ram_bytes();
        return sysctl_rdstruct(oldp, oldlenp, newp, &value, sizeof(value));
    case CPU_DM9000_STATS:
        if (namelen != 1)
            return ENOTDIR;
#ifdef CI20_DM9000_ENABLED
        ci20_dm9000_stats(dmstats, sizeof(dmstats));
        return sysctl_rdstring(oldp, oldlenp, newp, dmstats);
#else
        return EOPNOTSUPP;
#endif
    case CPU_TIMER_STATS:
        if (namelen != 1)
            return ENOTDIR;
        mips_timer_stats(timerstats, sizeof(timerstats));
        return sysctl_rdstring(oldp, oldlenp, newp, timerstats);
    default:
        return EOPNOTSUPP;
    }
}
