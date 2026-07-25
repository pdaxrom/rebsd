/*
 * Bootstrap prefix of the production syscall table.
 *
 * Keep the argument counts and numbers in sync with kernel/init_sysent.c.
 * Entries whose subsystems are not linked into the early i386 image return
 * ENOSYS.  The table grows toward the full generic table as those subsystems
 * enter the image.
 */
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>

void
nosys(void)
{
    u.u_error = ENOSYS;
}

const struct sysent sysent[] = {
    { 1, nosys },               /*   0 = out-of-range */
    { 1, rexit },               /*   1 = exit */
    { 0, fork },                /*   2 = fork */
    { 3, read },                /*   3 = read */
    { 3, nosys },               /*   4 = write */
    { 3, open },                /*   5 = open */
    { 1, close },               /*   6 = close */
    { 4, wait4 },               /*   7 = wait4 */
    { 0, nosys },               /*   8 = old creat */
    { 2, nosys },               /*   9 = link */
    { 1, nosys },               /*  10 = unlink */
    { 2, nosys },               /*  11 = execv */
    { 1, nosys },               /*  12 = chdir */
    { 1, nosys },               /*  13 = fchdir */
    { 3, nosys },               /*  14 = mknod */
    { 2, nosys },               /*  15 = chmod */
    { 3, nosys },               /*  16 = chown */
    { 2, nosys },               /*  17 = chflags */
    { 2, nosys },               /*  18 = fchflags */
    { 4, lseek },               /*  19 = lseek */
    { 0, getpid },              /*  20 = getpid */
};

const int nsysent = sizeof(sysent) / sizeof(sysent[0]);
