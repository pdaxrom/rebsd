#!/usr/bin/env python3
import argparse
import os
import shutil
from pathlib import Path


PRIMARY_TESTS = [
    ("c99__arith002", "c99", ["arith002.c"], ["/usr/lib/libm.a"]),
    ("c99__arith003", "c99", ["arith003.c"], ["/usr/lib/libm.a"]),
    ("jira__PCC-70", "jira", ["PCC-70.c"], []),
    ("jira__PCC-77", "jira", ["PCC-77.c"], []),
    ("jira__PCC-152", "jira", ["PCC-152.c"], []),
    ("jira__PCC-185", "jira", ["PCC-185.c"], []),
]

N64_LOG_TESTS = PRIMARY_TESTS + [
    ("jira__PCC-84", "jira", ["PCC-84.c"], []),
    ("jira__PCC-85", "jira", ["PCC-85.c"], []),
    ("misc__llcall001", "misc", ["llcall001.c"], []),
    ("misc__ssastrength001", "misc", ["ssastrength001.c"], []),
    ("misc__ssacounted001", "misc", ["ssacounted001.c"], []),
    ("misc__switchtable001", "misc", ["switchtable001.c"], []),
    ("misc__pointertemp001", "misc", ["pointertemp001.c"], []),
    ("jira__PCC-97", "jira", ["PCC-97.c"], []),
    ("jira__PCC-101", "jira", ["PCC-101.c"], []),
    ("jira__PCC-123", "jira", ["PCC-123.c"], []),
    ("jira__PCC-125", "jira", ["PCC-125.c"], []),
    ("jira__PCC-129", "jira", ["PCC-129.c"], []),
    ("jira__PCC-131", "jira", ["PCC-131.c"], []),
    ("jira__PCC-135", "jira", ["PCC-135.c"], []),
    ("jira__PCC-138", "jira", ["PCC-138.c"], []),
    ("jira__PCC-149", "jira", ["PCC-149.c"], []),
    ("jira__PCC-154", "jira", ["PCC-154.c"], []),
    ("jira__PCC-155", "jira", ["PCC-155.c"], []),
    ("jira__PCC-157", "jira", ["PCC-157.c"], []),
    ("jira__PCC-158", "jira", ["PCC-158.c"], []),
    ("jira__PCC-159", "jira", ["PCC-159.c"], []),
    ("jira__PCC-160", "jira", ["PCC-160.c"], []),
    ("jira__PCC-165", "jira", ["PCC-165.c"], []),
    ("jira__PCC-169", "jira", ["PCC-169.c"], []),
    ("jira__PCC-170", "jira", ["PCC-170.c"], []),
    ("jira__PCC-176", "jira", ["PCC-176.c"], []),
    ("jira__PCC-179", "jira", ["PCC-179.c"], []),
    ("jira__PCC-180", "jira", ["PCC-180.c"], []),
    ("jira__PCC-182", "jira", ["PCC-182.c"], []),
    ("jira__PCC-183", "jira", ["PCC-183.c"], []),
    ("jira__PCC-187", "jira", ["PCC-187.c"], []),
    ("jira__PCC-189", "jira", ["PCC-189.c"], []),
    ("jira__PCC-190", "jira", ["PCC-190.c"], []),
    ("jira__PCC-191", "jira", ["PCC-191.c"], []),
    ("jira__PCC-192", "jira", ["PCC-192.c"], []),
    ("jira__PCC-193", "jira", ["PCC-193.c"], []),
    ("jira__PCC-195", "jira", ["PCC-195.c"], []),
    ("jira__PCC-196", "jira", ["PCC-196.c"], []),
    ("jira__PCC-197", "jira", ["PCC-197.c"], []),
    ("jira__PCC-200", "jira", ["PCC-200.c"], []),
    ("jira__PCC-201", "jira", ["PCC-201.c"], []),
    ("jira__PCC-202", "jira", ["PCC-202.c"], []),
    ("jira__PCC-203", "jira", ["PCC-203.c"], []),
    ("jira__PCC-204", "jira", ["PCC-204.c"], []),
]

