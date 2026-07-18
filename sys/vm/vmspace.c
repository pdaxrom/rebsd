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
#include <sys/inode.h>
#include <sys/uio.h>
#define VMSPACE_MAX_SPACES      (NPROC + 4)
#define vmspace_zero_memory(p, n) bzero((caddr_t)(p), (unsigned)(n))
#define vmspace_copy_memory(s, d, n) bcopy((s), (d), (unsigned)(n))
#else
#include <errno.h>
#include <string.h>
#define VMSPACE_MAX_SPACES      36
#define vmspace_zero_memory(p, n) memset((p), 0, (n))
#define vmspace_copy_memory(s, d, n) memcpy((d), (s), (n))
#endif

#include <vm/vmspace.h>
#include <vm/vm_object.h>
#include <vm/vm_shm.h>
#include <vm/vm_sysv_shm.h>

#define VMSPACE_USER_MIN        0x00001000u
#define VMSPACE_USER_MAX        0x80000000u

static struct vmspace vmspace_pool[VMSPACE_MAX_SPACES];
static struct vm_page_allocator *vmspace_allocator;
static unsigned vmspace_initialized;

static int
vmspace_valid(const struct vmspace *vmspace)
{
    return vmspace != 0 && vmspace >= &vmspace_pool[0] &&
        vmspace < &vmspace_pool[VMSPACE_MAX_SPACES] &&
        vmspace->vms_in_use != 0;
}

int
vmspace_system_init(struct vm_page_allocator *allocator)
{
    int error;

    if (allocator == 0 || allocator->vpa_initialized == 0)
        return EINVAL;
    vmspace_zero_memory(vmspace_pool, sizeof(vmspace_pool));
    error = vm_object_system_init(allocator);
    if (error != 0)
        return error;
    error = vm_shm_system_init();
    if (error != 0)
        return error;
    error = vm_sysv_shm_system_init();
    if (error != 0)
        return error;
    vmspace_allocator = allocator;
    vmspace_initialized = 1;
    return 0;
}

int
vmspace_create(struct vmspace **result)
{
    struct vmspace *vmspace;
    unsigned index;
    int error;

    if (!vmspace_initialized || result == 0)
        return EINVAL;
    *result = 0;
    vmspace = 0;
    for (index = 0; index < VMSPACE_MAX_SPACES; ++index) {
        if (vmspace_pool[index].vms_in_use == 0) {
            vmspace = &vmspace_pool[index];
            break;
        }
    }
    if (vmspace == 0)
        return ENOSPC;
    vmspace_zero_memory(vmspace, sizeof(*vmspace));
    error = pmap_create(&vmspace->vms_pmap);
    if (error != 0)
        return error;
    error = vm_map_init(&vmspace->vms_map, VMSPACE_USER_MIN,
        VMSPACE_USER_MAX);
    if (error != 0) {
        (void)pmap_destroy(vmspace->vms_pmap);
        vmspace_zero_memory(vmspace, sizeof(*vmspace));
        return error;
    }
    vmspace->vms_in_use = 1;
    *result = vmspace;
    return 0;
}

struct vmspace_unmap_object {
    struct vm_object *vuo_object;
    vm_ooffset_t      vuo_offset;
    vm_size_t         vuo_size;
};

static int
vmspace_map_has_object(const struct vm_map *map, struct vm_object *object)
{
    unsigned index;

    for (index = 0; index < map->vmm_count; ++index) {
        if (map->vmm_entries[index].vme_object == object)
            return 1;
    }
    return 0;
}

int
vmspace_contains_object(const struct vmspace *vmspace,
    struct vm_object *object)
{
    return vmspace_valid(vmspace) && object != 0 &&
        vmspace_map_has_object(&vmspace->vms_map, object);
}

static int
vmspace_sysv_overlap(const struct vmspace *vmspace, vm_vaddr_t start,
    vm_vaddr_t end)
{
    const struct vmspace_sysv_attachment *attachment;
    unsigned index;

    for (index = 0; index < vmspace->vms_sysv_attachment_count; ++index) {
        attachment = &vmspace->vms_sysv_attachments[index];
        if (attachment->vsa_start < end &&
            attachment->vsa_start + attachment->vsa_size > start)
            return 1;
    }
    return 0;
}

static int
vmspace_preserve_dirty(const struct vm_map *map, struct pmap *pmap,
    vm_vaddr_t start, vm_vaddr_t end)
{
    const struct vm_map_entry *entry;
    vm_ooffset_t offset;
    vm_vaddr_t address;
    int error;

    for (address = start; address < end; address += VM_PAGE_SIZE) {
        if (!pmap_is_modified(pmap, address))
            continue;
        entry = vm_map_lookup(map, address);
        if (entry != 0 && (entry->vme_flags & VM_MAP_DEVICE) != 0)
            continue;
        if (entry == 0 || entry->vme_object == 0)
            return EFAULT;
        offset = entry->vme_offset + (address - entry->vme_start);
        error = vm_object_mark_dirty(entry->vme_object, offset);
        if (error != 0)
            return error;
    }
    return 0;
}

static int
vmspace_unwire_removed(const struct vm_map *map, vm_vaddr_t start,
    vm_vaddr_t end)
{
    const struct vm_map_entry *entry;
    struct vm_page *page;
    vm_ooffset_t offset;
    vm_vaddr_t address;
    vm_vaddr_t first;
    vm_vaddr_t last;
    unsigned index;
    int error;

    for (index = 0; index < map->vmm_count; ++index) {
        entry = &map->vmm_entries[index];
        if ((entry->vme_flags & VM_MAP_WIRED) == 0 ||
            (entry->vme_flags & VM_MAP_DEVICE) != 0 ||
            entry->vme_end <= start || entry->vme_start >= end)
            continue;
        first = entry->vme_start > start ? entry->vme_start : start;
        last = entry->vme_end < end ? entry->vme_end : end;
        for (address = first; address < last; address += VM_PAGE_SIZE) {
            offset = entry->vme_offset + (address - entry->vme_start);
            page = vm_object_resident_page(entry->vme_object, offset);
            if (page == 0 || page->vmp_wire_count == 0)
                return EFAULT;
        }
    }
    for (index = 0; index < map->vmm_count; ++index) {
        entry = &map->vmm_entries[index];
        if ((entry->vme_flags & VM_MAP_WIRED) == 0 ||
            (entry->vme_flags & VM_MAP_DEVICE) != 0 ||
            entry->vme_end <= start || entry->vme_start >= end)
            continue;
        first = entry->vme_start > start ? entry->vme_start : start;
        last = entry->vme_end < end ? entry->vme_end : end;
        for (address = first; address < last; address += VM_PAGE_SIZE) {
            offset = entry->vme_offset + (address - entry->vme_start);
            page = vm_object_resident_page(entry->vme_object, offset);
            error = vm_page_counter_dec(vmspace_allocator, page,
                VM_PAGE_COUNTER_WIRE);
            if (error != 0)
                return error;
        }
    }
    return 0;
}

