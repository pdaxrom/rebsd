#!/bin/sh
# Run the legacy recursive Makefiles in a mirrored object-directory tree.

set -eu

die()
{
	echo "objmake: $*" >&2
	exit 2
}

canonical_dir()
{
	(cd "$1" 2>/dev/null && pwd -P) || return 1
}

initial=0
requested_dir=
makefile=
REBSD_PRIVATE_BUILD=${REBSD_PRIVATE_BUILD:-0}

if [ "${1-}" = "--top-source" ]; then
	[ "$#" -ge 6 ] || die "incomplete initial invocation"
	REBSD_TOPSRC=$(canonical_dir "$2") || die "invalid source root: $2"
	shift 2
	[ "${1-}" = "--top-object" ] || die "--top-object is required"
	mkdir -p "$2"
	REBSD_TOPOBJ=$(canonical_dir "$2") || die "invalid object root: $2"
	shift 2
	[ "${1-}" = "--source-dir" ] || die "--source-dir is required"
	REBSD_SRCDIR=$(canonical_dir "$2") || die "invalid source directory: $2"
	shift 2
	REBSD_OBJTOP=$REBSD_TOPOBJ/obj
	mkdir -p "$REBSD_OBJTOP"
	initial=1
else
	: "${REBSD_TOPSRC:?objmake recursive call lacks REBSD_TOPSRC}"
	: "${REBSD_TOPOBJ:?objmake recursive call lacks REBSD_TOPOBJ}"
	: "${REBSD_OBJTOP:?objmake recursive call lacks REBSD_OBJTOP}"
	: "${REBSD_SRCDIR:?objmake recursive call lacks REBSD_SRCDIR}"
fi

mkdir -p "$REBSD_TOPOBJ/tmp"
args_file=$REBSD_TOPOBJ/tmp/objmake-args.$$
trap 'rm -f "$args_file"' 0 1 2 3 15
: > "$args_file"

# The old tree uses only simple make arguments. Preserve their ordering while
# translating -C and -f to the source/object directory pair.
while [ "$#" -gt 0 ]; do
	case "$1" in
	-C)
		[ "$#" -ge 2 ] || die "missing argument after -C"
		requested_dir=$2
		shift 2
		;;
	-f)
		[ "$#" -ge 2 ] || die "missing argument after -f"
		makefile=$2
		shift 2
		;;
	*)
		printf '%s\n' "$1" >> "$args_file"
		shift
		;;
	esac
done

if [ -n "$requested_dir" ]; then
	case "$requested_dir" in
	"$REBSD_TOPSRC"|"$REBSD_TOPSRC"/*)
		REBSD_SRCDIR=$(canonical_dir "$requested_dir") || die "invalid source directory: $requested_dir"
		rel=${REBSD_SRCDIR#"$REBSD_TOPSRC"}
		rel=${rel#/}
		REBSD_OBJDIR=$REBSD_OBJTOP/${rel:-.}
		;;
	"$REBSD_OBJTOP"|"$REBSD_OBJTOP"/*)
		mkdir -p "$requested_dir"
		REBSD_OBJDIR=$(canonical_dir "$requested_dir") || die "invalid object directory: $requested_dir"
		rel=${REBSD_OBJDIR#"$REBSD_OBJTOP"}
		rel=${rel#/}
		candidate=$REBSD_TOPSRC/${rel:-.}
		if [ -d "$candidate" ]; then
			REBSD_SRCDIR=$candidate
			REBSD_PRIVATE_BUILD=0
		else
			REBSD_SRCDIR=$REBSD_OBJDIR
			REBSD_PRIVATE_BUILD=1
		fi
		;;
	/*)
		die "recursive -C path is outside source/object trees: $requested_dir"
		;;
	*)
		if [ "$initial" -eq 1 ]; then
			REBSD_SRCDIR=$(canonical_dir "$REBSD_SRCDIR/$requested_dir") || die "invalid source directory: $requested_dir"
			rel=${REBSD_SRCDIR#"$REBSD_TOPSRC"}
			rel=${rel#/}
			REBSD_OBJDIR=$REBSD_OBJTOP/${rel:-.}
		else
			mkdir -p "$PWD/$requested_dir"
			REBSD_OBJDIR=$(canonical_dir "$PWD/$requested_dir") || die "invalid object directory: $requested_dir"
			rel=${REBSD_OBJDIR#"$REBSD_OBJTOP"}
			rel=${rel#/}
			candidate=$REBSD_TOPSRC/${rel:-.}
			if [ -d "$candidate" ]; then
				REBSD_SRCDIR=$candidate
				REBSD_PRIVATE_BUILD=0
			else
				REBSD_SRCDIR=$REBSD_OBJDIR
				REBSD_PRIVATE_BUILD=1
			fi
		fi
		;;
	esac
else
	if [ "$initial" -eq 1 ]; then
		rel=${REBSD_SRCDIR#"$REBSD_TOPSRC"}
		rel=${rel#/}
		REBSD_OBJDIR=$REBSD_OBJTOP/${rel:-.}
	else
		if [ "$REBSD_PRIVATE_BUILD" -eq 1 ]; then
			REBSD_OBJDIR=$(canonical_dir "$PWD")
			REBSD_SRCDIR=$REBSD_OBJDIR
		else
			case "$PWD" in
			"$REBSD_OBJTOP"|"$REBSD_OBJTOP"/*)
			REBSD_OBJDIR=$(canonical_dir "$PWD")
			rel=${REBSD_OBJDIR#"$REBSD_OBJTOP"}
			rel=${rel#/}
			REBSD_SRCDIR=$REBSD_TOPSRC/${rel:-.}
				;;
			*)
				die "recursive make ran outside object tree: $PWD"
				;;
			esac
		fi
	fi
fi

[ -d "$REBSD_SRCDIR" ] || die "source directory does not exist: $REBSD_SRCDIR"
mkdir -p "$REBSD_OBJDIR" "$REBSD_TOPOBJ/tmp"

if [ -z "$makefile" ]; then
	if [ -f "$REBSD_OBJDIR/Makefile" ]; then
		makefile=$REBSD_OBJDIR/Makefile
	else
		makefile=$REBSD_SRCDIR/Makefile
	fi
else
	case "$makefile" in
	/*) ;;
	*) makefile=$REBSD_SRCDIR/$makefile ;;
	esac
fi
[ -f "$makefile" ] || die "makefile does not exist: $makefile"

export REBSD_TOPSRC REBSD_TOPOBJ REBSD_OBJTOP REBSD_SRCDIR REBSD_OBJDIR
export REBSD_PRIVATE_BUILD
export TOPSRC=$REBSD_TOPSRC TOPOBJ=$REBSD_TOPOBJ OBJTOP=$REBSD_OBJTOP
export TMPDIR=$REBSD_TOPOBJ/tmp
MAKEFILES=$REBSD_TOPSRC/mk/obj-rules.mk
export MAKEFILES

real_make=${REBSD_REAL_MAKE:-make}
self=$REBSD_TOPSRC/tools/build/objmake.sh

set --
while IFS= read -r arg; do
	set -- "$@" "$arg"
done < "$args_file"
rm -f "$args_file"
trap - 0 1 2 3 15

exec "$real_make" -C "$REBSD_OBJDIR" -I "$REBSD_SRCDIR" -f "$makefile" \
    REBSD_OBJ_BUILD=1 TOPSRC="$REBSD_TOPSRC" TOPOBJ="$REBSD_TOPOBJ" \
    OBJTOP="$REBSD_OBJTOP" MAKE="$self" "$@"
