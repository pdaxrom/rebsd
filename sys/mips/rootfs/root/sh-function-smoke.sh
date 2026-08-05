#!/bin/sh

inner()
{
	test "$#" -eq 2 || return 1
	test "$1" = nested || return 1
	shift
	test "$1" = value || return 1
}

outer()
{
	test "$#" -eq 2 || return 1
	test "$1" = first || return 1
	inner nested value || return 1
	test "$#" -eq 2 || return 1
	test "$1" = first || return 1
	shift
	test "$#" -eq 1 || return 1
	test "$1" = second || return 1
}

outer first second || {
	echo "sh-function-smoke: nested positional parameters corrupted" >&2
	exit 1
}

test "$#" -eq 0 || {
	echo "sh-function-smoke: top-level positional parameters corrupted" >&2
	exit 1
}

echo "sh-function-smoke ok"