int
vmspace_unmap(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size)
{
    struct vmspace_unmap_object objects[VM_MAP_MAX_ENTRIES];
    struct vm_map replacement;
    const struct vm_map_entry *entry;
    vm_vaddr_t first;
    vm_vaddr_t last;
    vm_vaddr_t end;
    unsigned count;
    unsigned index;
    unsigned prior;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        vm_vaddr_add(start, size, &end) != 0)
        return EINVAL;
    if (end > vmspace->vms_map.vmm_max)
        return EINVAL;
    if (vmspace_sysv_overlap(vmspace, start, end))
        return EBUSY;
    replacement = vmspace->vms_map;
    error = vm_map_remove(&replacement, start, end);
    if (error != 0)
        return error;
    count = 0;
    for (index = 0; index < vmspace->vms_map.vmm_count; ++index) {
        entry = &vmspace->vms_map.vmm_entries[index];
        if (entry->vme_end <= start || entry->vme_start >= end ||
            entry->vme_object == 0)
            continue;
        first = entry->vme_start > start ? entry->vme_start : start;
        last = entry->vme_end < end ? entry->vme_end : end;
        objects[count].vuo_object = entry->vme_object;
        objects[count].vuo_offset = entry->vme_offset +
            (first - entry->vme_start);
        objects[count].vuo_size = last - first;
        ++count;
    }
    error = vmspace_preserve_dirty(&vmspace->vms_map,
        vmspace->vms_pmap, start, end);
    if (error != 0)
        return error;
    error = pmap_remove(vmspace->vms_pmap, start, end);
    if (error != 0)
        return error;
    error = vmspace_unwire_removed(&vmspace->vms_map, start, end);
    if (error != 0)
        return error;
    vmspace->vms_map = replacement;
    for (index = 0; index < count; ++index) {
        if (!vmspace_map_has_object(&replacement,
            objects[index].vuo_object)) {
            for (prior = 0; prior < index; ++prior) {
                if (objects[prior].vuo_object ==
                    objects[index].vuo_object)
                    break;
            }
            if (prior != index)
                continue;
            error = vm_object_release(objects[index].vuo_object);
        } else if (!vm_object_is_shared(objects[index].vuo_object) &&
            !vm_object_has_pageout(objects[index].vuo_object)) {
            error = vm_object_remove(objects[index].vuo_object,
                objects[index].vuo_offset, objects[index].vuo_size);
        } else
            error = 0;
        if (error != 0)
            return error;
    }
    return 0;
}

int
vmspace_sysv_attach(struct vmspace *vmspace,
    struct vm_sysv_shm *segment, vm_vaddr_t start, vm_size_t size,
    int pid, long now)
{
    const struct vm_map_entry *entry;
    struct vmspace_sysv_attachment *attachment;
    vm_vaddr_t address;
    vm_vaddr_t end;
    unsigned index;
    int error;

    if (!vmspace_valid(vmspace) || segment == 0 || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        start < vmspace->vms_map.vmm_min ||
        start >= vmspace->vms_map.vmm_max ||
        size > vmspace->vms_map.vmm_max - start ||
        vm_vaddr_add(start, size, &end) != 0)
        return EINVAL;
    if (vmspace->vms_sysv_attachment_count >=
        sizeof(vmspace->vms_sysv_attachments) /
        sizeof(vmspace->vms_sysv_attachments[0]))
        return ENOSPC;
    for (index = 0; index < vmspace->vms_sysv_attachment_count; ++index) {
        attachment = &vmspace->vms_sysv_attachments[index];
        if (attachment->vsa_start == start)
            return EEXIST;
    }
    for (address = start; address < end; address += VM_PAGE_SIZE) {
        entry = vm_map_lookup(&vmspace->vms_map, address);
        if (entry == 0 || (entry->vme_flags & VM_MAP_SYSV_SHM) == 0)
            return EINVAL;
    }
    error = vm_sysv_shm_attach(segment, pid, now);
    if (error != 0)
        return error;
    attachment = &vmspace->vms_sysv_attachments[
        vmspace->vms_sysv_attachment_count++];
    attachment->vsa_segment = segment;
    attachment->vsa_start = start;
    attachment->vsa_size = size;
    return 0;
}

int
vmspace_sysv_detach(struct vmspace *vmspace, vm_vaddr_t start,
    int pid, long now)
{
    struct vmspace_sysv_attachment attachment;
    unsigned count;
    unsigned index;
    int error;

    if (!vmspace_valid(vmspace) || !vm_vaddr_page_aligned(start))
        return EINVAL;
    count = vmspace->vms_sysv_attachment_count;
    for (index = 0; index < count; ++index) {
        if (vmspace->vms_sysv_attachments[index].vsa_start == start)
            break;
    }
    if (index == count)
        return EINVAL;
    attachment = vmspace->vms_sysv_attachments[index];
    --vmspace->vms_sysv_attachment_count;
    vmspace->vms_sysv_attachments[index] =
        vmspace->vms_sysv_attachments[vmspace->vms_sysv_attachment_count];
    vmspace_zero_memory(&vmspace->vms_sysv_attachments[
        vmspace->vms_sysv_attachment_count],
        sizeof(vmspace->vms_sysv_attachments[0]));
    error = vmspace_unmap(vmspace, attachment.vsa_start,
        attachment.vsa_size);
    if (error != 0) {
        vmspace->vms_sysv_attachments[
            vmspace->vms_sysv_attachment_count++] = attachment;
        return error;
    }
    return vm_sysv_shm_detach(attachment.vsa_segment, pid, now);
}

