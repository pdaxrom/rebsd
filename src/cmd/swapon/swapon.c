#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: swapon special\n");
        return 1;
    }
    if (swapon(argv[1]) < 0) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}
