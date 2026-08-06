	.text
start:
	boundl	%ecx, (%eax)
	larl	%ax, %edx
	lsll	%ax, %edx
	bsfl	%eax, %ecx
	bsrl	(%eax), %ecx
	btl	%eax, %ecx
	btcl	$3, (%eax)
	btrl	%eax, (%ecx)
	btsl	$7, %edx
	cmpxchgl	%eax, (%ecx)
	xaddl	%eax, %ecx
	cmpxchg8b	(%edi)
	sgdt	(%eax)
	sidt	(%eax)
	lgdt	(%eax)
	lidt	(%eax)
	smsw	%ax
	lmsw	%ax
	invlpg	(%eax)
	fxsave	(%eax)
	fxrstor	(%eax)
	ldmxcsr	(%eax)
	stmxcsr	(%eax)
	clflush	(%eax)
	prefetchnta	(%eax)
	prefetcht0	(%eax)
	prefetchw	(%eax)
	movnti	%eax, (%ecx)
	popcntl	%eax, %ecx

	movd	%eax, %mm0
	movd	%mm0, %eax
	movq	%mm0, %mm1
	movq	%mm1, (%eax)
	packsswb	%mm0, %mm1
	paddb	%mm0, %mm1
	pcmpeqw	%mm0, %mm1
	pmaddwd	%mm0, %mm1
	psllw	%mm0, %mm1
	psllw	$3, %mm1
	pshufw	$0x1b, %mm0, %mm1
	pinsrw	$2, %eax, %mm1
	pextrw	$2, %mm1, %eax
	pmovmskb	%mm1, %eax
	maskmovq	%mm0, %mm1
	movntq	%mm0, (%eax)
	pfadd	%mm0, %mm1
	pswapd	%mm0, %mm1

	movups	(%eax), %xmm0
	movups	%xmm0, (%eax)
	movss	%xmm0, %xmm1
	movlps	(%eax), %xmm0
	movhps	%xmm0, (%eax)
	movhlps	%xmm0, %xmm1
	movlhps	%xmm0, %xmm1
	unpcklps	%xmm0, %xmm1
	sqrtps	%xmm0, %xmm1
	rsqrtss	%xmm0, %xmm1
	rcpps	%xmm0, %xmm1
	andnps	%xmm0, %xmm1
	addss	%xmm0, %xmm1
	cmpps	$3, %xmm0, %xmm1
	shufps	$3, %xmm0, %xmm1
	ucomiss	%xmm0, %xmm1
	movmskps	%xmm0, %eax
	cvtsi2ss	%eax, %xmm0
	cvttss2si	%xmm0, %eax
	movntps	%xmm0, (%eax)

	movupd	(%eax), %xmm0
	movapd	%xmm0, %xmm1
	movsd	%xmm0, %xmm1
	movlpd	(%eax), %xmm0
	movhpd	%xmm0, (%eax)
	unpckhpd	%xmm0, %xmm1
	sqrtsd	%xmm0, %xmm1
	andpd	%xmm0, %xmm1
	addpd	%xmm0, %xmm1
	cmpsd	$2, %xmm0, %xmm1
	shufpd	$1, %xmm0, %xmm1
	ucomisd	%xmm0, %xmm1
	movmskpd	%xmm0, %eax
	cvtsi2sd	%eax, %xmm0
	cvttsd2si	%xmm0, %eax
	cvtps2pd	%xmm0, %xmm1
	cvtpd2ps	%xmm0, %xmm1
	cvttps2dq	%xmm0, %xmm1
	movntpd	%xmm0, (%eax)
	movd	%eax, %xmm0
	movd	%xmm0, %eax
	movq	%xmm0, %xmm1
	movq	%xmm0, (%eax)
	movdqa	%xmm0, %xmm1
	movdqu	%xmm0, (%eax)
	pshufd	$3, %xmm0, %xmm1
	punpcklqdq	%xmm0, %xmm1
	packsswb	%xmm0, %xmm1
	paddq	%xmm0, %xmm1
	psllq	$3, %xmm1
	pinsrw	$2, %eax, %xmm1
	pextrw	$2, %xmm1, %eax
	pmovmskb	%xmm1, %eax
	movntdq	%xmm0, (%eax)
	maskmovdqu	%xmm0, %xmm1

	addsubpd	%xmm0, %xmm1
	haddps	%xmm0, %xmm1
	lddqu	(%eax), %xmm0
	movddup	%xmm0, %xmm1
	movsldup	%xmm0, %xmm1
	pshufb	%xmm0, %xmm1
	phaddw	%mm0, %mm1
	pmaddubsw	%xmm0, %xmm1
	pabsd	%xmm0, %xmm1
	palignr	$3, %xmm0, %xmm1

	ptest	%xmm0, %xmm1
	pblendvb	%xmm0, %xmm1
	pmovsxbw	%xmm0, %xmm1
	pmuldq	%xmm0, %xmm1
	movntdqa	(%eax), %xmm0
	pmovzxdq	%xmm0, %xmm1
	pminud	%xmm0, %xmm1
	pmulld	%xmm0, %xmm1
	phminposuw	%xmm0, %xmm1
	roundps	$3, %xmm0, %xmm1
	blendpd	$1, %xmm0, %xmm1
	dpps	$0xff, %xmm0, %xmm1
	pextrb	$2, %xmm1, %eax
	pinsrd	$2, %eax, %xmm1
	insertps	$3, %xmm0, %xmm1
	pcmpgtq	%xmm0, %xmm1
	pcmpestri	$3, %xmm0, %xmm1
