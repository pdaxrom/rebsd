/*
 * Device nodes are now generated from the board rootfs manifest.
 *
 * The historical implementation followed private devsw pointers through
 * kernel memory and then deleted every /dev entry it did not recognise.  The
 * current devsw intentionally has no user-visible name table, so attempting
 * that walk is both unsafe and incapable of producing the board manifest.
 */
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
    struct stat sb;

    if (stat("/dev", &sb) < 0 || !S_ISDIR(sb.st_mode)) {
        fprintf(stderr, "devupdate: /dev is not available\n");
        return 1;
    }
    puts("devupdate: /dev is managed by the board rootfs manifest; "
        "nothing to update");
    return 0;
}