int
vmspace_destroy(struct vmspace *vmspace)
{
    struct vm_map_entry entry;
    int error;

    if (!vmspace_valid(vmspace))
        return EINVAL;
    while (vmspace->vms_sysv_attachment_count != 0) {
        error = vmspace_sysv_detach(vmspace,
            vmspace->vms_sysv_attachments[0].vsa_start, 0, 0);
        if (error != 0)
            return error;
    }
    while (vmspace->vms_map.vmm_count != 0) {
        entry = vmspace->vms_map.vmm_entries[0];
        error = vmspace_unmap(vmspace, entry.vme_start,
            entry.vme_end - entry.vme_start);
        if (error != 0)
            return error;
    }
    error = pmap_destroy(vmspace->vms_pmap);
    if (error != 0)
        return error;
    vmspace_zero_memory(vmspace, sizeof(*vmspace));
    return 0;
}

int
vmspace_activate(struct vmspace *vmspace)
{
    if (!vmspace_valid(vmspace))
        return EINVAL;
    return pmap_activate(vmspace->vms_pmap);
}

int
vmspace_map_object(struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection, vm_prot_t maximum,
    unsigned flags, struct vm_object *object, vm_ooffset_t offset)
{
    vm_vaddr_t end;

    if (!vmspace_valid(vmspace) || size == 0 ||
        (object == 0 && (flags & VM_MAP_DEVICE) == 0) ||
        (object != 0 && (flags & VM_MAP_DEVICE) != 0) ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        (offset & VM_PAGE_MASK) != 0 ||
        vm_vaddr_add(start, size, &end) != 0)
        return EINVAL;
    return vm_map_insert_object(&vmspace->vms_map, start, end,
        protection, maximum, flags, object, offset);
}

int
vmspace_map_object_any(struct vmspace *vmspace, vm_vaddr_t hint,
    vm_size_t size, vm_prot_t protection, vm_prot_t maximum,
    unsigned flags, struct vm_object *object, vm_ooffset_t offset,
    vm_vaddr_t *result)
{
    vm_vaddr_t start;
    int error;

    if (!vmspace_valid(vmspace) || result == 0 ||
        (object == 0 && (flags & VM_MAP_DEVICE) == 0) ||
        (object != 0 && (flags & VM_MAP_DEVICE) != 0))
        return EINVAL;
    error = vm_map_findspace(&vmspace->vms_map, hint, size, &start);
    if (error != 0)
        return error;
    error = vmspace_map_object(vmspace, start, size, protection,
        maximum, flags, object, offset);
    if (error != 0)
        return error;
    *result = start;
    return 0;
}

int
vmspace_map_object_fixed(struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection, vm_prot_t maximum,
    unsigned flags, struct vm_object *object, vm_ooffset_t offset)
{
    struct vmspace_unmap_object objects[VM_MAP_MAX_ENTRIES];
    struct vm_map replacement;
    const struct vm_map_entry *entry;
    vm_vaddr_t first;
    vm_vaddr_t last;
    vm_vaddr_t end;
    unsigned count;
    unsigned index;
    unsigned prior;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        (object == 0 && (flags & VM_MAP_DEVICE) == 0) ||
        (object != 0 && (flags & VM_MAP_DEVICE) != 0) ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        (offset & VM_PAGE_MASK) != 0 ||
        vm_vaddr_add(start, size, &end) != 0)
        return EINVAL;
    if (start < vmspace->vms_map.vmm_min ||
        end > vmspace->vms_map.vmm_max)
        return EINVAL;
    if (vmspace_sysv_overlap(vmspace, start, end))
        return EBUSY;
    replacement = vmspace->vms_map;
    error = vm_map_remove(&replacement, start, end);
    if (error == 0)
        error = vm_map_insert_object(&replacement, start, end,
            protection, maximum, flags, object, offset);
    if (error != 0)
        return error;
    count = 0;
    for (index = 0; index < vmspace->vms_map.vmm_count; ++index) {
        entry = &vmspace->vms_map.vmm_entries[index];
        if (entry->vme_end <= start || entry->vme_start >= end ||
            entry->vme_object == 0)
            continue;
        first = entry->vme_start > start ? entry->vme_start : start;
        last = entry->vme_end < end ? entry->vme_end : end;
        objects[count].vuo_object = entry->vme_object;
        objects[count].vuo_offset = entry->vme_offset +
            (first - entry->vme_start);
        objects[count].vuo_size = last - first;
        ++count;
    }
    error = vmspace_preserve_dirty(&vmspace->vms_map,
        vmspace->vms_pmap, start, end);
    if (error != 0)
        return error;
    error = pmap_remove(vmspace->vms_pmap, start, end);
    if (error != 0)
        return error;
    error = vmspace_unwire_removed(&vmspace->vms_map, start, end);
    if (error != 0)
        return error;
    vmspace->vms_map = replacement;
    for (index = 0; index < count; ++index) {
        if (!vmspace_map_has_object(&replacement,
            objects[index].vuo_object)) {
            for (prior = 0; prior < index; ++prior) {
                if (objects[prior].vuo_object ==
                    objects[index].vuo_object)
                    break;
            }
            if (prior != index)
                continue;
            error = vm_object_release(objects[index].vuo_object);
        } else if (!vm_object_is_shared(objects[index].vuo_object) &&
            !vm_object_has_pageout(objects[index].vuo_object)) {
            error = vm_object_remove(objects[index].vuo_object,
                objects[index].vuo_offset, objects[index].vuo_size);
        } else
            error = 0;
        if (error != 0)
            return error;
    }
    return 0;
}

