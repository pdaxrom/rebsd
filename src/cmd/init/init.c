/*
 * Copyright (c) 2026 ReBSD contributors.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * System V style process 1 with the public IRIX init/telinit interface.
 * The implementation uses the native ReBSD signal and process interfaces;
 * it has no Linux initctl protocol or private control FIFO.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INITTAB             "/etc/inittab"
#define SHELL               "/bin/sh"
#define MAX_INITTAB_LINE    512
#define ID_SIZE             4
#define TERM_WAIT_SECONDS   5
#define SPAWN_INTERVAL      (2 * 60)
#define SPAWN_LIMIT         10
#define SPAWN_INHIBIT       (5 * 60)

#define LEVEL_0             0x0001
#define LEVEL_1             0x0002
#define LEVEL_2             0x0004
#define LEVEL_3             0x0008
#define LEVEL_4             0x0010
#define LEVEL_5             0x0020
#define LEVEL_6             0x0040
#define LEVEL_S             0x0080
#define LEVEL_A             0x0100
#define LEVEL_B             0x0200
#define LEVEL_C             0x0400
#define LEVEL_TRUE          (LEVEL_0 | LEVEL_1 | LEVEL_2 | LEVEL_3 | \
                             LEVEL_4 | LEVEL_5 | LEVEL_6 | LEVEL_S)

enum action {
    ACT_OFF,
    ACT_RESPAWN,
    ACT_ONDEMAND,
    ACT_ONCE,
    ACT_WAIT,
    ACT_BOOT,
    ACT_BOOTWAIT,
    ACT_POWERFAIL,
    ACT_POWERWAIT,
    ACT_INITDEFAULT,
    ACT_SYSINIT
};

struct init_entry {
    struct init_entry *next;
    char id[ID_SIZE + 1];
    unsigned levels;
    enum action action;
    char command[MAX_INITTAB_LINE + 1];
    pid_t pid;
    time_t spawn_start;
    time_t inhibit_until;
    unsigned spawn_count;
    unsigned demand : 1;
    unsigned terminating : 1;
    char started_level;
};

struct level_map {
    char name;
    int signal;
    unsigned mask;
    int true_level;
};

/* Traditional System V signal mapping used by the IRIX public interface. */
static const struct level_map level_maps[] = {
    { 'Q', SIGHUP,  0,       0 },
    { 'q', SIGHUP,  0,       0 },
    { '0', SIGINT,  LEVEL_0, 1 },
    { '1', SIGQUIT, LEVEL_1, 1 },
    { '2', SIGILL,  LEVEL_2, 1 },
    { '3', SIGTRAP, LEVEL_3, 1 },
    { '4', SIGIOT,  LEVEL_4, 1 },
    { '5', SIGEMT,  LEVEL_5, 1 },
    { '6', SIGFPE,  LEVEL_6, 1 },
    { 'S', SIGBUS,  LEVEL_S, 1 },
    { 's', SIGBUS,  LEVEL_S, 1 },
    { 'a', SIGSEGV, LEVEL_A, 0 },
    { 'b', SIGSYS,  LEVEL_B, 0 },
    { 'c', SIGPIPE, LEVEL_C, 0 }
};

static struct init_entry *entries;
static struct init_entry *pending_entries;
static volatile sig_atomic_t requested_signal;
static volatile sig_atomic_t child_event;
static volatile sig_atomic_t power_event;
static volatile sig_atomic_t idle_event;
static char current_level;
static int boot_actions_done;

static int valid_in_level(const struct init_entry *, char);

static void
console(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fflush(stderr);
}

static const struct level_map *
map_name(char name)
{
    size_t i;

    for (i = 0; i < sizeof(level_maps) / sizeof(level_maps[0]); i++)
        if (level_maps[i].name == name)
            return &level_maps[i];
    return NULL;
}

static const struct level_map *
map_signal(int signo)
{
    size_t i;

    for (i = 0; i < sizeof(level_maps) / sizeof(level_maps[0]); i++)
        if (level_maps[i].signal == signo)
            return &level_maps[i];
    return NULL;
}

