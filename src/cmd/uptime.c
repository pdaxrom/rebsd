/*
 * Print system uptime and load averages.
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <utmp.h>

#define DIV60(t) (((t) + 30) / 60)

static int
count_users(void)
{
    FILE *fp;
    struct utmp ut;
    int users;

    fp = fopen(_PATH_UTMP, "r");
    if (fp == NULL)
        return 0;
    users = 0;
    while (fread(&ut, sizeof(ut), 1, fp) == 1)
        if (ut.ut_name[0] != '\0')
            users++;
    fclose(fp);
    return users;
}

static time_t
boot_time(void)
{
    int mib[2];
    struct timeval boottime;
    size_t size;

    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;
    size = sizeof(boottime);
    if (sysctl(mib, 2, &boottime, &size, NULL, 0) < 0) {
        perror("kern.boottime");
        exit(1);
    }
    return boottime.tv_sec;
}

int
main(void)
{
    unsigned load[3];
    time_t now, up;
    struct tm *tm;
    int days, hrs, mins, users;

    time(&now);
    tm = localtime(&now);
    if (tm)
        printf(" %2d:%02d", tm->tm_hour, tm->tm_min);

    up = now - boot_time();
    if (up < 0)
        up = 0;
    days = up / (24L * 60L * 60L);
    up %= 24L * 60L * 60L;
    hrs = up / (60L * 60L);
    up %= 60L * 60L;
    mins = DIV60(up);

    printf(" up");
    if (days > 0)
        printf(" %d day%s,", days, days == 1 ? "" : "s");
    if (hrs > 0)
        printf(" %2d:%02d,", hrs, mins);
    else
        printf(" %d min%s,", mins, mins == 1 ? "" : "s");

    users = count_users();
    printf(" %d user%s", users, users == 1 ? "" : "s");

    if (getloadavg(load, 3) == 3)
        printf(", load averages: %u.%02u %u.%02u %u.%02u",
            load[0] / 100, load[0] % 100,
            load[1] / 100, load[1] % 100,
            load[2] / 100, load[2] % 100);
    printf("\n");
    return 0;
}
