#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: swapoff special\n");
        return 1;
    }
    if (swapoff(argv[1]) < 0) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}