static unsigned
level_mask(char level)
{
    const struct level_map *map = map_name(level);

    return map == NULL ? 0 : map->mask;
}

static void
level_handler(int signo)
{
    requested_signal = signo;
}

static void
child_handler(int signo)
{
    (void)signo;
    child_event = 1;
}

static void
power_handler(int signo)
{
    (void)signo;
    power_event = 1;
}

static void
idle_handler(int signo)
{
    (void)signo;
    idle_event = 1;
}

static void
install_handler(int signo, sig_t handler)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(signo, &action, NULL) < 0) {
        console("init: sigaction(%d): %s\n", signo, strerror(errno));
        exit(1);
    }
}

static void
install_signal_handlers(void)
{
    unsigned long installed = 0;
    size_t i;

    for (i = 0; i < sizeof(level_maps) / sizeof(level_maps[0]); i++) {
        if ((installed & sigmask(level_maps[i].signal)) != 0)
            continue;
        install_handler(level_maps[i].signal, level_handler);
        installed |= sigmask(level_maps[i].signal);
    }
    install_handler(SIGCHLD, child_handler);
    install_handler(SIGPWR, power_handler);
    install_handler(SIGTSTP, idle_handler);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

static void
reset_child_signals(void)
{
    sigset_t empty;
    int signo;

    for (signo = 1; signo < NSIG; signo++) {
        if (signo != SIGKILL && signo != SIGSTOP)
            signal(signo, SIG_DFL);
    }
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
}

static int
action_from_name(const char *name, enum action *action)
{
    static const struct {
        const char *name;
        enum action action;
    } actions[] = {
        { "off", ACT_OFF },
        { "respawn", ACT_RESPAWN },
        { "ondemand", ACT_ONDEMAND },
        { "once", ACT_ONCE },
        { "wait", ACT_WAIT },
        { "boot", ACT_BOOT },
        { "bootwait", ACT_BOOTWAIT },
        { "powerfail", ACT_POWERFAIL },
        { "powerwait", ACT_POWERWAIT },
        { "initdefault", ACT_INITDEFAULT },
        { "sysinit", ACT_SYSINIT }
    };
    size_t i;

    for (i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
        if (strcmp(name, actions[i].name) == 0) {
            *action = actions[i].action;
            return 0;
        }
    }
    return -1;
}

static char *
trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t')
        text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
    return text;
}

/*
 * Read one logical inittab entry.  A backslash immediately before newline
 * continues the entry.  The complete entry, including continuations, is
 * limited to the System V 512-byte contract.
 */
static int
read_logical_line(FILE *file, char *line, size_t size, unsigned *line_number)
{
    size_t used = 0;
    int c, overflow = 0, saw = 0;

    while ((c = fgetc(file)) != EOF) {
        saw = 1;
        if (c == '\n') {
            (*line_number)++;
            if (used > 0 && line[used - 1] == '\\') {
                used--;
                continue;
            }
            break;
        }
        if (used + 1 < size)
            line[used++] = (char)c;
        else
            overflow = 1;
    }
    if (!saw)
        return 0;
    line[used] = '\0';
    if (overflow) {
        console("init: %s:%u: entry exceeds %d bytes\n",
            INITTAB, *line_number, MAX_INITTAB_LINE);
        return -1;
    }
    return 1;
}

static struct init_entry *
find_id(struct init_entry *list, const char *id)
{
    for (; list != NULL; list = list->next)
        if (strcmp(list->id, id) == 0)
            return list;
    return NULL;
}

static int
parse_levels(const char *names, unsigned *levels)
{
    const struct level_map *map;

    *levels = 0;
    while (*names != '\0') {
        map = map_name(*names++);
        if (map == NULL || map->mask == 0)
            return -1;
        *levels |= map->mask;
    }
    return 0;
}