COPY_PATHS = [
    "/bin/cat",
    "/bin/chmod",
    "/bin/ls",
    "/bin/mkdir",
    "/bin/rm",
    "/bin/sh",
    "/etc/gettytab",
    "/etc/group",
    "/etc/motd",
    "/etc/passwd",
    "/etc/rc",
    "/etc/rc0.d",
    "/etc/rc6.d",
    "/etc/termcap",
    "/libexec/getty",
    "/root/ccom-stress.sh",
    "/root/linpack-gcc",
    "/root/linpack-kernels-gcc",
    "/root/linpack-kernels-pcc",
    "/root/linpack-pcc",
    "/root/compiler-bench-gcc",
    "/root/compiler-bench-pcc",
    "/sbin/init",
    "/sbin/halt",
    "/sbin/mkfs",
    "/sbin/mount",
    "/sbin/reboot",
    "/usr/sbin/ramctl",
    "/usr/bin/aout",
    "/usr/bin/ar",
    "/usr/bin/as",
    "/usr/bin/cc",
    "/usr/bin/cpp",
    "/usr/bin/ld",
    "/usr/bin/nm",
    "/usr/bin/pcc",
    "/usr/bin/ranlib",
    "/usr/bin/size",
    "/usr/bin/strip",
    "/usr/include",
    "/usr/lib/crt0.o",
    "/usr/lib/libc.a",
    "/usr/lib/libm.a",
    "/usr/lib/libpcc.a",
    "/usr/libexec/pcc/ccom",
    "/usr/libexec/pcc/cpp",
]

ROOT_SYMLINKS = {
    "/bin/cpp": "../usr/bin/cpp",
    "/include": "usr/include",
    "/lib/crt0.o": "../usr/lib/crt0.o",
    "/lib/libc.a": "../usr/lib/libc.a",
    "/lib/libm.a": "../usr/lib/libm.a",
    "/lib/libpcc.a": "../usr/lib/libpcc.a",
    "/tmp": "var/tmp",
    "/etc/init": "../sbin/init",
    "/etc/telinit": "../sbin/init",
}

DEVICE_NODES = [
    ("bdev", "/dev/romdisk", 0, 0, ""),
    ("bdev", "/dev/ram0", 1, 0, ""),
    ("bdev", "/dev/ram1", 1, 1, ""),
    ("bdev", "/dev/ram2", 1, 2, ""),
    ("bdev", "/dev/ram3", 1, 3, ""),
    ("cdev", "/dev/console", 0, 0, ""),
    ("cdev", "/dev/tty", 2, 0, ""),
    ("cdev", "/dev/ttyS0", 3, 0, ""),
    ("cdev", "/dev/mem", 1, 0, "0640"),
    ("cdev", "/dev/kmem", 1, 1, "0640"),
    ("cdev", "/dev/null", 1, 2, "0666"),
    ("cdev", "/dev/zero", 1, 3, "0666"),
]


def unique_tests(tests):
    seen = set()
    result = []
    for test in tests:
        if test[0] in seen:
            continue
        seen.add(test[0])
        result.append(test)
    return result


def ensure_parent(path):
    path.parent.mkdir(parents=True, exist_ok=True)


def copy_path(src_root, dst_root, rel):
    src = src_root / rel.lstrip("/")
    dst = dst_root / rel.lstrip("/")
    if not src.exists() and not src.is_symlink():
        raise FileNotFoundError(src)
    ensure_parent(dst)
    if src.is_symlink():
        if dst.exists() or dst.is_symlink():
            dst.unlink()
        os.symlink(os.readlink(src), dst)
    elif src.is_dir():
        shutil.copytree(src, dst, symlinks=True)
    else:
        shutil.copy2(src, dst)


def write_file(dst_root, rel, text, mode):
    dst = dst_root / rel.lstrip("/")
    ensure_parent(dst)
    dst.write_text(text)
    os.chmod(dst, mode)


def make_symlink(dst_root, rel, target):
    dst = dst_root / rel.lstrip("/")
    ensure_parent(dst)
    if dst.exists() or dst.is_symlink():
        dst.unlink()
    os.symlink(target, dst)


def stage_test_sources(top, dst_root):
    regress = top / "src/dev/pcc/pcc-tests/regress"
    for _name, subdir, srcs, _flags in unique_tests(N64_LOG_TESTS):
        for src in srcs:
            copy_path(regress, dst_root / "root/pcc-debug-src", f"/{subdir}/{src}")


def shell_words(words):
    return " ".join(words)


