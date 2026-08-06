#if defined(__i386__)
	.text
	.align 4
	.globl asm_check_reg
asm_check_reg:
	xorl	%eax,%eax
	cmpl	$0x11,4(%esp)
	jne	.Li386_check_reg_done
	cmpl	$0x55667788,8(%esp)
	jne	.Li386_check_reg_done
	cmpl	$0x11223344,12(%esp)
	jne	.Li386_check_reg_done
	movl	$1,%eax
.Li386_check_reg_done:
	ret

	.align 4
	.globl asm_echo_reg
asm_echo_reg:
	movl	8(%esp),%eax
	movl	12(%esp),%edx
	ret

	.align 4
	.globl asm_check_stack3
asm_check_stack3:
	xorl	%eax,%eax
	cmpl	$1,4(%esp)
	jne	.Li386_check_stack3_done
	cmpl	$2,8(%esp)
	jne	.Li386_check_stack3_done
	cmpl	$3,12(%esp)
	jne	.Li386_check_stack3_done
	cmpl	$0x55667788,16(%esp)
	jne	.Li386_check_stack3_done
	cmpl	$0x11223344,20(%esp)
	jne	.Li386_check_stack3_done
	movl	$1,%eax
.Li386_check_stack3_done:
	ret

	.align 4
	.globl asm_check_stack4
asm_check_stack4:
	xorl	%eax,%eax
	cmpl	$1,4(%esp)
	jne	.Li386_check_stack4_done
	cmpl	$2,8(%esp)
	jne	.Li386_check_stack4_done
	cmpl	$3,12(%esp)
	jne	.Li386_check_stack4_done
	cmpl	$4,16(%esp)
	jne	.Li386_check_stack4_done
	cmpl	$0x44332211,20(%esp)
	jne	.Li386_check_stack4_done
	cmpl	$0x88776655,24(%esp)
	jne	.Li386_check_stack4_done
	movl	$1,%eax
.Li386_check_stack4_done:
	ret

	.align 4
	.globl asm_ret_ull
asm_ret_ull:
	movl	$0x44332211,%eax
	movl	$0x88776655,%edx
	ret

	.align 4
	.globl asm_call_c_check
asm_call_c_check:
	pushl	$0x01020304
	pushl	$0x05060708
	pushl	$0x33
	call	c_check_reg
	addl	$12,%esp
	ret

	.align 4
	.globl asm_call_c_ret
asm_call_c_ret:
	call	c_ret_ull
	cmpl	$0x44332211,%eax
	jne	.Li386_call_c_ret_bad
	cmpl	$0x88776655,%edx
	jne	.Li386_call_c_ret_bad
	movl	$1,%eax
	ret
.Li386_call_c_ret_bad:
	xorl	%eax,%eax
	ret
#else
	.text
	.set noreorder
	.align 2

#if defined(__MIPSEL__) || defined(__mipsel__) || defined(TARGET_LITTLE_ENDIAN)
#define ABI_WORD0_1122334455667788 0x55667788
#define ABI_WORD1_1122334455667788 0x11223344
#define ABI_WORD0_0102030405060708 0x05060708
#define ABI_WORD1_0102030405060708 0x01020304
#define ABI_WORD0_8877665544332211 0x44332211
#define ABI_WORD1_8877665544332211 0x88776655
#else
#define ABI_WORD0_1122334455667788 0x11223344
#define ABI_WORD1_1122334455667788 0x55667788
#define ABI_WORD0_0102030405060708 0x01020304
#define ABI_WORD1_0102030405060708 0x05060708
#define ABI_WORD0_8877665544332211 0x88776655
#define ABI_WORD1_8877665544332211 0x44332211
#endif

	.globl asm_check_reg
	.ent asm_check_reg
