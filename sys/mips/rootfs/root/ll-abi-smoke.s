	.text
	.set noreorder
	.align 2
	.globl asm_check_reg
	.ent asm_check_reg
asm_check_reg:
	li	$v0,0
	li	$t0,0x11
	bne	$a0,$t0,Lasm_check_reg_done
	nop
	li	$t0,0x11223344
	bne	$a2,$t0,Lasm_check_reg_done
	nop
	li	$t0,0x55667788
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
	lw	$t0,16($sp)
	li	$t1,0x11223344
	bne	$t0,$t1,Lasm_check_stack3_done
	nop
	lw	$t0,20($sp)
	li	$t1,0x55667788
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
	li	$t1,0x88776655
	bne	$t0,$t1,Lasm_check_stack4_done
	nop
	lw	$t0,20($sp)
	li	$t1,0x44332211
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
	li	$v0,0x88776655
	li	$v1,0x44332211
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
	li	$a2,0x01020304
	li	$a3,0x05060708
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
	li	$t0,0x88776655
	bne	$v0,$t0,Lasm_call_c_ret_done
	nop
	li	$t0,0x44332211
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
