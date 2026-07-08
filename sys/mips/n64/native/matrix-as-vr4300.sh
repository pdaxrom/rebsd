#!/bin/sh
#
# Test the ReBSD assembler for the 32-bit instruction subset we expect
# to use on NEC VR4300.  With a GNU as argument, compare accept/reject
# behavior against GNU as.  With no GNU as argument, run as a target-side
# self-test against /usr/bin/as.
#
# Keep the target-side runner deliberately simple: the historic shell keeps
# function bodies in its small heap, so a matrix implemented as hundreds of
# shell function calls can exhaust or corrupt that heap on N64.
#

if test $# -gt 2; then
	echo "usage: $0 [/path/to/rebsd-as [/path/to/gnu-as]]" >&2
	exit 2
fi

if test $# -eq 0; then
	retro_as=/usr/bin/as
	target_only=1
else
	retro_as=$1
	if test $# -eq 1; then
		target_only=1
	else
		target_only=0
	fi
fi

test -x "$retro_as"
if test $? -ne 0; then
	echo "matrix-as-vr4300: ReBSD as is not executable: $retro_as" >&2
	exit 2
fi

gnu_as=
gnu_objcopy=
if test $target_only -eq 0; then
	gnu_as=$2
	if test -z "$gnu_as"; then
		gnu_as=mips64-elf-as
	fi
	command -v "$gnu_as" >/dev/null 2>&1
	if test $? -ne 0; then
		echo "matrix-as-vr4300: GNU as not found: $gnu_as" >&2
		exit 2
	fi
	case "$gnu_as" in
	*/mips64-elf-as)
		gnu_objcopy=`dirname "$gnu_as"`/mips64-elf-objcopy
		;;
	mips64-elf-as)
		gnu_objcopy=mips64-elf-objcopy
		;;
	*as)
		gnu_objcopy=`echo "$gnu_as" | sed 's/as$/objcopy/'`
		;;
	*)
		gnu_objcopy=mips64-elf-objcopy
		;;
	esac
	command -v "$gnu_objcopy" >/dev/null 2>&1
	if test $? -ne 0; then
		echo "matrix-as-vr4300: GNU objcopy not found: $gnu_objcopy" >&2
		exit 2
	fi
fi

cd /tmp || cd /var/tmp || exit 1

base=n64-as-vr4300-matrix.$$
src=$base.s
retro_o=$base.retro.o
gnu_o=$base.gnu.o
retro_text=$base.retro.text
gnu_text=$base.gnu.text
retro_log=$base.retro.log
gnu_log=$base.gnu.log
objcopy_log=$base.objcopy.log
total=209

trap 'rm -f "$src" "$retro_o" "$gnu_o" "$retro_text" "$gnu_text" "$retro_log" "$gnu_log" "$objcopy_log"' 0 1 2 3 15

