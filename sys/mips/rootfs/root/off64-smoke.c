#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#define TMPFILE "/var/off64-smoke.tmp"
#define SECTOR 512

static int
putstr(const char *s)
{
    const char *p;

    for (p = s; *p; p++)
        ;
    return write(1, s, p - s);
}

static int
bad(const char *s)
{
    putstr("off64 smoke bad: ");
    putstr(s);
    putstr("\n");
    unlink(TMPFILE);
    return 1;
}

static int
check_temp_file(void)
{
    struct stat st;
    off_t huge;
    int fd;

    unlink(TMPFILE);
    fd = open(TMPFILE, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0)
        return bad("open temporary UFS file");
    if (ftruncate(fd, (off_t)12345) < 0)
        return bad("small ftruncate");
    if (fstat(fd, &st) < 0 || st.st_size != (off_t)12345)
        return bad("64-bit fstat result");

    /* Legacy UFS must reject, not truncate, a size it cannot encode. */
    huge = (off_t)3 * 1024 * 1024 * 1024;
    errno = 0;
    if (ftruncate(fd, huge) == 0 || errno != EFBIG)
        return bad("legacy UFS ftruncate limit");
    if (fstat(fd, &st) < 0 || st.st_size != (off_t)12345)
        return bad("failed ftruncate changed size");
    if (close(fd) < 0)
        return bad("close temporary file");

    errno = 0;
    if (truncate(TMPFILE, huge) == 0 || errno != EFBIG)
        return bad("legacy UFS truncate limit");
    if (stat(TMPFILE, &st) < 0 || st.st_size != (off_t)12345)
        return bad("64-bit stat result");
    if (truncate(TMPFILE, (off_t)0) < 0)
        return bad("small truncate");
    if (unlink(TMPFILE) < 0)
        return bad("unlink temporary file");
    return 0;
}

static int
check_device(const char *path)
{
    char buf[SECTOR];
    struct stat fst;
    struct stat pst;
    off_t pos;
    off_t want;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return bad("open block device");
    if (fstat(fd, &fst) < 0 || stat(path, &pst) < 0)
        return bad("stat block device");
    if (!S_ISBLK(fst.st_mode) || fst.st_rdev != pst.st_rdev)
        return bad("block-device stat contents");

    want = (off_t)3 * 1024 * 1024 * 1024;
    pos = lseek(fd, want, SEEK_SET);
    if (pos != want)
        return bad("seek to 3 GiB");
    if (read(fd, buf, sizeof(buf)) != sizeof(buf))
        return bad("read at 3 GiB");
    if (lseek(fd, (off_t)0, SEEK_CUR) != want + sizeof(buf))
        return bad("seek current after 3 GiB read");

    want = (off_t)4 * 1024 * 1024 * 1024 + SECTOR;
    pos = lseek(fd, want, SEEK_SET);
    if (pos != want)
        return bad("seek above 4 GiB");
    if (read(fd, buf, sizeof(buf)) != sizeof(buf))
        return bad("read above 4 GiB");

    errno = 0;
    if (lseek(fd, (off_t)-1, SEEK_SET) != (off_t)-1 || errno != EINVAL)
        return bad("negative seek rejection");
    if (close(fd) < 0)
        return bad("close block device");
    return 0;
}

int
main(int argc, char **argv)
{
    const char *device;

    if (sizeof(off_t) != 8 ||
        sizeof(((struct stat *)0)->st_size) != 8)
        return bad("off_t/stat size is not 64-bit");
    device = argc > 1 ? argv[1] : "/dev/sd0";
    if (check_temp_file())
        return 1;
    if (check_device(device))
        return 1;
    putstr("off64 smoke ok\n");
    return 0;
}
