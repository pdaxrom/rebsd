/*
 * Copyright (c) 2026 ReBSD contributors.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CHKCONFIG_DIR
#define CHKCONFIG_DIR "/var/config"
#endif

struct flag_entry {
    char *name;
    int enabled;
};

static void
usage(void)
{
    fprintf(stderr,
        "usage: chkconfig [-s]\n"
        "       chkconfig flag\n"
        "       chkconfig [-f] flag [on|off]\n");
    exit(2);
}

static char *
flag_path(const char *name)
{
    size_t dirlen, namelen;
    char *path;

    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        errno = EINVAL;
        return NULL;
    }
    dirlen = strlen(CHKCONFIG_DIR);
    namelen = strlen(name);
    path = malloc(dirlen + namelen + 2);
    if (path == NULL)
        return NULL;
    memcpy(path, CHKCONFIG_DIR, dirlen);
    path[dirlen] = '/';
    memcpy(path + dirlen + 1, name, namelen + 1);
    return path;
}

static int
flag_enabled(const char *name, int *exists)
{
    char buf[128];
    char *path;
    size_t i;
    ssize_t count;
    int enabled = 0, fd, previous_o = 0;

    *exists = 0;
    path = flag_path(name);
    if (path == NULL)
        return 0;
    fd = open(path, O_RDONLY);
    free(path);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }
    *exists = 1;
    while ((count = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < (size_t)count; i++) {
            if (previous_o && buf[i] == 'n') {
                enabled = 1;
                break;
            }
            previous_o = buf[i] == 'o';
        }
        if (enabled)
            break;
    }
    if (close(fd) < 0 && count >= 0)
        count = -1;
    if (count < 0)
        return -1;
    return enabled;
}

static int
name_compare(const void *left, const void *right)
{
    const struct flag_entry *a = left;
    const struct flag_entry *b = right;

    return strcmp(a->name, b->name);
}

static int
state_compare(const void *left, const void *right)
{
    const struct flag_entry *a = left;
    const struct flag_entry *b = right;

    if (a->enabled != b->enabled)
        return a->enabled - b->enabled;
    return strcmp(a->name, b->name);
}

static int
list_flags(int by_state)
{
    struct flag_entry *flags = NULL, *grown;
    struct dirent *entry;
    struct stat st;
    DIR *dir;
    char *path;
    size_t count = 0, capacity = 0, i;
    int enabled, exists, result = 1;

    dir = opendir(CHKCONFIG_DIR);
    if (dir == NULL) {
        perror(CHKCONFIG_DIR);
        return 1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
            (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
            continue;
        path = flag_path(entry->d_name);
        if (path == NULL)
            goto out;
        if (stat(path, &st) < 0) {
            perror(path);
            free(path);
            goto out;
        }
        free(path);
        if (!S_ISREG(st.st_mode))
            continue;
        enabled = flag_enabled(entry->d_name, &exists);
        if (enabled < 0) {
            perror(entry->d_name);
            goto out;
        }
        if (count == capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            grown = realloc(flags, capacity * sizeof(*flags));
            if (grown == NULL)
                goto out;
            flags = grown;
        }
        flags[count].name = malloc(strlen(entry->d_name) + 1);
        if (flags[count].name == NULL)
            goto out;
        strcpy(flags[count].name, entry->d_name);
        flags[count].enabled = enabled;
        count++;
    }
    qsort(flags, count, sizeof(*flags),
        by_state ? state_compare : name_compare);
    printf("Flag                 State\n");
    printf("====                 =====\n");
    for (i = 0; i < count; i++)
        printf("%-20s %s\n", flags[i].name,
            flags[i].enabled ? "on" : "off");
    result = 0;
out:
    if (closedir(dir) < 0 && result == 0) {
        perror(CHKCONFIG_DIR);
        result = 1;
    }
    for (i = 0; i < count; i++)
        free(flags[i].name);
    free(flags);
    return result;
}

static int
set_flag(const char *name, const char *state, int force)
{
    const char *value;
    char *path;
    struct stat st;
    ssize_t length, written;
    int fd, exists;

    if (strcmp(state, "on") == 0)
        value = "on\n";
    else if (strcmp(state, "off") == 0)
        value = "off\n";
    else
        usage();

    path = flag_path(name);
    if (path == NULL) {
        fprintf(stderr, "chkconfig: invalid flag name: %s\n", name);
        return 1;
    }
    if (stat(path, &st) == 0)
        exists = 1;
    else if (errno == ENOENT)
        exists = 0;
    else {
        perror(path);
        free(path);
        return 1;
    }
    if (!exists && !force) {
        fprintf(stderr, "chkconfig: flag does not exist: %s\n", name);
        free(path);
        return 1;
    }
    if (exists && !S_ISREG(st.st_mode)) {
        fprintf(stderr, "chkconfig: flag is not a regular file: %s\n", name);
        free(path);
        return 1;
    }
    fd = open(path, O_WRONLY | O_TRUNC | (force ? O_CREAT : 0), 0644);
    if (fd < 0) {
        perror(path);
        free(path);
        return 1;
    }
    length = (ssize_t)strlen(value);
    written = write(fd, value, (size_t)length);
    if (written != length) {
        if (written >= 0)
            errno = EIO;
        perror(path);
        close(fd);
        free(path);
        return 1;
    }
    if (close(fd) < 0) {
        perror(path);
        free(path);
        return 1;
    }
    free(path);
    return 0;
}

int
main(int argc, char **argv)
{
    int force = 0, by_state = 0, enabled, exists;
    int ch;

    while ((ch = getopt(argc, argv, "fs")) != -1) {
        switch (ch) {
        case 'f':
            force = 1;
            break;
        case 's':
            by_state = 1;
            break;
        default:
            usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0) {
        if (force)
            usage();
        return list_flags(by_state);
    }
    if (by_state || argc > 2)
        usage();
    if (argc == 2)
        return set_flag(argv[0], argv[1], force);
    if (force)
        usage();

    enabled = flag_enabled(argv[0], &exists);
    if (enabled < 0) {
        perror(argv[0]);
        return 1;
    }
    return exists && enabled ? 0 : 1;
}
