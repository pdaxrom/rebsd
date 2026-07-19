/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
#ifndef _SYS_VMPARAM_H_
#define _SYS_VMPARAM_H_

/*
 * CTL_VM identifiers
 */
#define VM_METER    1       /* struct vmmeter */
#define VM_LOADAVG  2       /* struct loadavg */
#define VM_SWAPMAP  3       /* struct mapent _swapmap[] */
#define VM_SWAPTOTAL 4      /* long: total swap bytes */
#define VM_PHYSPAGES 5      /* long: physical RAM pages */
#define VM_FREEPAGES 6      /* long: allocatable physical pages */
#define VM_RESERVEDPAGES 7  /* long: reserved physical pages */
#define VM_PAGEALLOCS 8     /* long: pages allocated since boot */
#define VM_PAGEFREES 9      /* long: pages freed since boot */
#define VM_PAGEFAILURES 10  /* long: failed allocation requests */
#define VM_PAGEPOISONFAILURES 11 /* long: free-page poison failures */
#define VM_BADPAGES 12      /* long: unusable physical pages */
#define VM_PMAPMAPPINGS 13  /* long: current pmap mappings */
#define VM_PMAPRESIDENT 14  /* long: current resident pmap pages */
#define VM_PMAPREFILLS 15   /* long: TLB refills */
#define VM_PMAPMODIFIED 16  /* long: first-write TLB updates */
#define VM_PMAPFAULTS 17    /* long: pmap protection faults */
#define VM_PMAPTARGETED 18  /* long: targeted TLB invalidations */
#define VM_PMAPFLUSHES 19   /* long: full non-wired TLB flushes */
#define VM_PMAPROLLOVERS 20 /* long: ASID generation rollovers */
#define VM_OBJECTS 21       /* long: current VM objects */
#define VM_ANONPAGES 22     /* long: anonymous page descriptors */
#define VM_OBJECTRESIDENT 23 /* long: resident object pages */
#define VM_OBJECTSWAPPED 24 /* long: swapped object pages */
#define VM_ZEROFAULTS 25    /* long: demand-zero faults */
#define VM_COWFAULTS 26     /* long: copy-on-write faults */
#define VM_PAGEINS 27       /* long: swap pager page-ins */
#define VM_PAGEOUTS 28      /* long: swap pager page-outs */
#define VM_SWAPFAILURES 29  /* long: swap pager I/O failures */
#define VM_SHMOBJECTS 30    /* long: current POSIX and SysV objects */
#define VM_SHMPAGES 31      /* long: logical shared-memory pages */
#define VM_SHMMAPPINGS 32   /* long: current process SHM mappings */
#define VM_SHMMAXOBJECTS 33 /* long: namespace object limit */
#define VM_SHMMAXPAGES 34   /* long: maximum pages per object */
#define VM_SHMMAXMAPPINGS 35 /* long: per-process VM map limit */
#define VM_SYSVSEGMENTS 36  /* long: current SysV segments */
#define VM_SYSVATTACHMENTS 37 /* long: current SysV attachments */
#define VM_OBJECTFAULTS 38  /* long: object fault resolutions */
#define VM_OBJECTWAITS 39   /* long: waits for busy object pages */
#define VM_FAULTWOULDBLOCK 40 /* long: non-sleeping faults rejected */
#define VM_RECLAIMATTEMPTS 41 /* long: bounded reclaim passes */
#define VM_RECLAIMFAILURES 42 /* long: reclaim passes without progress */
#define VM_UCBSTATS 43      /* struct kinfo_ucb_stats: legacy counters */
#define VM_UCBRESET 44      /* int: reset legacy cumulative counters */
#define VM_MAXID    45      /* number of valid vm ids */

#ifndef KERNEL
#define CTL_VM_NAMES { \
    { 0, 0 }, \
    { "vmmeter", CTLTYPE_STRUCT }, \
    { "loadavg", CTLTYPE_STRUCT }, \
    { "swapmap", CTLTYPE_STRUCT }, \
    { "swap_total", CTLTYPE_LONG }, \
    { "page_total", CTLTYPE_LONG }, \
    { "page_free", CTLTYPE_LONG }, \
    { "page_reserved", CTLTYPE_LONG }, \
    { "page_allocs", CTLTYPE_LONG }, \
    { "page_frees", CTLTYPE_LONG }, \
    { "page_failures", CTLTYPE_LONG }, \
    { "page_poison_failures", CTLTYPE_LONG }, \
    { "page_bad", CTLTYPE_LONG }, \
    { "pmap_mappings", CTLTYPE_LONG }, \
    { "pmap_resident", CTLTYPE_LONG }, \
    { "pmap_refills", CTLTYPE_LONG }, \
    { "pmap_modified", CTLTYPE_LONG }, \
    { "pmap_faults", CTLTYPE_LONG }, \
    { "pmap_targeted", CTLTYPE_LONG }, \
    { "pmap_flushes", CTLTYPE_LONG }, \
    { "pmap_rollovers", CTLTYPE_LONG }, \
    { "objects", CTLTYPE_LONG }, \
    { "anon_pages", CTLTYPE_LONG }, \
    { "object_resident", CTLTYPE_LONG }, \
    { "object_swapped", CTLTYPE_LONG }, \
    { "zero_faults", CTLTYPE_LONG }, \
    { "cow_faults", CTLTYPE_LONG }, \
    { "pageins", CTLTYPE_LONG }, \
    { "pageouts", CTLTYPE_LONG }, \
    { "swap_failures", CTLTYPE_LONG }, \
    { "shm_objects", CTLTYPE_LONG }, \
    { "shm_pages", CTLTYPE_LONG }, \
    { "shm_mappings", CTLTYPE_LONG }, \
    { "shm_max_objects", CTLTYPE_LONG }, \
    { "shm_max_pages", CTLTYPE_LONG }, \
    { "shm_max_mappings", CTLTYPE_LONG }, \
    { "sysv_segments", CTLTYPE_LONG }, \
    { "sysv_attachments", CTLTYPE_LONG }, \
    { "object_faults", CTLTYPE_LONG }, \
    { "object_waits", CTLTYPE_LONG }, \
    { "fault_wouldblock", CTLTYPE_LONG }, \
    { "reclaim_attempts", CTLTYPE_LONG }, \
    { "reclaim_failures", CTLTYPE_LONG }, \
    { "ucb_stats", CTLTYPE_STRUCT }, \
    { "ucb_reset", CTLTYPE_INT }, \
}
#endif

#endif /* _SYS_VMPARAM_H_ */