def emit_one_test(lines, name, subdir, srcs, flags):
    out = name.replace("-", "_")
    src_args = shell_words(srcs)
    flag_args = shell_words(flags)
    compile_args = shell_words(["cc", "-o", f'"$bindir/{out}"', src_args, flag_args]).strip()
    asm_args = shell_words(["cc", "-S", "-o", f'"$work/{out}.s"', src_args]).strip()
    obj_args = shell_words(["cc", "-c", "-o", f'"$work/{out}.o"', src_args]).strip()

    lines += [
        f"echo N64_PCC_TEST_BEGIN {name}",
        f'cd "$src/{subdir}" || exit 99',
        compile_args,
        "rc=$?",
        f"echo N64_PCC_COMPILE_RC {name} $rc",
        'cd "$work" || exit 99',
        "if [ $rc -ne 0 ]; then",
        f"    echo N64_PCC_DIAG_COMPILE_FAIL {name}",
        f'    cd "$src/{subdir}" || exit 99',
        f"    {asm_args}",
        "    echo N64_PCC_ASM_RC $?",
        f"    {obj_args}",
        "    objrc=$?",
        "    echo N64_PCC_OBJ_RC $objrc",
        f'    if [ -s "$work/{out}.o" ]; then',
        f'        nm "$work/{out}.o"',
        "    fi",
        '    cd "$work" || exit 99',
        f'elif [ ! -s "$bindir/{out}" ]; then',
        f"    echo N64_PCC_EMPTY_OUTPUT {name}",
        f'    cd "$src/{subdir}" || exit 99',
        f"    {asm_args}",
        "    echo N64_PCC_ASM_RC $?",
        f"    {obj_args}",
        "    objrc=$?",
        "    echo N64_PCC_OBJ_RC $objrc",
        f'    if [ -s "$work/{out}.o" ]; then',
        f'        nm "$work/{out}.o"',
        "    fi",
        '    cd "$work" || exit 99',
        "else",
        f'    size "$bindir/{out}"',
        f'    aout "$bindir/{out}"',
        '    if [ "$N64_PCC_DEBUG_NM" = 1 ]; then',
        f'        nm "$bindir/{out}"',
        "    fi",
        f"    echo N64_PCC_RUN {name}",
        f'    "$bindir/{out}"',
        "    runrc=$?",
        f"    echo N64_PCC_RUN_RC {name} $runrc",
        "fi",
        f"echo N64_PCC_TEST_END {name}",
    ]


def make_run_script(tests):
    lines = [
        "#!/bin/sh",
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        "export PATH",
        "src=/root/pcc-debug-src",
        "work=/var/tmp/n64-pcc-debug",
        'bindir="$work/bin"',
        "echo N64_PCC_DEBUG_BEGIN",
        'rm -rf "$work"',
        'mkdir -p "$bindir" || exit 1',
    ]
    for test in unique_tests(tests):
        emit_one_test(lines, *test)
    lines += [
        'rm -rf "$work"',
        "echo N64_PCC_DEBUG_END",
    ]
    return "\n".join(lines) + "\n"


def make_rc_sysinit():
    return """#!/bin/sh
HOME=/; export HOME
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH

echo N64_PCC_DEBUG_RC_BEGIN
/usr/sbin/ramctl create /dev/ram0 size=1M
mkfs -i 4096 /dev/ram0
rc=$?
echo N64_PCC_DEBUG_MKFS_RC $rc
if [ $rc -eq 0 ]; then
    mount -o rw /dev/ram0 /var
    rc=$?
    echo N64_PCC_DEBUG_MOUNT_DIRECT_RC $rc
    if [ $rc -eq 0 ]; then
        mkdir /var/config /var/db /var/log /var/run /var/tmp /var/lock
        chmod 1777 /var/tmp
        : >/var/run/utmp
        : >/var/log/wtmp
        chmod 664 /var/run/utmp /var/log/wtmp
    else
        echo N64_PCC_DEBUG_VAR_MOUNT_FAIL
    fi
else
    echo N64_PCC_DEBUG_VAR_MKFS_FAIL
fi

/root/n64-pcc-debug-runner
echo N64_PCC_DEBUG_RUNNER_RC $?
echo N64_PCC_DEBUG_RC_END
"""


def make_inittab():
    return """# N64 PCC debug System V initialization table.
is::sysinit:/etc/rc.sysinit
id:2:initdefault:
r0:0:wait:/etc/rc 0
r1:1:wait:/etc/rc 1
r2:2:wait:/etc/rc 2
r3:3:wait:/etc/rc 3
r4:4:wait:/etc/rc 4
r5:5:wait:/etc/rc 5
r6:6:wait:/etc/rc 6
"""


def make_ttys():
    return """# name    getty                           type    status      comments
console   "/libexec/getty std.default"    vt100   off secure
ttyS0     "/libexec/getty std.default"    vt100   off secure
"""


