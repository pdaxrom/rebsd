/*-
 * Copyright (c) 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Modified for 2.11BSD.  The ReBSD PCC frontend accepts old-style
 * system declarations more reliably than full prototypes, even when the
 * driver enables GCC compatibility defines.  Expose K&R declarations to
 * PCC and typed prototypes to compilers that can parse them.
*/

#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <sys/types.h>

#ifdef __PCC__
#define __UNISTD_P(args)        ()
#define __UNISTD_CONST
#else
#define __UNISTD_P(args)        args
#define __UNISTD_CONST          const
#endif

#define STDIN_FILENO    0       /* standard input file descriptor */
#define STDOUT_FILENO   1       /* standard output file descriptor */
#define STDERR_FILENO   2       /* standard error file descriptor */

#ifndef NULL
#define NULL            0       /* null pointer constant */
#endif

/* Values for the second argument to access.
   These may be OR'd together.  */
#define R_OK            4       /* Test for read permission.  */
#define W_OK            2       /* Test for write permission.  */
#define X_OK            1       /* Test for execute permission.  */
#define F_OK            0       /* Test for existence.  */

void    _exit __UNISTD_P((int));
int     access __UNISTD_P((const char *pathname, int mode));
unsigned int alarm __UNISTD_P((unsigned));
pid_t   fork __UNISTD_P((void));
pid_t   setsid __UNISTD_P((void));
gid_t   getegid __UNISTD_P((void));
uid_t   geteuid __UNISTD_P((void));
gid_t   getgid __UNISTD_P((void));
char    *getlogin __UNISTD_P((void));
int     setlogin __UNISTD_P((const char *name));
pid_t   getpgrp __UNISTD_P((void));
pid_t   getpid __UNISTD_P((void));
pid_t   getppid __UNISTD_P((void));
uid_t   getuid __UNISTD_P((void));
off_t   lseek __UNISTD_P((int fd, off_t offset, int whence));
ssize_t read __UNISTD_P((int fd, void *buf, size_t count));
unsigned int sleep __UNISTD_P((unsigned int seconds));
char    *ttyname __UNISTD_P((int fd));
ssize_t write __UNISTD_P((int fd, const void *buf, size_t count));
int     truncate __UNISTD_P((const char *path, off_t length));
int     ftruncate __UNISTD_P((int fd, off_t length));

void    *brk __UNISTD_P((const void *addr));
int     _brk __UNISTD_P((const void *addr));
char    *crypt __UNISTD_P((const char *phrase, const char *setting));
void    endusershell __UNISTD_P((void));
long    gethostid __UNISTD_P((void));
char    *getpass __UNISTD_P((char *prompt));
char    *getusershell __UNISTD_P((void));
char    *getwd __UNISTD_P((char buf[]));
void    psignal __UNISTD_P((int sig, const char *s));
extern  char    *sys_siglist[];
char    *re_comp __UNISTD_P((const char *regex));
void    *sbrk __UNISTD_P((int incr));
int     sethostid __UNISTD_P((long hostid));
void    setusershell __UNISTD_P((void));
void    sync __UNISTD_P((void));
unsigned int ualarm __UNISTD_P((unsigned usecs, unsigned interval));
void    usleep __UNISTD_P((unsigned));
int     pause __UNISTD_P((void));
pid_t   vfork __UNISTD_P((void));

int     pipe __UNISTD_P((int pipefd[2]));
int     close __UNISTD_P((int fd));
int     dup __UNISTD_P((int oldfd));
int     dup2 __UNISTD_P((int oldfd, int newfd));
int     unlink __UNISTD_P((const char *pathname));
int     link __UNISTD_P((const char *oldpath, const char *newpath));
ssize_t readlink __UNISTD_P((const char *path, char *buf, size_t bufsiz));
int     chown __UNISTD_P((const char *path, uid_t owner, gid_t group));
int     fchown __UNISTD_P((int fd, uid_t owner, gid_t group));
int     nice __UNISTD_P((int inc));
int     setuid __UNISTD_P((uid_t uid));
int     setgid __UNISTD_P((gid_t gid));
int     seteuid __UNISTD_P((uid_t euid));
int     setegid __UNISTD_P((gid_t egid));
int     setreuid __UNISTD_P((uid_t ruid, uid_t euid));
int     setregid __UNISTD_P((gid_t rgid, gid_t egid));
int     setpgrp __UNISTD_P((void));
int     isatty __UNISTD_P((int fd));
int     chdir __UNISTD_P((const char *path));
int     fchdir __UNISTD_P((int fd));
int     chflags __UNISTD_P((const char *path, u_long flags));
int     fchflags __UNISTD_P((int fd, u_long flags));
int     getgroups __UNISTD_P((int size, gid_t list[]));
int     getdtablesize __UNISTD_P((void));
int     rmdir __UNISTD_P((const char *pathname));

struct stat;
int     stat __UNISTD_P((const char *path, struct stat *buf));
int     fstat __UNISTD_P((int fd, struct stat *buf));
int     lstat __UNISTD_P((const char *path, struct stat *buf));

int     execl __UNISTD_P((const char *path, const char *arg0, ... /* NULL */));
int     execle __UNISTD_P((const char *path, const char *arg0, ... /* NULL, char *envp[] */));
int     execlp __UNISTD_P((const char *file, const char *arg0, ... /* NULL */));

int     execv __UNISTD_P((const char *path, char *const argv[]));
int     execve __UNISTD_P((const char *path, char *const arg0[], char *const envp[]));
int     execvp __UNISTD_P((const char *file, char *const argv[]));

extern  char    **environ;              /* Environment, from crt0. */
extern  char    *__progname;            /* Program name, from crt0. */

int     getopt __UNISTD_P((int argc, char * const argv[], const char *optstring));

extern  char    *optarg;                /* getopt(3) external variables */
extern  int     opterr, optind, optopt;

int     gethostname __UNISTD_P((char *name, int namelen));
int     sethostname __UNISTD_P((char *name, int namelen));

int     chroot __UNISTD_P((const char *path));
int     fsync __UNISTD_P((int fd));
int     getpagesize __UNISTD_P((void));
int     symlink __UNISTD_P((const char *target, const char *linkpath));
int     vhangup __UNISTD_P((void));
int     mknod __UNISTD_P((const char *, mode_t, dev_t));
int     reboot __UNISTD_P((int howto));
int     ttyslot __UNISTD_P((void));

#ifndef _VA_LIST_
# ifdef __GNUC__
#  define va_list   __builtin_va_list   /* For Gnu C */
# endif
# ifdef __SMALLER_C__
#  define va_list   char *              /* For Smaller C */
# endif
#endif

void    err __UNISTD_P((int eval, const char *fmt, ...));
void    errx __UNISTD_P((int eval, const char *fmt, ...));
void    warn __UNISTD_P((const char *fmt, ...));
void    warnx __UNISTD_P((const char *fmt, ...));
void    verr __UNISTD_P((int eval, const char *fmt, va_list ap));
void    verrx __UNISTD_P((int eval, const char *fmt, va_list ap));
void    vwarn __UNISTD_P((const char *fmt, va_list ap));
void    vwarnx __UNISTD_P((const char *fmt, va_list ap));

#ifndef _VA_LIST_
# undef va_list
#endif
#undef __UNISTD_CONST
#undef __UNISTD_P
#endif /* !_UNISTD_H_ */
