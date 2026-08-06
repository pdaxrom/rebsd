	.text
	.globl add_one
	.type add_one,@function
	.p2align 2
add_one:
	pushl %ebp
	movl %esp,%ebp
	movl 8(%ebp),%eax
	addl $1,%eax
	cmpl $0,%eax
	je 1f
	call external_function
1:
	leave
	ret
	.size add_one,.-add_one

	.data
	.globl object
	.type object,@object
object:
	.long add_one
	.long external_object+4
	.size object,.-object

	.section .rodata,"a",@progbits
message:
	.asciz "i386\n"

	.bss
	.lcomm scratch,16
	.comm common_object,8,8

	.section .note.GNU-stack,"",@progbits
