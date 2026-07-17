/* Focused QEMU regression coverage for per-process MIPS address spaces. */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SMOKE_EXEC_STATUS   23
#define SMOKE_FORK_STATUS   31
#define SMOKE_REUSE_COUNT   8
#define SMOKE_PRESSURE_PAGES 384
#define SMOKE_VM_PAGE_SIZE  4096

static volatile sig_atomic_t smoke_signal_seen;
static volatile unsigned smoke_bad_address = 1;
static unsigned char smoke_file_page[SMOKE_VM_PAGE_SIZE];

static void
smoke_signal(int signo)
{
    if (signo == SIGUSR1)
        smoke_signal_seen = 1;
}

static int
smoke_fail(const char *name)
{
    printf("vm-process-smoke: FAIL: %s\n", name);
    return 1;
}

static int
smoke_wait(pid_t child, int expected)
{
    int status;
    pid_t waited;

    waited = wait(&status);
    if (waited != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != expected) {
        printf("vm-process-smoke: wait pid=%d/%d status=%04x expected=%d\n",
            waited, child, status, expected);
        return -1;
    }
    return 0;
}

static int
smoke_wait_signal(pid_t child, int expected)
{
    int status;
    pid_t waited;

    waited = wait(&status);
    if (waited != child || !WIFSIGNALED(status) ||
        WTERMSIG(status) != expected) {
        printf("vm-process-smoke: signal pid=%d/%d status=%04x expected=%d\n",
            waited, child, status, expected);
        return -1;
    }
    return 0;
}

static int
smoke_stack(int depth)
{
    volatile unsigned char page_fragment[1536];
    int value;

    page_fragment[0] = (unsigned char)depth;
    page_fragment[sizeof(page_fragment) - 1] = (unsigned char)(depth + 1);
    value = page_fragment[0] + page_fragment[sizeof(page_fragment) - 1];
    if (depth != 0)
        value += smoke_stack(depth - 1);
    return value;
}