while read expect name insn
do
	reason=
	rm -f "$src" "$retro_o" "$gnu_o" "$retro_text" "$gnu_text" \
	    "$retro_log" "$gnu_log" "$objcopy_log"
	echo ".text" > "$src"
	echo ".set noreorder" >> "$src"
	echo ".globl start" >> "$src"
	echo "start:" >> "$src"
	echo "	$insn" >> "$src"
	echo "target:" >> "$src"
	echo "	nop" >> "$src"

	"$retro_as" -EB -mips3 -march=vr4300 -o "$retro_o" "$src" \
	    > "$retro_log" 2>&1
	retro_rc=$?

	if test $target_only -eq 0; then
		"$gnu_as" -EB -mips3 -march=vr4300 -o "$gnu_o" "$src" \
		    > "$gnu_log" 2>&1
		gnu_rc=$?
	else
		gnu_rc=0
	fi

	case "$expect" in
	A)
		if test $target_only -eq 0; then
			if test $gnu_rc -ne 0; then
				reason="$name: GNU as rejected expected-valid instruction"
			fi
		fi
		if test -z "$reason"; then
			if test $retro_rc -ne 0; then
				reason="$name: ReBSD as rejected expected-valid instruction"
			fi
		fi
		;;
	R)
		if test $target_only -eq 0; then
			if test $gnu_rc -eq 0; then
				reason="$name: GNU as accepted expected-invalid instruction"
			fi
		fi
		if test -z "$reason"; then
			if test $retro_rc -eq 0; then
				reason="$name: ReBSD as accepted expected-invalid instruction"
			fi
		fi
		;;
	*)
		reason="$name: bad matrix expectation"
		;;
	esac

	if test -z "$reason"; then
		if test "$expect" = A; then
			if test $target_only -eq 0; then
				text_hex=`od -An -tx1 -j 4 -N 4 "$retro_o" | tr -d '[:space:]'`
				case "$text_hex" in
				????????)
					text_size=`printf "%d" "0x$text_hex" 2>/dev/null`
					if test $? -ne 0; then
			reason="$name: cannot parse ReBSD a.out text size"
					fi
					;;
				*)
			reason="$name: cannot read ReBSD a.out text size"
					;;
				esac
				if test -z "$reason"; then
					"$gnu_objcopy" -O binary -j .text "$gnu_o" \
					    "$gnu_text" >> "$objcopy_log" 2>&1
					if test $? -ne 0; then
						reason="$name: cannot extract GNU .text"
					fi
				fi
				if test -z "$reason"; then
					gnu_size=`wc -c < "$gnu_text" | tr -d '[:space:]'`
					if test "$text_size" -lt "$gnu_size"; then
			reason="$name: ReBSD .text shorter than GNU .text"
					fi
				fi
				if test -z "$reason"; then
					dd if="$retro_o" of="$retro_text" bs=1 skip=32 \
					    count="$gnu_size" > "$objcopy_log" 2>&1
					if test $? -ne 0; then
			reason="$name: cannot extract ReBSD .text"
					fi
				fi
				if test -z "$reason"; then
					cmp -s "$retro_text" "$gnu_text"
					if test $? -ne 0; then
						reason="$name: .text bytes differ from GNU as"
					fi
				fi
			fi
		fi
	fi

	if test -n "$reason"; then
		echo "matrix-as-vr4300: $reason" >&2
		echo "source:" >&2
		cat "$src" >&2
		if test -s "$gnu_log"; then
			echo "gnu-as:" >&2
			cat "$gnu_log" >&2
		fi
		if test -s "$retro_log"; then
			echo "rebsd-as:" >&2
			cat "$retro_log" >&2
		fi
		if test -s "$objcopy_log"; then
			echo "extract:" >&2
			cat "$objcopy_log" >&2
		fi
		if test -s "$retro_text"; then
			echo "ReBSD .text:" >&2
			od -An -tx4 "$retro_text" >&2
		fi
		if test -s "$gnu_text"; then
			echo "gnu .text:" >&2
			od -An -tx4 "$gnu_text" >&2
		fi
		rm -f "$src" "$retro_o" "$gnu_o" "$retro_text" "$gnu_text" \
		    "$retro_log" "$gnu_log" "$objcopy_log"
		exit 1
	fi
