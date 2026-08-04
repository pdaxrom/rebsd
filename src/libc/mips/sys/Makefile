TOPSRC		= $(shell cd ../../../..; pwd)
include $(TOPSRC)/target.mk

ASFLAGS		+= ${DEFS}
ASFLAGS         += $(if $(REBSD_SRCDIR),-I$(REBSD_SRCDIR),)
CFLAGS          += -Os

# modules which can not use SYSCALL and must be assembled from sources.  The
# rest of the system calls are generated with printf(1) and do not have
# source files associated with them.

ASMOBJS		= _exit.o _brk.o getpgrp.o pipe.o ptrace.o sigaction.o

SYSOBJS		= __sysctl.o accept.o access.o adjtime.o bind.o chdir.o \
		  chflags.o chmod.o chown.o chroot.o close.o connect.o dup.o \
		  dup2.o execve.o fchdir.o fchflags.o fchmod.o fchown.o \
		  fcntl.o flock.o fork.o fsync.o \
		  getdtablesize.o getgroups.o getitimer.o getsockname.o \
		  getpeername.o getpriority.o getrlimit.o getrusage.o getsockopt.o \
		  gettimeofday.o ioctl.o kill.o killpg.o link.o listen.o mkdir.o \
		  mknod.o mount.o open.o pselect.o quota.o read.o readlink.o readv.o \
		  munmap.o mprotect.o msync.o madvise.o mincore.o mlock.o munlock.o \
		  shm_open.o shm_unlink.o shmget.o shmat.o shmdt.o shmctl.o \
		  reboot.o \
		  recv.o recvfrom.o recvmsg.o rename.o rmdir.o select.o send.o sendmsg.o \
		  sendto.o setgroups.o setitimer.o setpgid.o setpriority.o setquota.o \
		  setuid.o seteuid.o setgid.o setegid.o \
		  setrlimit.o setsockopt.o settimeofday.o shutdown.o \
		  sigaltstack.o socket.o socketpair.o symlink.o \
		  sigprocmask.o sigstack.o sigwait.o \
		  statfs.o fstatfs.o getfsstat.o \
		  umount.o unlink.o utimes.o wait4.o write.o writev.o swapon.o \
		  swapoff.o \
		  sigsuspend.o \
		  getgid.o getegid.o getpid.o \
		  getppid.o getuid.o geteuid.o profil.o sigpending.o sync.o \
                  ufetch.o ustore.o ucall.o umask.o vfork.o vhangup.o \
		  rdglob.o wrglob.o msec.o kmemdev.o

WIDEOBJS	= stat.o lstat.o fstat.o mmapraw.o lseek64w.o trunc64w.o \
		  ftrunc64w.o preadw.o pwritew.o

OBJS		= ${ASMOBJS} ${SYSOBJS} ${WIDEOBJS}

TAGSFILE	= tags

sys.a:		${OBJS}
		@echo "building sys.a"
		@$(AR) cru sys.a ${OBJS}

${SYSOBJS}:	SYS.h
		@echo creating $*.o
		@printf '#include "SYS.h"\nSYS($*)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

mmapraw.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__mmap_words,mmap)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

lseek64w.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__lseek64_words,lseek64)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

trunc64w.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__truncate64_words,truncate64)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

ftrunc64w.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__ftruncate64_words,ftruncate64)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

preadw.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__pread_words,pread)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

pwritew.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__pwrite_words,pwrite)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

stat.o:		SYS.h
		@printf '#include "SYS.h"\nSYS(stat)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

lstat.o:	SYS.h
		@printf '#include "SYS.h"\nSYS(lstat)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

fstat.o:	SYS.h
		@printf '#include "SYS.h"\nSYS(fstat)\n' | $(AS) ${ASFLAGS} - -c -o $*.o

clean:
		rm -f *~ *.o a.out sys.a