int
main(int argc, char **argv)
{
    char *arena;
    char *cross_page;
    char *pressure;
    char *file_mapping;
    unsigned char residency[3];
    char *mapped;
    char *replacement;
    char **bad_argv;
    char *exec_argv[3];
    void *old_break;
    pid_t child;
    int index;
    int page_size;
    int grow_size;
    int fd;

    if (argc == 2 && strcmp(argv[1], "--exec-child") == 0)
        return SMOKE_EXEC_STATUS;

    page_size = getpagesize();
    if (page_size <= 0 || (page_size & (page_size - 1)) != 0)
        return smoke_fail("page size");
    grow_size = 4 * page_size;
    old_break = sbrk(0);
    arena = sbrk(grow_size);
    if (arena == (void *)-1 || arena != old_break)
        return smoke_fail("sbrk grow");
    memset(arena, 0x5a, grow_size);

    cross_page = (char *)(((unsigned)arena + page_size - 1) &
        ~(unsigned)(page_size - 1));
    cross_page += page_size - 3;
    memcpy(cross_page, "cross\n", 6);
    if (write(STDOUT_FILENO, cross_page, 6) != 6)
        return smoke_fail("cross-page copyin");

    if (signal(SIGUSR1, smoke_signal) == SIG_ERR ||
        kill(getpid(), SIGUSR1) != 0 || !smoke_signal_seen)
        return smoke_fail("signal delivery");
    if (smoke_stack(4) != 25)
        return smoke_fail("stack growth");

    arena[0] = 0x21;
    child = fork();
    if (child < 0)
        return smoke_fail("fork isolation create");
    if (child == 0) {
        if (arena[0] != 0x21)
            _exit(1);
        arena[0] = 0x43;
        _exit(SMOKE_FORK_STATUS);
    }
    if (smoke_wait(child, SMOKE_FORK_STATUS) != 0 || arena[0] != 0x21)
        return smoke_fail("fork isolation");

    bad_argv = (char **)(unsigned)smoke_bad_address;
    child = fork();
    if (child < 0)
        return smoke_fail("failed exec create");
    if (child == 0) {
        errno = 0;
        execv("/root/vm-process-smoke", bad_argv);
        if (errno != EFAULT || arena[0] != 0x21) {
            printf("vm-process-smoke: exec rollback errno=%d marker=%x\n",
                errno, (unsigned char)arena[0]);
            _exit(2);
        }
        _exit(SMOKE_FORK_STATUS);
    }
    if (smoke_wait(child, SMOKE_FORK_STATUS) != 0)
        return smoke_fail("failed exec rollback");

    exec_argv[0] = "/root/vm-process-smoke";
    exec_argv[1] = "--exec-child";
    exec_argv[2] = 0;
    child = fork();
    if (child < 0)
        return smoke_fail("exec create");
    if (child == 0) {
        execv(exec_argv[0], exec_argv);
        _exit(3);
    }
    if (smoke_wait(child, SMOKE_EXEC_STATUS) != 0)
        return smoke_fail("exec/wait");

    for (index = 0; index < SMOKE_REUSE_COUNT; ++index) {
        child = fork();
        if (child < 0)
            return smoke_fail("process reuse create");
        if (child == 0) {
            arena[0] = (char)(index + 1);
            _exit(index);
        }
        if (smoke_wait(child, index) != 0 || arena[0] != 0x21)
            return smoke_fail("process reuse wait");
    }

    errno = 0;
    mapped = mmap(0, 3 * SMOKE_VM_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED)
        return smoke_fail("anonymous mmap");
    mapped[0] = 0x12;
    mapped[SMOKE_VM_PAGE_SIZE] = 0x34;
    mapped[2 * SMOKE_VM_PAGE_SIZE] = 0x56;
    replacement = mmap(mapped + SMOKE_VM_PAGE_SIZE, SMOKE_VM_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
        -1, 0);
    if (replacement != mapped + SMOKE_VM_PAGE_SIZE ||
        (unsigned char)mapped[0] != 0x12 ||
        (unsigned char)mapped[SMOKE_VM_PAGE_SIZE] != 0 ||
        (unsigned char)mapped[2 * SMOKE_VM_PAGE_SIZE] != 0x56)
        return smoke_fail("MAP_FIXED replace");
    mapped[SMOKE_VM_PAGE_SIZE] = 0x34;
    errno = 0;
    if (mmap(mapped + 1, SMOKE_VM_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
        -1, 0) != MAP_FAILED || errno != EINVAL ||
        (unsigned char)mapped[0] != 0x12)
        return smoke_fail("MAP_FIXED validation");
    if (mprotect(mapped + SMOKE_VM_PAGE_SIZE, SMOKE_VM_PAGE_SIZE,
        PROT_READ) != 0 ||
        (unsigned char)mapped[SMOKE_VM_PAGE_SIZE] != 0x34 ||
        mprotect(mapped + SMOKE_VM_PAGE_SIZE, SMOKE_VM_PAGE_SIZE,
        PROT_READ | PROT_WRITE) != 0)
        return smoke_fail("mprotect split");
    if (mlock(mapped, 3 * SMOKE_VM_PAGE_SIZE) != 0 ||
        mlock(mapped, 3 * SMOKE_VM_PAGE_SIZE) != 0)
        return smoke_fail("mlock");
    memset(residency, 0, sizeof(residency));
    if (mincore(mapped, 3 * SMOKE_VM_PAGE_SIZE, residency) != 0 ||
        (residency[0] & MINCORE_INCORE) == 0 ||
        (residency[1] & MINCORE_INCORE) == 0 ||
        (residency[2] & MINCORE_INCORE) == 0 ||
        madvise(mapped, 3 * SMOKE_VM_PAGE_SIZE, MADV_NORMAL) != 0 ||
        msync(mapped, 3 * SMOKE_VM_PAGE_SIZE, MS_SYNC) != 0)
        return smoke_fail("mmap auxiliary calls");
    child = fork();
    if (child < 0)
        return smoke_fail("mmap fork create");
    if (child == 0) {
        if ((unsigned char)mapped[0] != 0x12)
            _exit(1);
        if (munlock(mapped, 3 * SMOKE_VM_PAGE_SIZE) != 0)
            _exit(2);
        mapped[0] = 0x7a;
        _exit(SMOKE_FORK_STATUS);
    }
    if (smoke_wait(child, SMOKE_FORK_STATUS) != 0 ||
        (unsigned char)mapped[0] != 0x12)
        return smoke_fail("mmap fork isolation");
    if (munmap(mapped + SMOKE_VM_PAGE_SIZE, SMOKE_VM_PAGE_SIZE) != 0)
        return smoke_fail("partial munmap");
    replacement = mmap(mapped + SMOKE_VM_PAGE_SIZE, SMOKE_VM_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (replacement != mapped + SMOKE_VM_PAGE_SIZE)
        return smoke_fail("mmap hint reuse");
    replacement[0] = 0x45;
    if (munlock(mapped, 3 * SMOKE_VM_PAGE_SIZE) != 0 ||
        mlock(mapped, 3 * SMOKE_VM_PAGE_SIZE) != 0)
        return smoke_fail("munlock split range");
    if (munmap(mapped, 3 * SMOKE_VM_PAGE_SIZE) != 0)
        return smoke_fail("munmap split range");
    errno = 0;
    if (mmap(0, SMOKE_VM_PAGE_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON, -1, ((off_t)1 << 32)) != MAP_FAILED ||
        errno != EINVAL)
        return smoke_fail("mmap 64-bit offset ABI");

    for (index = 0; index < SMOKE_VM_PAGE_SIZE; ++index)
        smoke_file_page[index] = (unsigned char)(index * 13 + 5);
    unlink("/var/tmp/vm-mmap-smoke");
    fd = open("/var/tmp/vm-mmap-smoke", O_CREAT | O_TRUNC | O_RDWR,
        0600);
    if (fd < 0 || write(fd, smoke_file_page, SMOKE_VM_PAGE_SIZE) !=
        SMOKE_VM_PAGE_SIZE || write(fd, "file-tail-private", 17) != 17)
        return smoke_fail("file mmap create");
    file_mapping = mmap(0, 3 * SMOKE_VM_PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (file_mapping == MAP_FAILED || close(fd) != 0)
        return smoke_fail("private file mmap");
    if ((unsigned char)file_mapping[0] != smoke_file_page[0] ||
        (unsigned char)file_mapping[SMOKE_VM_PAGE_SIZE - 1] !=
        smoke_file_page[SMOKE_VM_PAGE_SIZE - 1] ||
        memcmp(file_mapping + SMOKE_VM_PAGE_SIZE,
        "file-tail-private", 17) != 0 ||
        file_mapping[SMOKE_VM_PAGE_SIZE + 17] != 0)
        return smoke_fail("private file demand read after close");
    child = fork();
    if (child < 0)
        return smoke_fail("private file fork create");
    if (child == 0) {
        file_mapping[0] = 0x66;
        file_mapping[SMOKE_VM_PAGE_SIZE] = 0x77;
        _exit(SMOKE_FORK_STATUS);
    }
    if (smoke_wait(child, SMOKE_FORK_STATUS) != 0 ||
        (unsigned char)file_mapping[0] != smoke_file_page[0] ||
        file_mapping[SMOKE_VM_PAGE_SIZE] != 'f')
        return smoke_fail("private file fork isolation");
    file_mapping[0] = 0x44;
    fd = open("/var/tmp/vm-mmap-smoke", O_RDONLY);
    if (fd < 0 || read(fd, smoke_file_page, 1) != 1 ||
        smoke_file_page[0] != 5 || close(fd) != 0)
        return smoke_fail("private file write isolation");
    child = fork();
    if (child < 0)
        return smoke_fail("file EOF fork create");
    if (child == 0) {
        volatile unsigned char beyond_eof;

        beyond_eof = (unsigned char)file_mapping[2 * SMOKE_VM_PAGE_SIZE];
        _exit(beyond_eof == 0xff ? 1 : 2);
    }
    if (smoke_wait_signal(child, SIGBUS) != 0)
        return smoke_fail("file EOF SIGBUS");
    if (munmap(file_mapping, 3 * SMOKE_VM_PAGE_SIZE) != 0 ||
        unlink("/var/tmp/vm-mmap-smoke") != 0)
        return smoke_fail("private file cleanup");

    pressure = sbrk(SMOKE_PRESSURE_PAGES * SMOKE_VM_PAGE_SIZE);
    if (pressure == (void *)-1)
        return smoke_fail("memory pressure grow");
    for (index = 0; index < SMOKE_PRESSURE_PAGES; ++index)
        pressure[index * SMOKE_VM_PAGE_SIZE] = (char)(index * 37 + 11);
    for (index = SMOKE_PRESSURE_PAGES - 1; index >= 0; --index) {
        if ((unsigned char)pressure[index * SMOKE_VM_PAGE_SIZE] !=
            (unsigned char)(index * 37 + 11))
            return smoke_fail("memory pressure data");
    }
    if (sbrk(-SMOKE_PRESSURE_PAGES * SMOKE_VM_PAGE_SIZE) == (void *)-1)
        return smoke_fail("memory pressure shrink");

    if (sbrk(-2 * page_size) == (void *)-1)
        return smoke_fail("sbrk shrink");
    puts("vm-process-smoke: ok");
    return 0;
}
