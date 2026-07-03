#!/usr/bin/env python3
import argparse
import json
import os
import pty
import re
import select
import subprocess
import sys
import time
from pathlib import Path


def command_run(args):
    malta = Path(args.malta_dir)
    log = Path(args.log)
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
    saw_expect = False

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
                    if args.expect in buf:
                        saw_expect = True
                    if len(buf) > 20000:
                        buf = buf[-20000:]
                    last = time.time()

                if state == "login" and "login:" in buf:
                    time.sleep(0.1)
                    os.write(master, b"root\n")
                    state = "shell"
                    buf = ""
                elif state == "shell" and re.search(r"(?:^|[\r\n])#\s*$", buf):
                    os.write(master, (args.command + "\n").encode("ascii"))
                    state = "running"
                    buf = ""
                elif state == "running" and re.search(r"(?:^|[\r\n])#\s*$", buf):
                    break

                if time.time() - started > args.timeout:
                    raise TimeoutError("QEMU command timeout")
                if state == "running" and time.time() - last > args.silence_timeout:
                    raise TimeoutError("no QEMU output during command")
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
    summary = {
        "expect": args.expect,
        "matched": args.expect in text,
    }
    print("\n" + json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["matched"] else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--malta-dir", required=True)
    parser.add_argument("--kernel", default="unix.elf")
    parser.add_argument("--log", required=True)
    parser.add_argument("--command", required=True)
    parser.add_argument("--expect", required=True)
    parser.add_argument("--qemu", default="qemu-system-mips")
    parser.add_argument("--cpu")
    parser.add_argument("--ram", default="32M")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--silence-timeout", type=int, default=45)
    args = parser.parse_args()
    return command_run(args)


if __name__ == "__main__":
    raise SystemExit(main())
