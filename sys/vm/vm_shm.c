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
#define vm_shm_zero(p, n) bzero((caddr_t)(p), (unsigned)(n))
#else
#include <errno.h>
#include <string.h>
#define vm_shm_zero(p, n) memset((p), 0, (n))
#endif

#include <vm/vm_object.h>
#include <vm/vm_shm.h>

struct vm_shm {
    struct vm_object *vsh_object;
    vm_size_t         vsh_size;
    unsigned          vsh_owner;
    unsigned          vsh_group;
    unsigned          vsh_mode;
    unsigned          vsh_open_count;
    unsigned          vsh_in_use;
    unsigned          vsh_linked;
    char              vsh_name[VM_SHM_NAME_MAX + 1];
};

static struct vm_shm vm_shm_pool[VM_SHM_MAX_OBJECTS];
static unsigned vm_shm_initialized;

static int
vm_shm_valid(const struct vm_shm *shm)
{
    return vm_shm_initialized && shm != 0 &&
        shm >= &vm_shm_pool[0] && shm < &vm_shm_pool[VM_SHM_MAX_OBJECTS] &&
        shm->vsh_in_use != 0 && shm->vsh_object != 0;
}

static int
vm_shm_name_equal(const char *left, const char *right)
{
    while (*left == *right) {
        if (*left == '\0')
            return 1;
        ++left;
        ++right;
    }
    return 0;
}

static int
vm_shm_name_copy(char *target, const char *source)
{
    unsigned index;

    if (target == 0 || source == 0 || source[0] == '\0')
        return EINVAL;
    for (index = 0; index <= VM_SHM_NAME_MAX; ++index) {
        target[index] = source[index];
        if (source[index] == '\0')
            return 0;
    }
    target[0] = '\0';
    return ENAMETOOLONG;
}

static int
vm_shm_destroy(struct vm_shm *shm)
{
    int error;

    error = vm_object_release(shm->vsh_object);
    if (error != 0)
        return error;
    vm_shm_zero(shm, sizeof(*shm));
    return 0;
}

int
vm_shm_system_init(void)
{
    vm_shm_zero(vm_shm_pool, sizeof(vm_shm_pool));
    vm_shm_initialized = 1;
    return 0;
}

int
vm_shm_lookup(const char *name, struct vm_shm **result)
{
    unsigned index;

    if (!vm_shm_initialized || name == 0 || result == 0)
        return EINVAL;
    *result = 0;
    for (index = 0; index < VM_SHM_MAX_OBJECTS; ++index) {
        if (vm_shm_pool[index].vsh_in_use != 0 &&
            vm_shm_pool[index].vsh_linked != 0 &&
            vm_shm_name_equal(vm_shm_pool[index].vsh_name, name)) {
            *result = &vm_shm_pool[index];
            return 0;
        }
    }
    return ENOENT;
}

int
vm_shm_create(const char *name, unsigned owner, unsigned group,
    unsigned mode, struct vm_shm **result)
{
    struct vm_object *object;
    struct vm_shm *existing;
    struct vm_shm *shm;
    unsigned index;
    int error;

    if (!vm_shm_initialized || result == 0)
        return EINVAL;
    error = vm_shm_lookup(name, &existing);
    if (error == 0)
        return EEXIST;
    if (error != ENOENT)
        return error;
    shm = 0;
    for (index = 0; index < VM_SHM_MAX_OBJECTS; ++index) {
        if (vm_shm_pool[index].vsh_in_use == 0) {
            shm = &vm_shm_pool[index];
            break;
        }
    }
    if (shm == 0)
        return ENOSPC;
    vm_shm_zero(shm, sizeof(*shm));
    error = vm_shm_name_copy(shm->vsh_name, name);
    if (error != 0)
        return error;
    error = vm_object_create(0, &object);
    if (error != 0) {
        vm_shm_zero(shm, sizeof(*shm));
        return error;
    }
    shm->vsh_object = object;
    shm->vsh_owner = owner;
    shm->vsh_group = group;
    shm->vsh_mode = mode & 0777u;
    shm->vsh_open_count = 1;
    shm->vsh_in_use = 1;
    shm->vsh_linked = 1;
    *result = shm;
    return 0;
}

