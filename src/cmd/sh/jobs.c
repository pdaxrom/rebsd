/*
 * Interactive job control for the Bourne shell.
 */
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "defs.h"

#define MAXJOBS 8
#define JOB_RUNNING 1
#define JOB_STOPPED 2
#define JOB_NAME_LEN 32

struct job {
    int used;
    int number;
    int pid;
    int pgrp;
    int state;
    char name[JOB_NAME_LEN];
};

static struct job jobs[MAXJOBS];
static int job_tty = -1;
static int shell_pgrp;
static int next_job = 1;
static BOOL job_enabled;
static BOOL job_initialized;

extern char *sysmsg[];

static void
job_set_foreground(int pgrp)
{
    if (job_tty >= 0)
        tcsetpgrp(job_tty, pgrp);
}

void
job_init(int tty_fd)
{
    int pid;
    int old_pgrp;
    int tty_pgrp;

    if (job_initialized)
        return;
    job_initialized = TRUE;

    if (tty_fd < 0 || ioctl(tty_fd, TIOCGPGRP, &tty_pgrp) < 0)
        return;

    pid = getpid();
    old_pgrp = getpgrp();
    /* Do not stop the shell while it takes foreground ownership. */
    ignsig(SIGTSTP);
    ignsig(SIGTTIN);
    ignsig(SIGTTOU);
    if (old_pgrp != pid && setpgid(0, pid) < 0)
        return;

    shell_pgrp = getpgrp();
    job_tty = tty_fd;
    if (shell_pgrp <= 0 ||
        tcsetpgrp(job_tty, shell_pgrp) < 0) {
        if (old_pgrp > 0 && old_pgrp != shell_pgrp)
            setpgid(0, old_pgrp);
        job_tty = -1;
        return;
    }

    job_enabled = TRUE;
}

BOOL
job_active()
{
    return (job_enabled);
}

void
job_child_start()
{
    int pid;

    pid = getpid();
    setpgid(0, pid);
}