static struct init_entry *
read_inittab(void)
{
    struct init_entry *head = NULL, **tail = &head, *entry;
    char line[MAX_INITTAB_LINE + 1];
    char *id, *levels, *action, *command, *colon;
    FILE *file;
    unsigned line_number = 1;
    int result;

    file = fopen(INITTAB, "r");
    if (file == NULL) {
        console("init: cannot open %s: %s\n", INITTAB, strerror(errno));
        return NULL;
    }
    while ((result = read_logical_line(file, line, sizeof(line),
        &line_number)) != 0) {
        if (result < 0)
            continue;
        id = trim(line);
        if (*id == '\0' || *id == '#')
            continue;
        colon = strchr(id, ':');
        if (colon == NULL)
            goto bad;
        *colon++ = '\0';
        levels = colon;
        colon = strchr(levels, ':');
        if (colon == NULL)
            goto bad;
        *colon++ = '\0';
        action = colon;
        colon = strchr(action, ':');
        if (colon == NULL)
            goto bad;
        *colon++ = '\0';
        command = trim(colon);
        id = trim(id);
        levels = trim(levels);
        action = trim(action);
        if (strlen(id) < 1 || strlen(id) > ID_SIZE ||
            find_id(head, id) != NULL)
            goto bad;
        entry = calloc(1, sizeof(*entry));
        if (entry == NULL) {
            console("init: out of memory while reading %s\n", INITTAB);
            break;
        }
        strcpy(entry->id, id);
        if (parse_levels(levels, &entry->levels) < 0 ||
            action_from_name(action, &entry->action) < 0) {
            free(entry);
            goto bad;
        }
        if ((entry->action == ACT_INITDEFAULT &&
            (entry->levels & ~LEVEL_TRUE) != 0) ||
            (entry->action == ACT_ONDEMAND &&
            (entry->levels & (LEVEL_A | LEVEL_B | LEVEL_C)) == 0)) {
            free(entry);
            goto bad;
        }
        if (entry->action != ACT_INITDEFAULT && entry->action != ACT_OFF &&
            *command == '\0') {
            free(entry);
            goto bad;
        }
        strcpy(entry->command, command);
        *tail = entry;
        tail = &entry->next;
        continue;
bad:
        console("init: %s:%u: invalid entry\n", INITTAB,
            line_number == 0 ? 0 : line_number - 1);
    }
    fclose(file);
    return head;
}

static struct init_entry *
find_pid_in(struct init_entry *list, pid_t pid)
{
    for (; list != NULL; list = list->next)
        if (list->pid == pid)
            return list;
    return NULL;
}

static void
record_dead_child(pid_t pid)
{
    struct init_entry *entry;

    entry = find_pid_in(entries, pid);
    if (entry == NULL)
        entry = find_pid_in(pending_entries, pid);
    if (entry != NULL) {
        entry->pid = 0;
        entry->terminating = 0;
    }
}

static int
reap_children(int options)
{
    pid_t pid;
    int status, count = 0;

    while ((pid = waitpid(WAIT_ANY, &status, options)) > 0) {
        record_dead_child(pid);
        count++;
        if ((options & WNOHANG) == 0)
            break;
    }
    if (pid < 0 && errno != ECHILD && errno != EINTR)
        console("init: waitpid: %s\n", strerror(errno));
    child_event = 0;
    return count;
}

static void
signal_entry(struct init_entry *entry, int signo)
{
    if (entry->pid <= 1)
        return;
    if (kill(-entry->pid, signo) < 0 && errno == ESRCH)
        (void)kill(entry->pid, signo);
}

static int
any_terminating(struct init_entry *list)
{
    for (; list != NULL; list = list->next)
        if (list->terminating && list->pid > 0)
            return 1;
    return 0;
}

static void
stop_marked(struct init_entry *list)
{
    struct init_entry *entry;
    int second;

    for (entry = list; entry != NULL; entry = entry->next)
        if (entry->terminating && entry->pid > 0)
            signal_entry(entry, SIGTERM);
    for (second = 0; second < TERM_WAIT_SECONDS && any_terminating(list);
        second++) {
        sleep(1);
        reap_children(WNOHANG);
    }
    for (entry = list; entry != NULL; entry = entry->next) {
        if (entry->terminating && entry->pid > 0)
            signal_entry(entry, SIGKILL);
    }
    for (second = 0; second < TERM_WAIT_SECONDS && any_terminating(list);
        second++) {
        sleep(1);
        reap_children(WNOHANG);
    }
    for (entry = list; entry != NULL; entry = entry->next) {
        entry->terminating = 0;
        if (entry->pid > 0 && kill(entry->pid, 0) < 0 && errno == ESRCH)
            entry->pid = 0;
    }
}

