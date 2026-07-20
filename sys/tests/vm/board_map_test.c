/* Host tests for the board physical-ownership descriptions. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_phys.h>
#include <machine/layout.h>

struct expected_region {
    vm_paddr_t start;
    vm_paddr_t end;
    enum vm_phys_region_kind kind;
    const char *name;
};

#define AVAILABLE(start, end, name) \
    { (start), (end), VM_PHYS_AVAILABLE, (name) }
#define RESERVED(start, end, name) \
    { (start), (end), VM_PHYS_RESERVED, (name) }

int vm_phys_board_register(struct vm_phys_map *, vm_size_t);

static int
check_map(vm_size_t ram_size, const struct expected_region *expected,
    unsigned expected_count)
{
    const struct vm_phys_region *actual;
    struct vm_phys_map map;
    vm_size_t available;
    vm_size_t reserved;
    unsigned i;
    int error;

    vm_phys_map_init(&map);
    error = vm_phys_board_register(&map, ram_size);
    if (error != 0) {
        fprintf(stderr, "board registration failed: %d\n", error);
        return 1;
    }
    error = vm_phys_map_finalize(&map);
    if (error != 0) {
        fprintf(stderr, "board map finalization failed: %d\n", error);
        return 1;
    }
    if (vm_phys_map_count(&map) != expected_count) {
        fprintf(stderr, "region count: got %u, expected %u\n",
            vm_phys_map_count(&map), expected_count);
        return 1;
    }
    for (i = 0; i < expected_count; ++i) {
        actual = vm_phys_map_region(&map, i);
        if (actual == NULL || actual->vpr_start != expected[i].start ||
            actual->vpr_end != expected[i].end ||
            actual->vpr_kind != expected[i].kind ||
            strcmp(actual->vpr_name, expected[i].name) != 0) {
            fprintf(stderr, "region %u does not match baseline\n", i);
            return 1;
        }
    }
    if (vm_phys_map_total(&map, VM_PHYS_AVAILABLE, &available) != 0 ||
        vm_phys_map_total(&map, VM_PHYS_RESERVED, &reserved) != 0 ||
        available > ram_size || reserved != ram_size - available) {
        fprintf(stderr, "board map does not account for all RAM\n");
        return 1;
    }
    return 0;
}

#if defined(TEST_MALTA)
static const struct expected_region malta_map[] = {
    RESERVED(0x00000000u, 0x00100000u, "firmware/vectors"),
    RESERVED(0x00100000u, 0x00180000u, "kernel"),
    AVAILABLE(0x00180000u, 0x002fe000u, "ram"),
    RESERVED(0x002fe000u, 0x00300000u, "bootstrap u area"),
    RESERVED(0x00300000u, 0x00700000u, "legacy user window"),
    RESERVED(0x00700000u, 0x00800000u, "/var ramdisk"),
    RESERVED(0x00800000u, 0x02800000u, "rootfs"),
    RESERVED(0x02800000u, 0x02a00000u, "cartflash"),
    RESERVED(0x02a00000u, 0x04000000u, "swap"),
};
#elif defined(TEST_MALTA_N64_8M)
static const struct expected_region malta_n64_8m_map[] = {
    RESERVED(0x00000000u, 0x00001000u, "vectors"),
    RESERVED(0x00001000u, 0x00080000u, "kernel"),
    AVAILABLE(0x00080000u, 0x000f2000u, "ram"),
    RESERVED(0x000f2000u, 0x000f4000u, "bootstrap u area"),
    AVAILABLE(0x000f4000u, 0x00300000u, "ram"),
    RESERVED(0x00300000u, 0x00380000u, "stage0/restart"),
    AVAILABLE(0x00380000u, 0x00500000u, "ram"),
    RESERVED(0x00500000u, 0x00540000u, "framebuffer"),
    RESERVED(0x00540000u, 0x00640000u, "/var ramdisk"),
    RESERVED(0x00640000u, 0x00800000u, "swap"),
};
#elif defined(TEST_CI20)
static const struct expected_region ci20_map[] = {
    RESERVED(0x00000000u, 0x00100000u, "vectors/low RAM"),
    RESERVED(0x00100000u, 0x00180000u, "kernel"),
    AVAILABLE(0x00180000u, 0x002fe000u, "ram"),
    RESERVED(0x002fe000u, 0x00300000u, "bootstrap u area"),
    RESERVED(0x00300000u, 0x00700000u, "legacy user window"),
    RESERVED(0x00700000u, 0x00800000u, "/var ramdisk"),
    RESERVED(0x00800000u, 0x02800000u, "rootfs"),
    RESERVED(0x02800000u, 0x04800000u, "swap"),
    AVAILABLE(0x04800000u, 0x10000000u, "ram"),
};
#elif defined(TEST_N64)
static const struct expected_region n64_4m_map[] = {
    RESERVED(0x00000000u, 0x00001000u, "vectors"),
    RESERVED(0x00001000u, 0x00080000u, "kernel"),
    AVAILABLE(0x00080000u, 0x000fe000u, "rdram"),
    RESERVED(0x000fe000u, 0x00100000u, "bootstrap u area"),
    AVAILABLE(0x00100000u, 0x00300000u, "rdram"),
    RESERVED(0x00300000u, 0x00340000u, "stage0/restart"),
    RESERVED(0x00340000u, 0x00380000u, "stage0/framebuffer alias"),
    RESERVED(0x00380000u, 0x003a0000u, "/var ramdisk"),
    RESERVED(0x003a0000u, 0x00400000u, "swap"),
};

#ifdef N64_DEBUG_USERMEM_4M
static const struct expected_region n64_8m_map[] = {
    RESERVED(0x00000000u, 0x00001000u, "vectors"),
    RESERVED(0x00001000u, 0x00080000u, "kernel"),
    AVAILABLE(0x00080000u, 0x000fe000u, "rdram"),
    RESERVED(0x000fe000u, 0x00100000u, "bootstrap u area"),
    AVAILABLE(0x00100000u, 0x00300000u, "rdram"),
    RESERVED(0x00300000u, 0x00380000u, "stage0/restart"),
    AVAILABLE(0x00380000u, 0x00500000u, "rdram"),
    RESERVED(0x00500000u, 0x00600000u, "/var ramdisk"),
    RESERVED(0x00600000u, 0x00800000u, "swap"),
};
#elif defined(N64_HIGHRES_FB)
static const struct expected_region n64_8m_map[] = {
    RESERVED(0x00000000u, 0x00001000u, "vectors"),
    RESERVED(0x00001000u, 0x00080000u, "kernel"),
    AVAILABLE(0x00080000u, 0x000fe000u, "rdram"),
    RESERVED(0x000fe000u, 0x00100000u, "bootstrap u area"),
    AVAILABLE(0x00100000u, 0x00300000u, "rdram"),
    RESERVED(0x00300000u, 0x00380000u, "stage0/restart"),
    AVAILABLE(0x00380000u, 0x00500000u, "rdram"),
    RESERVED(0x00500000u, 0x005a0000u, "framebuffer"),
    RESERVED(0x005a0000u, 0x006a0000u, "/var ramdisk"),
    RESERVED(0x006a0000u, 0x00800000u, "swap"),
};
#else
static const struct expected_region n64_8m_map[] = {
    RESERVED(0x00000000u, 0x00001000u, "vectors"),
    RESERVED(0x00001000u, 0x00080000u, "kernel"),
    AVAILABLE(0x00080000u, 0x000fe000u, "rdram"),
    RESERVED(0x000fe000u, 0x00100000u, "bootstrap u area"),
    AVAILABLE(0x00100000u, 0x00300000u, "rdram"),
    RESERVED(0x00300000u, 0x00380000u, "stage0/restart"),
    AVAILABLE(0x00380000u, 0x00500000u, "rdram"),
    RESERVED(0x00500000u, 0x00540000u, "framebuffer"),
    RESERVED(0x00540000u, 0x00640000u, "/var ramdisk"),
    RESERVED(0x00640000u, 0x00800000u, "swap"),
};
#endif
#endif

int
main(void)
{
#if defined(TEST_MALTA)
    if (MIPS_LEGACY_USER_BYTES != 0x00400000u ||
        MIPS_USER_MAXMEM <= MIPS_LEGACY_USER_BYTES ||
        MIPS_USER_VADDR_END != MIPS_USER_VADDR_START +
        MIPS_USER_MAXMEM) {
        fprintf(stderr, "Malta user geometry is not decoupled\n");
        return 1;
    }
    if (check_map(0x04000000u, malta_map,
        sizeof(malta_map) / sizeof(malta_map[0])) != 0)
        return 1;
#elif defined(TEST_MALTA_N64_8M)
    if (MIPS_LEGACY_USER_BYTES != 0x00400000u ||
        MIPS_USER_MAXMEM != 0x00400000u ||
        MIPS_USER_VADDR_END != 0x00800000u) {
        fprintf(stderr, "Malta N64 user geometry does not match\n");
        return 1;
    }
    if (check_map(0x00800000u, malta_n64_8m_map,
        sizeof(malta_n64_8m_map) / sizeof(malta_n64_8m_map[0])) != 0)
        return 1;
#elif defined(TEST_CI20)
    if (MIPS_LEGACY_USER_BYTES != 0x00400000u ||
        MIPS_USER_MAXMEM <= MIPS_LEGACY_USER_BYTES ||
        MIPS_USER_VADDR_END != MIPS_USER_VADDR_START +
        MIPS_USER_MAXMEM) {
        fprintf(stderr, "Ci20 user geometry is not decoupled\n");
        return 1;
    }
    if (check_map(0x10000000u, ci20_map,
        sizeof(ci20_map) / sizeof(ci20_map[0])) != 0)
        return 1;
#elif defined(TEST_N64)
    if (check_map(0x00400000u, n64_4m_map,
        sizeof(n64_4m_map) / sizeof(n64_4m_map[0])) != 0 ||
        check_map(0x00800000u, n64_8m_map,
        sizeof(n64_8m_map) / sizeof(n64_8m_map[0])) != 0)
        return 1;
#else
#error A board test selection is required
#endif
    puts("vm board map tests: ok");
    return 0;
}