void
job_child_default_signals()
{
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

static char *
job_tree_name(struct trenod *t)
{
    int type;

    while (t) {
        type = t->tretyp & COMMSK;
        switch (type) {
        case TCOM:
            if (comptr(t)->comarg)
                return (comptr(t)->comarg->argval);
            return ("command");

        case TFORK:
            t = forkptr(t)->forktre;
            break;

        case TPAR:
        case TNOT:
            t = parptr(t)->partre;
            break;

        case TFIL:
        case TLST:
        case TAND:
        case TORF:
            t = lstptr(t)->lstlef;
            break;

        default:
            return ("command");
        }
    }
    return ("command");
}

static void
job_copy_name(struct job *jp, struct trenod *tree)
{
    char *src;
    char *dst;
    int left;
    int c;

    src = job_tree_name(tree);
    dst = jp->name;
    left = JOB_NAME_LEN - 1;
    while (left-- > 0 && (c = smask(*src++)) != 0)
        *dst++ = c;
    *dst = 0;
}

static int
job_new_number()
{
    int number;
    int i;
    BOOL used;

    for (;;) {
        number = next_job++;
        if (next_job > 999)
            next_job = 1;
        used = FALSE;
        for (i = 0; i < MAXJOBS; i++)
            if (jobs[i].used && jobs[i].number == number)
                used = TRUE;
        if (!used)
            return (number);
    }
}

static struct job *
job_alloc(int pid, BOOL background, struct trenod *tree)
{
    struct job *jp;
    int i;

    jp = NIL;
    for (i = 0; i < MAXJOBS; i++)
        if (!jobs[i].used) {
            jp = &jobs[i];
            break;
        }
    if (jp == NIL)
        return (NIL);

    jp->used = TRUE;
    jp->number = background ? job_new_number() : 0;
    jp->pid = pid;
    jp->pgrp = pid;
    jp->state = JOB_RUNNING;
    job_copy_name(jp, tree);
    return (jp);
}

static void
job_print_direct(struct job *jp, char *state)
{
    prc('[');
    prn(jp->number);
    prs("] ");
    prs(state);
    prs(" ");
    prs(jp->name);
    newline();
}

static void
job_print_buffered(struct job *jp, char *state)
{
    prc_buff('[');
    prn_buff(jp->number);
    prs_buff("] ");
    prs_buff(state);
    prs_buff(" ");
    prs_buff(jp->name);
    prc_buff(NL);
}

static int
job_wait(struct job *jp, BOOL resume)
{
    union wait w;
    int pid;
    int sig;
    int rc;
    BOOL interrupted;

    rc = 0;
    interrupted = FALSE;
    jp->state = JOB_RUNNING;
    job_set_foreground(jp->pgrp);
    if (resume)
        kill(-jp->pgrp, SIGCONT);

    for (;;) {
        pid = waitpid(-jp->pgrp, &w.w_status, WUNTRACED);
        if (pid < 0 && errno == EINTR)
            continue;
        if (pid < 0) {
            jp->used = FALSE;
            break;
        }
        if (WIFSTOPPED(w.w_status)) {
            jp->state = JOB_STOPPED;
            if (jp->number == 0)
                jp->number = job_new_number();
            rc = WSTOPSIG(w.w_status) | SIGFLG;
            break;
        }

        sig = WTERMSIG(w.w_status);
        rc = sig ? sig | SIGFLG : WEXITSTATUS(w.w_status);
        interrupted = sig == SIGINT;
        if (sig && sysmsg[sig]) {
            prs(sysmsg[sig]);
            if (WCOREDUMP(w.w_status))
                prs(coredump);
            newline();
        }
        jp->used = FALSE;
        break;
    }

    job_set_foreground(shell_pgrp);
    if (interrupted)
        jobfault(SIGINT);
    if (jp->used && jp->state == JOB_STOPPED) {
        newline();
        job_print_direct(jp, "Stopped");
    }
    return (rc);
}

int
job_forked(int pid, BOOL background, struct trenod *tree)
{
    struct job *jp;
    struct job temporary;

    setpgid(pid, pid);
    jp = job_alloc(pid, background, tree);
    if (jp == NIL) {
        if (background) {
            prs("sh: too many jobs\n");
            return (ERROR);
        }
        temporary.used = TRUE;
        temporary.number = 0;
        temporary.pid = pid;
        temporary.pgrp = pid;
        temporary.state = JOB_RUNNING;
        job_copy_name(&temporary, tree);
        return (job_wait(&temporary, FALSE));
    }

    if (background) {
        prc('[');
        prn(jp->number);
        prs("] ");
        prn(pid);
        newline();
        return (0);
    }
    return (job_wait(jp, FALSE));
}

static void
job_reap(BOOL report)
{
    struct job *jp;
    union wait w;
    int i;
    int pid;

    for (i = 0; i < MAXJOBS; i++) {
        jp = &jobs[i];
        if (!jp->used)
            continue;
        pid = waitpid(jp->pid, &w.w_status, WNOHANG | WUNTRACED);
        if (pid <= 0)
            continue;
        if (WIFSTOPPED(w.w_status)) {
            jp->state = JOB_STOPPED;
            if (jp->number == 0)
                jp->number = job_new_number();
            if (report)
                job_print_direct(jp, "Stopped");
        } else {
            if (report)
                job_print_direct(jp, "Done");
            jp->used = FALSE;
        }
    }
}

void
job_notify()
{
    if (job_enabled)
        job_reap(TRUE);
}

static struct job *
job_find(char *spec, BOOL stopped_only)
{
    struct job *jp;
    int wanted;
    int i;

    jp = NIL;
    if (spec && *spec) {
        if (*spec == '%')
            spec++;
        if (!digit(*spec))
            return (NIL);
        wanted = stoi(spec);
        for (i = 0; i < MAXJOBS; i++)
            if (jobs[i].used &&
                (jobs[i].number == wanted || jobs[i].pid == wanted))
                return (&jobs[i]);
        return (NIL);
    }

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].used && (!stopped_only ||
            jobs[i].state == JOB_STOPPED)) {
            if (jp == NIL || jobs[i].number > jp->number)
                jp = &jobs[i];
        }
    return (jp);
}

static int
job_not_found()
{
    prs_buff("sh: no such job\n");
    return (ERROR);
}

int
job_list()
{
    int i;

    if (!job_enabled)
        return (job_not_found());
    job_reap(FALSE);
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].used)
            job_print_buffered(&jobs[i],
                jobs[i].state == JOB_STOPPED ? "Stopped" : "Running");
    return (0);
}

int
job_fg(char *spec)
{
    struct job *jp;
    BOOL resume;

    if (!job_enabled)
        return (job_not_found());
    job_reap(FALSE);
    jp = job_find(spec, FALSE);
    if (jp == NIL)
        return (job_not_found());
    resume = jp->state == JOB_STOPPED;
    return (job_wait(jp, resume));
}

int
job_bg(char *spec)
{
    struct job *jp;

    if (!job_enabled)
        return (job_not_found());
    job_reap(FALSE);
    jp = job_find(spec, TRUE);
    if (jp == NIL)
        return (job_not_found());
    if (kill(-jp->pgrp, SIGCONT) < 0)
        return (job_not_found());
    jp->state = JOB_RUNNING;
    job_print_buffered(jp, "Running");
    return (0);
}
