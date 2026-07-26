#include <sys/param.h>
#include <sys/buf.h>
#include <sys/callout.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>

dev_t rootdev = NODEV;
int nproc = NPROC;
int securelevel;
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
char runin;
char runout;
