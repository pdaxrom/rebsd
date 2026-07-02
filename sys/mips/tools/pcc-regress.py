#!/usr/bin/env python3
import argparse
import json
import os
import pty
import re
import select
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path


TOP = Path(__file__).resolve().parents[3]
REGRESS = TOP / "src/dev/pcc/pcc-tests/regress"

DEFAULT_COMPILE_FAILURES = {
    "gcccompat__typeof001",
    "misc__shlib3",
    "pcclist__init004",
}

DEFAULT_RUNTIME_FAILURES = {
    "pcclist__init006",
}


def make_vars(makefile):
    raw = makefile.read_text()
    logical = []
    current = ""
    for line in raw.splitlines():
        stripped = line.split("#", 1)[0].rstrip()
        if not stripped:
            continue
        current = current + " " + stripped if current else stripped
        if current.endswith("\\"):
            current = current[:-1].rstrip()
            continue
        logical.append(current)
        current = ""
    if current:
        logical.append(current)

    values = {}
    for line in logical:
        if "=" not in line:
            continue
        name, value = line.split("=", 1)
        name = name.strip()
        if name.isidentifier() or name.replace("_", "").isalnum():
            values[name] = value.strip().split()
    return values


def out_name(name):
    return name.replace("/", "__").replace("-", "_").replace(".", "_")


def add_runtime(tests, name, cwd, srcs, flags=None, args=None):
    tests.append({
        "kind": "runtime",
        "name": name,
        "cwd": str(cwd),
        "srcs": [str(s) for s in srcs],
        "flags": flags or [],
        "args": args or [],
    })


def add_compile(tests, name, cwd, srcs, flags=None):
    tests.append({
        "kind": "compile",
        "name": name,
        "cwd": str(cwd),
        "srcs": [str(s) for s in srcs],
        "flags": flags or [],
    })


def add_expect_fail(tests, name, cwd, srcs, flags=None):
    tests.append({
        "kind": "expect-fail",
        "name": name,
        "cwd": str(cwd),
        "srcs": [str(s) for s in srcs],
        "flags": flags or [],
    })


