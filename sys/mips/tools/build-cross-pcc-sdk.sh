#!/bin/sh
set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
topsrc=$(CDPATH= cd -- "$script_dir/../../.." && pwd)

cpu=vr4300
float=hard
endian=big
prefix=$topsrc/cross-pcc
build=
target=cross-pcc-sdk

usage()
{
	cat >&2 <<EOF
usage: $0 [options]

Options:
  --cpu vr4300|mips32r2       Target CPU profile (default: vr4300)
  --float hard|soft           Runtime float ABI (default: hard)
  --endian big|little         Target endian selector (default: big)
  --prefix DIR                SDK install prefix (default: TOPSRC/cross-pcc)
  --build DIR                 Build directory (default: /private/tmp/...)
  --tools-only                Build compiler and binutils, not runtime
  --clean                     Remove the selected build directory
  -h, --help                  Show this help
EOF
	exit 2
}

while [ $# -gt 0 ]; do
	case "$1" in
	--cpu)
		[ $# -ge 2 ] || usage
		cpu=$2
		shift 2
		;;
	--float|--float-abi)
		[ $# -ge 2 ] || usage
		float=$2
		shift 2
		;;
	--endian)
		[ $# -ge 2 ] || usage
		endian=$2
		shift 2
		;;
	--prefix)
		[ $# -ge 2 ] || usage
		prefix=$2
		shift 2
		;;
	--build)
		[ $# -ge 2 ] || usage
		build=$2
		shift 2
		;;
	--tools-only)
		target=cross-pcc-sdk-tools
		shift
		;;
	--clean)
		target=clean-cross-pcc-sdk
		shift
		;;
	-h|--help)
		usage
		;;
	*)
		echo "unknown option: $1" >&2
		usage
		;;
	esac
done

make_args="MIPS_SDK_CPU=$cpu MIPS_SDK_FLOAT=$float MIPS_SDK_ENDIAN=$endian MIPS_SDK_PREFIX=$prefix"
if [ -n "$build" ]; then
	make_args="$make_args MIPS_SDK_BUILD=$build"
fi

cd "$topsrc/sys/mips"
# shellcheck disable=SC2086
exec ${MAKE:-make} -f sdk.mk $target $make_args
