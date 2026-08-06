	.text
legacy_start:
	aaa
	cbw
	cdq
	clc
	cli
	cmc
	cwd
	cwde
	daa
	hlt
	int1
	into
	iret
	iretw
	lahf
	popa
	popaw
	popf
	popfw
	pusha
	pushaw
	pushf
	pushfw
	rdmsr
	rdpmc
	rdtsc
	rsm
	stc
	sti
	sysexit
	ud2
	wrmsr
	xlat
	emms
	femms
	sfence
	lfence
	mfence
	movsw
	movsl
	cmpsw
	cmpsl
	stosw
	stosl
	lodsw
	lodsl
	scasw
	scasl
	insw
	insl
	outsw
	outsl
	arpl	%ax, (%eax)
	boundw	%ax, (%eax)
	movl	(%bx,%si), %eax
	movl	1(%bp,%di), %eax
	movl	0x1234(%si), %eax
	addr16 movl	0x1234, %eax
	data16 movl	(%eax), %ecx
	ldsw	(%eax), %cx
	ldsl	(%eax), %ecx
	lesw	(%eax), %cx
	lesl	(%eax), %ecx
	larw	%ax, %cx
	lslw	%ax, %cx
	lssw	(%eax), %cx
	lssl	(%eax), %ecx
	lfsw	(%eax), %cx
	lfsl	(%eax), %ecx
	lgsw	(%eax), %cx
	lgsl	(%eax), %ecx
	bsfw	%ax, %cx
	bsrw	%ax, %cx
	btw	%ax, %cx
	btw	$3, (%eax)
	btcw	%ax, %cx
	btcw	$3, (%eax)
	btrw	%ax, %cx
	btrw	$3, (%eax)
	btsw	%ax, %cx
	btsw	$3, (%eax)
	cmpxchgb	%al, (%ecx)
	cmpxchgw	%ax, (%ecx)
	xaddb	%al, %cl
	xaddw	%ax, %cx
	sldt	%ax
	str	%ax
	lldt	%ax
	ltr	%ax
	verr	%ax
	verw	%ax
	rclb	$1, %al
	rcrl	%cl, %eax
	rolw	$3, %ax
	rorl	%cl, %eax
	inb	$0x60, %al
	inw	%dx, %ax
	inl	%dx, %eax
	outb	%al, $0x80
	outw	%ax, %dx
	outl	%eax, %dx
	aad	$10
	aam
	loop	1f
1:	loope	2f
2:	loopne	3f
3:	jecxz	4f
4:	jcxz	5f
5:
	nop	(%eax)
	nopl	(%eax)
	prefetcht1	(%eax)
	prefetcht2	(%eax)
	prefetch	(%eax)
	popcntw	%ax, %cx
	lzcntw	%ax, %cx
	callw	*%ax
	jmpw	*%ax
	lcall	$0x8, $0x12345678
	ljmp	$0x8, $0x12345678
	lcall	*(%eax)
	ljmp	*(%eax)
	lret
	lret	$8
	retf
	retf	$8
	movbew	(%eax), %cx
	movbel	(%eax), %ecx
	movbew	%cx, (%eax)
	movbel	%ecx, (%eax)
	movsx	%al, %ecx
	movzx	%ax, %ecx
	movzb	%al, %ecx
	movzw	%ax, %ecx
	movsb	%al, %cx
	movsw	%ax, %ecx
	crc32b	%al, %ecx
	crc32w	%ax, %ecx
	crc32l	%eax, %ecx
	extractps	$2, %xmm1, %eax
	pslldq	$3, %xmm1
	psrldq	$3, %xmm1
	cvtpd2dq	%xmm0, %xmm1
	cvttpd2dq	%xmm0, %xmm1
	cvtdq2pd	%xmm0, %xmm1
	movq2dq	%mm0, %xmm1
	movdq2q	%xmm0, %mm1
	cmpeqps	%xmm0, %xmm1
	cmpnltss	%xmm0, %xmm1
	cmplepd	%xmm0, %xmm1
	cmpordsd	%xmm0, %xmm1
	ud1w	%ax, %cx
	ud1l	%eax, %ecx
	ud1	%eax, %ecx
	ud2bw	%ax, %cx
	ud2bl	%eax, %ecx
	ud2b	%eax, %ecx
	ud0w	%ax, %cx
	ud0	%eax, %ecx
	ud2a
	ud0l	%eax, %ecx
	ud1w	%ax, %cx
