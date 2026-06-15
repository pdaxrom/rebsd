#!/bin/sh
#
# Compare the RetroBSD in-tree assembler against GNU as for the 32-bit
# instruction subset we expect to use on NEC VR4300.
#

if test $# -lt 1 -o $# -gt 2; then
	echo "usage: $0 /path/to/retrobsd-as [/path/to/gnu-as]" >&2
	exit 2
fi

retro_as=$1
gnu_as=${2:-mips64-elf-as}

if ! test -x "$retro_as"; then
	echo "matrix-as-vr4300: retrobsd as is not executable: $retro_as" >&2
	exit 2
fi

if ! command -v "$gnu_as" >/dev/null 2>&1; then
	echo "matrix-as-vr4300: GNU as not found: $gnu_as" >&2
	exit 2
fi

cd /tmp || cd /var/tmp || exit 1

base=n64-as-vr4300-matrix.$$
src=$base.s
retro_o=$base.retro.o
gnu_o=$base.gnu.o
retro_log=$base.retro.log
gnu_log=$base.gnu.log
ok=0
failed=0

trap 'rm -f "$src" "$retro_o" "$gnu_o" "$retro_log" "$gnu_log"' 0 1 2 3 15

make_source()
{
	insn=$1

	rm -f "$src" "$retro_o" "$gnu_o" "$retro_log" "$gnu_log"
	echo ".text" > "$src"
	echo ".set noreorder" >> "$src"
	echo ".globl start" >> "$src"
	echo "start:" >> "$src"
	echo "	$insn" >> "$src"
	echo "target:" >> "$src"
	echo "	nop" >> "$src"
}

run_retro()
{
	"$retro_as" -EB -mips3 -march=vr4300 -o "$retro_o" "$src" \
	    > "$retro_log" 2>&1
	return $?
}

run_gnu()
{
	"$gnu_as" -EB -mips3 -march=vr4300 -o "$gnu_o" "$src" \
	    > "$gnu_log" 2>&1
	return $?
}

show_failure()
{
	name=$1

	echo "matrix-as-vr4300: $name" >&2
	echo "source:" >&2
	cat "$src" >&2
	if test -s "$gnu_log"; then
		echo "gnu-as:" >&2
		cat "$gnu_log" >&2
	fi
	if test -s "$retro_log"; then
		echo "retrobsd-as:" >&2
		cat "$retro_log" >&2
	fi
}

accept()
{
	name=$1
	insn=$2

	make_source "$insn"
	run_gnu
	gnu_rc=$?
	run_retro
	retro_rc=$?

	if test $gnu_rc -ne 0; then
		show_failure "$name: GNU as rejected expected-valid instruction"
		failed=`expr $failed + 1`
		return
	fi
	if test $retro_rc -ne 0; then
		show_failure "$name: RetroBSD as rejected GNU-valid instruction"
		failed=`expr $failed + 1`
		return
	fi
	ok=`expr $ok + 1`
}

reject()
{
	name=$1
	insn=$2

	make_source "$insn"
	run_gnu
	gnu_rc=$?
	run_retro
	retro_rc=$?

	if test $gnu_rc -eq 0; then
		show_failure "$name: GNU as accepted expected-invalid instruction"
		failed=`expr $failed + 1`
		return
	fi
	if test $retro_rc -eq 0; then
		show_failure "$name: RetroBSD as accepted GNU-invalid instruction"
		failed=`expr $failed + 1`
		return
	fi
	ok=`expr $ok + 1`
}

# Integer arithmetic and logic.
accept add 'add $2,$3,$4'
accept addu 'addu $2,$3,$4'
accept addi 'addi $2,$3,123'
accept addiu 'addiu $2,$3,123'
accept sub 'sub $2,$3,$4'
accept subu 'subu $2,$3,$4'
accept slt 'slt $2,$3,$4'
accept sltu 'sltu $2,$3,$4'
accept slti 'slti $2,$3,123'
accept sltiu 'sltiu $2,$3,123'
accept and 'and $2,$3,$4'
accept andi 'andi $2,$3,0x1234'
accept or 'or $2,$3,$4'
accept ori 'ori $2,$3,0x1234'
accept xor 'xor $2,$3,$4'
accept xori 'xori $2,$3,0x1234'
accept nor 'nor $2,$3,$4'
accept lui 'lui $2,0x1234'

# Shifts.
accept sll 'sll $2,$3,4'
accept srl 'srl $2,$3,4'
accept sra 'sra $2,$3,4'
accept sllv 'sllv $2,$3,$4'
accept srlv 'srlv $2,$3,$4'
accept srav 'srav $2,$3,$4'

# Multiply/divide and HI/LO.
accept mult 'mult $2,$3'
accept multu 'multu $2,$3'
accept div 'div $2,$3'
accept divu 'divu $2,$3'
accept mfhi 'mfhi $2'
accept mflo 'mflo $2'
accept mthi 'mthi $2'
accept mtlo 'mtlo $2'

