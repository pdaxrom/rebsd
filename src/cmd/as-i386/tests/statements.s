	.text
	.globl statement_separators
	.type statement_separators, @function
statement_separators: pushl %ebx; movl $1, %eax; addl $2, %eax
	popl %ebx ; jmp 1f
1: nop; ret # a comment after the final statement
	.size statement_separators, .-statement_separators
	.ascii "semicolon;inside;string"
	.weak weak_target; .globl weak_target
weak_target: ret
