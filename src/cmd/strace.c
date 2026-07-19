/* Run a command with kernel-assisted per-process system call tracing. */

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
usage(void)
{
    fputs("usage: strace command [argument ...]\n", stderr);
    exit(2);
}

int
main(int argc, char **argv)
{
    pid_t child;
    pid_t waited;
    int status;

    if (argc < 2)
        usage();
    child = fork();
    if (child < 0) {
        perror("strace: fork");
        return 1;
    }
    if (child == 0) {
        int error;

        if (ptrace(PT_SYSCALL_TRACE, 0, 0, 1) < 0) {
            perror("strace: ptrace");
            _exit(126);
        }
        execvp(argv[1], &argv[1]);
        error = errno;
        perror(argv[1]);
        _exit(error == ENOENT ? 127 : 126);
    }
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        perror("strace: waitpid");
        return 1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "strace: process %d terminated by signal %d\n",
            child, WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return 1;
}
