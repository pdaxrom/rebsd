#!/bin/sh
set -e

if [ "$#" -ne 1 ]; then
	echo "usage: $0 program.dis" >&2
	exit 2
fi

awk '
/^[[:space:]]*[[:xdigit:]]+:[[:space:]]+[[:xdigit:]]+[[:space:]]+/ {
	op = $3
	operands = $4
	gsub(/\$/, "", operands)
	if (op == "addiu" &&
	    (operands == "sp,s8,16" || operands == "sp,fp,16")) {
		printf "%s:%d: interrupt-unsafe MIPS frame epilogue: %s\n", \
		    FILENAME, FNR, $0 > "/dev/stderr"
		bad = 1
	}
}
END { exit bad ? 1 : 0 }
' "$1"

echo "check-mips-async-epilogue: ok"
