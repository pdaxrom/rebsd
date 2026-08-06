#!/usr/bin/env python3
"""Build and boot the minimal-rootfs matrix for supported MIPS boards."""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(command, log_path):
    print("+", " ".join(str(item) for item in command), flush=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert process.stdout is not None
        for chunk in iter(lambda: process.stdout.read(8192), b""):
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            log.write(chunk)
            log.flush()
        return_code = process.wait()
    if return_code:
        raise subprocess.CalledProcessError(return_code, command)


def make_command(make, top, out, jobs, board, targets, variables):
    command = [
        make,
        "-C",
        str(top / "sys/mips"),
        f"-j{jobs}",
        f"BOARD={board}",
        f"O={out}",
    ]
    command.extend(f"{key}={value}" for key, value in variables.items())
    command.extend(targets)
    return command


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--top", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    top = args.top.resolve()
    make = shutil.which("make")
    if make is None:
        parser.error("make was not found")
    for qemu in ("qemu-system-mips", "qemu-system-mips64", "qemu-system-mipsel"):
        if shutil.which(qemu) is None:
            parser.error(f"{qemu} was not found")

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = args.out.resolve() / f"run-{stamp}-{os.getpid()}"
    run_dir.mkdir(parents=True, exist_ok=False)
    logs = run_dir / "logs"
    results = []

    runtime_boards = (
        ("malta", {}),
        ("malta64", {}),
        ("maltael", {}),
    )
    minimal = {
        "KERNEL_COMPILER": "gcc",
        "USERLAND_COMPILER": "gcc",
        "MIPS_ROOTFS_NATIVE_PCC": "0",
        "MIPS_ROOTFS_PROFILE": "minimal",
        "MIPS_ROOTFS_KBYTES": "6144",
    }

    for board, extra in runtime_boards:
        output = run_dir / board
        variables = dict(minimal)
        variables.update(extra)
        run(
            make_command(
                make,
                top,
                output,
                args.jobs,
                board,
                ("rootfs-boot-smoke-runtime",),
                variables,
            ),
            logs / f"{board}.log",
        )
        results.append({"board": board, "gate": "qemu-boot", "status": "ok"})

    ci20_out = run_dir / "ci20"
    run(
        make_command(
            make,
            top,
            ci20_out,
            args.jobs,
            "ci20",
            ("all",),
            minimal,
        ),
        logs / "ci20-build.log",
    )
    run(
        make_command(
            make,
            top,
            ci20_out,
            args.jobs,
            "ci20",
            ("rootfs-contract-check",),
            minimal,
        ),
        logs / "ci20-contract.log",
    )
    results.append({"board": "ci20", "gate": "build", "status": "ok"})

    n64_configs = (
        "uartmin-gcc",
        "uartmin-pcc-gcc",
        "uartmin-pcc-pcc",
    )
    for config in n64_configs:
        n64_out = run_dir / f"n64-{config}"
        n64_vars = {"N64_BUILD_CONFIG": config}
        run(
            make_command(
                make,
                top,
                n64_out,
                args.jobs,
                "n64",
                ("all",),
                n64_vars,
            ),
            logs / f"n64-{config}-build.log",
        )
        run(
            make_command(
                make,
                top,
                n64_out,
                args.jobs,
                "n64",
                ("rootfs-contract-check",),
                n64_vars,
            ),
            logs / f"n64-{config}-contract.log",
        )
        results.append(
            {
                "board": "n64",
                "config": config,
                "gate": "rom-build",
                "status": "ok",
            }
        )

        n64_rootfs = n64_out / "obj/sys/mips/n64/rootfs.img"
        if not n64_rootfs.is_file():
            raise FileNotFoundError(
                f"N64 rootfs was not produced for {config}: {n64_rootfs}"
            )

        n64_qemu_out = run_dir / f"n64-{config}-rootfs-on-malta64"
        n64_qemu = dict(minimal)
        n64_qemu.update(
            {
                "MALTA_MEMORY_PROFILE": "n64-8m",
                "MALTA_QEMU_RAM": "32M",
                "MIPS_ROOTFS_CPU": "vr4300",
                "MIPS_ROOTFS_EXEC_FORMAT": "aout",
                "MIPS_ROOTFS_EXTERNAL_IMAGE": str(n64_rootfs),
            }
        )
        run(
            make_command(
                make,
                top,
                n64_qemu_out,
                args.jobs,
                "malta64",
                ("rootfs-boot-smoke-runtime",),
                n64_qemu,
            ),
            logs / f"n64-{config}-rootfs-on-malta64.log",
        )
        results.append(
            {
                "board": "n64",
                "config": config,
                "gate": "exact-rootfs-qemu-boot",
                "status": "ok",
            }
        )

    summary = {
        "run_directory": str(run_dir),
        "results": results,
    }
    summary_path = run_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
