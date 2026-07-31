#ifndef _STDDEF_H_
#define _STDDEF_H_

#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef int ptrdiff_t;
#endif

#ifndef _SIZE_T
#define _SIZE_T
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned size_t;
#endif
#endif

#ifndef _WCHAR_T
#define _WCHAR_T
#if defined(__WCHAR_TYPE__) && defined(__SIZEOF_WCHAR_T__) && \
    __SIZEOF_WCHAR_T__ == 2
typedef __WCHAR_TYPE__ wchar_t;
#else
typedef unsigned short wchar_t;
#endif
#endif

#ifndef NULL
#define NULL    0
#endif

/* Offset of member MEMBER in a struct of type TYPE. */
#ifndef offsetof
#if defined(__GNUC__) && __GNUC__ > 3
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE*)0)->MEMBER)
#endif
#endif

#endif /* _STDDEF_H_ */
