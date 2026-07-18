/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#define vm_sysv_zero(p, n) bzero((caddr_t)(p), (unsigned)(n))
#else
#include <errno.h>
#include <string.h>
#define vm_sysv_zero(p, n) memset((p), 0, (n))
#endif

#include <vm/vm_object.h>
#include <vm/vm_sysv_shm.h>

#define VM_SYSV_SHM_INDEX_BITS 5u
#define VM_SYSV_SHM_INDEX_MASK ((1u << VM_SYSV_SHM_INDEX_BITS) - 1u)
#define VM_SYSV_SHM_SEQUENCE_MAX 0x03ffffffu

struct vm_sysv_shm {
    struct vm_object *vss_object;
    vm_size_t         vss_size;
    int               vss_key;
    unsigned          vss_owner;
    unsigned          vss_group;
    unsigned          vss_creator;
    unsigned          vss_creator_group;
    unsigned          vss_mode;
    unsigned          vss_sequence;
    unsigned          vss_attach_count;
    int               vss_creator_pid;
    int               vss_last_pid;
    long              vss_attach_time;
    long              vss_detach_time;
    long              vss_change_time;
    unsigned          vss_in_use;
    unsigned          vss_removed;
};

static struct vm_sysv_shm vm_sysv_pool[VM_SYSV_SHM_MAX_SEGMENTS];
static unsigned vm_sysv_generation[VM_SYSV_SHM_MAX_SEGMENTS];
static unsigned vm_sysv_initialized;

static int
vm_sysv_valid(const struct vm_sysv_shm *segment)
{
    return vm_sysv_initialized && segment != 0 &&
        segment >= &vm_sysv_pool[0] &&
        segment < &vm_sysv_pool[VM_SYSV_SHM_MAX_SEGMENTS] &&
        segment->vss_in_use != 0 && segment->vss_object != 0;
}

static unsigned
vm_sysv_index(const struct vm_sysv_shm *segment)
{
    return (unsigned)(segment - &vm_sysv_pool[0]);
}

static int
vm_sysv_id(const struct vm_sysv_shm *segment)
{
    return (int)((segment->vss_sequence << VM_SYSV_SHM_INDEX_BITS) |
        vm_sysv_index(segment));
}

static int
vm_sysv_destroy(struct vm_sysv_shm *segment)
{
    unsigned index;
    int error;

    if (!vm_sysv_valid(segment) || segment->vss_attach_count != 0)
        return EINVAL;
    index = vm_sysv_index(segment);
    error = vm_object_release(segment->vss_object);
    if (error != 0)
        return error;
    vm_sysv_zero(segment, sizeof(*segment));
    if (++vm_sysv_generation[index] == 0 ||
        vm_sysv_generation[index] > VM_SYSV_SHM_SEQUENCE_MAX)
        vm_sysv_generation[index] = 1;
    return 0;
}

int
vm_sysv_shm_system_init(void)
{
    unsigned index;

    vm_sysv_zero(vm_sysv_pool, sizeof(vm_sysv_pool));
    for (index = 0; index < VM_SYSV_SHM_MAX_SEGMENTS; ++index)
        vm_sysv_generation[index] = 1;
    vm_sysv_initialized = 1;
    return 0;
}

int
vm_sysv_shm_lookup_key(int key, struct vm_sysv_shm **result)
{
    unsigned index;

    if (!vm_sysv_initialized || result == 0 || key == 0)
        return EINVAL;
    *result = 0;
    for (index = 0; index < VM_SYSV_SHM_MAX_SEGMENTS; ++index) {
        if (vm_sysv_pool[index].vss_in_use != 0 &&
            vm_sysv_pool[index].vss_removed == 0 &&
            vm_sysv_pool[index].vss_key == key) {
            *result = &vm_sysv_pool[index];
            return 0;
        }
    }
    return ENOENT;
}

int
vm_sysv_shm_lookup_id(int id, struct vm_sysv_shm **result)
{
    struct vm_sysv_shm *segment;
    unsigned index;
    unsigned sequence;

    if (!vm_sysv_initialized || result == 0 || id < 0)
        return EINVAL;
    *result = 0;
    index = (unsigned)id & VM_SYSV_SHM_INDEX_MASK;
    sequence = (unsigned)id >> VM_SYSV_SHM_INDEX_BITS;
    if (index >= VM_SYSV_SHM_MAX_SEGMENTS || sequence == 0)
        return EINVAL;
    segment = &vm_sysv_pool[index];
    if (!vm_sysv_valid(segment) || segment->vss_removed != 0 ||
        segment->vss_sequence != sequence)
        return EINVAL;
    *result = segment;
    return 0;
}

int
vm_sysv_shm_create(int key, vm_size_t size, unsigned owner,
    unsigned group, unsigned mode, int creator_pid, long now,
    struct vm_sysv_shm **result)
{
    struct vm_sysv_shm *existing;
    struct vm_sysv_shm *segment;
    struct vm_object *object;
    vm_size_t rounded;
    unsigned index;
    int error;

