#!/bin/sh
#
# Shared MIPS target-side smoke test for /bin/cpp compatibility and the
# historical calendar(1) cpp integration.
#

echo "cpp-calendar-smoke diag v1"

tmp=/var/tmp/cpp-calendar-smoke.$$
rm -rf "$tmp"
mkdir "$tmp" || exit 1
trap 'rm -rf "$tmp"' 0 1 2 3 15

echo "step 1: /bin/cpp direct"
cat > "$tmp/cpp.in" <<'EOF'
#define CPP_CALENDAR_VALUE 42
CPP_CALENDAR_VALUE
EOF
/bin/cpp -P "$tmp/cpp.in" "$tmp/cpp.out" || exit 1
grep '^42$' "$tmp/cpp.out" >/dev/null || {
	echo "cpp-calendar-smoke: /bin/cpp failed"
	cat "$tmp/cpp.out"
	exit 1
}

echo "step 2: calendar through /bin/cpp"
cd "$tmp" || exit 1
set -- `date`
today="$2 $3"
cat > calendar <<'EOF'
#define CPP_CALENDAR_SMOKE cpp_calendar_smoke_ok
EOF
echo "$today	CPP_CALENDAR_SMOKE" >> calendar
/usr/bin/calendar > calendar.out || exit 1
grep 'cpp_calendar_smoke_ok' calendar.out >/dev/null || {
	echo "cpp-calendar-smoke: calendar failed"
	cat calendar.out
	exit 1
}

echo "cpp-calendar smoke ok"
