#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

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
    static const char initial[] = "abcdefgh";
    static const char changed[] = "abXYefgh";
    char buf[sizeof(initial)];
    struct stat st;
    off_t huge;
    int appendfd, fd, pfd[2];

    unlink(TMPFILE);
    fd = open(TMPFILE, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0)
        return bad("open temporary UFS file");
    if (write(fd, initial, sizeof(initial) - 1) != sizeof(initial) - 1)
        return bad("write positional-I/O test data");
    if (lseek(fd, (off_t)5, SEEK_SET) != (off_t)5)
        return bad("seek before pread");
    memset(buf, 0, sizeof(buf));
    if (pread(fd, buf, 3, (off_t)1) != 3 ||
        memcmp(buf, "bcd", 3) != 0)
        return bad("pread contents");
    if (lseek(fd, (off_t)0, SEEK_CUR) != (off_t)5)
        return bad("pread changed descriptor offset");
    if (pwrite64(fd, "XY", 2, (off64_t)2) != 2)
        return bad("pwrite64 contents");
    if (lseek(fd, (off_t)0, SEEK_CUR) != (off_t)5)
        return bad("pwrite changed descriptor offset");
    if (pread(fd, buf, sizeof(initial) - 1, (off_t)0) !=
        sizeof(initial) - 1 ||
        memcmp(buf, changed, sizeof(changed) - 1) != 0)
        return bad("pwrite result");

    errno = 0;
    if (pread(fd, buf, 1, (off_t)-1) != -1 || errno != EINVAL)
        return bad("negative pread offset");
    errno = 0;
    if (pwrite(fd, "Q", 1, (off_t)-1) != -1 || errno != EINVAL)
        return bad("negative pwrite offset");

    appendfd = open(TMPFILE, O_WRONLY | O_APPEND);
    if (appendfd < 0)
        return bad("open append descriptor");
    if (pwrite(appendfd, "Z", 1, (off_t)0) != 1)
        return bad("pwrite on append descriptor");
    if (lseek(appendfd, (off_t)0, SEEK_CUR) != (off_t)0)
        return bad("append pwrite changed descriptor offset");
    if (close(appendfd) < 0)
        return bad("close append descriptor");
    if (pread(fd, buf, sizeof(initial) - 1, (off_t)0) !=
        sizeof(initial) - 1 || buf[0] != 'Z')
        return bad("pwrite ignored offset on append descriptor");

    if (pipe(pfd) < 0)
        return bad("pipe for positional-I/O test");
    errno = 0;
    if (pread(pfd[0], buf, 1, (off_t)0) != -1 || errno != ESPIPE)
        return bad("pread accepted pipe");
    errno = 0;
    if (pwrite(pfd[1], "Q", 1, (off_t)0) != -1 || errno != ESPIPE)
        return bad("pwrite accepted pipe");
    close(pfd[0]);
    close(pfd[1]);

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
        return bad("open disk device");
    if (fstat(fd, &fst) < 0 || stat(path, &pst) < 0)
        return bad("stat disk device");
    if ((!S_ISBLK(fst.st_mode) && !S_ISCHR(fst.st_mode)) ||
        (fst.st_mode & S_IFMT) != (pst.st_mode & S_IFMT) ||
        fst.st_rdev != pst.st_rdev)
        return bad("disk-device stat contents");

    want = (off_t)3 * 1024 * 1024 * 1024;
    if (pread(fd, buf, sizeof(buf), want) != sizeof(buf))
        return bad("pread at 3 GiB");
    if (lseek(fd, (off_t)0, SEEK_CUR) != (off_t)0)
        return bad("device pread changed descriptor offset");
    pos = lseek(fd, want, SEEK_SET);
    if (pos != want)
        return bad("seek to 3 GiB");
    if (read(fd, buf, sizeof(buf)) != sizeof(buf))
        return bad("read at 3 GiB");
    if (lseek(fd, (off_t)0, SEEK_CUR) != want + sizeof(buf))
        return bad("seek current after 3 GiB read");

    want = (off_t)4 * 1024 * 1024 * 1024 + SECTOR;
    if (pread64(fd, buf, sizeof(buf), want) != sizeof(buf))
        return bad("pread64 above 4 GiB");
    if (lseek(fd, (off_t)0, SEEK_CUR) !=
        (off_t)3 * 1024 * 1024 * 1024 + sizeof(buf))
        return bad("large device pread changed descriptor offset");
    pos = lseek(fd, want, SEEK_SET);
    if (pos != want)
        return bad("seek above 4 GiB");
    if (read(fd, buf, sizeof(buf)) != sizeof(buf))
        return bad("read above 4 GiB");

    errno = 0;
    if (lseek(fd, (off_t)-1, SEEK_SET) != (off_t)-1 || errno != EINVAL)
        return bad("negative seek rejection");
    if (close(fd) < 0)
        return bad("close disk device");
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
