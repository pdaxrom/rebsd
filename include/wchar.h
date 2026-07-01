#ifndef _WCHAR_H_
#define _WCHAR_H_

#ifndef _WCHAR_T
#define _WCHAR_T
#if defined(__WCHAR_TYPE__) && defined(__SIZEOF_WCHAR_T__) && \
    __SIZEOF_WCHAR_T__ == 2
typedef __WCHAR_TYPE__ wchar_t;
#else
typedef unsigned short wchar_t;
#endif
#endif

#ifndef _WINT_T
#define _WINT_T
#ifdef __WINT_TYPE__
typedef __WINT_TYPE__ wint_t;
#else
typedef unsigned int wint_t;
#endif
#endif

#ifndef NULL
#define NULL 0
#endif

int wprintf(const wchar_t *, ...);

#endif /* _WCHAR_H_ */
