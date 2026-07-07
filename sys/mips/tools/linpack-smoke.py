#!/usr/bin/env python3
import argparse
import json
import os
import pty
import re
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path


def run(cmd, *, cwd=None, env=None):
    print("$ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], cwd=cwd, env=env, check=True)


def write_all(master, data):
    view = memoryview(data)
    while view:
        try:
            written = os.write(master, view)
            view = view[written:]
        except BlockingIOError:
            select.select([], [master], [], 1.0)


def command_build(args):
    source = Path(args.source)
    out = Path(args.out)
    rootfs = Path(args.rootfs)
    pcc_bin = out / "linpack-pcc"
    libdir = rootfs / "usr/lib"
    ldscript_name = {
        "big": "elf32-bigmips.ld",
        "little": "elf32-littlemips.ld",
    }[args.endian]
    ldscript = libdir / "ldscripts" / ldscript_name

    if not source.is_file():
        raise FileNotFoundError(source)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    march_flag = f"-march={args.cpu}"
    float_flag = f"-m{args.float_abi}-float"
    cmd = [
        args.pcc,
        march_flag,
        float_flag,
        "-O2",
    ]
    if ldscript.is_file():
        cmd += ["-T", ldscript]
    cmd += [
        "-I",
        rootfs / "usr/include",
        "-L",
        libdir,
        "-o",
        pcc_bin,
        source,
        "-lm",
    ]
    run(cmd)

    summary = {
        "pcc": {"path": str(pcc_bin), "bytes": pcc_bin.stat().st_size},
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return 0


def command_stage(args):
    out = Path(args.out)
    rootfs = Path(args.rootfs)
    manifest = Path(args.manifest)
    manifest_out = Path(args.manifest_out)

    entries = []
    for name in ("linpack-pcc",):
        src = out / name
        if not src.is_file():
            raise FileNotFoundError(src)
        dst = rootfs / "root" / name
        shutil.copy2(src, dst)
        os.chmod(dst, 0o775)
        entries.append((f"/root/{name}", f"file /root/{name}\nmode 0775"))

    text = manifest.read_text()
    with manifest_out.open("w") as f:
        f.write(text)
        if text and not text.endswith("\n"):
            f.write("\n")
        for path, entry in entries:
            if f"file {path}\n" not in text:
                f.write(entry + "\n")

    print(f"staged {len(entries)} linpack binaries")
    print(manifest_out)
    return 0


def command_run(args):
    malta = Path(args.malta_dir)
    log = Path(args.log)
    command = (
        f"LINPACK_ARRAY_SIZE={args.array_size} "
        f"LINPACK_MIN_SECONDS={args.min_seconds} "
        "/root/linpack-smoke.sh; echo LINPACK_RC:$?"
    )
    cmd = [
        args.qemu,
        "-M",
        "malta",
    ]
    if args.cpu:
        cmd += ["-cpu", args.cpu]
    cmd += [
        "-m",
        args.ram,
        "-nographic",
        "-serial",
        "mon:stdio",
        "-no-reboot",
        "-kernel",
        args.kernel,
    ]

    master, slave = pty.openpty()
    proc = subprocess.Popen(
        cmd,
        cwd=malta,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
    )
    os.close(slave)
    os.set_blocking(master, False)

    buf = ""
    state = "login"
    last = time.time()
    started = time.time()

    with log.open("wb") as log_file:
        try:
            while True:
                if proc.poll() is not None:
                    break
                ready, _, _ = select.select([master], [], [], 0.2)
                if ready:
                    data = os.read(master, 8192)
                    if not data:
                        break
                    log_file.write(data)
                    log_file.flush()
                    text = data.decode("latin1", errors="replace")
                    sys.stdout.write(text)
                    sys.stdout.flush()
                    buf += text
                    if len(buf) > 20000:
                        buf = buf[-20000:]
                    last = time.time()

                if state == "login" and "login:" in buf:
                    time.sleep(0.1)
                    write_all(master, b"root\n")
                    state = "shell"
                    buf = ""
                elif state == "shell" and re.search(r"(?:^|[\r\n])#\s*$", buf):
                    write_all(master, (command + "\n").encode("ascii"))
                    state = "running"
                    buf = ""
                elif state == "running" and "LINPACK_RC:" in buf and re.search(r"(?:^|[\r\n])#\s*$", buf):
                    break

                if time.time() - started > args.timeout:
                    raise TimeoutError("QEMU linpack timeout")
                if state == "running" and time.time() - last > args.silence_timeout:
                    raise TimeoutError("no QEMU output during linpack")
        finally:
            try:
                os.write(master, b"\x01x")
                time.sleep(0.5)
            except OSError:
                pass
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
            os.close(master)

    text = log.read_text(errors="replace")
    match = re.search(r"LINPACK_RC:([0-9]+)", text)
    rc = int(match.group(1)) if match else 99
    labels = re.findall(r"linpack smoke: (gcc|pcc)", text)
    summary = {"rc": rc, "ran": labels}
    print("\n" + json.dumps(summary, indent=2, sort_keys=True))
    return 0 if rc == 0 and "pcc" in labels else 1


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("build")
    p.add_argument("--source", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--rootfs", required=True)
    p.add_argument("--pcc", required=True)
    p.add_argument("--cpu", choices=["vr4300", "mips32r2"], default="vr4300")
    p.add_argument("--float-abi", choices=["hard", "soft"], default="hard")
    p.add_argument("--endian", choices=["big", "little"], required=True)
    p.set_defaults(func=command_build)

    p = sub.add_parser("stage")
    p.add_argument("--out", required=True)
    p.add_argument("--rootfs", required=True)
    p.add_argument("--manifest", required=True)
    p.add_argument("--manifest-out", required=True)
    p.set_defaults(func=command_stage)

    p = sub.add_parser("run")
    p.add_argument("--malta-dir", required=True)
    p.add_argument("--kernel", default="unix.elf")
    p.add_argument("--log", required=True)
    p.add_argument("--qemu", default="qemu-system-mips")
    p.add_argument("--cpu")
    p.add_argument("--ram", default="32M")
    p.add_argument("--array-size", default="120")
    p.add_argument("--min-seconds", default="1")
    p.add_argument("--timeout", type=int, default=240)
    p.add_argument("--silence-timeout", type=int, default=45)
    p.set_defaults(func=command_run)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