static int
vmspace_device_arguments(vm_size_t size, vm_prot_t protection,
    vm_prot_t maximum, vm_paddr_t paddr, enum pmap_cache cache,
    unsigned *flags)
{
    struct vm_page *page;
    vm_paddr_t address;
    vm_size_t remaining;

    if (size == 0 || !vm_size_page_aligned(size) ||
        !vm_paddr_page_aligned(paddr) ||
        size - 1 > VM_PADDR_MAX - paddr ||
        (protection & ~maximum) != 0 ||
        (maximum & ~VM_PROT_ALL) != 0 ||
        (maximum & VM_PROT_EXECUTE) != 0 ||
        (cache != PMAP_CACHE_CACHED && cache != PMAP_CACHE_UNCACHED) ||
        flags == 0)
        return EINVAL;
    address = paddr;
    remaining = size;
    while (remaining != 0) {
        page = vm_page_lookup(vmspace_allocator, address);
        if (page != 0 && page->vmp_state != VM_PAGE_RESERVED)
            return EBUSY;
        remaining -= VM_PAGE_SIZE;
        if (remaining != 0)
            address += VM_PAGE_SIZE;
    }
    *flags = VM_MAP_SHARED | VM_MAP_DEVICE;
    if (cache == PMAP_CACHE_UNCACHED)
        *flags |= VM_MAP_UNCACHED;
    return 0;
}

int
vmspace_map_device(struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection, vm_prot_t maximum,
    vm_paddr_t paddr, enum pmap_cache cache)
{
    unsigned flags;
    int error;

    error = vmspace_device_arguments(size, protection, maximum, paddr,
        cache, &flags);
    if (error != 0)
        return error;
    return vmspace_map_object(vmspace, start, size, protection, maximum,
        flags, 0, (vm_ooffset_t)paddr);
}

int
vmspace_map_device_any(struct vmspace *vmspace, vm_vaddr_t hint,
    vm_size_t size, vm_prot_t protection, vm_prot_t maximum,
    vm_paddr_t paddr, enum pmap_cache cache, vm_vaddr_t *result)
{
    unsigned flags;
    int error;

    error = vmspace_device_arguments(size, protection, maximum, paddr,
        cache, &flags);
    if (error != 0)
        return error;
    return vmspace_map_object_any(vmspace, hint, size, protection,
        maximum, flags, 0, (vm_ooffset_t)paddr, result);
}

int
vmspace_map_device_fixed(struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection, vm_prot_t maximum,
    vm_paddr_t paddr, enum pmap_cache cache)
{
    unsigned flags;
    int error;

    error = vmspace_device_arguments(size, protection, maximum, paddr,
        cache, &flags);
    if (error != 0)
        return error;
    return vmspace_map_object_fixed(vmspace, start, size, protection,
        maximum, flags, 0, (vm_ooffset_t)paddr);
}

int
vmspace_map_anon(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    vm_prot_t protection, unsigned flags)
{
    struct vm_object *object;
    int error;

    error = vm_object_create(size, &object);
    if (error != 0)
        return error;
    error = vmspace_map_object(vmspace, start, size, protection,
        VM_PROT_ALL, flags | VM_MAP_ANON, object, 0);
    if (error != 0)
        (void)vm_object_release(object);
    return error;
}

int
vmspace_grow_anon(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    vm_prot_t protection, unsigned flags)
{
    const struct vm_map_entry *entry;
    struct vm_object *object;
    vm_ooffset_t object_end;
    vm_size_t new_size;
    vm_size_t old_size;
    vm_vaddr_t end;
    unsigned anon_flags;
    int error;
    int resized;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        vm_vaddr_add(start, size, &end) != 0)
        return EINVAL;
    anon_flags = flags | VM_MAP_ANON;
    entry = start == 0 ? 0 :
        vm_map_lookup(&vmspace->vms_map, start - 1);
    if (entry == 0 || entry->vme_end != start ||
        entry->vme_protection != protection ||
        entry->vme_max_protection != VM_PROT_ALL ||
        entry->vme_flags != anon_flags || entry->vme_object == 0 ||
        vm_object_is_shared(entry->vme_object) ||
        vm_ooffset_add(entry->vme_offset,
        entry->vme_end - entry->vme_start, &object_end) != 0 ||
        object_end > VM_SIZE_MAX || size > VM_SIZE_MAX - object_end)
        return vmspace_map_anon(vmspace, start, size, protection, flags);

    object = entry->vme_object;
    new_size = (vm_size_t)object_end + size;
    error = vm_object_get_size(object, &old_size);
    if (error != 0 || old_size < (vm_size_t)object_end)
        return error != 0 ? error : EFAULT;
    resized = new_size > old_size;
    if (resized) {
        error = vm_object_resize(object, new_size);
        if (error != 0)
            return error;
    }
    error = vmspace_map_object(vmspace, start, size, protection,
        VM_PROT_ALL, anon_flags, object, object_end);
    if (error != 0 && resized && vm_object_resize(object, old_size) != 0)
        return EFAULT;
    return error;
}

int
vmspace_map_anon_any(struct vmspace *vmspace, vm_vaddr_t hint,
    vm_size_t size, vm_prot_t protection, unsigned flags,
    vm_vaddr_t *result)
{
    struct vm_object *object;
    int error;

    error = vm_object_create(size, &object);
    if (error != 0)
        return error;
    error = vmspace_map_object_any(vmspace, hint, size, protection,
        VM_PROT_ALL, flags | VM_MAP_ANON, object, 0, result);
    if (error != 0)
        (void)vm_object_release(object);
    return error;
}

int
vmspace_map_anon_fixed(struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection, unsigned flags)
{
    struct vm_object *object;
    int error;

    error = vm_object_create(size, &object);
    if (error != 0)
        return error;
    error = vmspace_map_object_fixed(vmspace, start, size, protection,
        VM_PROT_ALL, flags | VM_MAP_ANON, object, 0);
    if (error != 0)
        (void)vm_object_release(object);
    return error;
}

