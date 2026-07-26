#include "boot.h"
#include "memory.h"

#include <sys/param.h>
#include <sys/systm.h>

#define I386_E820_USABLE 1u

extern char __kernel_end[];

static struct i386_phys_range i386_ranges[I386_PHYS_MAX_RANGES];
static struct i386_phys_range i386_blocked[I386_E820_MAX_ENTRIES];
static unsigned i386_range_count;
static unsigned i386_alloc_range;
static i386_u32 i386_alloc_next;
static i386_u32 i386_total_pages;
static i386_u32 i386_free_pages;
static unsigned i386_handed_off;

static i386_u32
i386_align_down(i386_u32 value)
{
    return value & ~I386_PAGE_MASK;
}

static i386_u32
i386_align_up(i386_u32 value)
{
    if (value > I386_PHYS_LIMIT - I386_PAGE_MASK)
        return I386_PHYS_LIMIT;
    return (value + I386_PAGE_MASK) & ~I386_PAGE_MASK;
}

static int
i386_e820_bounds(const struct i386_e820_entry *entry, int usable,
    struct i386_phys_range *range)
{
    i386_u32 end;

    if (entry->addr_hi != 0 || entry->addr_lo >= I386_PHYS_LIMIT)
        return 0;

    if (entry->size_hi != 0 ||
        entry->size_lo > I386_PHYS_LIMIT - entry->addr_lo)
        end = I386_PHYS_LIMIT;
    else
        end = entry->addr_lo + entry->size_lo;

    if (end <= entry->addr_lo)
        return 0;

    if (usable) {
        range->start = i386_align_up(entry->addr_lo);
        range->end = i386_align_down(end);
    } else {
        range->start = i386_align_down(entry->addr_lo);
        range->end = i386_align_up(end);
    }
    return range->start < range->end;
}

