#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/dk.h>
#include <sys/file.h>
#include <sys/inode.h>
#include <sys/kernel.h>
#include <sys/map.h>
#include <sys/namei.h>
#ifdef PTY_ENABLED
#include <sys/pty.h>
#endif
#if defined(INET) || defined(UNIXDOMAIN)
#include <sys/domain.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#endif
#include <sys/ptrace.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/vm.h>
#include <sys/sysctl.h>
#ifdef INET
#include <net/if.h>
#include <net/route.h>
#include <net/raw_cb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

extern int rthashsize;
#endif
#ifdef UNIXDOMAIN
extern struct protosw unixsw[];
#endif
#include <machine/cpu.h>

extern struct tty cnttys[];

/*
 * Errno messages used by libc strerror(3) through machdep.errmsg.
 */
static const char *errlist[] = {
    "Undefined error: 0",                   /*  0 - ENOERROR */
    "Operation not permitted",              /*  1 - EPERM */
    "No such file or directory",            /*  2 - ENOENT */
    "No such process",                      /*  3 - ESRCH */
    "Interrupted system call",              /*  4 - EINTR */
    "Input/output error",                   /*  5 - EIO */
    "Device not configured",                /*  6 - ENXIO */
    "Argument list too long",               /*  7 - E2BIG */
    "Exec format error",                    /*  8 - ENOEXEC */
    "Bad file descriptor",                  /*  9 - EBADF */
    "No child processes",                   /* 10 - ECHILD */
    "No more processes",                    /* 11 - EAGAIN */
    "Cannot allocate memory",               /* 12 - ENOMEM */
    "Permission denied",                    /* 13 - EACCES */
    "Bad address",                          /* 14 - EFAULT */
    "Block device required",                /* 15 - ENOTBLK */
    "Device busy",                          /* 16 - EBUSY */
    "File exists",                          /* 17 - EEXIST */
    "Cross-device link",                    /* 18 - EXDEV */
    "Operation not supported by device",    /* 19 - ENODEV */
    "Not a directory",                      /* 20 - ENOTDIR */
    "Is a directory",                       /* 21 - EISDIR */
    "Invalid argument",                     /* 22 - EINVAL */
    "Too many open files in system",        /* 23 - ENFILE */
    "Too many open files",                  /* 24 - EMFILE */
    "Inappropriate ioctl for device",       /* 25 - ENOTTY */
    "Text file busy",                       /* 26 - ETXTBSY */
    "File too large",                       /* 27 - EFBIG */
    "No space left on device",              /* 28 - ENOSPC */
    "Illegal seek",                         /* 29 - ESPIPE */
    "Read-only file system",                /* 30 - EROFS */
    "Too many links",                       /* 31 - EMLINK */
    "Broken pipe",                          /* 32 - EPIPE */
    "Numerical argument out of domain",     /* 33 - EDOM */
    "Result too large",                     /* 34 - ERANGE */
    "Resource temporarily unavailable",     /* 35 - EWOULDBLOCK */
    "Operation now in progress",            /* 36 - EINPROGRESS */
    "Operation already in progress",        /* 37 - EALREADY */
    "Socket operation on non-socket",       /* 38 - ENOTSOCK */
    "Destination address required",         /* 39 - EDESTADDRREQ */
    "Message too long",                     /* 40 - EMSGSIZE */
    "Protocol wrong type for socket",       /* 41 - EPROTOTYPE */
    "Protocol not available",               /* 42 - ENOPROTOOPT */
    "Protocol not supported",               /* 43 - EPROTONOSUPPORT */
    "Socket type not supported",            /* 44 - ESOCKTNOSUPPORT */
    "Operation not supported",              /* 45 - EOPNOTSUPP */
    "Protocol family not supported",        /* 46 - EPFNOSUPPORT */
    "Address family not supported by protocol family", /* 47 - EAFNOSUPPORT */
    "Address already in use",               /* 48 - EADDRINUSE */
    "Can't assign requested address",       /* 49 - EADDRNOTAVAIL */
    "Network is down",                      /* 50 - ENETDOWN */
    "Network is unreachable",               /* 51 - ENETUNREACH */
    "Network dropped connection on reset",  /* 52 - ENETRESET */
    "Software caused connection abort",     /* 53 - ECONNABORTED */
    "Connection reset by peer",             /* 54 - ECONNRESET */
    "No buffer space available",            /* 55 - ENOBUFS */
    "Socket is already connected",          /* 56 - EISCONN */
    "Socket is not connected",              /* 57 - ENOTCONN */
    "Can't send after socket shutdown",     /* 58 - ESHUTDOWN */
    "Too many references: can't splice",    /* 59 - ETOOMANYREFS */
    "Operation timed out",                  /* 60 - ETIMEDOUT */
    "Connection refused",                   /* 61 - ECONNREFUSED */
    "Too many levels of symbolic links",    /* 62 - ELOOP */
    "File name too long",                   /* 63 - ENAMETOOLONG */
    "Host is down",                         /* 64 - EHOSTDOWN */
    "No route to host",                     /* 65 - EHOSTUNREACH */
    "Directory not empty",                  /* 66 - ENOTEMPTY */
    "Too many processes",                   /* 67 - EPROCLIM */
    "Too many users",                       /* 68 - EUSERS */
    "Disc quota exceeded",                  /* 69 - EDQUOT */
    "Stale NFS file handle",                /* 70 - ESTALE */
    "Too many levels of remote in path",    /* 71 - EREMOTE */
    "RPC struct is bad",                    /* 72 - EBADRPC */
    "RPC version wrong",                    /* 73 - ERPCMISMATCH */
    "RPC prog. not avail",                  /* 74 - EPROGUNAVAIL */
    "Program version wrong",                /* 75 - EPROGMISMATCH */
    "Bad procedure for program",            /* 76 - EPROCUNAVAIL */
    "No locks available",                   /* 77 - ENOLCK */
    "Function not implemented",             /* 78 - ENOSYS */
    "Inappropriate file type or format",    /* 79 - EFTYPE */
    "Authentication error",                 /* 80 - EAUTH */
    "Need authenticator",                   /* 81 - ENEEDAUTH */
    "Value too large to be stored",         /* 82 - EOVERFLOW */
};