def build_test_list(rootfs):
    tests = []
    libm = rootfs / "usr/lib/libm.a"

    c99 = REGRESS / "c99"
    v = make_vars(c99 / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"c99__{target}", c99, [f"{target}.c"], [str(libm)])
    for target in v["REGRESS_TARGETS_FAIL"]:
        add_expect_fail(tests, f"c99__{target}", c99, [f"{target}.c"], [str(libm)])
    add_runtime(tests, "c99__basic006", c99, ["basic006.c"], [str(libm)],
                ["param1", "PARAM2", "Param3"])
    add_runtime(tests, "c99__darray003", c99, ["darray003.c", "darray003_p2.c"], [str(libm)])
    add_expect_fail(tests, "c99__inline999", c99, ["inline999.c"], ["-c", str(libm)])

    c23 = REGRESS / "c23"
    v = make_vars(c23 / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"c23__{target}", c23, [f"{target}.c"], [str(libm)])
    for target in v.get("REGRESS_TARGETS_FAIL", []):
        add_expect_fail(tests, f"c23__{target}", c23, [f"{target}.c"], [str(libm)])

    gcccompat = REGRESS / "gcccompat"
    v = make_vars(gcccompat / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"gcccompat__{target}", gcccompat, [f"{target}.c"])
    for target in v["OPT_REGRESS_TARGETS"]:
        add_runtime(tests, f"gcccompat__{target}", gcccompat, [f"{target}.c"], ["-O2"])
    for target in v.get("REGRESS_TARGETS_FAIL", []):
        add_expect_fail(tests, f"gcccompat__{target}", gcccompat, [f"{target}.c"])

    jira = REGRESS / "jira"
    v = make_vars(jira / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"jira__{target}", jira, [f"{target}.c"])
    for target in v["REGRESS_O2_TARGETS"]:
        add_runtime(tests, f"jira__{target}", jira, [f"{target}.c"], ["-O2"])
    for target in v["REGRESS_PIC_TARGETS"]:
        add_runtime(tests, f"jira__{target}", jira, [f"{target}.c"], ["-k"])
    for target in v["REGRESS_E_C_TARGETS"]:
        add_compile(tests, f"jira__{target}", jira, [f"{target}.c"], ["-E", "-C"])
    for target in v["REGRESS_E_CC_TARGETS"]:
        add_compile(tests, f"jira__{target}", jira, [f"{target}.c"], ["-E", "-Wp,-CC"])
    for target in v["REGRESS_TARGETS_FAIL"]:
        add_expect_fail(tests, f"jira__{target}", jira, [f"{target}.c"])

    misc = REGRESS / "misc"
    v = make_vars(misc / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"misc__{target}", misc, [f"{target}.c"])
    for target in v["OPTIM_TARGETS"]:
        add_runtime(tests, f"misc__{target}", misc, [f"{target}.c"], ["-O"])
    for target in v["PIC_TARGETS"]:
        add_runtime(tests, f"misc__{target}", misc, [f"{target}.c"])
        add_runtime(tests, f"misc__{target}_pic", misc, [f"{target}.c"], ["-fpic"])
    for target in v.get("REGRESS_TARGETS_FAIL", []):
        add_expect_fail(tests, f"misc__{target}", misc, [f"{target}.c"])
    add_runtime(tests, "misc__shlib1", misc / "shlib", ["main.c", "lib.c"])
    add_compile(tests, "misc__shlib3", misc / "shlib", ["lib.c"], ["-fPIC", "-shared"])

    obsd = REGRESS / "obsd"
    v = make_vars(obsd / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"obsd__{target}", obsd, [f"{target}.c"])
    for target in v["REGRESS_TARGETS_FAIL"]:
        add_expect_fail(tests, f"obsd__{target}", obsd, [f"{target}.c"])

    pcclist = REGRESS / "pcclist"
    v = make_vars(pcclist / "Makefile")
    for target in v["REGRESS_TARGETS"]:
        add_runtime(tests, f"pcclist__{target}", pcclist, [f"{target}.c"])
    for target in v["REGRESS_TARGETS_FAIL"]:
        add_expect_fail(tests, f"pcclist__{target}", pcclist, [f"{target}.c"])
    add_compile(tests, "pcclist__basic002", pcclist, ["basic002.c"], ["-fpic", "-O", "-S"])
    add_compile(tests, "pcclist__basic006", pcclist, ["basic006.c"], ["-Wc,-xinline"])
    add_compile(tests, "pcclist__func001", pcclist, ["func001.c"], ["-Wc,-xtemps,-xinline"])
    add_compile(tests, "pcclist__func002", pcclist, ["func002.c"], ["-Wl,--fatal-warnings"])
    add_compile(tests, "pcclist__func007", pcclist, ["func007.c"], ["-Werror", "-Wc,-xtemps,-xinline"])
    add_compile(tests, "pcclist__init002", pcclist, ["init002.c"], ["-fPIC", "-fstack-protector"])
    add_compile(tests, "pcclist__attr004", pcclist, ["attr004.c"], ["-fPIC"])
    add_compile(tests, "pcclist__func008", pcclist, ["func008.c"], ["-fPIC"])
    return tests