int
vmspace_protect(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    vm_prot_t protection)
{
    struct vm_map replacement;
    const struct vm_map_entry *entry;
    vm_prot_t effective;
    vm_vaddr_t address;
    vm_vaddr_t end;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        vm_vaddr_add(start, size, &end) != 0)
        return EINVAL;
    replacement = vmspace->vms_map;
    error = vm_map_protect(&replacement, start, end, protection);
    if (error != 0)
        return error;
    for (address = start; address < end; address += VM_PAGE_SIZE) {
        entry = vm_map_lookup(&replacement, address);
        if (entry == 0)
            return EFAULT;
        effective = entry->vme_protection;
        if ((entry->vme_flags & VM_MAP_COW) != 0)
            effective &= ~VM_PROT_WRITE;
        error = pmap_protect(vmspace->vms_pmap, address,
            address + VM_PAGE_SIZE, effective);
        if (error != 0)
            return error;
    }
    vmspace->vms_map = replacement;
    return 0;
}

static void
vmspace_wire_rollback(const struct vm_map *map, vm_vaddr_t start,
    vm_vaddr_t end)
{
    const struct vm_map_entry *entry;
    struct vm_page *page;
    vm_ooffset_t offset;
    vm_vaddr_t address;

    for (address = start; address < end; address += VM_PAGE_SIZE) {
        entry = vm_map_lookup(map, address);
        if (entry == 0 || (entry->vme_flags & VM_MAP_WIRED) != 0 ||
            (entry->vme_flags & VM_MAP_DEVICE) != 0)
            continue;
        offset = entry->vme_offset + (address - entry->vme_start);
        page = vm_object_resident_page(entry->vme_object, offset);
        if (page != 0)
            (void)vm_page_counter_dec(vmspace_allocator, page,
                VM_PAGE_COUNTER_WIRE);
    }
}

int
vmspace_wire(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    int wire)
{
    struct vm_map replacement;
    const struct vm_map_entry *entry;
    struct vm_page *page;
    vm_ooffset_t offset;
    vm_vaddr_t address;
    vm_vaddr_t end;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        vm_vaddr_add(start, size, &end) != 0 ||
        (wire != 0 && wire != 1))
        return EINVAL;
    replacement = vmspace->vms_map;
    error = vm_map_set_flags(&replacement, start, end,
        wire ? VM_MAP_WIRED : 0, wire ? 0 : VM_MAP_WIRED);
    if (error != 0)
        return error;
    if (wire) {
        for (address = start; address < end; address += VM_PAGE_SIZE) {
            entry = vm_map_lookup(&vmspace->vms_map, address);
            if ((entry->vme_flags & VM_MAP_WIRED) != 0)
                continue;
            if ((entry->vme_flags & VM_MAP_DEVICE) != 0)
                continue;
            offset = entry->vme_offset + (address - entry->vme_start);
            error = vm_object_fault(entry->vme_object, offset, 0, &page);
            if (error == 0)
                error = vm_page_counter_inc(vmspace_allocator, page,
                    VM_PAGE_COUNTER_WIRE);
            if (error != 0) {
                vmspace_wire_rollback(&vmspace->vms_map, start, address);
                return error;
            }
        }
    } else {
        for (address = start; address < end; address += VM_PAGE_SIZE) {
            entry = vm_map_lookup(&vmspace->vms_map, address);
            if ((entry->vme_flags & VM_MAP_WIRED) == 0)
                continue;
            if ((entry->vme_flags & VM_MAP_DEVICE) != 0)
                continue;
            offset = entry->vme_offset + (address - entry->vme_start);
            page = vm_object_resident_page(entry->vme_object, offset);
            if (page == 0 || page->vmp_wire_count == 0)
                return EFAULT;
        }
        for (address = start; address < end; address += VM_PAGE_SIZE) {
            entry = vm_map_lookup(&vmspace->vms_map, address);
            if ((entry->vme_flags & VM_MAP_WIRED) == 0)
                continue;
            if ((entry->vme_flags & VM_MAP_DEVICE) != 0)
                continue;
            offset = entry->vme_offset + (address - entry->vme_start);
            page = vm_object_resident_page(entry->vme_object, offset);
            error = vm_page_counter_dec(vmspace_allocator, page,
                VM_PAGE_COUNTER_WIRE);
            if (error != 0)
                return error;
        }
    }
    vmspace->vms_map = replacement;
    return 0;
}

int
vmspace_mincore(const struct vmspace *vmspace, vm_vaddr_t address,
    int *resident)
{
    const struct vm_map_entry *entry;
    vm_ooffset_t offset;

    if (!vmspace_valid(vmspace) || resident == 0)
        return EINVAL;
    entry = vm_map_lookup(&vmspace->vms_map, address);
    if (entry == 0)
        return EFAULT;
    if ((entry->vme_flags & VM_MAP_DEVICE) != 0) {
        *resident = 1;
        return 0;
    }
    if (entry->vme_object == 0)
        return EFAULT;
    offset = entry->vme_offset +
        (vm_vaddr_trunc_page(address) - entry->vme_start);
    *resident = vm_object_resident_page(entry->vme_object, offset) != 0;
    return 0;
}

int
vmspace_sync(struct vmspace *vmspace, vm_vaddr_t start, vm_size_t size,
    unsigned flags)
{
    const struct vm_map_entry *entry;
    vm_ooffset_t offset;
    vm_vaddr_t first;
    vm_vaddr_t last;
    vm_vaddr_t end;
    unsigned index;
    int error;

    if (!vmspace_valid(vmspace) || size == 0 ||
        !vm_vaddr_page_aligned(start) || !vm_size_page_aligned(size) ||
        vm_vaddr_add(start, size, &end) != 0 ||
        (flags & ~(VM_PAGER_IO_SYNC | VM_PAGER_IO_INVALIDATE)) != 0)
        return EINVAL;
    error = vm_map_check(&vmspace->vms_map, start, size, VM_PROT_NONE);
    if (error != 0)
        return error;
    for (index = 0; index < vmspace->vms_map.vmm_count; ++index) {
        entry = &vmspace->vms_map.vmm_entries[index];
        if ((entry->vme_flags & VM_MAP_SHARED) == 0 ||
            (entry->vme_flags & VM_MAP_DEVICE) != 0 ||
            entry->vme_end <= start || entry->vme_start >= end)
            continue;
        first = entry->vme_start > start ? entry->vme_start : start;
        last = entry->vme_end < end ? entry->vme_end : end;
        offset = entry->vme_offset + (first - entry->vme_start);
        error = vm_object_sync(entry->vme_object, offset,
            last - first, flags);
        if (error != 0)
            return error;
    }
    return 0;
}