static void
free_entries(struct init_entry *list)
{
    struct init_entry *next;

    while (list != NULL) {
        next = list->next;
        free(list);
        list = next;
    }
}

static int
reload_inittab(int reset_throttle)
{
    struct init_entry *fresh, *new_entry, *old_entry, *old;

    fresh = read_inittab();
    if (fresh == NULL)
        return -1;
    pending_entries = fresh;
    for (new_entry = fresh; new_entry != NULL;
        new_entry = new_entry->next) {
        old_entry = find_id(entries, new_entry->id);
        if (old_entry != NULL) {
            new_entry->spawn_start = old_entry->spawn_start;
            new_entry->inhibit_until = old_entry->inhibit_until;
            new_entry->spawn_count = old_entry->spawn_count;
            new_entry->demand = old_entry->demand;
            if (old_entry->levels == new_entry->levels &&
                old_entry->action == new_entry->action)
                new_entry->started_level = old_entry->started_level;
            if (old_entry->pid > 0 && new_entry->action != ACT_OFF &&
                ((old_entry->demand && current_level != 'S') ||
                valid_in_level(new_entry, current_level))) {
                /* Command changes take effect when this child next spawns. */
                new_entry->pid = old_entry->pid;
                old_entry->pid = 0;
            }
            if (reset_throttle) {
                new_entry->spawn_start = 0;
                new_entry->inhibit_until = 0;
                new_entry->spawn_count = 0;
            }
        }
    }
    old = entries;
    for (old_entry = old; old_entry != NULL; old_entry = old_entry->next)
        if (old_entry->pid > 0)
            old_entry->terminating = 1;
    stop_marked(old);
    entries = fresh;
    pending_entries = NULL;
    free_entries(old);
    return 0;
}

static pid_t
start_entry(struct init_entry *entry)
{
    char shell_command[MAX_INITTAB_LINE + 7];
    time_t now;
    pid_t pid;

    now = time(NULL);
    if (entry->action == ACT_RESPAWN || entry->action == ACT_ONDEMAND) {
        if (entry->inhibit_until != 0) {
            if (now < entry->inhibit_until)
                return 0;
            entry->inhibit_until = 0;
            entry->spawn_start = 0;
            entry->spawn_count = 0;
        }
        if (entry->spawn_start == 0 ||
            now - entry->spawn_start >= SPAWN_INTERVAL) {
            entry->spawn_start = now;
            entry->spawn_count = 0;
        }
        if (entry->spawn_count >= SPAWN_LIMIT) {
            entry->inhibit_until = now + SPAWN_INHIBIT;
            console("init: '%s' respawning too rapidly; disabled for %d seconds\n",
                entry->id, SPAWN_INHIBIT);
            return 0;
        }
        entry->spawn_count++;
    }
    pid = fork();
    if (pid < 0) {
        console("init: cannot fork '%s': %s\n", entry->id,
            strerror(errno));
        return -1;
    }
    if (pid == 0) {
        reset_child_signals();
        (void)setpgrp();
        strcpy(shell_command, "exec ");
        strcat(shell_command, entry->command);
        execl(SHELL, "sh", "-c", shell_command, (char *)NULL);
        console("init: cannot execute '%s': %s\n", entry->command,
            strerror(errno));
        _exit(127);
    }
    entry->pid = pid;
    return pid;
}

static int
wait_entry(struct init_entry *entry)
{
    pid_t pid, waited;
    int status;

    pid = start_entry(entry);
    if (pid <= 0)
        return pid < 0 ? 1 : 0;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited == pid)
        record_dead_child(pid);
    return waited == pid ? status : 1;
}