# Branches and jumps.
accept j 'j target'
accept jal 'jal target'
accept jr 'jr $31'
accept jalr 'jalr $31,$2'
accept beq 'beq $2,$3,target'
accept bne 'bne $2,$3,target'
accept blez 'blez $2,target'
accept bgtz 'bgtz $2,target'
accept bltz 'bltz $2,target'
accept bgez 'bgez $2,target'
accept bltzal 'bltzal $2,target'
accept bgezal 'bgezal $2,target'
accept beql 'beql $2,$3,target'
accept bnel 'bnel $2,$3,target'
accept blezl 'blezl $2,target'
accept bgtzl 'bgtzl $2,target'
accept bltzl 'bltzl $2,target'
accept bgezl 'bgezl $2,target'
accept bltzall 'bltzall $2,target'
accept bgezall 'bgezall $2,target'

# Loads/stores and atomics.
accept lb 'lb $2,8($3)'
accept lbu 'lbu $2,8($3)'
accept lh 'lh $2,8($3)'
accept lhu 'lhu $2,8($3)'
accept lw 'lw $2,8($3)'
accept lwl 'lwl $2,8($3)'
accept lwr 'lwr $2,8($3)'
accept sb 'sb $2,8($3)'
accept sh 'sh $2,8($3)'
accept sw 'sw $2,8($3)'
accept swl 'swl $2,8($3)'
accept swr 'swr $2,8($3)'
accept ll 'll $2,8($3)'
accept sc 'sc $2,8($3)'

# System, CP0, cache, and traps.
accept syscall 'syscall 7'
accept break 'break 7'
accept sync 'sync'
accept cache 'cache 16,8($3)'
accept ehb 'ehb'
accept eret 'eret'
accept wait 'wait'
accept tlbp 'tlbp'
accept tlbr 'tlbr'
accept tlbwi 'tlbwi'
accept tlbwr 'tlbwr'
accept mfc0 'mfc0 $2,$12'
accept mtc0 'mtc0 $2,$12'
accept teq 'teq $2,$3'
accept tge 'tge $2,$3'
accept tgeu 'tgeu $2,$3'
accept tlt 'tlt $2,$3'
accept tltu 'tltu $2,$3'
accept tne 'tne $2,$3'
accept teqi 'teqi $2,7'
accept tgei 'tgei $2,7'
accept tgeiu 'tgeiu $2,7'
accept tlti 'tlti $2,7'
accept tltiu 'tltiu $2,7'
accept tnei 'tnei $2,7'

# Common assembler pseudo-instructions already supported by RetroBSD as.
accept nop 'nop'
accept move 'move $2,$3'
accept li 'li $2,0x12345678'
accept la 'la $2,target'
accept b 'b target'
accept bal 'bal target'
accept beqz 'beqz $2,target'
accept bnez 'bnez $2,target'

# COP1 transfer, load/store, branch, arithmetic, conversion, and compare.
accept mfc1 'mfc1 $2,$f4'
accept mtc1 'mtc1 $2,$f4'
accept cfc1 'cfc1 $2,$31'
accept ctc1 'ctc1 $2,$31'
accept lwc1 'lwc1 $f2,8($3)'
accept swc1 'swc1 $f2,8($3)'
accept ldc1 'ldc1 $f2,8($3)'
accept sdc1 'sdc1 $f2,8($3)'
accept l_s 'l.s $f2,8($3)'
accept s_s 's.s $f2,8($3)'
accept l_d 'l.d $f2,8($3)'
accept s_d 's.d $f2,8($3)'
accept bc1f 'bc1f target'
accept bc1t 'bc1t target'
accept bc1fl 'bc1fl target'
accept bc1tl 'bc1tl target'
accept add_s 'add.s $f2,$f4,$f6'
accept add_d 'add.d $f2,$f4,$f6'
accept sub_s 'sub.s $f2,$f4,$f6'
accept sub_d 'sub.d $f2,$f4,$f6'
accept mul_s 'mul.s $f2,$f4,$f6'
accept mul_d 'mul.d $f2,$f4,$f6'
accept div_s 'div.s $f2,$f4,$f6'
accept div_d 'div.d $f2,$f4,$f6'
accept sqrt_s 'sqrt.s $f2,$f4'
accept sqrt_d 'sqrt.d $f2,$f4'
accept abs_s 'abs.s $f2,$f4'
accept abs_d 'abs.d $f2,$f4'
accept mov_s 'mov.s $f2,$f4'
accept mov_d 'mov.d $f2,$f4'
accept neg_s 'neg.s $f2,$f4'
accept neg_d 'neg.d $f2,$f4'
accept cvt_s_d 'cvt.s.d $f2,$f4'
accept cvt_s_w 'cvt.s.w $f2,$f4'
accept cvt_s_l 'cvt.s.l $f2,$f4'
accept cvt_d_s 'cvt.d.s $f2,$f4'
accept cvt_d_w 'cvt.d.w $f2,$f4'
accept cvt_d_l 'cvt.d.l $f2,$f4'
accept cvt_w_s 'cvt.w.s $f2,$f4'
accept cvt_w_d 'cvt.w.d $f2,$f4'
accept cvt_l_s 'cvt.l.s $f2,$f4'
accept cvt_l_d 'cvt.l.d $f2,$f4'
accept round_w_s 'round.w.s $f2,$f4'
accept round_w_d 'round.w.d $f2,$f4'
accept trunc_w_s 'trunc.w.s $f2,$f4'
accept trunc_w_d 'trunc.w.d $f2,$f4'
accept ceil_w_s 'ceil.w.s $f2,$f4'
accept ceil_w_d 'ceil.w.d $f2,$f4'
accept floor_w_s 'floor.w.s $f2,$f4'
accept floor_w_d 'floor.w.d $f2,$f4'
accept round_l_s 'round.l.s $f2,$f4'
accept round_l_d 'round.l.d $f2,$f4'
accept trunc_l_s 'trunc.l.s $f2,$f4'
accept trunc_l_d 'trunc.l.d $f2,$f4'
accept ceil_l_s 'ceil.l.s $f2,$f4'
accept ceil_l_d 'ceil.l.d $f2,$f4'
accept floor_l_s 'floor.l.s $f2,$f4'
accept floor_l_d 'floor.l.d $f2,$f4'