int
vmspace_check(const struct vmspace *vmspace, vm_vaddr_t start,
    vm_size_t size, vm_prot_t protection)
{
    if (!vmspace_valid(vmspace))
        return EINVAL;
    return vm_map_check(&vmspace->vms_map, start, size, protection);
}

unsigned
vmspace_shared_mapping_count(const struct vmspace *vmspace)
{
    unsigned count;
    unsigned index;

    if (!vmspace_valid(vmspace))
        return 0;
    count = vmspace->vms_sysv_attachment_count;
    for (index = 0; index < vmspace->vms_map.vmm_count; ++index) {
        if ((vmspace->vms_map.vmm_entries[index].vme_flags &
            VM_MAP_POSIX_SHM) != 0)
            ++count;
    }
    return count;
}

static int
vmspace_fault_context_valid(unsigned context)
{
    unsigned kind;

    if ((context & ~(VM_FAULT_CONTEXT_MASK | VM_FAULT_CAN_SLEEP)) != 0)
        return 0;
    kind = context & VM_FAULT_CONTEXT_MASK;
    return kind >= VM_FAULT_USER && kind <= VM_FAULT_INTERRUPT;
}

int
vmspace_fault_context(struct vmspace *vmspace, vm_vaddr_t address,
    vm_prot_t access, unsigned context)
{
    const struct vm_map_entry *entry;
    struct vm_page *page;
    struct vm_page *old_page;
    vm_ooffset_t offset;
    vm_paddr_t current;
    vm_prot_t effective;
    vm_vaddr_t page_address;
    int cow_write;
    int error;

    if (!vmspace_valid(vmspace) || !vmspace_fault_context_valid(context) ||
        (access != VM_PROT_READ &&
        access != VM_PROT_WRITE && access != VM_PROT_EXECUTE))
        return EINVAL;
    entry = vm_map_lookup(&vmspace->vms_map, address);
    if (entry == 0 || (entry->vme_protection & access) != access)
        return EFAULT;
    page_address = vm_vaddr_trunc_page(address);
    offset = entry->vme_offset + (page_address - entry->vme_start);
    if ((entry->vme_flags & VM_MAP_DEVICE) != 0) {
        if (entry->vme_object != 0 || offset > VM_PADDR_MAX ||
            access == VM_PROT_EXECUTE)
            return EFAULT;
        effective = entry->vme_protection & ~VM_PROT_EXECUTE;
        error = pmap_extract(vmspace->vms_pmap, page_address, &current);
        if (error == 0 && (current & ~VM_PAGE_MASK) ==
            (vm_paddr_t)offset)
            return 0;
        if (error != 0 && error != ENOENT)
            return error;
        return pmap_enter_device(vmspace->vms_pmap, page_address,
            (vm_paddr_t)offset, effective,
            (entry->vme_flags & VM_MAP_UNCACHED) != 0 ?
            PMAP_CACHE_UNCACHED : PMAP_CACHE_CACHED);
    }
    if (entry->vme_object == 0)
        return EFAULT;
    cow_write = access == VM_PROT_WRITE &&
        (entry->vme_flags & VM_MAP_COW) != 0;
    old_page = cow_write && (entry->vme_flags & VM_MAP_WIRED) != 0 ?
        vm_object_resident_page(entry->vme_object, offset) : 0;
    error = vm_object_fault_context(entry->vme_object, offset, cow_write,
        (context & VM_FAULT_CAN_SLEEP) != 0 ? 0 :
        VM_OBJECT_FAULT_NOWAIT, &page);
    if (error != 0)
        return error;
    if (old_page != 0 && old_page != page) {
        error = vm_page_counter_inc(vmspace_allocator, page,
            VM_PAGE_COUNTER_WIRE);
        if (error != 0)
            return error;
        error = vm_page_counter_dec(vmspace_allocator, old_page,
            VM_PAGE_COUNTER_WIRE);
        if (error != 0) {
            (void)vm_page_counter_dec(vmspace_allocator, page,
                VM_PAGE_COUNTER_WIRE);
            return error;
        }
    }
    error = pmap_extract(vmspace->vms_pmap, page_address, &current);
    effective = entry->vme_protection;
    if ((entry->vme_flags & VM_MAP_COW) != 0 && !cow_write)
        effective &= ~VM_PROT_WRITE;
    if (error == 0 && (current & ~VM_PAGE_MASK) == page->vmp_paddr) {
        if (cow_write)
            return pmap_protect(vmspace->vms_pmap, page_address,
                page_address + VM_PAGE_SIZE, effective);
        return 0;
    }
    if (error != 0 && error != ENOENT)
        return error;
    return pmap_enter(vmspace->vms_pmap, page_address, page, effective,
        PMAP_CACHE_CACHED);
}

int
vmspace_fault(struct vmspace *vmspace, vm_vaddr_t address,
    vm_prot_t access)
{
    return vmspace_fault_context(vmspace, address, access,
        VM_FAULT_USER | VM_FAULT_CAN_SLEEP);
}