class CompilerRun:
    def __init__(self, pcc, rootfs, out, ar):
        self.pcc = Path(pcc)
        self.rootfs = Path(rootfs)
        self.out = Path(out)
        self.bin = self.out / "bin"
        self.log = self.out / "compile.log"
        self.ar = Path(ar)
        self.incdir = self.rootfs / "usr/include"
        self.libdir = self.rootfs / "usr/lib"
        self.env = os.environ.copy()
        self.env["PATH"] = f"{self.pcc.parent}:{self.env.get('PATH', '')}"

    def run(self, cmd, cwd):
        with self.log.open("a") as log:
            log.write(f"$ cd {cwd} && {' '.join(shlex.quote(str(c)) for c in cmd)}\n")
            proc = subprocess.run(
                [str(c) for c in cmd],
                cwd=cwd,
                env=self.env,
                stdout=log,
                stderr=log,
            )
            log.write(f"[exit {proc.returncode}]\n\n")
            return proc.returncode

    def compile_one(self, test):
        cwd = Path(test["cwd"])
        name = test["name"]
        base = out_name(name)
        flags = list(test["flags"])
        srcs = list(test["srcs"])

        if "-S" in flags:
            out = self.out / "asm" / f"{base}.s"
        elif "-c" in flags:
            out = self.out / "obj" / f"{base}.o"
        elif "-E" in flags:
            out = self.out / "pp" / f"{base}.i"
        elif "-shared" in flags:
            out = self.out / "shlib" / f"{base}.so"
        elif name == "c99__basic006":
            out = self.bin / "basic006.out"
        else:
            out = self.bin / base
        out.parent.mkdir(parents=True, exist_ok=True)

        post_flags = [
            f for f in flags
            if str(f).endswith(".a") or str(f).startswith("-l") or str(f).startswith("-L")
        ]
        pre_flags = [f for f in flags if f not in post_flags]
        compile_only = any(f in flags for f in ("-S", "-c", "-E"))
        common = [self.pcc, "-I", self.incdir]
        if not compile_only:
            common += ["-L", self.libdir]
        cmd = [*common, *pre_flags, "-o", out, *srcs, *post_flags]
        rc = self.run(cmd, cwd)

        if test["kind"] == "expect-fail":
            return {"name": name, "kind": test["kind"], "compile_rc": rc, "compile_ok": rc != 0}

        ok = rc == 0 and out.exists() and out.stat().st_size > 0
        item = {"name": name, "kind": test["kind"], "compile_rc": rc, "compile_ok": ok}
        if ok and test["kind"] == "runtime":
            item["binary"] = out.name
            item["args"] = test.get("args", [])
        return item

    def build_static_shlib2(self, results):
        cwd = REGRESS / "misc/shlib"
        build = self.out / "misc-shlib2"
        build.mkdir(parents=True, exist_ok=True)
        lib_o = build / "lib.o"
        lib_a = build / "lib.a"
        out = self.bin / "misc__shlib2"
        ok = True
        if self.run([self.pcc, "-I", self.incdir, "-c", "-o", lib_o, "lib.c"], cwd) != 0:
            ok = False
        if ok and self.run([self.ar, "r", lib_a, lib_o], cwd) != 0:
            ok = False
        if ok and self.run([self.pcc, "-I", self.incdir, "-L", self.libdir,
                            "-Wall", "-o", out, "main.c", lib_a], cwd) != 0:
            ok = False
        item = {
            "name": "misc__shlib2",
            "kind": "runtime",
            "compile_rc": 0 if ok else 1,
            "compile_ok": ok,
        }
        if ok:
            item["binary"] = out.name
            item["args"] = []
        results.append(item)

    def write_runtime_script(self, results):
        lines = [
            "#!/bin/sh",
            "cd /root/pcc-regress || exit 99",
            "fail=0",
        ]
        for item in results:
            if item.get("kind") != "runtime" or not item.get("compile_ok"):
                continue
            binary = item["binary"]
            args = " ".join(shlex.quote(str(a)) for a in item.get("args", []))
            lines.append(f"./{binary} {args} >/dev/null 2>&1")
            lines.append("rc=$?")
            lines.append(f"echo RESULT {item['name']} $rc")
            lines.append("if [ $rc -ne 0 ]; then fail=1; fi")
        lines.append("exit $fail")
        script = self.out / "run-pcc-regress.sh"
        script.write_text("\n".join(lines) + "\n")
        script.chmod(0o775)

    def compile_all(self, expected_failures):
        if not self.pcc.is_file():
            raise FileNotFoundError(self.pcc)
        if not self.incdir.is_dir():
            raise FileNotFoundError(self.incdir)
        if not self.libdir.is_dir():
            raise FileNotFoundError(self.libdir)
        if self.out.exists():
            shutil.rmtree(self.out)
        for path in [self.bin, self.out / "obj", self.out / "asm", self.out / "pp", self.out / "shlib"]:
            path.mkdir(parents=True, exist_ok=True)
        self.log.write_text("")

        results = [self.compile_one(test) for test in build_test_list(self.rootfs)]
        self.build_static_shlib2(results)
        self.write_runtime_script(results)

        compile_fail = [r for r in results if not r["compile_ok"]]
        runtime_candidates = [r for r in results if r["kind"] == "runtime" and r.get("compile_ok")]
        unexpected = [r for r in compile_fail if r["name"] not in expected_failures]
        summary = {
            "total": len(results),
            "compile_pass": len(results) - len(compile_fail),
            "compile_fail": len(compile_fail),
            "runtime_candidates": len(runtime_candidates),
            "expected_compile_failures": sorted(r["name"] for r in compile_fail if r["name"] in expected_failures),
            "unexpected_compile_failures": unexpected,
            "compile_failures": compile_fail,
        }
        (self.out / "compile-summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n"
        )
        print(json.dumps(summary, indent=2, sort_keys=True))
        return 0 if not unexpected else 1


def command_compile(args):
    expected = set(DEFAULT_COMPILE_FAILURES)
    expected.update(args.expected_compile_fail)
    runner = CompilerRun(args.pcc, args.rootfs, args.out, args.ar)
    return runner.compile_all(expected)


def runtime_names_from_script(run_script):
    names = []
    for line in Path(run_script).read_text().splitlines():
        if line.startswith("./"):
            names.append(line.split()[0][2:])
    return names