accept c_f_s 'c.f.s $f2,$f4'
accept c_un_s 'c.un.s $f2,$f4'
accept c_eq_s 'c.eq.s $f2,$f4'
accept c_ueq_s 'c.ueq.s $f2,$f4'
accept c_olt_s 'c.olt.s $f2,$f4'
accept c_ult_s 'c.ult.s $f2,$f4'
accept c_ole_s 'c.ole.s $f2,$f4'
accept c_ule_s 'c.ule.s $f2,$f4'
accept c_sf_s 'c.sf.s $f2,$f4'
accept c_ngle_s 'c.ngle.s $f2,$f4'
accept c_seq_s 'c.seq.s $f2,$f4'
accept c_ngl_s 'c.ngl.s $f2,$f4'
accept c_lt_s 'c.lt.s $f2,$f4'
accept c_nge_s 'c.nge.s $f2,$f4'
accept c_le_s 'c.le.s $f2,$f4'
accept c_ngt_s 'c.ngt.s $f2,$f4'
accept c_f_d 'c.f.d $f2,$f4'
accept c_un_d 'c.un.d $f2,$f4'
accept c_eq_d 'c.eq.d $f2,$f4'
accept c_ueq_d 'c.ueq.d $f2,$f4'
accept c_olt_d 'c.olt.d $f2,$f4'
accept c_ult_d 'c.ult.d $f2,$f4'
accept c_ole_d 'c.ole.d $f2,$f4'
accept c_ule_d 'c.ule.d $f2,$f4'
accept c_sf_d 'c.sf.d $f2,$f4'
accept c_ngle_d 'c.ngle.d $f2,$f4'
accept c_seq_d 'c.seq.d $f2,$f4'
accept c_ngl_d 'c.ngl.d $f2,$f4'
accept c_lt_d 'c.lt.d $f2,$f4'
accept c_nge_d 'c.nge.d $f2,$f4'
accept c_le_d 'c.le.d $f2,$f4'
accept c_ngt_d 'c.ngt.d $f2,$f4'

# MIPS32/MIPS32r2 real instructions that GNU rejects for -march=vr4300.
# GAS accepts "mul" as a macro that expands to multu/mflo, so it belongs to
# pseudo-instruction compatibility coverage rather than this real-opcode list.
reject movn 'movn $2,$3,$4'
reject movz 'movz $2,$3,$4'
reject clz 'clz $2,$3'
reject clo 'clo $2,$3'
reject ext 'ext $2,$3,0,8'
reject ins 'ins $2,$3,0,8'
reject madd 'madd $2,$3'
reject maddu 'maddu $2,$3'
reject msub 'msub $2,$3'
reject msubu 'msubu $2,$3'
reject seb 'seb $2,$3'
reject seh 'seh $2,$3'
reject wsbh 'wsbh $2,$3'
reject rdhwr 'rdhwr $2,$3'
reject di 'di'
reject ei 'ei'
reject cache32 'cache 32,0($3)'

if test $failed -ne 0; then
	echo "matrix-as-vr4300: $failed failed, $ok passed" >&2
	exit 1
fi

echo "matrix-as-vr4300: $ok checks ok"