asm_check_reg:
	li	$v0,0
	li	$t0,0x11
	bne	$a0,$t0,Lasm_check_reg_done
	nop
	li	$t0,ABI_WORD0_1122334455667788
	bne	$a2,$t0,Lasm_check_reg_done
	nop
	li	$t0,ABI_WORD1_1122334455667788
	bne	$a3,$t0,Lasm_check_reg_done
	nop
	li	$v0,1
Lasm_check_reg_done:
	jr	$ra
	nop
	.end asm_check_reg

	.align 2
	.globl asm_echo_reg
	.ent asm_echo_reg
asm_echo_reg:
	move	$v0,$a2
	move	$v1,$a3
	jr	$ra
	nop
	.end asm_echo_reg

	.align 2
	.globl asm_check_stack3
	.ent asm_check_stack3
asm_check_stack3:
	li	$v0,0
	li	$t0,1
	bne	$a0,$t0,Lasm_check_stack3_done
	nop
	li	$t0,2
	bne	$a1,$t0,Lasm_check_stack3_done
	nop
	li	$t0,3
	bne	$a2,$t0,Lasm_check_stack3_done
	nop
	li	$t1,ABI_WORD0_1122334455667788
	lw	$t0,16($sp)
	bne	$t0,$t1,Lasm_check_stack3_done
	nop
	lw	$t0,20($sp)
	li	$t1,ABI_WORD1_1122334455667788
	bne	$t0,$t1,Lasm_check_stack3_done
	nop
	li	$v0,1
Lasm_check_stack3_done:
	jr	$ra
	nop
	.end asm_check_stack3

	.align 2
	.globl asm_check_stack4
	.ent asm_check_stack4
asm_check_stack4:
	li	$v0,0
	li	$t0,1
	bne	$a0,$t0,Lasm_check_stack4_done
	nop
	li	$t0,2
	bne	$a1,$t0,Lasm_check_stack4_done
	nop
	li	$t0,3
	bne	$a2,$t0,Lasm_check_stack4_done
	nop
	li	$t0,4
	bne	$a3,$t0,Lasm_check_stack4_done
	nop
	lw	$t0,16($sp)
	li	$t1,ABI_WORD0_8877665544332211
	bne	$t0,$t1,Lasm_check_stack4_done
	nop
	lw	$t0,20($sp)
	li	$t1,ABI_WORD1_8877665544332211
	bne	$t0,$t1,Lasm_check_stack4_done
	nop
	li	$v0,1
Lasm_check_stack4_done:
	jr	$ra
	nop
	.end asm_check_stack4

	.align 2
	.globl asm_ret_ull
	.ent asm_ret_ull
asm_ret_ull:
	li	$v0,ABI_WORD0_8877665544332211
	li	$v1,ABI_WORD1_8877665544332211
	jr	$ra
	nop
	.end asm_ret_ull

	.align 2
	.globl asm_call_c_check
	.ent asm_call_c_check
asm_call_c_check:
	addiu	$sp,$sp,-32
	sw	$ra,28($sp)
	li	$a0,0x33
	li	$a2,ABI_WORD0_0102030405060708
	li	$a3,ABI_WORD1_0102030405060708
	jal	c_check_reg
	nop
	lw	$ra,28($sp)
	addiu	$sp,$sp,32
	jr	$ra
	nop
	.end asm_call_c_check

	.align 2
	.globl asm_call_c_ret
	.ent asm_call_c_ret
asm_call_c_ret:
	addiu	$sp,$sp,-32
	sw	$ra,28($sp)
	move	$t2,$zero
	jal	c_ret_ull
	nop
	li	$t0,ABI_WORD0_8877665544332211
	bne	$v0,$t0,Lasm_call_c_ret_done
	nop
	li	$t0,ABI_WORD1_8877665544332211
	bne	$v1,$t0,Lasm_call_c_ret_done
	nop
	li	$t2,1
Lasm_call_c_ret_done:
	move	$v0,$t2
	lw	$ra,28($sp)
	addiu	$sp,$sp,32
	jr	$ra
	nop
	.end asm_call_c_ret
#endif
