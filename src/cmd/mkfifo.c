#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *setmode(char *);
extern mode_t getmode(void *, mode_t);

static void
usage(void)
{
    fprintf(stderr, "usage: mkfifo [-m mode] fifo ...\n");
    exit(1);
}

int
main(int argc, char **argv)
{
    void *set;
    mode_t mask, mode;
    char *modestr;
    int ch, error, i;

    modestr = NULL;
    while ((ch = getopt(argc, argv, "m:")) != EOF) {
        switch (ch) {
        case 'm':
            modestr = optarg;
            break;
        default:
            usage();
        }
    }
    if (optind == argc)
        usage();

    set = NULL;
    mode = 0666;
    if (modestr != NULL) {
        set = setmode(modestr);
        if (set == NULL) {
            fprintf(stderr, "mkfifo: invalid mode: %s\n", modestr);
            return 1;
        }
        mask = umask(0);
        (void)umask(mask);
        mode = getmode(set, (mode_t)(0666 & ~mask));
    }

    error = 0;
    for (i = optind; i < argc; i++) {
        if (mkfifo(argv[i], modestr != NULL ? 0666 : mode) < 0) {
            fprintf(stderr, "mkfifo: %s: %s\n", argv[i], strerror(errno));
            error = 1;
            continue;
        }
        if (modestr != NULL && chmod(argv[i], mode) < 0) {
            fprintf(stderr, "mkfifo: %s: %s\n", argv[i], strerror(errno));
            error = 1;
        }
    }
    free(set);
    return error;
}
