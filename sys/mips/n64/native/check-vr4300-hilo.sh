#!/bin/sh
set -e

if [ "$#" -ne 1 ]; then
	echo "usage: $0 kernel.dis" >&2
	exit 2
fi

awk '
function is_mf(op) {
	return op == "mfhi" || op == "mflo"
}
function is_multdiv(op) {
	return op == "mult" || op == "multu" ||
	    op == "dmult" || op == "dmultu" ||
	    op == "div" || op == "divu" ||
	    op == "ddiv" || op == "ddivu"
}
/^[[:space:]]*[[:xdigit:]]+:[[:space:]]+[[:xdigit:]]+[[:space:]]+/ {
	op = $3
	if (is_multdiv(op) && (is_mf(previous) || is_mf(before_previous))) {
		printf "%s:%d: unsafe VR4300 HI/LO sequence before: %s\n", \
		    FILENAME, FNR, $0 > "/dev/stderr"
		bad = 1
	}
	before_previous = previous
	previous = op
}
END { exit bad ? 1 : 0 }
' "$1"

echo "check-vr4300-hilo: ok"