int
vm_shm_retain(struct vm_shm *shm)
{
    if (!vm_shm_valid(shm) || shm->vsh_open_count == ~0u)
        return EOVERFLOW;
    ++shm->vsh_open_count;
    return 0;
}

int
vm_shm_close(struct vm_shm *shm)
{
    int error;

    if (!vm_shm_valid(shm) || shm->vsh_open_count == 0)
        return EINVAL;
    if (shm->vsh_open_count != 1 || shm->vsh_linked != 0) {
        --shm->vsh_open_count;
        return 0;
    }
    error = vm_shm_destroy(shm);
    return error;
}

int
vm_shm_unlink(struct vm_shm *shm)
{
    if (!vm_shm_valid(shm) || shm->vsh_linked == 0)
        return EINVAL;
    shm->vsh_linked = 0;
    if (shm->vsh_open_count == 0)
        return vm_shm_destroy(shm);
    return 0;
}

int
vm_shm_get_info(const struct vm_shm *shm, struct vm_shm_info *info)
{
    if (!vm_shm_valid(shm) || info == 0)
        return EINVAL;
    info->vsi_size = shm->vsh_size;
    info->vsi_owner = shm->vsh_owner;
    info->vsi_group = shm->vsh_group;
    info->vsi_mode = shm->vsh_mode;
    info->vsi_open_count = shm->vsh_open_count;
    info->vsi_linked = shm->vsh_linked != 0;
    return 0;
}

int
vm_shm_truncate(struct vm_shm *shm, vm_size_t size)
{
    vm_size_t old_rounded;
    vm_size_t rounded;
    vm_size_t zero_end;
    int error;

    if (!vm_shm_valid(shm))
        return EINVAL;
    if (size == 0)
        rounded = 0;
    else {
        error = vm_size_round_page(size, &rounded);
        if (error != 0)
            return error;
    }
    if (shm->vsh_size == 0)
        old_rounded = 0;
    else if (vm_size_round_page(shm->vsh_size, &old_rounded) != 0)
        return EFAULT;
    error = vm_object_resize(shm->vsh_object, rounded);
    if (error != 0)
        return error;
    if (size > shm->vsh_size && shm->vsh_size < old_rounded) {
        zero_end = size < old_rounded ? size : old_rounded;
        error = vm_object_zero_range(shm->vsh_object, shm->vsh_size,
            zero_end - shm->vsh_size);
    } else if (size < shm->vsh_size && size < rounded) {
        error = vm_object_zero_range(shm->vsh_object, size,
            rounded - size);
    } else
        error = 0;
    if (error != 0)
        return error;
    shm->vsh_size = size;
    return 0;
}

int
vm_shm_object_reference(struct vm_shm *shm, struct vm_object **result)
{
    int error;

    if (!vm_shm_valid(shm) || result == 0)
        return EINVAL;
    error = vm_object_reference(shm->vsh_object);
    if (error == 0)
        *result = shm->vsh_object;
    return error;
}

int
vm_shm_object_clone(struct vm_shm *shm, struct vm_object **result)
{
    if (!vm_shm_valid(shm) || result == 0)
        return EINVAL;
    return vm_object_clone(shm->vsh_object, result);
}

int
vm_shm_get_stats(struct vm_shm_stats *stats)
{
    unsigned index;

    if (!vm_shm_initialized || stats == 0)
        return EINVAL;
    vm_shm_zero(stats, sizeof(*stats));
    for (index = 0; index < VM_SHM_MAX_OBJECTS; ++index) {
        if (vm_shm_pool[index].vsh_in_use == 0)
            continue;
        ++stats->vss_objects;
        if (vm_shm_pool[index].vsh_linked != 0)
            ++stats->vss_named_objects;
        stats->vss_open_files += vm_shm_pool[index].vsh_open_count;
        if (VM_SIZE_MAX - stats->vss_bytes <
            vm_shm_pool[index].vsh_size)
            stats->vss_bytes = VM_SIZE_MAX;
        else
            stats->vss_bytes += vm_shm_pool[index].vsh_size;
    }
    return 0;
}