static int
vmspace_transfer(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size, vm_prot_t protection, int write,
    unsigned context)
{
    struct vm_page *page;
    unsigned char *physical;
    unsigned char *bytes;
    vm_paddr_t paddr;
    vm_size_t chunk;
    int error;

    if (!vmspace_valid(vmspace) || buffer == 0 || size == 0 ||
        !vmspace_fault_context_valid(context))
        return EINVAL;
    error = vm_map_check(&vmspace->vms_map, address, size, protection);
    if (error != 0)
        return error;
    bytes = (unsigned char *)buffer;
    while (size != 0) {
        const struct vm_map_entry *entry;
        vm_ooffset_t offset;

        error = vmspace_fault_context((struct vmspace *)vmspace, address,
            protection, context);
        if (error != 0)
            return error;
        error = pmap_extract(vmspace->vms_pmap, address, &paddr);
        if (error != 0)
            return error;
        entry = vm_map_lookup(&vmspace->vms_map, address);
        if (entry == 0)
            return EFAULT;
        if ((entry->vme_flags & VM_MAP_DEVICE) != 0) {
            page = 0;
            physical = (unsigned char *)pmap_device_direct_map(
                paddr & ~VM_PAGE_MASK,
                (entry->vme_flags & VM_MAP_UNCACHED) != 0 ?
                PMAP_CACHE_UNCACHED : PMAP_CACHE_CACHED);
        } else {
            page = vm_page_lookup(vmspace_allocator,
                paddr & ~VM_PAGE_MASK);
            if (page == 0)
                return EFAULT;
            physical = (unsigned char *)pmap_page_direct_map(page,
                PMAP_CACHE_CACHED);
        }
        if (physical == 0)
            return EFAULT;
        physical += paddr & VM_PAGE_MASK;
        chunk = VM_PAGE_SIZE - (paddr & VM_PAGE_MASK);
        if (chunk > size)
            chunk = size;
        if (write)
            vmspace_copy_memory(bytes, physical, chunk);
        else
            vmspace_copy_memory(physical, bytes, chunk);
        if (write && (entry->vme_flags & VM_MAP_DEVICE) == 0) {
            offset = entry->vme_offset +
                (vm_vaddr_trunc_page(address) - entry->vme_start);
            error = vm_object_mark_dirty(entry->vme_object, offset);
            if (error != 0)
                return error;
        }
        if (write && page != 0 &&
            (entry->vme_protection & VM_PROT_EXECUTE) != 0) {
            error = pmap_page_sync(page,
                PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
            if (error != 0)
                return error;
        }
        address += chunk;
        bytes += chunk;
        size -= chunk;
    }
    return 0;
}

int
vmspace_read(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size)
{
    return vmspace_transfer(vmspace, address, buffer, size,
        VM_PROT_READ, 0, VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}

int
vmspace_write(struct vmspace *vmspace, vm_vaddr_t address,
    const void *buffer, vm_size_t size)
{
    return vmspace_transfer(vmspace, address, (void *)buffer, size,
        VM_PROT_WRITE, 1, VM_FAULT_COPY | VM_FAULT_CAN_SLEEP);
}

int
vmspace_read_context(const struct vmspace *vmspace, vm_vaddr_t address,
    void *buffer, vm_size_t size, unsigned context)
{
    return vmspace_transfer(vmspace, address, buffer, size,
        VM_PROT_READ, 0, context);
}

int
vmspace_write_context(struct vmspace *vmspace, vm_vaddr_t address,
    const void *buffer, vm_size_t size, unsigned context)
{
    return vmspace_transfer(vmspace, address, (void *)buffer, size,
        VM_PROT_WRITE, 1, context);
}

int
vmspace_zero(struct vmspace *vmspace, vm_vaddr_t address, vm_size_t size)
{
    unsigned char zeros[64];
    vm_size_t chunk;
    int error;

    if (size == 0)
        return 0;
    vmspace_zero_memory(zeros, sizeof(zeros));
    while (size != 0) {
        chunk = size < sizeof(zeros) ? size : sizeof(zeros);
        error = vmspace_write(vmspace, address, zeros, chunk);
        if (error != 0)
            return error;
        address += chunk;
        size -= chunk;
    }
    return 0;
}

int
vmspace_grow_stack(struct vmspace *vmspace, vm_vaddr_t current_start,
    vm_vaddr_t requested_start, vm_vaddr_t guard_end)
{
    vm_vaddr_t old_page;
    vm_vaddr_t new_page;

    if (!vmspace_valid(vmspace))
        return EINVAL;
    old_page = vm_vaddr_trunc_page(current_start);
    new_page = vm_vaddr_trunc_page(requested_start);
    if (new_page >= old_page)
        return 0;
    if (new_page < guard_end || old_page - new_page > VM_SIZE_MAX)
        return EFAULT;
    return vmspace_map_anon(vmspace, new_page, old_page - new_page,
        VM_PROT_READ | VM_PROT_WRITE, VM_MAP_STACK);
}

int
vmspace_clone(struct vmspace *source, struct vmspace **result)
{
    struct vm_object *source_objects[VM_MAP_MAX_ENTRIES];
    struct vm_object *target_objects[VM_MAP_MAX_ENTRIES];
    struct vm_map_entry *source_entry;
    const struct vm_map_entry *target_entry;
    struct vm_object *target_object;
    struct vm_page *page;
    struct vmspace *target;
    vm_ooffset_t offset;
    vm_prot_t effective;
    vm_vaddr_t address;
    unsigned index;
    unsigned object_count;
    unsigned object_index;
    unsigned flags;
    int new_object;
    int error;

    if (!vmspace_valid(source) || result == 0)
        return EINVAL;
    error = vmspace_create(&target);
    if (error != 0)
        return error;
    object_count = 0;
    for (index = 0; index < source->vms_map.vmm_count; ++index) {
        source_entry = &source->vms_map.vmm_entries[index];
        if ((source_entry->vme_flags & VM_MAP_DEVICE) != 0) {
            flags = source_entry->vme_flags & ~VM_MAP_WIRED;
            error = vm_map_insert_object(&target->vms_map,
                source_entry->vme_start, source_entry->vme_end,
                source_entry->vme_protection,
                source_entry->vme_max_protection, flags, 0,
                source_entry->vme_offset);
            if (error != 0)
                goto fail;
            continue;
        }
        target_object = 0;
        new_object = 0;
        for (object_index = 0; object_index < object_count;
            ++object_index) {
            if (source_objects[object_index] == source_entry->vme_object) {
                target_object = target_objects[object_index];
                break;
            }
        }
        if (target_object == 0) {
            if ((source_entry->vme_flags & VM_MAP_SHARED) != 0) {
                error = vm_object_reference(source_entry->vme_object);
                target_object = source_entry->vme_object;
            } else {
                error = vm_object_clone(source_entry->vme_object,
                    &target_object);
            }
            if (error != 0)
                goto fail;
            new_object = 1;
        }
        flags = source_entry->vme_flags & ~VM_MAP_WIRED;
        if ((flags & VM_MAP_SHARED) == 0 &&
            (source_entry->vme_max_protection & VM_PROT_WRITE) != 0)
            flags |= VM_MAP_COW;
        error = vm_map_insert_object(&target->vms_map,
            source_entry->vme_start, source_entry->vme_end,
            source_entry->vme_protection,
            source_entry->vme_max_protection, flags, target_object,
            source_entry->vme_offset);
        if (error != 0) {
            if (new_object)
                (void)vm_object_release(target_object);
            goto fail;
        }
        if (new_object) {
            source_objects[object_count] = source_entry->vme_object;
            target_objects[object_count] = target_object;
            ++object_count;
        }
    }

    for (index = 0; index < source->vms_sysv_attachment_count; ++index) {
        error = vmspace_sysv_attach(target,
            source->vms_sysv_attachments[index].vsa_segment,
            source->vms_sysv_attachments[index].vsa_start,
            source->vms_sysv_attachments[index].vsa_size, 0, 0);
        if (error != 0)
            goto fail;
    }

    for (index = 0; index < source->vms_map.vmm_count; ++index) {
        source_entry = &source->vms_map.vmm_entries[index];
        if ((source_entry->vme_flags & VM_MAP_DEVICE) != 0)
            continue;
        if ((source_entry->vme_flags & VM_MAP_SHARED) == 0 &&
            (source_entry->vme_max_protection & VM_PROT_WRITE) != 0) {
            source_entry->vme_flags |= VM_MAP_COW;
        }
        if ((source_entry->vme_flags & VM_MAP_COW) != 0 &&
            (source_entry->vme_protection & VM_PROT_WRITE) != 0) {
            effective = source_entry->vme_protection & ~VM_PROT_WRITE;
            error = pmap_protect(source->vms_pmap,
                source_entry->vme_start, source_entry->vme_end,
                effective);
            if (error != 0)
                goto fail;
        }
        for (address = source_entry->vme_start;
            address < source_entry->vme_end; address += VM_PAGE_SIZE) {
            target_entry = vm_map_lookup(&target->vms_map, address);
            if (target_entry == 0) {
                error = EFAULT;
                goto fail;
            }
            offset = target_entry->vme_offset +
                (address - target_entry->vme_start);
            page = vm_object_resident_page(target_entry->vme_object,
                offset);
            if (page == 0)
                continue;
            effective = target_entry->vme_protection;
            if ((target_entry->vme_flags & VM_MAP_COW) != 0)
                effective &= ~VM_PROT_WRITE;
            error = pmap_enter(target->vms_pmap, address, page,
                effective, PMAP_CACHE_CACHED);
            if (error != 0)
                goto fail;
        }
    }
    *result = target;
    return 0;
fail:
    (void)vmspace_destroy(target);
    return error;
}

int
vmspace_validate(const struct vmspace *vmspace)
{
    if (!vmspace_valid(vmspace) ||
        vm_map_validate(&vmspace->vms_map) != 0)
        return EINVAL;
    return pmap_validate(vmspace->vms_pmap);
}

#if defined(KERNEL) && !defined(REBSD_VM_HOST_TEST)
int
vmspace_read_inode(struct vmspace *vmspace, struct inode *inode,
    vm_vaddr_t address, vm_size_t size, off_t offset)
{
    struct vm_page *page;
    unsigned char *physical;
    vm_paddr_t paddr;
    vm_size_t chunk;
    const struct vm_map_entry *entry;
    vm_ooffset_t object_offset;
    int error;

    if (!vmspace_valid(vmspace) || inode == 0 || size == 0 ||
        vmspace_check(vmspace, address, size, VM_PROT_WRITE) != 0)
        return EINVAL;
    while (size != 0) {
        error = vmspace_fault(vmspace, address, VM_PROT_WRITE);
        if (error != 0)
            return error;
        error = pmap_extract(vmspace->vms_pmap, address, &paddr);
        if (error != 0)
            return error;
        page = vm_page_lookup(vmspace_allocator,
            paddr & ~VM_PAGE_MASK);
        if (page == 0)
            return EFAULT;
        physical = (unsigned char *)pmap_page_direct_map(page,
            PMAP_CACHE_CACHED);
        if (physical == 0)
            return EFAULT;
        physical += paddr & VM_PAGE_MASK;
        chunk = VM_PAGE_SIZE - (paddr & VM_PAGE_MASK);
        if (chunk > size)
            chunk = size;
        error = rdwri(UIO_READ, inode, (caddr_t)physical, chunk,
            offset, IO_UNIT, 0);
        if (error != 0)
            return error;
        entry = vm_map_lookup(&vmspace->vms_map, address);
        if (entry == 0)
            return EFAULT;
        object_offset = entry->vme_offset +
            (vm_vaddr_trunc_page(address) - entry->vme_start);
        error = vm_object_mark_dirty(entry->vme_object, object_offset);
        if (error != 0)
            return error;
        if ((entry->vme_protection & VM_PROT_EXECUTE) != 0) {
            error = pmap_page_sync(page,
                PMAP_SYNC_DATA | PMAP_SYNC_INSTRUCTION);
            if (error != 0)
                return error;
        }
        address += chunk;
        offset += chunk;
        size -= chunk;
    }
    return 0;
}

int
vmspace_write_inode(const struct vmspace *vmspace, struct inode *inode,
    vm_vaddr_t address, vm_size_t size, off_t offset)
{
    unsigned char buffer[256];
    vm_size_t chunk;
    int error;

    if (!vmspace_valid(vmspace) || inode == 0)
        return EINVAL;
    if (size == 0)
        return 0;
    if (vmspace_check(vmspace, address, size, VM_PROT_READ) != 0)
        return EFAULT;
    while (size != 0) {
        chunk = size < sizeof(buffer) ? size : sizeof(buffer);
        error = vmspace_read(vmspace, address, buffer, chunk);
        if (error != 0)
            return error;
        error = rdwri(UIO_WRITE, inode, (caddr_t)buffer, chunk,
            offset, IO_UNIT, 0);
        if (error != 0)
            return error;
        address += chunk;
        offset += chunk;
        size -= chunk;
    }
    return 0;
}
#endif
