#include <errno.h>
#include <limits.h>
#include <time.h>

/*
 * Return the number of days since 1970-01-01 in the proleptic Gregorian
 * calendar.  Month is 1 through 12.
 */
static long long
days_from_civil(long long year, unsigned month, unsigned day)
{
	long long era;
	unsigned yoe, doy, doe;

	year -= month <= 2;
	era = (year >= 0 ? year : year - 399) / 400;
	yoe = (unsigned)(year - era * 400);
	month = month > 2 ? month - 3 : month + 9;
	doy = (153 * month + 2) / 5 + day - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long)doe - 719468;
}

static long long
tm_seconds(const struct tm *tm)
{
	long long year, month, month0;

	year = (long long)tm->tm_year + 1900;
	month0 = tm->tm_mon;
	if (month0 < 0) {
		long long years;

		years = (-month0 + 11) / 12;
		year -= years;
		month0 += years * 12;
	}
	year += month0 / 12;
	month = month0 % 12 + 1;
	return days_from_civil(year, (unsigned)month, 1) * 86400 +
	    ((long long)tm->tm_mday - 1) * 86400 +
	    (long long)tm->tm_hour * 3600 +
	    (long long)tm->tm_min * 60 + tm->tm_sec;
}

static int
same_civil(const struct tm *tm, long long wanted)
{
	return tm_seconds(tm) == wanted;
}

static int
add_offset(long long *offsets, int count, long long offset)
{
	int i;

	for (i = 0; i < count; i++)
		if (offsets[i] == offset)
			return count;
	if (count < 16)
		offsets[count++] = offset;
	return count;
}

time_t
mktime(struct tm *tm)
{
	static const long sample_delta[] = {
		0, -86400L, 86400L, -7L * 86400, 7L * 86400,
		-90L * 86400, 90L * 86400, -180L * 86400,
		180L * 86400, -370L * 86400, 370L * 86400
	};
	long long offsets[16], wanted, sample, local_value, candidate;
	long long best_after_delta, best_delta;
	time_t sample_time, best_time, after_time, exact_time, exact_fallback;
	struct tm *local;
	int offset_count, have_best, have_after, have_exact;
	int have_exact_fallback, requested_isdst;
	unsigned i;
	int j;

	if (tm == 0) {
		errno = EINVAL;
		return (time_t)-1;
	}

	wanted = tm_seconds(tm);
	requested_isdst = tm->tm_isdst;
	offset_count = 0;

	/*
	 * Learn the offsets actually used by the active timezone around this
	 * date.  Testing candidates derived from each observed offset handles
	 * ordinary dates, non-hour offsets, DST folds, and DST gaps.
	 */
	for (i = 0; i < sizeof(sample_delta) / sizeof(sample_delta[0]); i++) {
		sample = wanted + sample_delta[i];
		if (sample < LLONG_MIN)
			sample = LLONG_MIN;
		else if (sample > LLONG_MAX)
			sample = LLONG_MAX;
		sample_time = (time_t)sample;
		local = localtime(&sample_time);
		if (local != 0)
			offset_count = add_offset(offsets, offset_count,
			    tm_seconds(local) - sample);
	}
	if (offset_count == 0) {
		errno = EOVERFLOW;
		return (time_t)-1;
	}

	have_best = 0;
	have_after = 0;
	have_exact = 0;
	have_exact_fallback = 0;
	best_after_delta = 0;
	best_delta = 0;
	best_time = 0;
	after_time = 0;
	exact_time = 0;
	exact_fallback = 0;

	for (j = 0; j < offset_count; j++) {
		long long delta, abs_delta;
		time_t t;

		candidate = wanted - offsets[j];
		t = (time_t)candidate;
		local = localtime(&t);
		if (local == 0)
			continue;
		local_value = tm_seconds(local);
		if (same_civil(local, wanted)) {
			if (requested_isdst < 0 ||
			    !!local->tm_isdst == !!requested_isdst) {
				if (!have_exact || t < exact_time) {
					exact_time = t;
					have_exact = 1;
				}
			} else if (!have_exact_fallback) {
				exact_fallback = t;
				have_exact_fallback = 1;
			}
			continue;
		}

		delta = local_value - wanted;
		abs_delta = delta < 0 ? -delta : delta;
		if (!have_best || abs_delta < best_delta) {
			best_delta = abs_delta;
			best_time = t;
			have_best = 1;
		}
		if (delta >= 0 &&
		    (!have_after || delta < best_after_delta)) {
			best_after_delta = delta;
			after_time = t;
			have_after = 1;
		}
	}

	if (have_exact)
		best_time = exact_time;
	else if (have_exact_fallback)
		best_time = exact_fallback;
	else if (have_after)
		best_time = after_time;
	else if (!have_best) {
		errno = EOVERFLOW;
		return (time_t)-1;
	}

	local = localtime(&best_time);
	if (local == 0) {
		errno = EOVERFLOW;
		return (time_t)-1;
	}
	*tm = *local;
	return best_time;
}

time_t
timegm(struct tm *tm)
{
	long long value;
	time_t result;
	struct tm *normalized;

	if (tm == 0) {
		errno = EINVAL;
		return (time_t)-1;
	}
	value = tm_seconds(tm);
	result = (time_t)value;
	normalized = gmtime(&result);
	if (normalized != 0)
		*tm = *normalized;
	return result;
}

double
difftime(time_t end, time_t beginning)
{
	return (double)end - (double)beginning;
}
