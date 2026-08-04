/*
 * Machine-independent kernel storage sized by the selected configuration.
 *
 * Every architecture uses these same tables.  The board configuration owns
 * only hardware attachment; it must not provide private process, file,
 * inode, buffer-cache, callout, mount, clock, or security storage.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/callout.h>
#include <sys/clist.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>

int hz = HZ;
int usechz = (1000000L + HZ - 1) / HZ;
struct timezone tz = { 0, 0 };
int nproc = NPROC;
int securelevel;
int waittime = -1;

struct namecache namecache[NNAMECACHE];
char bufdata[NBUF * MAXBSIZE];
struct file file[NFILE];
struct inode inode[NINODE];
const u_int inode_table_stride = sizeof(inode[0]);
struct callout callout[NCALL];
struct mount mount[NMOUNT];
struct buf buf[NBUF], bfreelist[BQUEUES];
struct bufhd bufhash[BUFHSZ];
struct proc proc[NPROC];
struct cblock cfree[NCLIST];
