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
#define __used         __attribute__((__used__))
#define __dead         __attribute__((__noreturn__))
#define __pure         __attribute__((__const__))
#define __pure2        __attribute__((__pure__))
#define __unused       __attribute__((__unused__))
#define __packed       __attribute__((__packed__))
#define __aligned(x)   __attribute__((__aligned__(x)))
#define __section(x)   __attribute__((__section__(x)))
#define __malloclike   __attribute__((__malloc__))
#define __noinline     __attribute__((__noinline__))
#define __always_inline __inline __attribute__((__always_inline__))
#define __printflike(fmtarg, firstvararg) \
        __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define __scanflike(fmtarg, firstvararg) \
        __attribute__((__format__(__scanf__, fmtarg, firstvararg)))
#define __predict_true(exp)  __builtin_expect(!!(exp), 1)
#define __predict_false(exp) __builtin_expect(!!(exp), 0)
#else
#define __used
#define __dead
#define __pure
#define __pure2
#define __unused
#define __packed
#define __aligned(x)
#define __section(x)
#define __malloclike
#define __noinline
#define __always_inline
#define __printflike(fmtarg, firstvararg)
#define __scanflike(fmtarg, firstvararg)
#define __predict_true(exp)  (exp)
#define __predict_false(exp) (exp)
#endif

#if defined(__PCC__)
#define __bounded(args) __attribute__((__bounded__ args))
#else
#define __bounded(args)
#endif

#ifndef __GNUC_PREREQ__
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define __GNUC_PREREQ__(maj, min) \
        ((__GNUC__ > (maj)) || (__GNUC__ == (maj) && __GNUC_MINOR__ >= (min)))
#else
#define __GNUC_PREREQ__(maj, min) 0
#endif
#endif

#ifndef __PCC_PREREQ__
#if defined(__PCC__) && defined(__PCC_MINOR__)
#define __PCC_PREREQ__(maj, min) \
        ((__PCC__ > (maj)) || (__PCC__ == (maj) && __PCC_MINOR__ >= (min)))
#else
#define __PCC_PREREQ__(maj, min) 0
#endif
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

#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef __IDSTRING
#define __IDSTRING(name, string) \
        static const char name[] __used = string
#endif

#ifndef __RCSID
#define __RCSID(string) __IDSTRING(rcsid, string)
#endif

#ifndef __COPYRIGHT
#define __COPYRIGHT(string) __IDSTRING(copyright, string)
#endif

#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, string) __IDSTRING(__CONCAT(rcsid_, n), string)
#endif

#endif /* _SYS_CDEFS_H_ */
