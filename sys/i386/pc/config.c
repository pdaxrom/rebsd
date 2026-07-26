#include <sys/param.h>
#include <sys/buf.h>
#include <sys/callout.h>
#include <sys/clist.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include "disk_bootstrap.h"
#include "ide.h"
#include "pci.h"
#include "usb_pci.h"

int nproc = NPROC;
int securelevel;
int waittime = -1;
int hz = HZ;
int usechz = (1000000L + HZ - 1) / HZ;
struct timezone tz = { 0, 0 };
struct namecache namecache[NNAMECACHE];
char bufdata[NBUF * MAXBSIZE];
struct file file[NFILE];
struct inode inode[NINODE];
struct callout callout[NCALL];
struct mount mount[NMOUNT];
struct buf buf[NBUF], bfreelist[BQUEUES];
struct bufhd bufhash[BUFHSZ];
struct proc proc[NPROC];
struct cblock cfree[NCLIST];
struct mapent swapent[SMAPSIZ];
struct map swapmap[1] = {
    { swapent, &swapent[SMAPSIZ], "swapmap" },
};

void
kconfig(void)
{
}

void
pcattach(int unit)
{
    int error;

    (void)unit;
    error = i386_pci_probe();
    if (error != 0) {
        printf("pci: probe failed, error=%d\n", error);
        return;
    }
    if (i386_ide_probe()) {
        error = i386_disk_attach_ide();
        if (error != 0)
            printf("ide0: disk attach failed, error=%d\n", error);
    }
    error = i386_usb_pci_prepare();
    if (error != 0 && error != ENXIO)
        printf("usb-pci: preparation failed, error=%d\n", error);
    i386_pci_report_summary();
}
