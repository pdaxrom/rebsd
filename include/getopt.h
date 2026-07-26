#ifndef _GETOPT_H_
#define _GETOPT_H_

#include <unistd.h>

#define no_argument        0
#define required_argument  1
#define optional_argument  2

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

int getopt_long(int, char * const *, const char *,
    const struct option *, int *);

#endif
