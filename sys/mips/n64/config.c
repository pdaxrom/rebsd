#include <sys/param.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/clist.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/dir.h>
#include <sys/namei.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/map.h>
#include <sys/systm.h>
#include <machine/ramswap.h>

int hz = HZ;
int usechz = (1000000L + HZ - 1) / HZ;
struct timezone tz = { 0, 0 };
int nproc = NPROC;

struct namecache namecache[NNAMECACHE];
char bufdata[NBUF * MAXBSIZE];
struct inode inode[NINODE];
struct callout callout[NCALL];
struct mount mount[NMOUNT];
struct buf buf[NBUF], bfreelist[BQUEUES];
struct bufhd bufhash[BUFHSZ];
struct cblock cfree[NCLIST];
struct proc proc[NPROC];
struct file file[NFILE];

int securelevel = 0;
int waittime = -1;

struct mapent swapent[SMAPSIZ];
struct map swapmap[1] = {
    { swapent, &swapent[SMAPSIZ], "swapmap" },
};

void
kconfig(void)
{
}

void
nintendoattach(void)
{
    pipedev = makedev(N64_RAMSWAP_MAJOR, N64_RAMDISK_VAR_MINOR);
}
