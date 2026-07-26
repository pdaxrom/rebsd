TOPSRC		= $(shell cd ../../../..; pwd)
include $(TOPSRC)/target.mk

ASFLAGS		+= $(DEFS)
CFLAGS		+= -Os

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
		  reboot.o recv.o recvfrom.o recvmsg.o rename.o rmdir.o select.o \
		  send.o sendmsg.o sendto.o setgroups.o setitimer.o setpgrp.o \
		  setpriority.o setquota.o setuid.o seteuid.o setgid.o setegid.o \
		  setrlimit.o setsockopt.o settimeofday.o shutdown.o \
		  sigaltstack.o socket.o socketpair.o symlink.o sigprocmask.o \
		  sigstack.o sigwait.o statfs.o fstatfs.o getfsstat.o umount.o \
		  unlink.o utimes.o wait4.o write.o writev.o sigsuspend.o \
		  getgid.o getegid.o getpid.o getppid.o getuid.o geteuid.o \
		  profil.o sigpending.o sync.o ufetch.o ustore.o ucall.o umask.o \
		  vfork.o vhangup.o rdglob.o wrglob.o msec.o kmemdev.o

WIDEOBJS	= stat.o lstat.o fstat.o mmapraw.o lseek64w.o trunc64w.o \
		  ftrunc64w.o preadw.o pwritew.o

OBJS		= $(ASMOBJS) $(SYSOBJS) $(WIDEOBJS)

all:		sys.a

sys.a:		$(OBJS)
		$(AR) cru $@ $(OBJS)

$(SYSOBJS):	SYS.h
		@printf '#include "SYS.h"\nSYS($*)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

mmapraw.o:	SYS.h
		@printf '#include "SYS.h"\nSYS8(__mmap_words,mmap)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

lseek64w.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__lseek64_words,lseek64)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

trunc64w.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__truncate64_words,truncate64)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

ftrunc64w.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__ftruncate64_words,ftruncate64)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

preadw.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__pread_words,pread)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

pwritew.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(__pwrite_words,pwrite)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

stat.o:		SYS.h
		@printf '#include "SYS.h"\nSYS2(stat,stat64)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

lstat.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(lstat,lstat64)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

fstat.o:	SYS.h
		@printf '#include "SYS.h"\nSYS2(fstat,fstat64)\n' | \
		    $(CC) $(CFLAGS) $(ASFLAGS) -x assembler-with-cpp -c - -o $@

clean:
		rm -f *~ *.o a.out sys.a
