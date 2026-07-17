#!/bin/sh

fail()
{
	echo "release-smoke fail: $*"
	exit 1
}

echo "release-smoke: uname fields"
sysname=`uname -s` || fail "uname -s"
release=`uname -r` || fail "uname -r"
version=`uname -v` || fail "uname -v"
all=`uname -a` || fail "uname -a"

test "$sysname" = "ReBSD" || fail "uname -s returned '$sysname'"
test "$release" = "0.1-Resurgence" || fail "uname -r returned '$release'"
case "$version" in
"ReBSD 0.1-Resurgence (GENERIC) #"*": "*) ;;
*) fail "uname -v returned '$version'" ;;
esac
case "$all" in
*"ReBSD"*"0.1-Resurgence"*"GENERIC"*) ;;
*) fail "uname -a omitted release identity" ;;
esac
case "$all" in
*"built on"*|*"cpu="*|*"float="*|*"endian="*)
	fail "uname -a leaked detailed build information: '$all'"
	;;
esac

echo "release-smoke: sysctl fields"
sysname=`sysctl -n kern.ostype` || fail "kern.ostype"
release=`sysctl -n kern.osrelease` || fail "kern.osrelease"
version=`sysctl -n kern.version` || fail "kern.version"
codename=`sysctl -n kern.codename` || fail "kern.codename"
compiler=`sysctl -n kern.compiler` || fail "kern.compiler"
builduser=`sysctl -n kern.builduser` || fail "kern.builduser"
buildhost=`sysctl -n kern.buildhost` || fail "kern.buildhost"
build=`sysctl -n kern.build` || fail "kern.build"
toolchain=`sysctl -n kern.toolchain` || fail "kern.toolchain"
toolversion=`sysctl -n kern.toolchain.version` ||
	fail "kern.toolchain.version"
gitrev=`sysctl -n kern.gitrev` || fail "kern.gitrev"
branch=`sysctl -n kern.branch` || fail "kern.branch"
dirty=`sysctl -n kern.dirty` || fail "kern.dirty"
cpu=`sysctl -n hw.cpu` || fail "hw.cpu"
fpu=`sysctl -n hw.fpu` || fail "hw.fpu"
byteorder=`sysctl -n hw.byteorder` || fail "hw.byteorder"
machine=`sysctl -n hw.machine` || fail "hw.machine"
buildinfo=`sysctl -n kern.buildinfo` || fail "kern.buildinfo"

test "$sysname" = "ReBSD" || fail "kern.ostype returned '$sysname'"
test "$release" = "0.1-Resurgence" ||
	fail "kern.osrelease returned '$release'"
test "$codename" = "Resurgence" ||
	fail "kern.codename returned '$codename'"
case "$version" in
"ReBSD 0.1-Resurgence (GENERIC) #1: "*) ;;
*) fail "kern.version returned '$version'" ;;
esac
case "$compiler" in
gcc|pcc) ;;
*) fail "kern.compiler returned '$compiler'" ;;
esac
test -n "$builduser" || fail "kern.builduser is empty"
test -n "$buildhost" || fail "kern.buildhost is empty"
case "$build" in
[1-9]|[1-9][0-9]*) ;;
*) fail "kern.build returned '$build'" ;;
esac
test -n "$toolchain" || fail "kern.toolchain is empty"
test -n "$toolversion" || fail "kern.toolchain.version is empty"
test -n "$gitrev" || fail "kern.gitrev is empty"
test -n "$branch" || fail "kern.branch is empty"
case "$dirty" in
0|1) ;;
*) fail "kern.dirty returned '$dirty'" ;;
esac
case "$cpu" in
mips32r2|vr4300) ;;
*) fail "hw.cpu returned '$cpu'" ;;
esac
case "$fpu" in
hard|soft) ;;
*) fail "hw.fpu returned '$fpu'" ;;
esac
case "$byteorder" in
1234|4321) ;;
*) fail "hw.byteorder returned '$byteorder'" ;;
esac
case "$machine" in
mips|mipsel) ;;
*) fail "hw.machine returned '$machine'" ;;
esac
case "$buildinfo" in
*"version: ReBSD 0.1-Resurgence"*"codename: Resurgence"*\
*"build: #$build"*"compiler: $compiler"*"git revision: $gitrev"*\
*"target: $machine"*) ;;
*) fail "kern.buildinfo is incomplete" ;;
esac

echo "release smoke ok"
