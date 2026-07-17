#ifndef _NETSTAT_LOCAL_H_
#define _NETSTAT_LOCAL_H_

#include <sys/types.h>

/*
 * ReBSD/MIPS uses 32-bit kernel pointers even though off_t is 64-bit.
 * Keep live-kernel addresses in their native width and widen them only at
 * the lseek(2) boundary.  This also keeps the MIPS o32 calling convention
 * from treating a kernel address as a two-register long long argument.
 */
typedef u_int kaddr_t;

typedef char netstat_kaddr_must_match_pointer[
    sizeof(kaddr_t) == sizeof(void *) ? 1 : -1];

#define KADDR(pointer) ((kaddr_t)(u_long)(pointer))

extern int kmem;

off_t klseek(int fd, kaddr_t address, int whence);
int kread(kaddr_t address, void *buffer, size_t length, char *description);

void intpr(int interval, kaddr_t ifnet_address);
void sidewaysintpr(unsigned interval, kaddr_t ifnet_address);
void mbpr(kaddr_t mbstat_address);
void protopr(kaddr_t pcb_address, char *name);
void tcp_stats(kaddr_t address, char *name);
void udp_stats(kaddr_t address, char *name);
void ip_stats(kaddr_t address, char *name);
void icmp_stats(kaddr_t address, char *name);
void routepr(kaddr_t host_address, kaddr_t net_address,
    kaddr_t hashsize_address);
void rt_stats(kaddr_t address);
void unixpr(kaddr_t file_address, kaddr_t unixsw_address);

#endif
