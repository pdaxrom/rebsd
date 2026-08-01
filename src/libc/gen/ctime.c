/*
 * Copyright (c) 1987 Regents of the University of California.
 * This file may be freely redistributed provided that this
 * notice remains attached.
 */
#include <sys/param.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <alloca.h>
#include <tzfile.h>
#include <paths.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

char *
ctime(const time_t *t)
{
	struct tm *tm;

	tm = localtime(t);
	return tm == 0 ? 0 : asctime(tm);
}

/*
** A la X3J11
*/

char *
asctime(const struct tm *timeptr)
{
	static char	wday_name[DAYS_PER_WEEK][4] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static char	mon_name[MONS_PER_YEAR][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	static char	result[26];
	long long	year;

	year = (long long)TM_YEAR_BASE + timeptr->tm_year;
	if (year < 0 || year > 9999) {
		errno = EOVERFLOW;
		return 0;
	}
	(void) snprintf(result, sizeof(result),
		"%.3s %.3s%3d %02d:%02d:%02d %04d\n",
		wday_name[timeptr->tm_wday],
		mon_name[timeptr->tm_mon],
		timeptr->tm_mday, timeptr->tm_hour,
		timeptr->tm_min, timeptr->tm_sec,
		(int)year);
	return result;
}

static struct tm *
offtime(const time_t *clock, long offset)
{
	long long		days;
	long long		era;
	long long		year;
	long long		value;
	unsigned		day_of_era;
	unsigned		year_of_era;
	unsigned		day_of_year;
	unsigned		month_prime;
	unsigned		month;
	long			rem;
	static struct tm	tm;

	if ((offset > 0 && *clock > LLONG_MAX - offset) ||
	    (offset < 0 && *clock < LLONG_MIN - offset)) {
		errno = EOVERFLOW;
		return 0;
	}
	value = *clock + offset;
	days = value / SECS_PER_DAY;
	rem = (long)(value % SECS_PER_DAY);
	while (rem < 0) {
		rem += SECS_PER_DAY;
		--days;
	}
	tm.tm_hour = (int) (rem / SECS_PER_HOUR);
	rem = rem % SECS_PER_HOUR;
	tm.tm_min = (int) (rem / SECS_PER_MIN);
	tm.tm_sec = (int) (rem % SECS_PER_MIN);
	tm.tm_wday = (int) ((EPOCH_WDAY + days) % DAYS_PER_WEEK);
	if (tm.tm_wday < 0)
		tm.tm_wday += DAYS_PER_WEEK;

	days += 719468;
	era = (days >= 0 ? days : days - 146096) / 146097;
	day_of_era = (unsigned)(days - era * 146097);
	year_of_era = (day_of_era - day_of_era / 1460 +
	    day_of_era / 36524 - day_of_era / 146096) / 365;
	year = (long long)year_of_era + era * 400;
	day_of_year = day_of_era - (365 * year_of_era +
	    year_of_era / 4 - year_of_era / 100);
	month_prime = (5 * day_of_year + 2) / 153;
	month = month_prime < 10 ? month_prime + 3 : month_prime - 9;
	year += month <= 2;
	if (year - TM_YEAR_BASE < INT_MIN ||
	    year - TM_YEAR_BASE > INT_MAX) {
		errno = EOVERFLOW;
		return 0;
	}
	tm.tm_year = (int)(year - TM_YEAR_BASE);
	if (month <= 2)
		tm.tm_yday = (int)day_of_year - 306;
	else
		tm.tm_yday = (int)day_of_year + 59 + isleap((int)year);
	tm.tm_mon = (int)month - 1;
	tm.tm_mday = (int)(day_of_year -
	    (153 * month_prime + 2) / 5 + 1);
	tm.tm_isdst = 0;
	tm.tm_zone = "";
	tm.tm_gmtoff = offset;
	return &tm;
}

#ifndef TRUE
#define TRUE		1
#define FALSE		0
#endif /* !TRUE */

struct ttinfo {				/* time type information */
	long		tt_gmtoff;	/* GMT offset in seconds */
	int		tt_isdst;	/* used to set tm_isdst */
	int		tt_abbrind;	/* abbreviation list index */
};

struct state {
	int		timecnt;
	int		typecnt;
	int		charcnt;
	time_t		ats[TZ_MAX_TIMES];
	unsigned char	types[TZ_MAX_TIMES];
	struct ttinfo	ttis[TZ_MAX_TYPES];
	char		chars[TZ_MAX_CHARS + 1];
};

static struct state	s;

static int		tz_is_set;

char *			tzname[2] = {
	"GMT",
	"GMT"
};

extern char *tztab(int, int);

#ifdef USG_COMPAT
time_t			timezone = 0;
int			daylight = 0;
#endif /* USG_COMPAT */

struct tzcounts {
	unsigned long ttisutcnt;
	unsigned long ttisstdcnt;
	unsigned long leapcnt;
	unsigned long timecnt;
	unsigned long typecnt;
	unsigned long charcnt;
};

static unsigned long
detzu32(const char *codep)
{
	unsigned long result;
	int i;

	result = 0;
	for (i = 0; i < 4; ++i)
		result = (result << 8) | (codep[i] & 0xff);
	return result;
}

static long
detzcode32(const char *codep)
{
	unsigned long value;

	value = detzu32(codep);
	if (value & 0x80000000ul)
		return (-2147483647L - 1L) +
		    (long)(value & 0x7ffffffful);
	return (long)value;
}

static time_t
detzcode64(const char *codep)
{
	unsigned long long value;
	int i;

	value = 0;
	for (i = 0; i < 8; ++i)
		value = (value << 8) | (codep[i] & 0xff);
	if (value & 0x8000000000000000ull)
		return (time_t)(LLONG_MIN +
		    (long long)(value & 0x7fffffffffffffffull));
	return (time_t)value;
}

static void
tzdecodecounts(const struct tzhead *header, struct tzcounts *counts)
{
	counts->ttisutcnt = detzu32(header->tzh_ttisutcnt);
	counts->ttisstdcnt = detzu32(header->tzh_ttisstdcnt);
	counts->leapcnt = detzu32(header->tzh_leapcnt);
	counts->timecnt = detzu32(header->tzh_timecnt);
	counts->typecnt = detzu32(header->tzh_typecnt);
	counts->charcnt = detzu32(header->tzh_charcnt);
}

static int
tzsizeadd(size_t *size, unsigned long count, size_t width)
{
	if (count > ((size_t)-1 - *size) / width)
		return -1;
	*size += count * width;
	return 0;
}

static int
tzblocksize(const struct tzcounts *counts, size_t time_width, size_t *result)
{
	size_t size;

	size = 0;
	if (tzsizeadd(&size, counts->timecnt, time_width) != 0 ||
	    tzsizeadd(&size, counts->timecnt, 1) != 0 ||
	    tzsizeadd(&size, counts->typecnt, 6) != 0 ||
	    tzsizeadd(&size, counts->charcnt, 1) != 0 ||
	    tzsizeadd(&size, counts->leapcnt, time_width + 4) != 0 ||
	    tzsizeadd(&size, counts->ttisstdcnt, 1) != 0 ||
	    tzsizeadd(&size, counts->ttisutcnt, 1) != 0)
		return -1;
	*result = size;
	return 0;
}

static int
tzparseblock(const struct tzhead *header, const char *data, size_t available,
    size_t time_width)
{
	struct tzcounts counts;
	const char *p;
	size_t block_size;
	int i;

	tzdecodecounts(header, &counts);
	if (counts.timecnt > TZ_MAX_TIMES || counts.typecnt == 0 ||
	    counts.typecnt > TZ_MAX_TYPES || counts.charcnt > TZ_MAX_CHARS ||
	    (counts.ttisstdcnt != 0 && counts.ttisstdcnt != counts.typecnt) ||
	    (counts.ttisutcnt != 0 && counts.ttisutcnt != counts.typecnt) ||
	    tzblocksize(&counts, time_width, &block_size) != 0 ||
	    block_size > available)
		return -1;

	s.timecnt = (int)counts.timecnt;
	s.typecnt = (int)counts.typecnt;
	s.charcnt = (int)counts.charcnt;
	p = data;
	for (i = 0; i < s.timecnt; ++i) {
		s.ats[i] = time_width == 8 ? detzcode64(p) : detzcode32(p);
		p += time_width;
	}
	for (i = 0; i < s.timecnt; ++i)
		s.types[i] = (unsigned char)*p++;
	for (i = 0; i < s.typecnt; ++i) {
		struct ttinfo *ttisp;

		ttisp = &s.ttis[i];
		ttisp->tt_gmtoff = detzcode32(p);
		p += 4;
		ttisp->tt_isdst = (unsigned char)*p++;
		ttisp->tt_abbrind = (unsigned char)*p++;
	}
	for (i = 0; i < s.charcnt; ++i)
		s.chars[i] = *p++;
	s.chars[i] = '\0';

	for (i = 0; i < s.timecnt; ++i)
		if (s.types[i] >= s.typecnt ||
		    (i != 0 && s.ats[i] <= s.ats[i - 1]))
			return -1;
	for (i = 0; i < s.typecnt; ++i)
		if (s.ttis[i].tt_abbrind >= s.charcnt)
			return -1;
	return 0;
}

static int
tzload(char *name)
{
	int i;
	int fid;
	char *buf;
	off_t file_size;
	size_t size;
	size_t used;
	ssize_t count;
	struct tzhead *header;
	struct tzcounts counts;
	size_t block_size;
	int error;

	if (name == 0 && (name = _PATH_LOCALTIME) == 0)
		return -1;
	{
		register char *	p;
		register int	doaccess;
                char          * fullname;

		doaccess = name[0] == '/';
		if (!doaccess) {
			if ((p = _PATH_ZONEINFO) == 0)
				return -1;
			if ((strlen(p) + strlen(name) + 1) >= MAXPATHLEN)
				return -1;
                        fullname = alloca(MAXPATHLEN);
			(void) strcpy(fullname, p);
			(void) strcat(fullname, "/");
			(void) strcat(fullname, name);
			/*
			** Set doaccess if '.' (as in "../") shows up in name.
			*/
			while (*name != '\0')
				if (*name++ == '.')
					doaccess = TRUE;
			name = fullname;
		}
		if (doaccess && access(name, 4) != 0)
			return -1;
		if ((fid = open(name, 0)) == -1)
			return -1;
	}
	file_size = lseek(fid, (off_t)0, SEEK_END);
	if (file_size < (off_t)sizeof(struct tzhead) ||
	    (off_t)(size_t)file_size != file_size ||
	    lseek(fid, (off_t)0, SEEK_SET) < 0) {
		(void)close(fid);
		return -1;
	}
	size = (size_t)file_size;
	buf = malloc(size);
	if (buf == NULL) {
		(void)close(fid);
		return -1;
	}
	used = 0;
	while (used < size) {
		count = read(fid, buf + used, size - used);
		if (count <= 0)
			break;
		used += (size_t)count;
	}
	if (close(fid) != 0 || used != size) {
		free(buf);
		return -1;
	}

	header = (struct tzhead *)buf;
	error = -1;
	if (memcmp(header->tzh_magic, TZ_MAGIC, 4) == 0 &&
	    header->tzh_version[0] >= TZ_VERSION_2) {
		tzdecodecounts(header, &counts);
		if (tzblocksize(&counts, 4, &block_size) == 0 &&
		    block_size <= size - sizeof(*header) &&
		    block_size + sizeof(*header) <=
		    size - sizeof(*header)) {
			header = (struct tzhead *)(buf + sizeof(*header) +
			    block_size);
			if (memcmp(header->tzh_magic, TZ_MAGIC, 4) == 0)
				error = tzparseblock(header,
				    (char *)header + sizeof(*header),
				    size - ((char *)header - buf) - sizeof(*header), 8);
		}
	} else {
		error = tzparseblock(header, buf + sizeof(*header),
		    size - sizeof(*header), 4);
	}
	free(buf);
	if (error != 0)
		return -1;
	/*
	** Set tzname elements to initial values.
	*/
	tzname[0] = tzname[1] = &s.chars[0];
#ifdef USG_COMPAT
	timezone = s.ttis[0].tt_gmtoff;
	daylight = 0;
#endif /* USG_COMPAT */
	for (i = 1; i < s.typecnt; ++i) {
		register struct ttinfo *	ttisp;

		ttisp = &s.ttis[i];
		if (ttisp->tt_isdst) {
			tzname[1] = &s.chars[ttisp->tt_abbrind];
#ifdef USG_COMPAT
			daylight = 1;
#endif /* USG_COMPAT */
		} else {
			tzname[0] = &s.chars[ttisp->tt_abbrind];
#ifdef USG_COMPAT
			timezone = ttisp->tt_gmtoff;
#endif /* USG_COMPAT */
		}
	}
	return 0;
}

static int
tzsetkernel(void)
{
	struct timeval	tv;
	struct timezone	tz;

	if (gettimeofday(&tv, &tz))
		return -1;
	s.timecnt = 0;		/* UNIX counts *west* of Greenwich */
	s.typecnt = 1;
	s.ttis[0].tt_gmtoff = tz.tz_minuteswest * -SECS_PER_MIN;
	s.ttis[0].tt_isdst = 0;
	s.ttis[0].tt_abbrind = 0;
	(void)strcpy(s.chars, tztab(tz.tz_minuteswest, 0));
	s.charcnt = strlen(s.chars) + 1;
	tzname[0] = tzname[1] = s.chars;
#ifdef USG_COMPAT
	timezone = tz.tz_minuteswest * 60;
	daylight = tz.tz_dsttime;
#endif /* USG_COMPAT */
	return 0;
}

static void
tzsetgmt(void)
{
	s.timecnt = 0;
	s.typecnt = 1;
	s.ttis[0].tt_gmtoff = 0;
	s.ttis[0].tt_isdst = 0;
	s.ttis[0].tt_abbrind = 0;
	(void) strcpy(s.chars, "GMT");
	s.charcnt = 4;
	tzname[0] = tzname[1] = s.chars;
#ifdef USG_COMPAT
	timezone = 0;
	daylight = 0;
#endif /* USG_COMPAT */
}

void
tzset(void)
{
	register char *	name;

	tz_is_set = TRUE;
	name = getenv("TZ");
	if (!name || *name) {			/* did not request GMT */
		if (name && !tzload(name))	/* requested name worked */
			return;
		if (!tzload((char *)0))		/* default name worked */
			return;
		if (!tzsetkernel())		/* kernel guess worked */
			return;
	}
	tzsetgmt();				/* GMT is default */
}

struct tm *
localtime(const time_t *timep)
{
	register struct ttinfo *	ttisp;
	register struct tm *		tmp;
	register int			i;
	time_t				t;

	if (!tz_is_set)
		(void) tzset();
	t = *timep;
	if (s.timecnt == 0 || t < s.ats[0]) {
		i = 0;
		while (s.ttis[i].tt_isdst)
			if (++i >= s.typecnt) {
				i = 0;
				break;
			}
	} else {
		for (i = 1; i < s.timecnt; ++i)
			if (t < s.ats[i])
				break;
		i = s.types[i - 1];
	}
	ttisp = &s.ttis[i];
	/*
	** To get (wrong) behavior that's compatible with System V Release 2.0
	** you'd replace the statement below with
	**	tmp = offtime((time_t) (t + ttisp->tt_gmtoff), 0L);
	*/
	tmp = offtime(&t, ttisp->tt_gmtoff);
	if (tmp == 0)
		return 0;
	tmp->tm_isdst = ttisp->tt_isdst;
	tzname[tmp->tm_isdst] = &s.chars[ttisp->tt_abbrind];
	tmp->tm_zone = &s.chars[ttisp->tt_abbrind];
	return tmp;
}

struct tm *
gmtime(const time_t *clock)
{
	register struct tm *	tmp;

	tmp = offtime(clock, 0L);
	if (tmp == 0)
		return 0;
	tzname[0] = "GMT";
	tmp->tm_zone = "GMT";		/* UCT ? */
	return tmp;
}