static int
valid_in_level(const struct init_entry *entry, char level)
{
    unsigned mask;

    if (entry->demand)
        return level != 'S';
    if (entry->action == ACT_ONDEMAND)
        return 0;
    if (entry->action != ACT_RESPAWN && entry->action != ACT_ONCE &&
        entry->action != ACT_WAIT)
        return 1;
    mask = level_mask(level);
    return entry->levels == 0 || (entry->levels & mask) != 0;
}

static void
start_level_entries(char level, int entering)
{
    struct init_entry *entry;
    time_t now = time(NULL);

    for (entry = entries; entry != NULL; entry = entry->next) {
        if (!valid_in_level(entry, level) || entry->pid > 0)
            continue;
        switch (entry->action) {
        case ACT_RESPAWN:
        case ACT_ONDEMAND:
            if (entry->action == ACT_ONDEMAND && !entry->demand)
                break;
            (void)start_entry(entry);
            break;
        case ACT_ONCE:
            if (entering || entry->started_level == '\0') {
                entry->started_level = level;
                (void)start_entry(entry);
            }
            break;
        case ACT_WAIT:
            if (entering || entry->started_level == '\0') {
                entry->started_level = level;
                (void)wait_entry(entry);
            }
            break;
        default:
            break;
        }
        if (entry->inhibit_until != 0 && now >= entry->inhibit_until)
            entry->inhibit_until = 0;
    }
}

static void
enter_level(char level, int entering)
{
    struct init_entry *entry;

    for (entry = entries; entry != NULL; entry = entry->next) {
        if (entry->pid > 0 &&
            (entry->action == ACT_OFF || !valid_in_level(entry, level)))
            entry->terminating = 1;
    }
    stop_marked(entries);
    current_level = level;
    start_level_entries(level, entering);
}

static int
entry_matches_level(const struct init_entry *entry, char level)
{
    unsigned mask = level_mask(level);

    return entry->levels == 0 || (entry->levels & mask) != 0;
}

static void
run_sysinit_actions(void)
{
    struct init_entry *entry;

    for (entry = entries; entry != NULL; entry = entry->next)
        if (entry->action == ACT_SYSINIT)
            (void)wait_entry(entry);
}

static void
run_boot_actions(char level)
{
    struct init_entry *entry;

    if (boot_actions_done || level == 'S')
        return;
    boot_actions_done = 1;
    for (entry = entries; entry != NULL; entry = entry->next) {
        if (!entry_matches_level(entry, level))
            continue;
        if (entry->action == ACT_BOOTWAIT)
            (void)wait_entry(entry);
        else if (entry->action == ACT_BOOT)
            (void)start_entry(entry);
    }
}

static void
run_power_actions(void)
{
    struct init_entry *entry;

    for (entry = entries; entry != NULL; entry = entry->next) {
        if (entry->pid > 0 || !entry_matches_level(entry, current_level))
            continue;
        if (entry->action == ACT_POWERWAIT)
            (void)wait_entry(entry);
        else if (entry->action == ACT_POWERFAIL)
            (void)start_entry(entry);
    }
}

static void
run_demand(char demand)
{
    struct init_entry *entry;
    unsigned mask = level_mask(demand);

    for (entry = entries; entry != NULL; entry = entry->next) {
        if ((entry->levels & mask) == 0)
            continue;
        if (entry->action != ACT_ONDEMAND && entry->action != ACT_RESPAWN)
            continue;
        entry->demand = 1;
        if (entry->pid == 0)
            (void)start_entry(entry);
    }
}

static char
default_level(void)
{
    struct init_entry *entry;
    int level;

    for (entry = entries; entry != NULL; entry = entry->next) {
        if (entry->action != ACT_INITDEFAULT)
            continue;
        if (entry->levels == 0)
            return '6';
        for (level = 6; level >= 0; level--)
            if ((entry->levels & level_mask((char)('0' + level))) != 0)
                return (char)('0' + level);
        if ((entry->levels & LEVEL_S) != 0)
            return 'S';
    }
    return '\0';
}

