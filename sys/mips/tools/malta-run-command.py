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


COMMAND_DONE = "__MALTA_RUN_COMMAND_DONE__:"
SHELL_PROMPT_RE = re.compile(r"(?:^|[\r\n])#\s*$")


def write_all(master, data):
    view = memoryview(data)
    while view:
        try:
            written = os.write(master, view)
            view = view[written:]
        except BlockingIOError:
            select.select([], [master], [], 1.0)


def write_command(master, command, line_delay, chunk_size, chunk_delay):
    wrapped = command + "\necho " + COMMAND_DONE + "$?\n"
    for line in wrapped.splitlines(True):
        data = line.encode("ascii")
        if chunk_size:
            for off in range(0, len(data), chunk_size):
                write_all(master, data[off:off + chunk_size])
                if chunk_delay:
                    time.sleep(chunk_delay)
        else:
            write_all(master, data)
        if len(line) > 1 and line_delay:
            time.sleep(line_delay)


def command_run(args):
    malta = Path(args.malta_dir)
    log = Path(args.log)
    log.parent.mkdir(parents=True, exist_ok=True)
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
    cmd += args.qemu_arg

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
    saw_login = False

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

                if state == "login":
                    if "login:" in buf:
                        saw_login = True
                        time.sleep(0.1)
                        os.write(master, b"root\n")
                        state = "shell"
                        buf = ""
                    elif SHELL_PROMPT_RE.search(buf):
                        if args.require_login:
                            raise RuntimeError(
                                "single-user shell reached before login prompt"
                            )
                        state = "shell"
                elif state == "shell" and SHELL_PROMPT_RE.search(buf):
                    write_command(
                        master, args.command, args.line_delay,
                        args.write_chunk_size, args.chunk_delay)
                    state = "running"
                    buf = ""
                elif (state == "running" and
                      re.search(r"(?:^|[\r\n])#?\s*" +
                                re.escape(COMMAND_DONE), buf) and
                      re.search(r"(?:^|[\r\n])#\s*$", buf)):
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
        "require_login": args.require_login,
        "saw_login": saw_login,
    }
    print("\n" + json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["matched"] and (
        not args.require_login or saw_login
    ) else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--malta-dir", required=True)
    parser.add_argument("--kernel", default="unix.elf")
    parser.add_argument("--log", required=True)
    parser.add_argument("--command", required=True)
    parser.add_argument("--expect", required=True)
    parser.add_argument("--qemu", default="qemu-system-mips")
    parser.add_argument("--qemu-arg", action="append", default=[])
    parser.add_argument("--cpu")
    parser.add_argument("--ram", default="32M")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--silence-timeout", type=int, default=45)
    parser.add_argument("--line-delay", type=float, default=0.06)
    parser.add_argument("--write-chunk-size", type=int, default=0)
    parser.add_argument("--chunk-delay", type=float, default=0.0)
    parser.add_argument("--require-login", action="store_true")
    args = parser.parse_args()
    return command_run(args)


if __name__ == "__main__":
    raise SystemExit(main())