def command_stage(args):
    out = Path(args.out)
    rootfs = Path(args.rootfs)
    manifest = Path(args.manifest)
    manifest_out = Path(args.manifest_out)
    dest = rootfs / "root/pcc-regress"

    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)

    run_src = out / "run-pcc-regress.sh"
    runtime_names = runtime_names_from_script(run_src)
    entries = []
    for name in sorted(runtime_names):
        src = out / "bin" / name
        if not src.is_file():
            raise FileNotFoundError(src)
        dst = dest / src.name
        shutil.copy2(src, dst)
        os.chmod(dst, 0o775)
        entries.append(f"file /root/pcc-regress/{src.name}\nmode 0775")

    run_dst = rootfs / "root/run-pcc-regress.sh"
    shutil.copy2(run_src, run_dst)
    os.chmod(run_dst, 0o775)

    text = manifest.read_text()
    with manifest_out.open("w") as f:
        f.write(text)
        if text and not text.endswith("\n"):
            f.write("\n")
        f.write("dir /root/pcc-regress\nmode 0775\n")
        for entry in entries:
            f.write(entry + "\n")
        f.write("file /root/run-pcc-regress.sh\nmode 0775\n")

    print(f"staged {len(entries)} runtime binaries")
    print(manifest_out)
    return 0


def parse_runtime_log(log):
    text = Path(log).read_text(errors="replace")
    return [(name, int(rc)) for name, rc in re.findall(r"RESULT\s+([^ \r\n]+)\s+([0-9]+)", text)]


def command_run(args):
    malta = Path(args.malta_dir)
    log = Path(args.log)
    expected = set(DEFAULT_RUNTIME_FAILURES)
    expected.update(args.expected_runtime_fail)
    cmd = [
        args.qemu,
        "-M", "malta",
        "-m", args.ram,
        "-nographic",
        "-serial", "mon:stdio",
        "-no-reboot",
        "-kernel", args.kernel,
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
    seen_result = False

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
                    if "RESULT " in buf:
                        seen_result = True
                    if len(buf) > 20000:
                        buf = buf[-20000:]
                    last = time.time()

                if state == "login" and "login:" in buf:
                    os.write(master, b"root\n")
                    state = "shell"
                    buf = ""
                elif state == "shell" and re.search(r"(?:^|[\r\n])#\s*$", buf):
                    os.write(master, b"/root/run-pcc-regress.sh\n")
                    state = "running"
                    buf = ""
                elif state == "running" and seen_result and re.search(r"(?:^|[\r\n])#\s*$", buf):
                    break

                if time.time() - started > args.timeout:
                    raise TimeoutError("QEMU runtime timeout")
                if state == "running" and time.time() - last > args.silence_timeout:
                    raise TimeoutError("no QEMU output during runtime")
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

    results = parse_runtime_log(log)
    failures = [(name, rc) for name, rc in results if rc != 0]
    unexpected = [(name, rc) for name, rc in failures if name not in expected]
    summary = {
        "runtime_total": len(results),
        "runtime_pass": len(results) - len(failures),
        "runtime_fail": len(failures),
        "expected_runtime_failures": [
            {"name": name, "rc": rc} for name, rc in failures if name in expected
        ],
        "unexpected_runtime_failures": [
            {"name": name, "rc": rc} for name, rc in unexpected
        ],
    }
    print("\n" + json.dumps(summary, indent=2, sort_keys=True))
    return 0 if results and not unexpected else 1


def command_summarize(args):
    out = Path(args.out)
    summary = json.loads((out / "compile-summary.json").read_text())
    print(json.dumps(summary, indent=2, sort_keys=True))
    if args.log and Path(args.log).exists():
        results = parse_runtime_log(args.log)
        failures = [(name, rc) for name, rc in results if rc != 0]
        runtime = {
            "runtime_total": len(results),
            "runtime_pass": len(results) - len(failures),
            "runtime_fail": len(failures),
            "failures": [{"name": name, "rc": rc} for name, rc in failures],
        }
        print(json.dumps(runtime, indent=2, sort_keys=True))
    return 0


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("compile")
    p.add_argument("--pcc", required=True)
    p.add_argument("--rootfs", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--ar", required=True)
    p.add_argument("--expected-compile-fail", action="append", default=[])
    p.set_defaults(func=command_compile)

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
    p.add_argument("--ram", default="32M")
    p.add_argument("--timeout", type=int, default=240)
    p.add_argument("--silence-timeout", type=int, default=30)
    p.add_argument("--expected-runtime-fail", action="append", default=[])
    p.set_defaults(func=command_run)

    p = sub.add_parser("summarize")
    p.add_argument("--out", required=True)
    p.add_argument("--log")
    p.set_defaults(func=command_summarize)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