def make_fstab():
    return """/dev/romdisk\t/\tufs\tro\t0\t0
/dev/ram0\t/var\tufs\trw\t0\t0
/dev/ram1\tnone\tswap\tsw\t0\t0
"""


def make_profile():
    return """PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
"""


def stage_rootfs(args):
    top = Path(args.top).resolve()
    src_rootfs = Path(args.src_rootfs).resolve()
    out_rootfs = Path(args.out_rootfs).resolve()
    manifest = Path(args.manifest_out).resolve()

    if out_rootfs.exists():
        shutil.rmtree(out_rootfs)
    out_rootfs.mkdir(parents=True)

    for rel in COPY_PATHS:
        copy_path(src_rootfs, out_rootfs, rel)

    for rel, target in ROOT_SYMLINKS.items():
        make_symlink(out_rootfs, rel, target)

    for rel in [
        "/bin",
        "/dev",
        "/etc",
        "/lib",
        "/libexec",
        "/root",
        "/sbin",
        "/usr",
        "/usr/bin",
        "/usr/lib",
        "/usr/libexec",
        "/usr/libexec/pcc",
        "/var",
        "/var/db",
        "/var/lock",
        "/var/log",
        "/var/run",
        "/var/tmp",
    ]:
        (out_rootfs / rel.lstrip("/")).mkdir(parents=True, exist_ok=True)

    stage_test_sources(top, out_rootfs)
    if args.runner:
        runner_src = Path(args.runner).resolve()
        if not runner_src.is_file():
            raise FileNotFoundError(runner_src)
        runner_dst = out_rootfs / "root/n64-pcc-debug-runner"
        ensure_parent(runner_dst)
        shutil.copy2(runner_src, runner_dst)
        os.chmod(runner_dst, 0o775)
        sh_dst = out_rootfs / "bin/sh"
        ensure_parent(sh_dst)
        shutil.copy2(runner_src, sh_dst)
        os.chmod(sh_dst, 0o775)
    write_file(out_rootfs, "/etc/inittab", make_inittab(), 0o664)
    write_file(out_rootfs, "/etc/rc.sysinit", make_rc_sysinit(), 0o775)
    write_file(out_rootfs, "/etc/ttys", make_ttys(), 0o664)
    write_file(out_rootfs, "/etc/fstab", make_fstab(), 0o664)
    write_file(out_rootfs, "/etc/profile", make_profile(), 0o664)
    write_file(out_rootfs, "/root/.profile", make_profile(), 0o664)
    write_file(out_rootfs, "/root/run-n64-pcc-debug.sh",
               make_run_script(PRIMARY_TESTS), 0o775)
    write_file(out_rootfs, "/root/run-n64-pcc-debug-all.sh",
               make_run_script(N64_LOG_TESTS), 0o775)

    write_manifest(out_rootfs, manifest)
    print(f"staged N64 PCC debug rootfs: {out_rootfs}")
    print(f"manifest: {manifest}")


def mode_of(path):
    return f"{path.lstat().st_mode & 0o777:04o}"


def manifest_path(root, path):
    return "/" + path.relative_to(root).as_posix()


def write_manifest(root, manifest):
    dirs = []
    files = []
    symlinks = []
    for path in sorted(root.rglob("*"), key=lambda p: p.as_posix()):
        rel = manifest_path(root, path)
        if path.is_symlink():
            symlinks.append((rel, os.readlink(path)))
        elif path.is_dir():
            dirs.append((rel, mode_of(path)))
        elif path.is_file():
            files.append((rel, mode_of(path)))

    with manifest.open("w") as f:
        f.write("#\n# Minimal N64 PCC hardware debug root filesystem.\n#\n")
        f.write("default\nowner 0\ngroup 0\ndirmode 0775\nfilemode 0664\n\n")
        for rel, mode in dirs:
            f.write(f"dir {rel}\nmode {mode}\n\n")
        for rel, target in symlinks:
            f.write(f"symlink {rel}\ntarget {target}\n\n")
        for rel, mode in files:
            f.write(f"file {rel}\nmode {mode}\n\n")
        for kind, rel, major, minor, mode in DEVICE_NODES:
            f.write(f"{kind} {rel}\nmajor {major}\nminor {minor}\n")
            if mode:
                f.write(f"mode {mode}\n")
            f.write("\n")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--top", required=True)
    parser.add_argument("--src-rootfs", required=True)
    parser.add_argument("--out-rootfs", required=True)
    parser.add_argument("--manifest-out", required=True)
    parser.add_argument("--runner")
    return parser.parse_args()


def main():
    stage_rootfs(parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