    if (!vm_sysv_initialized || result == 0 || size == 0 ||
        size > VM_SYSV_SHM_MAX_SEGMENT_BYTES)
        return EINVAL;
    if (key != 0) {
        error = vm_sysv_shm_lookup_key(key, &existing);
        if (error == 0)
            return EEXIST;
        if (error != ENOENT)
            return error;
    }
    error = vm_size_round_page(size, &rounded);
    if (error != 0 || rounded == 0)
        return EINVAL;
    segment = 0;
    for (index = 0; index < VM_SYSV_SHM_MAX_SEGMENTS; ++index) {
        if (vm_sysv_pool[index].vss_in_use == 0) {
            segment = &vm_sysv_pool[index];
            break;
        }
    }
    if (segment == 0)
        return ENOSPC;
    error = vm_object_create(rounded, &object);
    if (error != 0)
        return error;
    vm_sysv_zero(segment, sizeof(*segment));
    segment->vss_object = object;
    segment->vss_size = size;
    segment->vss_key = key;
    segment->vss_owner = owner;
    segment->vss_group = group;
    segment->vss_creator = owner;
    segment->vss_creator_group = group;
    segment->vss_mode = mode & 0777u;
    segment->vss_sequence = vm_sysv_generation[index];
    segment->vss_creator_pid = creator_pid;
    segment->vss_change_time = now;
    segment->vss_in_use = 1;
    *result = segment;
    return 0;
}

int
vm_sysv_shm_get_info(const struct vm_sysv_shm *segment,
    struct vm_sysv_shm_info *info)
{
    if (!vm_sysv_valid(segment) || info == 0)
        return EINVAL;
    info->vssi_id = vm_sysv_id(segment);
    info->vssi_key = segment->vss_key;
    info->vssi_size = segment->vss_size;
    info->vssi_owner = segment->vss_owner;
    info->vssi_group = segment->vss_group;
    info->vssi_creator = segment->vss_creator;
    info->vssi_creator_group = segment->vss_creator_group;
    info->vssi_mode = segment->vss_mode;
    info->vssi_sequence = segment->vss_sequence;
    info->vssi_attach_count = segment->vss_attach_count;
    info->vssi_creator_pid = segment->vss_creator_pid;
    info->vssi_last_pid = segment->vss_last_pid;
    info->vssi_attach_time = segment->vss_attach_time;
    info->vssi_detach_time = segment->vss_detach_time;
    info->vssi_change_time = segment->vss_change_time;
    info->vssi_removed = segment->vss_removed != 0;
    return 0;
}

int
vm_sysv_shm_set_permissions(struct vm_sysv_shm *segment, unsigned owner,
    unsigned group, unsigned mode, long now)
{
    if (!vm_sysv_valid(segment) || segment->vss_removed != 0)
        return EINVAL;
    segment->vss_owner = owner;
    segment->vss_group = group;
    segment->vss_mode = mode & 0777u;
    segment->vss_change_time = now;
    return 0;
}

int
vm_sysv_shm_mark_remove(struct vm_sysv_shm *segment, int pid, long now)
{
    if (!vm_sysv_valid(segment) || segment->vss_removed != 0)
        return EINVAL;
    segment->vss_removed = 1;
    segment->vss_key = 0;
    segment->vss_last_pid = pid;
    segment->vss_change_time = now;
    if (segment->vss_attach_count == 0)
        return vm_sysv_destroy(segment);
    return 0;
}

int
vm_sysv_shm_object_reference(struct vm_sysv_shm *segment,
    struct vm_object **result)
{
    int error;

    if (!vm_sysv_valid(segment) || segment->vss_removed != 0 || result == 0)
        return EINVAL;
    error = vm_object_reference(segment->vss_object);
    if (error == 0)
        *result = segment->vss_object;
    return error;
}

int
vm_sysv_shm_attach(struct vm_sysv_shm *segment, int pid, long now)
{
    if (!vm_sysv_valid(segment) ||
        (segment->vss_removed != 0 && pid != 0))
        return EINVAL;
    if (segment->vss_attach_count == ~0u)
        return EOVERFLOW;
    ++segment->vss_attach_count;
    if (pid != 0) {
        segment->vss_last_pid = pid;
        segment->vss_attach_time = now;
    }
    return 0;
}

int
vm_sysv_shm_detach(struct vm_sysv_shm *segment, int pid, long now)
{
    if (!vm_sysv_valid(segment) || segment->vss_attach_count == 0)
        return EINVAL;
    --segment->vss_attach_count;
    if (pid != 0) {
        segment->vss_last_pid = pid;
        segment->vss_detach_time = now;
    }
    if (segment->vss_removed != 0 && segment->vss_attach_count == 0)
        return vm_sysv_destroy(segment);
    return 0;
}

int
vm_sysv_shm_get_stats(struct vm_sysv_shm_stats *stats)
{
    unsigned index;

    if (!vm_sysv_initialized || stats == 0)
        return EINVAL;
    vm_sysv_zero(stats, sizeof(*stats));
    for (index = 0; index < VM_SYSV_SHM_MAX_SEGMENTS; ++index) {
        if (vm_sysv_pool[index].vss_in_use == 0)
            continue;
        ++stats->vsss_segments;
        if (vm_sysv_pool[index].vss_removed != 0)
            ++stats->vsss_removed_segments;
        stats->vsss_attachments +=
            vm_sysv_pool[index].vss_attach_count;
        stats->vsss_pages += (vm_sysv_pool[index].vss_size +
            VM_PAGE_MASK) >> VM_PAGE_SHIFT;
        if (VM_SIZE_MAX - stats->vsss_bytes <
            vm_sysv_pool[index].vss_size)
            stats->vsss_bytes = VM_SIZE_MAX;
        else
            stats->vsss_bytes += vm_sysv_pool[index].vss_size;
    }
    return 0;
}
