#!/usr/bin/env python3
"""Check final MIPS disassembly for VR4300 ordering hazards.

The checker consumes GNU objdump -d output.  It deliberately checks the
linked instruction stream rather than compiler assembly so delay slots and
the final order presented to the processor are visible.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


INSN_RE = re.compile(
    r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{8,16})\s+"
    r"([.A-Za-z0-9_]+)(?:\s+(.*?))?\s*$"
)
FUNC_RE = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$")
MEM_RE = re.compile(r"(-?(?:0x[0-9a-fA-F]+|[0-9]+))\(([^)]+)\)")


CP0_ALIASES = {
    "c0_index": "index",
    "c0_entrylo0": "entrylo0",
    "c0_entrylo1": "entrylo1",
    "c0_pagemask": "pagemask",
    "c0_wired": "wired",
    "c0_badvaddr": "badvaddr",
    "c0_count": "count",
    "c0_entryhi": "entryhi",
    "c0_compare": "compare",
    "c0_status": "status",
    "c0_sr": "status",
    "c0_cause": "cause",
    "c0_epc": "epc",
    "c0_prid": "prid",
    "c0_config": "config",
    "c0_lladdr": "lladdr",
    "c0_watchlo": "watchlo",
    "c0_watchhi": "watchhi",
    "c0_xcontext": "xcontext",
    "c0_ecc": "ecc",
    "c0_cacheerr": "cacheerr",
    "c0_taglo": "taglo",
    "c0_taghi": "taghi",
    "c0_errorepc": "errorepc",
}

CP0_NUMBERS = {
    0: "index",
    2: "entrylo0",
    3: "entrylo1",
    5: "pagemask",
    6: "wired",
    8: "badvaddr",
    9: "count",
    10: "entryhi",
    11: "compare",
    12: "status",
    13: "cause",
    14: "epc",
    15: "prid",
    16: "config",
    17: "lladdr",
    18: "watchlo",
    19: "watchhi",
    20: "xcontext",
    26: "ecc",
    27: "cacheerr",
    28: "taglo",
    29: "taghi",
    30: "errorepc",
}


@dataclass(frozen=True)
class Instruction:
    address: int
    word: str
    op: str
    operands: str
    line_number: int
    text: str
    function: str


@dataclass(frozen=True)
class Finding:
    severity: str
    rule: str
    message: str
    instruction: Instruction
    producer: Instruction | None = None


def split_operands(text: str) -> list[str]:
    return [part.strip().lower().replace("$", "") for part in text.split(",")]


def cp0_register(insn: Instruction) -> str | None:
    if insn.op not in {"mfc0", "dmfc0", "mtc0", "dmtc0"}:
        return None
    operands = split_operands(insn.operands)
    if len(operands) < 2:
        return None
    reg = operands[1]
    if reg in CP0_ALIASES:
        return CP0_ALIASES[reg]
    reg = reg.removeprefix("c0_")
    if reg in CP0_ALIASES.values():
        return reg
    try:
        return CP0_NUMBERS.get(int(reg, 0), reg)
    except ValueError:
        return reg


def is_control(op: str) -> bool:
    if op in {"j", "jal", "jr", "jalr", "b", "bal", "eret", "deret"}:
        return True
    if op.startswith("bc1") or op.startswith("bc2"):
        return True
    return op.startswith(("beq", "bne", "bgez", "bgtz", "blez", "bltz"))


def is_cop1(insn: Instruction) -> bool:
    op = insn.op
    return (
        op in {"mfc1", "dmfc1", "mtc1", "dmtc1", "cfc1", "ctc1"}
        or op.startswith("bc1")
        or op.startswith("c.")
        or op.endswith((".s", ".d", ".w", ".l"))
        or op in {"lwc1", "ldc1", "swc1", "sdc1"}
    )


def is_load(insn: Instruction) -> bool:
    return insn.op in {
        "lb", "lbu", "lh", "lhu", "lw", "lwu", "ld", "lwl", "lwr",
        "ldl", "ldr", "ll", "lld", "lwc1", "ldc1",
    }


def is_store(insn: Instruction) -> bool:
    return insn.op in {
        "sb", "sh", "sw", "sd", "swl", "swr", "sdl", "sdr", "sc",
        "scd", "swc1", "sdc1",
    }


def is_multdiv(op: str) -> bool:
    return op in {"mult", "multu", "dmult", "dmultu", "div", "divu", "ddiv", "ddivu"}


def parse_immediate(text: str) -> int | None:
    try:
        return int(text, 0)
    except ValueError:
        return None


def raised_stack_pointer(insn: Instruction) -> bool:
    if insn.op not in {"addiu", "daddiu"}:
        return False
    operands = split_operands(insn.operands)
    if len(operands) != 3 or operands[0] != "sp" or operands[1] not in {"sp", "s8", "fp"}:
        return False
    immediate = parse_immediate(operands[2])
    return immediate is not None and immediate > 0


def written_gpr(insn: Instruction) -> str | None:
    operands = split_operands(insn.operands)
    if not operands:
        return None
    if is_store(insn) or is_control(insn.op) or insn.op in {
        "mtc0", "dmtc0", "mtc1", "dmtc1", "ctc1", "mult", "multu",
        "dmult", "dmultu", "div", "divu", "ddiv", "ddivu",
    }:
        return None
    return operands[0]


def parse_disassembly(path: Path) -> list[Instruction]:
    result: list[Instruction] = []
    function = "?"
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\n")
            function_match = FUNC_RE.match(line)
            if function_match:
                function = function_match.group(1)
                continue
            match = INSN_RE.match(line)
            if not match:
                continue
            result.append(
                Instruction(
                    address=int(match.group(1), 16),
                    word=match.group(2).lower(),
                    op=match.group(3).lower(),
                    operands=(match.group(4) or "").strip(),
                    line_number=line_number,
                    text=line.strip(),
                    function=function,
                )
            )
    return result


def contiguous_window(instructions: list[Instruction], start: int, count: int) -> list[Instruction]:
    result: list[Instruction] = []
    previous = instructions[start]
    for insn in instructions[start + 1 : start + 1 + count]:
        if insn.address != previous.address + 4:
            break
        result.append(insn)
        previous = insn
    return result


def add_gap_finding(
    findings: list[Finding], producer: Instruction, consumer: Instruction,
    between: int, required: int, rule: str, detail: str,
    severity: str = "ERROR",
) -> None:
    findings.append(
        Finding(
            severity=severity,
            rule=rule,
            message=(
                f"{detail}: {between} intervening instruction(s), "
                f"VR4300 requires {required}"
            ),
            instruction=consumer,
            producer=producer,
        )
    )


def scan(instructions: list[Instruction]) -> list[Finding]:
    findings: list[Finding] = []

    # A control transfer in another control transfer's delay slot is
    # architecturally unpredictable.
    for index, insn in enumerate(instructions[:-1]):
        if not is_control(insn.op) or insn.op in {"eret", "deret"}:
            continue
        delay = instructions[index + 1]
        if delay.address == insn.address + 4 and is_control(delay.op):
            findings.append(
                Finding(
                    "ERROR", "control-in-delay-slot",
                    "control transfer in a control-transfer delay slot",
                    delay, insn,
                )
            )

        if insn.op in {"jr", "jalr"}:
            operands = split_operands(insn.operands)
            target = operands[-1] if operands else None
            if target is not None and written_gpr(delay) == target:
                findings.append(
                    Finding(
                        "ERROR", "jump-target-clobbered-in-delay-slot",
                        f"delay slot overwrites jump target register {target}",
                        delay, insn,
                    )
                )

    for index, producer in enumerate(instructions):
        window = contiguous_window(instructions, index, 7)
        producer_cp0 = cp0_register(producer)

        for offset, consumer in enumerate(window):
            between = offset
            consumer_cp0 = cp0_register(consumer)

            if (
                producer.op in {"mtc0", "dmtc0"}
                and consumer.op in {"mfc0", "dmfc0"}
                and producer_cp0 == consumer_cp0
                and between < 1
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 1,
                    "mtc0-mfc0-same-register",
                    f"{producer.op}/{consumer.op} dependency on CP0 {producer_cp0}",
                )

            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 in {"entrylo0", "entrylo1", "pagemask", "entryhi"}
                and consumer.op in {"tlbwi", "tlbwr"}
                and between < 1
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 1,
                    "mtc0-tlb-write",
                    f"TLB write consumes CP0 {producer_cp0}",
                )

            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 == "entryhi"
                and consumer.op == "tlbp"
                and between < 1
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 1,
                    "mtc0-entryhi-tlbp", "TLBP consumes EntryHi",
                )

            if (
                producer.op == "tlbp"
                and consumer.op in {"mfc0", "dmfc0"}
                and consumer_cp0 == "index"
                and between < 2
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 2,
                    "tlbp-mfc0-index", "MFC0 consumes TLBP result",
                )

            if (
                producer.op == "tlbr"
                and consumer.op in {"mfc0", "dmfc0"}
                and consumer_cp0 in {"entrylo0", "entrylo1", "pagemask", "entryhi"}
                and between < 3
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 3,
                    "tlbr-mfc0", f"MFC0 consumes TLBR result in {consumer_cp0}",
                )

            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 in {"epc", "errorepc"}
                and consumer.op in {"eret", "deret"}
                and between < 2
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 2,
                    "mtc0-epc-eret", f"ERET consumes CP0 {producer_cp0}",
                )

            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 == "status"
                and consumer.op in {"eret", "deret"}
                and between < 3
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 3,
                    "mtc0-status-eret", "ERET consumes CP0 Status",
                )

            if (
                producer.op in {"tlbwi", "tlbwr"}
                and consumer.op == "tlbp"
                and between < 3
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 3,
                    "tlb-write-tlbp", "TLBP consumes a newly written TLB entry",
                )

            if (
                producer.op in {"tlbwi", "tlbwr"}
                and consumer.op in {"eret", "deret"}
                and between < 5
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 5,
                    "tlb-write-eret",
                    "returned instruction fetch may consume a newly written TLB entry",
                    severity="POTENTIAL",
                )

            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 == "status"
                and is_cop1(consumer)
                and between < 4
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 4,
                    "mtc0-status-cop1",
                    "COP1 usability may depend on newly written Status.CU1",
                    severity="POTENTIAL",
                )

            if (
                producer.op in {"mfhi", "mflo"}
                and is_multdiv(consumer.op)
                and between < 2
            ):
                add_gap_finding(
                    findings, producer, consumer, between, 2,
                    "mfhilo-multdiv",
                    f"{consumer.op} follows {producer.op} too closely",
                )

        if not is_store(producer):
            if raised_stack_pointer(producer):
                for consumer in window:
                    if is_control(consumer.op):
                        break
                    if not (is_load(consumer) or is_store(consumer)):
                        continue
                    memory = MEM_RE.search(consumer.operands.lower().replace("$", ""))
                    if memory is None or memory.group(2) != "sp":
                        continue
                    displacement = parse_immediate(memory.group(1))
                    if displacement is None or displacement >= 0:
                        continue
                    findings.append(
                        Finding(
                            "ERROR", "stack-pop-before-restore",
                            "stack pointer was raised before a live save slot below sp was accessed",
                            consumer, producer,
                        )
                    )
            continue
        producer_mem = MEM_RE.search(producer.operands.lower().replace("$", ""))
        if producer_mem is None:
            continue
        producer_address = producer_mem.group(0)
        for offset, consumer in enumerate(window[:3]):
            if consumer.op != "cache":
                continue
            consumer_mem = MEM_RE.search(consumer.operands.lower().replace("$", ""))
            if consumer_mem is None or consumer_mem.group(0) != producer_address:
                continue
            intervening = window[:offset]
            if len(intervening) < 2 or any(is_load(item) or item.op == "cache" for item in intervening):
                findings.append(
                    Finding(
                        "ERROR", "store-cache-same-address",
                        "CACHE to the same textual address requires two intervening "
                        "non-load, non-CACHE instructions",
                        consumer, producer,
                    )
                )

    return findings


def audit_margins(instructions: list[Instruction]) -> dict[str, tuple[int, int, int]]:
    """Return rule -> (minimum observed gap, required gap, occurrence count)."""
    margins: dict[str, tuple[int, int, int]] = {}

    def record(rule: str, between: int, required: int) -> None:
        old = margins.get(rule)
        if old is None:
            margins[rule] = (between, required, 1)
        else:
            margins[rule] = (min(old[0], between), required, old[2] + 1)

    for index, producer in enumerate(instructions):
        producer_cp0 = cp0_register(producer)
        for offset, consumer in enumerate(contiguous_window(instructions, index, 12)):
            between = offset
            consumer_cp0 = cp0_register(consumer)
            if (
                producer.op in {"mtc0", "dmtc0"}
                and consumer.op in {"mfc0", "dmfc0"}
                and producer_cp0 == consumer_cp0
            ):
                record("mtc0-mfc0-same-register", between, 1)
            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 in {"entrylo0", "entrylo1", "pagemask", "entryhi"}
                and consumer.op in {"tlbwi", "tlbwr"}
            ):
                record("mtc0-tlb-write", between, 1)
            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 == "entryhi"
                and consumer.op == "tlbp"
            ):
                record("mtc0-entryhi-tlbp", between, 1)
            if (
                producer.op == "tlbp"
                and consumer.op in {"mfc0", "dmfc0"}
                and consumer_cp0 == "index"
            ):
                record("tlbp-mfc0-index", between, 2)
            if (
                producer.op == "tlbr"
                and consumer.op in {"mfc0", "dmfc0"}
                and consumer_cp0 in {"entrylo0", "entrylo1", "pagemask", "entryhi"}
            ):
                record("tlbr-mfc0", between, 3)
            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 in {"epc", "errorepc"}
                and consumer.op in {"eret", "deret"}
            ):
                record("mtc0-epc-eret", between, 2)
            if (
                producer.op in {"mtc0", "dmtc0"}
                and producer_cp0 == "status"
                and consumer.op in {"eret", "deret"}
            ):
                record("mtc0-status-eret", between, 3)
            if producer.op in {"tlbwi", "tlbwr"} and consumer.op == "tlbp":
                record("tlb-write-tlbp", between, 3)
            if producer.op in {"tlbwi", "tlbwr"} and consumer.op in {"eret", "deret"}:
                record("tlb-write-eret", between, 5)
            if producer.op in {"mfhi", "mflo"} and is_multdiv(consumer.op):
                record("mfhilo-multdiv", between, 2)
    return margins


def main() -> int:
    parser = argparse.ArgumentParser(
        description="check GNU objdump output for VR4300 ordering hazards"
    )
    parser.add_argument("disassembly", type=Path)
    parser.add_argument(
        "--strict-potential", action="store_true",
        help="make data-dependent POTENTIAL findings fatal",
    )
    parser.add_argument(
        "--show-margins", action="store_true",
        help="report minimum observed spacing for each architectural rule",
    )
    args = parser.parse_args()

    instructions = parse_disassembly(args.disassembly)
    if not instructions:
        print(f"{args.disassembly}: no objdump instructions found", file=sys.stderr)
        return 2

    findings = scan(instructions)
    for finding in findings:
        location = (
            f"{args.disassembly}:{finding.instruction.line_number}:"
            f"0x{finding.instruction.address:08x}"
        )
        print(
            f"{location}: {finding.severity} [{finding.rule}] "
            f"{finding.message} ({finding.instruction.function})",
            file=sys.stderr,
        )
        if finding.producer is not None:
            print(f"    writer: {finding.producer.text}", file=sys.stderr)
        print(f"    user:   {finding.instruction.text}", file=sys.stderr)

    errors = sum(item.severity == "ERROR" for item in findings)
    potentials = sum(item.severity == "POTENTIAL" for item in findings)
    if args.show_margins:
        for rule, (observed, required, count) in sorted(audit_margins(instructions).items()):
            print(
                f"margin {rule}: min-between={observed} required={required} "
                f"occurrences={count}"
            )
    print(
        f"check-vr4300-order: {len(instructions)} instructions, "
        f"{errors} error(s), {potentials} potential hazard(s)"
    )
    return 1 if errors or (args.strict_potential and potentials) else 0


if __name__ == "__main__":
    raise SystemExit(main())
