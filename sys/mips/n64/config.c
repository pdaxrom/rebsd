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
#include <sys/systm.h>
#include <machine/ramswap.h>
#include <machine/video.h>
#ifdef N64_USB_GDB
#include <machine/n64gdb.h>
#endif

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

void
kconfig(void)
{
#ifdef N64_USB_GDB
    /*
     * N64 does not walk conf_device_init, so n64cartdriver.d_init is not a
     * usable board attach hook.  Start the debugger transport here, before
     * the VM bootstrap, so the USB device can enumerate during early boot
     * and remains available when VM initialization itself fails.
     */
    n64_gdb_init();
    printf("n64 gdb: USB %s\n",
        n64_gdb_usb_ready() ? "configured" : "waiting for host");
#endif
}

void
nintendoattach(void)
{
    pipedev = makedev(N64_RAMSWAP_MAJOR, N64_RAMDISK_VAR_MINOR);
}
