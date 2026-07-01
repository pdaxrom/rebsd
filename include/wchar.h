#ifndef _WCHAR_H_
#define _WCHAR_H_

#ifndef _WCHAR_T
#define _WCHAR_T
typedef int wchar_t;
#endif

#ifndef _WINT_T
#define _WINT_T
typedef int wint_t;
#endif

#ifndef NULL
#define NULL 0
#endif

int wprintf(const wchar_t *, ...);

#endif /* _WCHAR_H_ */
