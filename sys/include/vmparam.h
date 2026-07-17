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
#define VM_MAXID    13      /* number of valid vm ids */

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
}
#endif

#endif /* _SYS_VMPARAM_H_ */
