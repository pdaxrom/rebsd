	.text
	.globl pcc_integer
	.type pcc_integer,@function
pcc_integer:
	pushl %ebp
	movl %esp,%ebp
	pushw %ax
	popl %eax
	movb %al,(%edi)
	movw 4(%ebp),%ax
	movl symbol,%eax
	movsbl (%esi),%eax
	movsbw (%esi),%ax
	movswl (%esi),%eax
	movzbl (%esi),%eax
	movzbw (%esi),%ax
	movzwl (%esi),%eax
	leal symbol@GOTOFF(%ebx),%eax
	addl $7,%eax
	adcl %edx,%eax
	subl %eax,(%esp)
	sbbl $1,%edx
	andl %ecx,%eax
	orl $4,%eax
	xorl %edx,%edx
	cmpl %ecx,%eax
	testb $32,%cl
	incl %eax
	decl (%esp)
	negl %eax
	notl %eax
	mull %ecx
	imull %ecx,%eax
	imull $9,%eax
	idivl %ecx
	divl %ecx
	shll $2,%eax
	shrl %cl,%eax
	sarl $31,%edx
	shldl %cl,%edx,%eax
	shrdl $1,%edx,%eax
	call symbol@PLT
	call *%eax
	jmp *4(%eax)
	je 1f
	jne 1f
	ja 1f
	jae 1f
	jb 1f
	jbe 1f
	jg 1f
	jge 1f
	jl 1f
	jle 1f
	jp 1f
	jnp 1f
1:
	setne %al
	cmovel %edx,%eax
	cltd
	leave
	ret $4
	.size pcc_integer,.-pcc_integer

	.globl pcc_x87
	.type pcc_x87,@function
pcc_x87:
	flds (%eax)
	fldl (%eax)
	fldt (%eax)
	fsts (%eax)
	fstps (%eax)
	fstpl (%eax)
	fstpt (%eax)
	fild (%eax)
	fildl (%eax)
	fildq (%eax)
	fistpl (%eax)
	fistpq (%eax)
	fldcw (%eax)
	fnstcw (%eax)
	fnstsw %ax
	fadds (%eax)
	faddl (%eax)
	fsubs (%eax)
	fsubl (%eax)
	fmuls (%eax)
	fmull (%eax)
	fdivs (%eax)
	fdivl (%eax)
	fxch
	fucomip %st(1),%st
	fstp %st(0)
	fucompp
	faddp
	fsubp
	fsubrp
	fmulp
	fdivp
	fdivrp
	sahf
	ret
	.size pcc_x87,.-pcc_x87

	.data
	.align 4
symbol:
	.long pcc_integer
	.word 1
	.byte 2
	.quad 0x1122334455667788
	.float 0f1.5
	.double 0d2.5
	.ascii "pcc"
	.asciz "i386"
