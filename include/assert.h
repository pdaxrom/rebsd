#ifndef _ASSERT_H_
#define _ASSERT_H_

#ifndef NDEBUG
#include <stdio.h>
#include <stdlib.h>

#define _assert(ex) \
	do { \
		if (!(ex)) { \
			fprintf(stderr, "Assertion failed: file \"%s\", line %d\n", \
			    __FILE__, __LINE__); \
			exit(1); \
		} \
	} while (0)
#define assert(ex)	_assert(ex)
#else
#define _assert(ex)	((void)0)
#define assert(ex)	((void)0)
#endif

#endif /* _ASSERT_H_ */
