/*
 * Interactive line editing, history and completion for the Bourne shell.
 */
#include <readline/history.h>
#include <readline/readline.h>
#include <errno.h>
#include <string.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "defs.h"

/*
 * defs.h maps free() directly to the shell allocator.  readline() uses the
 * standard malloc/free interface supplied by heap.c, including its small
 * allocation header, so release returned lines through that interface.
 */
#undef free

#define SH_HISTORY_LENGTH 100
#define SH_MAX_COMPLETIONS 128

struct completion_context {
    const char *line;
    size_t cursor;
    size_t word_start;
    readline_completions *completions;
};

static struct completion_context completion;
static char replacement[READLINE_MAX_LINE];
static char history_file[MAXPATHLEN];
static const char *next_prompt;
static char *pending_line;
static size_t pending_length;
static size_t pending_offset;
static int pending_newline;
static int edit_initialized;
static int edit_owner_pid;

static int prefix_match(const char *name, const char *prefix)
{
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

static int completion_exists(const char *line)
{
    size_t i;

    for (i = 0; i < completion.completions->len; i++)
        if (strcmp(completion.completions->cvec[i], line) == 0)
            return 1;
    return 0;
}

static void add_word_completion(const char *word)
{
    size_t before;
    size_t word_length;
    size_t after;
    size_t total;

    if (completion.completions->len >= SH_MAX_COMPLETIONS)
        return;

    before = completion.word_start;
    word_length = strlen(word);
    after = strlen(completion.line + completion.cursor);
    total = before + word_length + after;
    if (total >= sizeof(replacement))
        return;

    memcpy(replacement, completion.line, before);
    memcpy(replacement + before, word, word_length);
    memcpy(replacement + before + word_length,
           completion.line + completion.cursor, after + 1);
    if (!completion_exists(replacement))
        (void) readline_add_completion(completion.completions, replacement);
}

static int completion_compare(const void *left, const void *right)
{
    const char *const *a = left;
    const char *const *b = right;

    return strcmp(*a, *b);
}

static int command_position(const char *line, size_t word_start)
{
    size_t i = word_start;
    char c;

    while (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t'))
        i--;
    if (i == 0)
        return 1;

    c = line[i - 1];
    return c == ';' || c == '|' || c == '&' || c == '(' || c == ')' ||
           c == '{';
}

static int make_path(char *result, size_t result_size,
                     const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    int slash = directory_length != 0 &&
                directory[directory_length - 1] != '/';

    if (directory_length + slash + name_length + 1 > result_size)
        return 0;
    memcpy(result, directory, directory_length);
    if (slash)
        result[directory_length++] = '/';
    memcpy(result + directory_length, name, name_length + 1);
    return 1;
}

static void complete_files(const char *word)
{
    char display_directory[MAXPATHLEN];
    char scan_directory[MAXPATHLEN];
    char candidate[MAXPATHLEN + MAXNAMLEN + 2];
    char stat_path[MAXPATHLEN + MAXNAMLEN + 2];
    const char *prefix;
    const char *slash;
    const char *home;
    size_t directory_length;
    DIR *directory;
    struct direct *entry;

    slash = strrchr(word, '/');
    if (slash) {
        directory_length = slash - word + 1;
        if (directory_length >= sizeof(display_directory))
            return;
        memcpy(display_directory, word, directory_length);
        display_directory[directory_length] = '\0';
        prefix = slash + 1;
    } else {
        display_directory[0] = '\0';
        directory_length = 0;
        prefix = word;
    }

    if (display_directory[0] == '~' &&
        (display_directory[1] == '/' || display_directory[1] == '\0')) {
        home = homenod.namval;
        if (home == NULL)
            return;
        if (strlen(home) + strlen(display_directory + 1) + 1 >
            sizeof(scan_directory))
            return;
        strcpy(scan_directory, home);
        strcat(scan_directory, display_directory + 1);
    } else if (directory_length != 0) {
        strcpy(scan_directory, display_directory);
    } else {
        strcpy(scan_directory, ".");
    }

    directory = opendir(scan_directory);
    if (directory == NULL)
        return;

    while ((entry = readdir(directory)) != NULL) {
        struct stat status;
        size_t candidate_length;

        if (entry->d_ino == 0 || !prefix_match(entry->d_name, prefix))
            continue;
        if (entry->d_name[0] == '.' && prefix[0] != '.')
            continue;
        if (strlen(display_directory) + strlen(entry->d_name) + 2 >
            sizeof(candidate))
            continue;

        strcpy(candidate, display_directory);
        strcat(candidate, entry->d_name);
        if (make_path(stat_path, sizeof(stat_path),
                      scan_directory, entry->d_name) &&
            stat(stat_path, &status) == 0 &&
            (status.st_mode & S_IFMT) == S_IFDIR) {
            candidate_length = strlen(candidate);
            candidate[candidate_length++] = '/';
            candidate[candidate_length] = '\0';
        }
        add_word_completion(candidate);
    }
    closedir(directory);
}

static void complete_path_directory(const char *directory_name,
                                    const char *prefix)
{
    DIR *directory;
    struct direct *entry;

    directory = opendir(*directory_name ? directory_name : ".");
    if (directory == NULL)
        return;
    while ((entry = readdir(directory)) != NULL) {
        char path[MAXPATHLEN + MAXNAMLEN + 2];
        struct stat status;

        if (entry->d_ino == 0 || entry->d_name[0] == '.' ||
            !prefix_match(entry->d_name, prefix))
            continue;
        if (!make_path(path, sizeof(path),
                       *directory_name ? directory_name : ".",
                       entry->d_name))
            continue;
        if (stat(path, &status) < 0 ||
            (status.st_mode & S_IFMT) == S_IFDIR ||
            access(path, X_OK) < 0)
            continue;
        add_word_completion(entry->d_name);
    }
    closedir(directory);
}

static void complete_path_commands(const char *prefix)
{
    const char *path = pathnod.namval ? pathnod.namval : defpath;
    const char *start;
    char directory[MAXPATHLEN];

    do {
        size_t length;

        start = path;
        while (*path && *path != ':')
            path++;
        length = path - start;
        if (length < sizeof(directory)) {
            memcpy(directory, start, length);
            directory[length] = '\0';
            complete_path_directory(directory, prefix);
        }
        if (*path == ':')
            path++;
        else
            break;
    } while (1);
}

static void complete_function(struct namnod *name)
{
    const char *prefix =
        completion.line + completion.word_start;
    size_t prefix_length = completion.cursor - completion.word_start;

    if ((name->namflg & N_FUNCTN) &&
        strlen(name->namid) >= prefix_length &&
        strncmp(name->namid, prefix, prefix_length) == 0)
        add_word_completion(name->namid);
}

static void complete_commands(const char *prefix)
{
    int i;

    for (i = 0; i < no_reserved; i++)
        if (prefix_match(reserved[i].sysnam, prefix))
            add_word_completion(reserved[i].sysnam);
    for (i = 0; i < no_commands; i++)
        if (prefix_match(commands[i].sysnam, prefix))
            add_word_completion(commands[i].sysnam);
    namscan(complete_function);
    complete_path_commands(prefix);
}

static void shell_completion(const char *line, size_t cursor,
                             readline_completions *completions)
{
    char word[MAXPATHLEN];
    size_t start = cursor;
    size_t length;

    while (start > 0 &&
           strchr(" \t\n;&|()<>'\"`", line[start - 1]) == NULL)
        start--;
    length = cursor - start;
    if (length >= sizeof(word))
        return;

    memcpy(word, line + start, length);
    word[length] = '\0';
    completion.line = line;
    completion.cursor = cursor;
    completion.word_start = start;
    completion.completions = completions;

    if (command_position(line, start) && strchr(word, '/') == NULL)
        complete_commands(word);
    else
        complete_files(word);

    if (completions->len > 1)
        qsort(completions->cvec, completions->len,
              sizeof(*completions->cvec), completion_compare);
}

void sh_edit_init(void)
{
    const char *home;
    size_t home_length;

    if (edit_initialized)
        return;
    edit_initialized = 1;
    edit_owner_pid = getpid();
    next_prompt = ps1nod.namval;
    history_file[0] = '\0';

    readline_set_completion_callback(shell_completion);
    (void) history_set_length(SH_HISTORY_LENGTH);

    home = homenod.namval;
    if (home == NULL || *home == '\0')
        return;
    home_length = strlen(home);
    if (home_length + sizeof("/.sh_history") > sizeof(history_file))
        return;
    strcpy(history_file, home);
    if (home[home_length - 1] != '/')
        strcat(history_file, "/");
    strcat(history_file, ".sh_history");
    (void) read_history(history_file);
}

void sh_edit_prompt(const char *prompt_string)
{
    next_prompt = prompt_string ? prompt_string : "";
}

int sh_edit_read(char *buffer, int size, int input_fd, int output_fd)
{
    int copied = 0;

    if (size <= 0)
        return -1;

    if (pending_line == NULL && !pending_newline) {
        errno = 0;
        pending_line = readline_fd(next_prompt ? next_prompt : "",
                                   input_fd, output_fd);
        next_prompt = ps2nod.namval;
        if (pending_line == NULL)
            return errno == EINTR || errno == EAGAIN ? -1 : 0;

        pending_length = strlen(pending_line);
        pending_offset = 0;
        pending_newline = 1;
        if (pending_length != 0)
            add_history(pending_line);
    }

    while (copied < size && pending_offset < pending_length)
        buffer[copied++] = pending_line[pending_offset++];
    if (copied < size && pending_offset == pending_length &&
        pending_newline) {
        buffer[copied++] = '\n';
        pending_newline = 0;
        free(pending_line);
        pending_line = NULL;
    }
    return copied;
}

void sh_edit_save_history(void)
{
    if (!edit_initialized || edit_owner_pid != getpid())
        return;
    if (history_file[0] != '\0')
        (void) write_history(history_file);
}