/*
 * Kernel symbol name list for historical diagnostics using knlist(3).
 * These tools still read selected kernel tables through /dev/kmem.
 */
static const struct {
    const char *name;
    int addr;
} nlist[] = {
    { "_boottime",      (int)&boottime      },  /* vmstat */
    { "_cnttys",        (int)&cnttys        },  /* pstat */
    { "_cp_time",       (int)&cp_time       },  /* vmstat */
#ifdef UCB_METER
    { "_dk_busy",       (int)&dk_busy       },  /* iostat */
    { "_dk_name",       (int)&dk_name       },  /* vmstat */
    { "_dk_ndrive",     (int)&dk_ndrive     },  /* vmstat */
    { "_dk_unit",       (int)&dk_unit       },  /* vmstat */
    { "_dk_bytes",      (int)&dk_bytes      },  /* iostat */
    { "_dk_xfer",       (int)&dk_xfer       },  /* vmstat */
#endif
    { "_file",          (int)&file          },  /* pstat */
    { "_forkstat",      (int)&forkstat      },  /* vmstat */
#ifdef INET
    { "_icmpstat",      (int)&icmpstat      },  /* netstat */
    { "_ifnet",         (int)&ifnet         },  /* netstat */
    { "_ipstat",        (int)&ipstat        },  /* netstat */
#endif
#ifdef UCB_METER
    { "_freemem",       (int)&freemem       },  /* vmstat */
#endif
    { "_hz",            (int)&hz            },  /* ps */
    { "_inode",         (int)&inode         },  /* pstat */
    { "_ipc",           (int)&ipc           },  /* ps */
    { "_lbolt",         (int)&lbolt         },  /* ps */
    { "_memlock",       (int)&memlock       },  /* ps */
    { "_nchstats",      (int)&nchstats      },  /* vmstat */
    { "_nproc",         (int)&nproc         },  /* ps, pstat */
    { "_nswap",         (int)&nswap         },  /* pstat */
    { "_proc",          (int)&proc          },  /* ps, pstat */
#ifdef UCB_METER
    { "_rate",          (int)&rate          },  /* vmstat */
#endif
    { "_runin",         (int)&runin         },  /* ps */
    { "_runout",        (int)&runout        },  /* ps */
    { "_selwait",       (int)&selwait       },  /* ps */
#ifdef UCB_METER
    { "_sum",           (int)&sum           },  /* vmstat */
#endif
    { "_swapmap",       (int)&swapmap       },  /* pstat */
    { "_tk_nin",        (int)&tk_nin        },  /* iostat */
    { "_tk_nout",       (int)&tk_nout       },  /* iostat */
    { "_total",         (int)&total         },  /* vmstat */
    { "_u",             0                   },  /* current u area */
#ifdef PTY_ENABLED
    { "_npty",          (int)&npty          },  /* pstat */
    { "_pt_tty",        (int)&pt_tty        },  /* pstat */
#endif
#ifdef INET
    { "_mbstat",        (int)&mbstat        },  /* netstat */
    { "_rawcb",         (int)&rawcb         },  /* netstat */
    { "_rthashsize",    (int)&rthashsize    },  /* netstat */
    { "_rthost",        (int)&rthost        },  /* netstat */
    { "_rtnet",         (int)&rtnet         },  /* netstat */
    { "_rtstat",        (int)&rtstat        },  /* netstat */
    { "_tcb",           (int)&tcb           },  /* netstat */
    { "_tcpstat",       (int)&tcpstat       },  /* netstat */
    { "_udb",           (int)&udb           },  /* netstat */
    { "_udpstat",       (int)&udpstat       },  /* netstat */
#endif
#ifdef UNIXDOMAIN
    { "_unixsw",        (int)&unixsw        },  /* netstat */
#endif
    { "_bdevsw",        (int)&bdevsw        },  /* devupdate */
    { "_cdevsw",        (int)&cdevsw        },  /* devupdate */
    { "_nblkdev",       (int)&nblkdev       },  /* devupdate */
    { "_nchrdev",       (int)&nchrdev       },  /* devupdate */
    { 0, 0 },
};

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
    int i, value;
    dev_t dev;

    switch (name[0]) {
    case CPU_CONSDEV:
        if (namelen != 1)
            return ENOTDIR;
        dev = makedev(0, CONS_MINOR);
        return sysctl_rdstruct(oldp, oldlenp, newp, &dev, sizeof(dev));
    case CPU_ERRMSG:
        if (namelen != 2)
            return ENOTDIR;
        if (name[1] < 1 ||
            (unsigned)name[1] >= sizeof(errlist) / sizeof(errlist[0]))
            return EOPNOTSUPP;
        return sysctl_string(oldp, oldlenp, 0, 0,
            (char *)errlist[name[1]], 1 + strlen(errlist[name[1]]));
    case CPU_NLIST:
        for (i = 0; nlist[i].name; i++) {
            if (strncmp(newp, nlist[i].name, newlen) == 0) {
                value = nlist[i].addr;
                if (value == 0 && strncmp(nlist[i].name, "_u", 3) == 0)
                    value = (int)md_curuser;
                if (!oldp)
                    return 0;
                if (*oldlenp < sizeof(value))
                    return ENOMEM;
                *oldlenp = sizeof(value);
                return copyout((caddr_t)&value, (caddr_t)oldp,
                    sizeof(value));
            }
        }
        return EOPNOTSUPP;
    default:
        return md_sysctl(name, namelen, oldp, oldlenp, newp, newlen);
    }
}