done <<'MATRIX_EOF'
A add add $2,$3,$4
A addu addu $2,$3,$4
A addi addi $2,$3,123
A addiu addiu $2,$3,123
A sub sub $2,$3,$4
A subu subu $2,$3,$4
A slt slt $2,$3,$4
A sltu sltu $2,$3,$4
A slti slti $2,$3,123
A sltiu sltiu $2,$3,123
A and and $2,$3,$4
A andi andi $2,$3,0x1234
A or or $2,$3,$4
A ori ori $2,$3,0x1234
A xor xor $2,$3,$4
A xori xori $2,$3,0x1234
A nor nor $2,$3,$4
A lui lui $2,0x1234
A sll sll $2,$3,4
A srl srl $2,$3,4
A sra sra $2,$3,4
A sllv sllv $2,$3,$4
A srlv srlv $2,$3,$4
A srav srav $2,$3,$4
A mult mult $2,$3
A multu multu $2,$3
A div div $0,$2,$3
A divu divu $0,$2,$3
A mfhi mfhi $2
A mflo mflo $2
A mthi mthi $2
A mtlo mtlo $2
A j j target
A jal jal target
A jr jr $31
A jalr jalr $31,$2
A beq beq $2,$3,target
A bne bne $2,$3,target
A blez blez $2,target
A bgtz bgtz $2,target
A bltz bltz $2,target
A bgez bgez $2,target
A bltzal bltzal $2,target
A bgezal bgezal $2,target
A beql beql $2,$3,target
A bnel bnel $2,$3,target
A blezl blezl $2,target
A bgtzl bgtzl $2,target
A bltzl bltzl $2,target
A bgezl bgezl $2,target
A bltzall bltzall $2,target
A bgezall bgezall $2,target
A lb lb $2,8($3)
A lbu lbu $2,8($3)
A lh lh $2,8($3)
A lhu lhu $2,8($3)
A lw lw $2,8($3)
A lwl lwl $2,8($3)
A lwr lwr $2,8($3)
A ld ld $2,8($3)
A sb sb $2,8($3)
A sh sh $2,8($3)
A sw sw $2,8($3)
A swl swl $2,8($3)
A swr swr $2,8($3)
A sd sd $2,8($3)
A ll ll $2,8($3)
A sc sc $2,8($3)
A syscall syscall 7
A break break 7
A sync sync
A cache cache 16,8($3)
A ehb ehb
A eret eret
A wait wait
A tlbp tlbp
A tlbr tlbr
A tlbwi tlbwi
A tlbwr tlbwr
A mfc0 mfc0 $2,$12
A mtc0 mtc0 $2,$12
A teq teq $2,$3
A tge tge $2,$3
A tgeu tgeu $2,$3
A tlt tlt $2,$3
A tltu tltu $2,$3
A tne tne $2,$3
A teqi teqi $2,7
A tgei tgei $2,7
A tgeiu tgeiu $2,7
A tlti tlti $2,7
A tltiu tltiu $2,7
A tnei tnei $2,7
A nop nop
A move move $2,$3
A li li $2,0x12345678
A la la $2,target
A mul mul $2,$3,$4
A b b target
A bal bal target
A beqz beqz $2,target
A bnez bnez $2,target
A mfc1 mfc1 $2,$f4
A mtc1 mtc1 $2,$f4
A cfc1 cfc1 $2,$31
A ctc1 ctc1 $2,$31
A lwc1 lwc1 $f2,8($3)
A swc1 swc1 $f2,8($3)
A ldc1 ldc1 $f2,8($3)
A sdc1 sdc1 $f2,8($3)
A l_s l.s $f2,8($3)
A s_s s.s $f2,8($3)
A l_d l.d $f2,8($3)
A s_d s.d $f2,8($3)
A bc1f bc1f target
A bc1t bc1t target
A bc1fl bc1fl target
A bc1tl bc1tl target
A add_s add.s $f2,$f4,$f6
A add_d add.d $f2,$f4,$f6
A sub_s sub.s $f2,$f4,$f6
A sub_d sub.d $f2,$f4,$f6
A mul_s mul.s $f2,$f4,$f6
A mul_d mul.d $f2,$f4,$f6
A div_s div.s $f2,$f4,$f6
A div_d div.d $f2,$f4,$f6
A sqrt_s sqrt.s $f2,$f4
A sqrt_d sqrt.d $f2,$f4
A abs_s abs.s $f2,$f4
A abs_d abs.d $f2,$f4
A mov_s mov.s $f2,$f4
A mov_d mov.d $f2,$f4
A neg_s neg.s $f2,$f4
A neg_d neg.d $f2,$f4
A cvt_s_d cvt.s.d $f2,$f4
A cvt_s_w cvt.s.w $f2,$f4
A cvt_s_l cvt.s.l $f2,$f4
A cvt_d_s cvt.d.s $f2,$f4
A cvt_d_w cvt.d.w $f2,$f4
A cvt_d_l cvt.d.l $f2,$f4
A cvt_w_s cvt.w.s $f2,$f4
A cvt_w_d cvt.w.d $f2,$f4
A cvt_l_s cvt.l.s $f2,$f4
A cvt_l_d cvt.l.d $f2,$f4
A round_w_s round.w.s $f2,$f4
A round_w_d round.w.d $f2,$f4
A trunc_w_s trunc.w.s $f2,$f4
A trunc_w_d trunc.w.d $f2,$f4
A ceil_w_s ceil.w.s $f2,$f4
A ceil_w_d ceil.w.d $f2,$f4
A floor_w_s floor.w.s $f2,$f4
A floor_w_d floor.w.d $f2,$f4
A round_l_s round.l.s $f2,$f4
A round_l_d round.l.d $f2,$f4
A trunc_l_s trunc.l.s $f2,$f4
A trunc_l_d trunc.l.d $f2,$f4
A ceil_l_s ceil.l.s $f2,$f4
A ceil_l_d ceil.l.d $f2,$f4
A floor_l_s floor.l.s $f2,$f4
A floor_l_d floor.l.d $f2,$f4
A c_f_s c.f.s $f2,$f4
A c_un_s c.un.s $f2,$f4
A c_eq_s c.eq.s $f2,$f4
A c_ueq_s c.ueq.s $f2,$f4
A c_olt_s c.olt.s $f2,$f4
A c_ult_s c.ult.s $f2,$f4
A c_ole_s c.ole.s $f2,$f4
A c_ule_s c.ule.s $f2,$f4
A c_sf_s c.sf.s $f2,$f4
A c_ngle_s c.ngle.s $f2,$f4
A c_seq_s c.seq.s $f2,$f4
A c_ngl_s c.ngl.s $f2,$f4
A c_lt_s c.lt.s $f2,$f4
A c_nge_s c.nge.s $f2,$f4
A c_le_s c.le.s $f2,$f4
A c_ngt_s c.ngt.s $f2,$f4
A c_f_d c.f.d $f2,$f4
A c_un_d c.un.d $f2,$f4
A c_eq_d c.eq.d $f2,$f4
A c_ueq_d c.ueq.d $f2,$f4
A c_olt_d c.olt.d $f2,$f4
A c_ult_d c.ult.d $f2,$f4
A c_ole_d c.ole.d $f2,$f4
A c_ule_d c.ule.d $f2,$f4
A c_sf_d c.sf.d $f2,$f4
A c_ngle_d c.ngle.d $f2,$f4
A c_seq_d c.seq.d $f2,$f4
A c_ngl_d c.ngl.d $f2,$f4
A c_lt_d c.lt.d $f2,$f4
A c_nge_d c.nge.d $f2,$f4
A c_le_d c.le.d $f2,$f4
A c_ngt_d c.ngt.d $f2,$f4
R movn movn $2,$3,$4
R movz movz $2,$3,$4
R clz clz $2,$3
R clo clo $2,$3
R ext ext $2,$3,0,8
R ins ins $2,$3,0,8
R madd madd $2,$3
R maddu maddu $2,$3
R msub msub $2,$3
R msubu msubu $2,$3
R seb seb $2,$3
R seh seh $2,$3
R wsbh wsbh $2,$3
R rdhwr rdhwr $2,$3
R di di
R ei ei
R cache32 cache 32,0($3)
MATRIX_EOF

rm -f "$src" "$retro_o" "$gnu_o" "$retro_text" "$gnu_text" \
    "$retro_log" "$gnu_log" "$objcopy_log"
echo "matrix-as-vr4300: $total checks ok"
exit 0
