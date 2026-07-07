#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


LABEL_RE = re.compile(r"^[A-Za-z_$\.][A-Za-z0-9_$\.]*:\s*")
OP_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_\.]*)\b")

BRANCH_OPS = {
    "b",
    "bal",
    "beq",
    "beql",
    "beqz",
    "beqzl",
    "bgez",
    "bgezal",
    "bgezall",
    "bgezl",
    "bgtz",
    "bgtzl",
    "blez",
    "blezl",
    "bltz",
    "bltzal",
    "bltzall",
    "bltzl",
    "bne",
    "bnel",
    "bnez",
    "bnezl",
}

JUMP_OPS = {
    "j",
    "jal",
    "jalr",
    "jr",
}

LOAD_OPS = {
    "lb",
    "lbu",
    "ld",
    "ldc1",
    "lh",
    "lhu",
    "ll",
    "lld",
    "lw",
    "lwc1",
    "lwl",
    "lwr",
    "lwu",
    "l.d",
    "l.s",
}

STORE_OPS = {
    "sb",
    "sd",
    "sdc1",
    "sc",
    "scd",
    "sh",
    "sw",
    "swc1",
    "swl",
    "swr",
    "s.d",
    "s.s",
}

MULT_DIV_OPS = {
    "div",
    "divu",
    "ddiv",
    "ddivu",
    "dmult",
    "dmultu",
    "mult",
    "multu",
}

HILO_OPS = {
    "mfhi",
    "mflo",
    "mthi",
    "mtlo",
}

FPU_BASE_OPS = {
    "abs",
    "add",
    "ceil",
    "cvt",
    "div",
    "floor",
    "mov",
    "mul",
    "neg",
    "round",
    "sqrt",
    "sub",
    "trunc",
}


def strip_comment(line):
    return line.split("#", 1)[0].strip()


def parse_instruction(line):
    text = strip_comment(line)
    if not text:
        return None
    while True:
        match = LABEL_RE.match(text)
        if not match:
            break
        text = text[match.end():].strip()
        if not text:
            return None
    if not text or text.startswith("."):
        return None
    match = OP_RE.match(text)
    if not match:
        return None
    return match.group(1).lower()


def is_branch(op):
    return op in BRANCH_OPS or op.startswith("bc1")


def is_jump(op):
    return op in JUMP_OPS


def is_load(op):
    return op in LOAD_OPS


def is_store(op):
    return op in STORE_OPS


def is_fpu_op(op):
    if "." not in op:
        return False
    return op.split(".", 1)[0] in FPU_BASE_OPS


def count_file(path):
    lines = path.read_text(errors="replace").splitlines()
    instructions = []
    ent_names = []

    for line in lines:
        stripped = strip_comment(line)
        if stripped.startswith(".ent"):
            parts = stripped.split()
            if len(parts) > 1:
                ent_names.append(parts[1])
        op = parse_instruction(line)
        if op is not None:
            instructions.append(op)

    counts = {
        "path": str(path),
        "bytes": path.stat().st_size,
        "lines": len(lines),
        "functions": len(ent_names),
        "instructions": len(instructions),
        "non_nop_instructions": sum(1 for op in instructions if op != "nop"),
        "nops": instructions.count("nop"),
        "loads": sum(1 for op in instructions if is_load(op)),
        "stores": sum(1 for op in instructions if is_store(op)),
        "branches": sum(1 for op in instructions if is_branch(op)),
        "jumps": sum(1 for op in instructions if is_jump(op)),
        "calls": sum(1 for op in instructions if op in {"jal", "jalr"}),
        "mult_div": sum(1 for op in instructions if op in MULT_DIV_OPS),
        "hilo": sum(1 for op in instructions if op in HILO_OPS),
        "fpu_arith": sum(1 for op in instructions if is_fpu_op(op)),
        "load_followed_by_nop": 0,
        "store_followed_by_nop": 0,
        "branch_delay_nop": 0,
        "jump_delay_nop": 0,
    }
    counts["load_store"] = counts["loads"] + counts["stores"]

    for index, op in enumerate(instructions[:-1]):
        next_op = instructions[index + 1]
        if next_op != "nop":
            continue
        if is_load(op):
            counts["load_followed_by_nop"] += 1
        if is_store(op):
            counts["store_followed_by_nop"] += 1
        if is_branch(op):
            counts["branch_delay_nop"] += 1
        if is_jump(op):
            counts["jump_delay_nop"] += 1

    counts["branch_or_jump_delay_nop"] = (
        counts["branch_delay_nop"] + counts["jump_delay_nop"])
    counts["functions_sample"] = ent_names[:20]
    return counts


def parse_key_value(text, option):
    if "=" not in text:
        raise argparse.ArgumentTypeError(f"{option} expects key=value")
    key, value = text.split("=", 1)
    if not key:
        raise argparse.ArgumentTypeError(f"{option} key must not be empty")
    return key, value


def command_count(args):
    inputs = {}
    for item in args.input:
        label, value = parse_key_value(item, "--input")
        path = Path(value)
        if not path.is_file():
            raise FileNotFoundError(path)
        inputs[label] = count_file(path)

    metadata = {}
    for item in args.metadata:
        key, value = parse_key_value(item, "--metadata")
        metadata[key] = value

    summary = {
        "metadata": metadata,
        "inputs": inputs,
    }

    if "gcc" in inputs and "pcc" in inputs:
        ratio_keys = [
            "bytes",
            "lines",
            "instructions",
            "non_nop_instructions",
            "nops",
            "loads",
            "stores",
            "load_store",
            "branches",
            "jumps",
            "calls",
            "mult_div",
            "hilo",
            "fpu_arith",
            "branch_or_jump_delay_nop",
        ]
        ratios = {}
        for key in ratio_keys:
            base = inputs["gcc"].get(key, 0)
            value = inputs["pcc"].get(key, 0)
            ratios[key] = None if base == 0 else value / base
        summary["pcc_to_gcc_ratio"] = ratios

    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

    print_table(summary)
    if args.out:
        print(args.out)
    return 0


def print_table(summary):
    columns = [
        "label",
        "instr",
        "non_nop",
        "nop",
        "ld",
        "st",
        "br",
        "jmp",
        "brjmp_nop",
        "muldiv",
        "bytes",
    ]
    rows = []
    for label, data in summary["inputs"].items():
        rows.append([
            label,
            data["instructions"],
            data["non_nop_instructions"],
            data["nops"],
            data["loads"],
            data["stores"],
            data["branches"],
            data["jumps"],
            data["branch_or_jump_delay_nop"],
            data["mult_div"],
            data["bytes"],
        ])

    widths = [len(name) for name in columns]
    for row in rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(str(value)))

    print(" ".join(name.rjust(widths[index]) for index, name in enumerate(columns)))
    for row in rows:
        print(" ".join(str(value).rjust(widths[index]) for index, value in enumerate(row)))

    ratios = summary.get("pcc_to_gcc_ratio")
    if ratios:
        print("pcc/gcc instruction ratio: %.3f" % ratios["instructions"])
        print("pcc/gcc nop ratio: %.3f" % ratios["nops"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        action="append",
        default=[],
        help="assembly input as label=path; may be repeated",
    )
    parser.add_argument(
        "--metadata",
        action="append",
        default=[],
        help="metadata as key=value; may be repeated",
    )
    parser.add_argument("--out")
    args = parser.parse_args()
    if not args.input:
        parser.error("at least one --input is required")
    return command_count(args)


if __name__ == "__main__":
    raise SystemExit(main())
