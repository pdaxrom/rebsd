	.text
simd_full_start:
	# MMX integer rows, including register and immediate shift forms.
	packsswb %mm0, %mm1
	packssdw %mm0, %mm1
	packuswb %mm0, %mm1
	paddb %mm0, %mm1
	paddw %mm0, %mm1
	paddd %mm0, %mm1
	paddsb %mm0, %mm1
	paddsw %mm0, %mm1
	paddusb %mm0, %mm1
	paddusw %mm0, %mm1
	pand %mm0, %mm1
	pandn %mm0, %mm1
	pcmpeqb %mm0, %mm1
	pcmpeqw %mm0, %mm1
	pcmpeqd %mm0, %mm1
	pcmpgtb %mm0, %mm1
	pcmpgtw %mm0, %mm1
	pcmpgtd %mm0, %mm1
	pmaddwd %mm0, %mm1
	pmulhw %mm0, %mm1
	pmullw %mm0, %mm1
	por %mm0, %mm1
	psubb %mm0, %mm1
	psubw %mm0, %mm1
	psubd %mm0, %mm1
	psubsb %mm0, %mm1
	psubsw %mm0, %mm1
	psubusb %mm0, %mm1
	psubusw %mm0, %mm1
	punpckhbw %mm0, %mm1
	punpckhwd %mm0, %mm1
	punpckhdq %mm0, %mm1
	punpcklbw %mm0, %mm1
	punpcklwd %mm0, %mm1
	punpckldq %mm0, %mm1
	pxor %mm0, %mm1
	psllw %mm0, %mm1
	pslld %mm0, %mm1
	psllq %mm0, %mm1
	psraw %mm0, %mm1
	psrad %mm0, %mm1
	psrlw %mm0, %mm1
	psrld %mm0, %mm1
	psrlq %mm0, %mm1
	psllw $3, %mm1
	pslld $3, %mm1
	psllq $3, %mm1
	psraw $3, %mm1
	psrad $3, %mm1
	psrlw $3, %mm1
	psrld $3, %mm1
	psrlq $3, %mm1
	pshufw $0x1b, %mm0, %mm1
	pavgb %mm0, %mm1
	pavgw %mm0, %mm1
	pmaxsw %mm0, %mm1
	pmaxub %mm0, %mm1
	pminsw %mm0, %mm1
	pminub %mm0, %mm1
	pmulhuw %mm0, %mm1
	psadbw %mm0, %mm1
	paddq %mm0, %mm1
	psubq %mm0, %mm1
	pmuludq %mm0, %mm1
	movd %eax, %mm0
	movd %mm0, %eax
	movq %mm0, %mm1
	movq %mm0, (%eax)
	pinsrw $2, %eax, %mm1
	pextrw $2, %mm1, %eax
	pmovmskb %mm1, %eax
	movntq %mm0, (%eax)
	maskmovq %mm0, %mm1
	pi2fw %mm0, %mm1
	pf2iw %mm0, %mm1
	pfnacc %mm0, %mm1
	pfcmpge %mm0, %mm1
	pfrcp %mm0, %mm1
	pfsub %mm0, %mm1
	pfadd %mm0, %mm1
	pfcmpgt %mm0, %mm1
	pfrcpit1 %mm0, %mm1
	pfsubr %mm0, %mm1
	pfcmpeq %mm0, %mm1
	pfrcpit2 %mm0, %mm1

	# SSE scalar/packed single rows and both directions of move rows.
	movups (%eax), %xmm0
	movups %xmm0, (%eax)
	movaps (%eax), %xmm0
	movaps %xmm0, (%eax)
	movss (%eax), %xmm0
	movss %xmm0, (%eax)
	movlps (%eax), %xmm0
	movlps %xmm0, (%eax)
	movhps (%eax), %xmm0
	movhps %xmm0, (%eax)
	movhlps %xmm0, %xmm1
	movlhps %xmm0, %xmm1
	unpcklps %xmm0, %xmm1
	unpckhps %xmm0, %xmm1
	sqrtps %xmm0, %xmm1
	sqrtss %xmm0, %xmm1
	rsqrtps %xmm0, %xmm1
	rsqrtss %xmm0, %xmm1
	rcpps %xmm0, %xmm1
	rcpss %xmm0, %xmm1
	andps %xmm0, %xmm1
	andnps %xmm0, %xmm1
	orps %xmm0, %xmm1
	xorps %xmm0, %xmm1
	addps %xmm0, %xmm1
	addss %xmm0, %xmm1
	mulps %xmm0, %xmm1
	mulss %xmm0, %xmm1
	subps %xmm0, %xmm1
	subss %xmm0, %xmm1
	minps %xmm0, %xmm1
	minss %xmm0, %xmm1
	divps %xmm0, %xmm1
	divss %xmm0, %xmm1
	maxps %xmm0, %xmm1
	maxss %xmm0, %xmm1
	cmpps $3, %xmm0, %xmm1
	cmpss $3, %xmm0, %xmm1
	shufps $3, %xmm0, %xmm1
	ucomiss %xmm0, %xmm1
	comiss %xmm0, %xmm1
	movmskps %xmm0, %eax
	cvtpi2ps %mm0, %xmm1
	cvtsi2ss %eax, %xmm1
	cvttps2pi %xmm0, %mm1
	cvttss2si %xmm0, %eax
	cvtps2pi %xmm0, %mm1
	cvtss2si %xmm0, %eax
	movntps %xmm0, (%eax)

	# SSE2 floating-point, conversions, and XMM integer rows.
	movupd (%eax), %xmm0
	movupd %xmm0, (%eax)
	movapd (%eax), %xmm0
	movapd %xmm0, (%eax)
	movsd (%eax), %xmm0
	movsd %xmm0, (%eax)
	movlpd (%eax), %xmm0
	movlpd %xmm0, (%eax)
	movhpd (%eax), %xmm0
	movhpd %xmm0, (%eax)
	unpcklpd %xmm0, %xmm1
	unpckhpd %xmm0, %xmm1
	sqrtpd %xmm0, %xmm1
	sqrtsd %xmm0, %xmm1
	andpd %xmm0, %xmm1
	andnpd %xmm0, %xmm1
	orpd %xmm0, %xmm1
	xorpd %xmm0, %xmm1
	addpd %xmm0, %xmm1
	addsd %xmm0, %xmm1
	mulpd %xmm0, %xmm1
	mulsd %xmm0, %xmm1
	subpd %xmm0, %xmm1
	subsd %xmm0, %xmm1
	minpd %xmm0, %xmm1
	minsd %xmm0, %xmm1
	divpd %xmm0, %xmm1
	divsd %xmm0, %xmm1
	maxpd %xmm0, %xmm1
	maxsd %xmm0, %xmm1
	cmppd $3, %xmm0, %xmm1
	cmpsd $3, %xmm0, %xmm1
	shufpd $3, %xmm0, %xmm1
	ucomisd %xmm0, %xmm1
	comisd %xmm0, %xmm1
	movmskpd %xmm0, %eax
	cvtpi2pd %mm0, %xmm1
	cvtsi2sd %eax, %xmm1
	cvttpd2pi %xmm0, %mm1
	cvttsd2si %xmm0, %eax
	cvtpd2pi %xmm0, %mm1
	cvtsd2si %xmm0, %eax
	cvtps2pd %xmm0, %xmm1
	cvtss2sd %xmm0, %xmm1
	cvtpd2ps %xmm0, %xmm1
	cvtsd2ss %xmm0, %xmm1
	cvtdq2ps %xmm0, %xmm1
	cvttps2dq %xmm0, %xmm1
	cvtps2dq %xmm0, %xmm1
	movntpd %xmm0, (%eax)
	movd %eax, %xmm0
	movd %xmm0, %eax
	movq %xmm0, %xmm1
	movq %xmm0, (%eax)
	movdqa (%eax), %xmm0
	movdqa %xmm0, (%eax)
	movdqu (%eax), %xmm0
	movdqu %xmm0, (%eax)
	pshufd $3, %xmm0, %xmm1
	pshufhw $3, %xmm0, %xmm1
	pshuflw $3, %xmm0, %xmm1
	punpcklqdq %xmm0, %xmm1
	punpckhqdq %xmm0, %xmm1
	packsswb %xmm0, %xmm1
	packssdw %xmm0, %xmm1
	packuswb %xmm0, %xmm1
	paddb %xmm0, %xmm1
	paddw %xmm0, %xmm1
	paddd %xmm0, %xmm1
	paddq %xmm0, %xmm1
	paddsb %xmm0, %xmm1
	paddsw %xmm0, %xmm1
	paddusb %xmm0, %xmm1
	paddusw %xmm0, %xmm1
	pand %xmm0, %xmm1
	pandn %xmm0, %xmm1
	pcmpeqb %xmm0, %xmm1
	pcmpeqw %xmm0, %xmm1
	pcmpeqd %xmm0, %xmm1
	pcmpgtb %xmm0, %xmm1
	pcmpgtw %xmm0, %xmm1
	pcmpgtd %xmm0, %xmm1
	pmaddwd %xmm0, %xmm1
	pmulhuw %xmm0, %xmm1
	pmulhw %xmm0, %xmm1
	pmullw %xmm0, %xmm1
	pmuludq %xmm0, %xmm1
	por %xmm0, %xmm1
	psadbw %xmm0, %xmm1
	psubb %xmm0, %xmm1
	psubw %xmm0, %xmm1
	psubd %xmm0, %xmm1
	psubq %xmm0, %xmm1
	psubsb %xmm0, %xmm1
	psubsw %xmm0, %xmm1
	psubusb %xmm0, %xmm1
	psubusw %xmm0, %xmm1
	punpckhbw %xmm0, %xmm1
	punpckhwd %xmm0, %xmm1
	punpckhdq %xmm0, %xmm1
	punpcklbw %xmm0, %xmm1
	punpcklwd %xmm0, %xmm1
	punpckldq %xmm0, %xmm1
	pxor %xmm0, %xmm1
	psllw %xmm0, %xmm1
	pslld %xmm0, %xmm1
	psllq %xmm0, %xmm1
	psraw %xmm0, %xmm1
	psrad %xmm0, %xmm1
	psrlw %xmm0, %xmm1
	psrld %xmm0, %xmm1
	psrlq %xmm0, %xmm1
	psllw $3, %xmm1
	pslld $3, %xmm1
	psllq $3, %xmm1
	psraw $3, %xmm1
	psrad $3, %xmm1
	psrlw $3, %xmm1
	psrld $3, %xmm1
	psrlq $3, %xmm1
	pslldq $3, %xmm1
	psrldq $3, %xmm1
	pinsrw $2, %eax, %xmm1
	pextrw $2, %xmm1, %eax
	pmovmskb %xmm1, %eax
	movntdq %xmm0, (%eax)
	maskmovdqu %xmm0, %xmm1
	cvtpd2dq %xmm0, %xmm1
	cvttpd2dq %xmm0, %xmm1
	cvtdq2pd %xmm0, %xmm1
	movq2dq %mm0, %xmm1
	movdq2q %xmm0, %mm1

	# SSE3 and SSSE3 MMX/XMM rows.
	addsubpd %xmm0, %xmm1
	addsubps %xmm0, %xmm1
	haddpd %xmm0, %xmm1
	haddps %xmm0, %xmm1
	hsubpd %xmm0, %xmm1
	hsubps %xmm0, %xmm1
	lddqu (%eax), %xmm1
	movddup %xmm0, %xmm1
	movshdup %xmm0, %xmm1
	movsldup %xmm0, %xmm1
	pshufb %mm0, %mm1
	phaddw %mm0, %mm1
	phaddd %mm0, %mm1
	phaddsw %mm0, %mm1
	pmaddubsw %mm0, %mm1
	phsubw %mm0, %mm1
	phsubd %mm0, %mm1
	phsubsw %mm0, %mm1
	psignb %mm0, %mm1
	psignw %mm0, %mm1
	psignd %mm0, %mm1
	pmulhrsw %mm0, %mm1
	pabsb %mm0, %mm1
	pabsw %mm0, %mm1
	pabsd %mm0, %mm1
	palignr $3, %mm0, %mm1
	pshufb %xmm0, %xmm1
	phaddw %xmm0, %xmm1
	phaddd %xmm0, %xmm1
	phaddsw %xmm0, %xmm1
	pmaddubsw %xmm0, %xmm1
	phsubw %xmm0, %xmm1
	phsubd %xmm0, %xmm1
	phsubsw %xmm0, %xmm1
	psignb %xmm0, %xmm1
	psignw %xmm0, %xmm1
	psignd %xmm0, %xmm1
	pmulhrsw %xmm0, %xmm1
	pabsb %xmm0, %xmm1
	pabsw %xmm0, %xmm1
	pabsd %xmm0, %xmm1
	palignr $3, %xmm0, %xmm1

	# SSE4.1, SSE4.2, and AMD SSE4a rows.
	ptest %xmm0, %xmm1
	pblendvb %xmm0, %xmm1
	blendvps %xmm0, %xmm1
	blendvpd %xmm0, %xmm1
	pmovsxbw %xmm0, %xmm1
	pmovsxbd %xmm0, %xmm1
	pmovsxbq %xmm0, %xmm1
	pmovsxwd %xmm0, %xmm1
	pmovsxwq %xmm0, %xmm1
	pmovsxdq %xmm0, %xmm1
	pmuldq %xmm0, %xmm1
	pcmpeqq %xmm0, %xmm1
	movntdqa (%eax), %xmm1
	packusdw %xmm0, %xmm1
	pmovzxbw %xmm0, %xmm1
	pmovzxbd %xmm0, %xmm1
	pmovzxbq %xmm0, %xmm1
	pmovzxwd %xmm0, %xmm1
	pmovzxwq %xmm0, %xmm1
	pmovzxdq %xmm0, %xmm1
	pminsb %xmm0, %xmm1
	pminsd %xmm0, %xmm1
	pminuw %xmm0, %xmm1
	pminud %xmm0, %xmm1
	pmaxsb %xmm0, %xmm1
	pmaxsd %xmm0, %xmm1
	pmaxuw %xmm0, %xmm1
	pmaxud %xmm0, %xmm1
	pmulld %xmm0, %xmm1
	phminposuw %xmm0, %xmm1
	roundps $3, %xmm0, %xmm1
	roundpd $3, %xmm0, %xmm1
	roundss $3, %xmm0, %xmm1
	roundsd $3, %xmm0, %xmm1
	blendps $3, %xmm0, %xmm1
	blendpd $3, %xmm0, %xmm1
	pblendw $3, %xmm0, %xmm1
	dpps $0xff, %xmm0, %xmm1
	dppd $0xff, %xmm0, %xmm1
	mpsadbw $3, %xmm0, %xmm1
	pextrb $2, %xmm1, %eax
	pextrw $2, %xmm1, %eax
	pextrd $2, %xmm1, %eax
	pinsrb $2, %eax, %xmm1
	insertps $3, %xmm0, %xmm1
	pinsrd $2, %eax, %xmm1
	extractps $2, %xmm1, %eax
	pcmpgtq %xmm0, %xmm1
	pcmpestrm $3, %xmm0, %xmm1
	pcmpestri $3, %xmm0, %xmm1
	pcmpistrm $3, %xmm0, %xmm1
	pcmpistri $3, %xmm0, %xmm1
	movntsd %xmm0, (%eax)
	movntss %xmm0, (%eax)
