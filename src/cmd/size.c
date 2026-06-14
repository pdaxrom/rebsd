/*
 * size
 */
#ifdef CROSS
#include </usr/include/stdio.h>
#else
#include <stdio.h>
#endif
#include <a.out.h>
#include <stdlib.h>
#include <unistd.h>
#include "aoutio.h"

int header;

int main(int argc, char **argv)
{
    struct exec buf;
    long sum;
    int nfiles, ch, err = 0;
    FILE *f;

#ifdef TARGET_BIG_ENDIAN
    aout_set_big_endian(1);
#else
    aout_set_big_endian(0);
#endif

    while ((ch = getopt(argc, argv, "hE:")) != EOF) {
        switch (ch) {
        case 'E':
            if (optarg[0] == 'L' && optarg[1] == 0)
                aout_set_big_endian(0);
            else if (optarg[0] == 'B' && optarg[1] == 0)
                aout_set_big_endian(1);
            else
                goto usage;
            break;
        case 'h':
        default:
usage:
            fprintf(stderr, "Usage:\n");
            fprintf(stderr, "  size [-EL|-EB] file...\n");
            return (1);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        *argv = "a.out";
        argc++;
    }

    for (nfiles = argc; argc--; argv++) {
        if ((f = fopen(*argv, "r")) == NULL) {
            printf("size: %s not found\n", *argv);
            err++;
            continue;
        }
        if (!aout_read_exec(f, &buf) || N_BADMAG(buf)) {
            printf("size: %s not an object file\n", *argv);
            fclose(f);
            err++;
            continue;
        }
        if (header == 0) {
            printf("text\tdata\tbss\tdec\thex\n");
            header = 1;
        }
        printf("%u\t%u\t%u\t", buf.a_text, buf.a_data, buf.a_bss);
        sum = (long)buf.a_text + (long)buf.a_data + (long)buf.a_bss;
        printf("%ld\t%lx", sum, sum);
        if (nfiles > 1)
            printf("\t%s", *argv);
        printf("\n");
        fclose(f);
    }
    exit(err);
}
