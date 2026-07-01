#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

#ifdef __cplusplus
#define __BEGIN_DECLS  extern "C" {
#define __END_DECLS    }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#if defined(__STDC__) || defined(__cplusplus)
#define __P(protos)    protos
#else
#define __P(protos)    ()
#endif

#if defined(__GNUC__) || defined(__PCC__)
#define __dead         __attribute__((__noreturn__))
#define __pure         __attribute__((__const__))
#define __unused       __attribute__((__unused__))
#define __packed       __attribute__((__packed__))
#define __aligned(x)   __attribute__((__aligned__(x)))
#define __section(x)   __attribute__((__section__(x)))
#define __printflike(fmtarg, firstvararg) \
        __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define __scanflike(fmtarg, firstvararg) \
        __attribute__((__format__(__scanf__, fmtarg, firstvararg)))
#else
#define __dead
#define __pure
#define __unused
#define __packed
#define __aligned(x)
#define __section(x)
#define __printflike(fmtarg, firstvararg)
#define __scanflike(fmtarg, firstvararg)
#endif

#ifndef __CONCAT
#define __CONCAT(x, y) x ## y
#endif

#ifndef __STRING
#define __STRING(x)    #x
#endif

#ifndef __const
#define __const        const
#endif

#endif /* _SYS_CDEFS_H_ */