static char
ask_level(void)
{
    char line[32];
    const struct level_map *map;

    for (;;) {
        console("Enter run level (0-6, S): ");
        if (fgets(line, sizeof(line), stdin) == NULL)
            return 'S';
        map = map_name(line[0]);
        if (map != NULL && map->true_level)
            return line[0] == 's' ? 'S' : line[0];
        console("init: invalid run level\n");
    }
}

static void
process_request(void)
{
    const struct level_map *map;
    char requested_level;
    int signo;

    signo = requested_signal;
    requested_signal = 0;
    map = map_signal(signo);
    if (map == NULL)
        return;
    if (signo == SIGHUP) {
        idle_event = 0;
        if (reload_inittab(1) == 0)
            start_level_entries(current_level, 0);
        return;
    }
    if (!map->true_level) {
        run_demand(map->name);
        return;
    }
    requested_level = map->name == 's' ? 'S' : map->name;
    if (reload_inittab(1) == 0) {
        run_boot_actions(requested_level);
        enter_level(requested_level, current_level != requested_level);
    }
}

static int
has_children(void)
{
    struct init_entry *entry;

    for (entry = entries; entry != NULL; entry = entry->next)
        if (entry->pid > 0)
            return 1;
    return 0;
}

static int
has_inhibited_entries(void)
{
    struct init_entry *entry;

    for (entry = entries; entry != NULL; entry = entry->next)
        if (entry->inhibit_until != 0)
            return 1;
    return 0;
}

static void
supervise(void)
{
    for (;;) {
        (void)reap_children(WNOHANG);
        if (requested_signal != 0)
            process_request();
        if (power_event) {
            power_event = 0;
            run_power_actions();
        }
        if (idle_event) {
            if (has_children())
                (void)reap_children(0);
            else
                pause();
            continue;
        }
        start_level_entries(current_level, 0);
        if (requested_signal != 0 || power_event)
            continue;
        if (has_children()) {
            (void)reap_children(0);
        } else if (has_inhibited_entries()) {
            sleep(1);
        } else {
            pause();
        }
    }
}

static struct init_entry *
single_user_table(void)
{
    struct init_entry *entry;

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
        return NULL;
    strcpy(entry->id, "su");
    entry->levels = LEVEL_S;
    entry->action = ACT_RESPAWN;
    strcpy(entry->command, SHELL);
    return entry;
}

static void
open_console(void)
{
    int fd;

    fd = open(_PATH_CONSOLE, O_RDWR);
    if (fd < 0)
        return;
    if (fd != STDIN_FILENO)
        dup2(fd, STDIN_FILENO);
    if (fd != STDOUT_FILENO)
        dup2(fd, STDOUT_FILENO);
    if (fd != STDERR_FILENO)
        dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

static int
user_init(int argc, char **argv)
{
    const struct level_map *map;

    if (argc != 2 || argv[1][0] == '\0' || argv[1][1] != '\0' ||
        (map = map_name(argv[1][0])) == NULL) {
        fprintf(stderr, "usage: %s [0123456SsQqabc]\n", argv[0]);
        return 2;
    }
    if (getuid() != 0 || kill(1, map->signal) < 0) {
        fprintf(stderr, "%s: cannot signal init: %s\n", argv[0],
            strerror(errno));
        return 1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    char initial;

    if (getpid() != 1)
        return user_init(argc, argv);
    umask(022);
    setenv("HOME", "/", 1);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    install_signal_handlers();
    entries = read_inittab();
    run_sysinit_actions();
    open_console();
    if (entries == NULL) {
        console("init: no usable %s; entering single-user mode\n", INITTAB);
        entries = single_user_table();
        if (entries == NULL) {
            console("init: cannot allocate single-user entry\n");
            return 1;
        }
        initial = 'S';
    } else if (argc > 1 && strcmp(argv[1], "-s") == 0) {
        initial = 'S';
    } else {
        initial = default_level();
        if (initial == '\0')
            initial = ask_level();
    }
    run_boot_actions(initial);
    enter_level(initial, 1);
    supervise();
    return 0;
}