static void
i386_sort_ranges(struct i386_phys_range *ranges, unsigned count)
{
    struct i386_phys_range value;
    unsigned index;
    unsigned position;

    for (index = 1; index < count; ++index) {
        value = ranges[index];
        position = index;
        while (position != 0 &&
            ranges[position - 1].start > value.start) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
}

static void
i386_merge_ranges(void)
{
    unsigned input;
    unsigned output;

    if (i386_range_count == 0)
        return;

    i386_sort_ranges(i386_ranges, i386_range_count);
    output = 0;
    for (input = 1; input < i386_range_count; ++input) {
        if (i386_ranges[input].start <= i386_ranges[output].end) {
            if (i386_ranges[input].end > i386_ranges[output].end)
                i386_ranges[output].end = i386_ranges[input].end;
        } else {
            ++output;
            i386_ranges[output] = i386_ranges[input];
        }
    }
    i386_range_count = output + 1;
}

static void
i386_remove_range(unsigned index)
{
    unsigned move;

    for (move = index + 1; move < i386_range_count; ++move)
        i386_ranges[move - 1] = i386_ranges[move];
    --i386_range_count;
}

static int
i386_subtract_blocked(const struct i386_phys_range *blocked)
{
    struct i386_phys_range right;
    unsigned index;
    unsigned move;

    index = 0;
    while (index < i386_range_count) {
        if (blocked->end <= i386_ranges[index].start ||
            blocked->start >= i386_ranges[index].end) {
            ++index;
            continue;
        }

        if (blocked->start <= i386_ranges[index].start) {
            if (blocked->end >= i386_ranges[index].end) {
                i386_remove_range(index);
                continue;
            }
            i386_ranges[index].start = blocked->end;
            ++index;
            continue;
        }

        if (blocked->end >= i386_ranges[index].end) {
            i386_ranges[index].end = blocked->start;
            ++index;
            continue;
        }

        if (i386_range_count == I386_PHYS_MAX_RANGES)
            return 0;
        right.start = blocked->end;
        right.end = i386_ranges[index].end;
        i386_ranges[index].end = blocked->start;
        for (move = i386_range_count; move > index + 1; --move)
            i386_ranges[move] = i386_ranges[move - 1];
        i386_ranges[index + 1] = right;
        ++i386_range_count;
        index += 2;
    }
    return 1;
}

int
i386_memory_init(i386_u32 boot_params_phys)
{
    const struct i386_e820_entry *entries;
    struct i386_phys_range range;
    i386_u32 first_free;
    unsigned blocked_count;
    unsigned count;
    unsigned index;

    i386_range_count = 0;
    i386_total_pages = 0;
    i386_free_pages = 0;
    i386_alloc_range = 0;
    i386_alloc_next = 0;
    i386_handed_off = 0;
    blocked_count = 0;

    count = *(const volatile i386_u8 *)(boot_params_phys +
        I386_BOOT_PARAMS_E820_COUNT);
    if (count > I386_E820_MAX_ENTRIES)
        count = I386_E820_MAX_ENTRIES;
    entries = (const struct i386_e820_entry *)
        (boot_params_phys + I386_BOOT_PARAMS_E820_TABLE);
    first_free = i386_align_up((i386_u32)(unsigned long)__kernel_end);

    for (index = 0; index < count; ++index) {
        if (!i386_e820_bounds(&entries[index],
            entries[index].type == I386_E820_USABLE, &range))
            continue;

        if (entries[index].type == I386_E820_USABLE) {
            if (range.start < first_free)
                range.start = first_free;
            if (range.start >= range.end)
                continue;
            if (i386_range_count == I386_PHYS_MAX_RANGES)
                return 0;
            i386_ranges[i386_range_count++] = range;
        } else {
            if (blocked_count == I386_E820_MAX_ENTRIES)
                return 0;
            i386_blocked[blocked_count++] = range;
        }
    }

    i386_merge_ranges();
    for (index = 0; index < blocked_count; ++index) {
        if (!i386_subtract_blocked(&i386_blocked[index]))
            return 0;
    }
    i386_merge_ranges();
    if (i386_range_count == 0)
        return 0;

    for (index = 0; index < i386_range_count; ++index)
        i386_total_pages +=
            (i386_ranges[index].end - i386_ranges[index].start) /
            I386_PAGE_SIZE;
    physmem = (size_t)i386_total_pages * I386_PAGE_SIZE;
    i386_free_pages = i386_total_pages;
    i386_alloc_next = i386_ranges[0].start;
    return i386_total_pages != 0;
}

unsigned
i386_memory_range_count(void)
{
    return i386_range_count;
}

const struct i386_phys_range *
i386_memory_range(unsigned index)
{
    if (index >= i386_range_count)
        return (const struct i386_phys_range *)0;
    return &i386_ranges[index];
}

i386_u32
i386_memory_total_pages(void)
{
    return i386_total_pages;
}

i386_u32
i386_memory_free_pages(void)
{
    return i386_free_pages;
}

i386_u32
i386_memory_allocated_end(unsigned index)
{
    if (index >= i386_range_count)
        return 0;
    if (index < i386_alloc_range)
        return i386_ranges[index].end;
    if (index == i386_alloc_range)
        return i386_alloc_next;
    return i386_ranges[index].start;
}

i386_u32
i386_phys_alloc_page(void)
{
    volatile i386_u32 *words;
    i386_u32 address;
    unsigned index;

    if (i386_handed_off)
        return 0;
    while (i386_alloc_range < i386_range_count) {
        if (i386_alloc_next < i386_ranges[i386_alloc_range].end) {
            address = i386_alloc_next;
            i386_alloc_next += I386_PAGE_SIZE;
            --i386_free_pages;

            words = (volatile i386_u32 *)address;
            for (index = 0; index < I386_PAGE_SIZE / sizeof(*words);
                ++index)
                words[index] = 0;
            return address;
        }
        ++i386_alloc_range;
        if (i386_alloc_range < i386_range_count)
            i386_alloc_next = i386_ranges[i386_alloc_range].start;
    }
    return 0;
}

void
i386_memory_handoff(void)
{
    i386_handed_off = 1;
}
