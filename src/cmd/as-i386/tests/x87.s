	.text
x87_start:
	f2xm1
	fabs
	fchs
	fcos
	fdecstp
	fincstp
	fld1
	fldl2t
	fldl2e
	fldpi
	fldlg2
	fldln2
	fldz
	fnclex
	fclex
	fninit
	finit
	fnop
	feni
	fndisi
	fnsetpm
	frstpm
	fcomps	(%eax)
	fdivrs	(%eax)
	fiadd	(%eax)
	ficomp	(%eax)
	fidiv	(%eax)
	fidivl	(%eax)
	fist	(%eax)
	fisttp	(%eax)
	fisubrs	(%eax)
	fsubrs	(%eax)
	fpatan
	fprem
	fprem1
	fptan
	frndint
	fscale
	fsin
	fsincos
	fsqrt
	ftst
	fucompp
	fxam
	fxtract
	fyl2x
	fyl2xp1
	fcompp

	fld	%st(3)
	fxch	%st(3)
	fst	%st(3)
	fstp	%st(3)
	fcom	%st(3)
	fcomp	%st(3)
	fucom	%st(3)
	fucomp	%st(3)
	ffree	%st(3)
	ffreep	%st(3)
	fnstsw	%ax
	fstsw	%ax
	fcmovb	%st(3), %st
	fcmovne	%st(3), %st
	fcomi	%st(3), %st
	fucomi	%st(3), %st
	fcomip	%st(3), %st
	fucomip	%st(3), %st
	fadd	%st(3), %st
	fadd	%st, %st(3)
	fmul	%st(3), %st
	fmul	%st, %st(3)
	fsub	%st(3), %st
	fsub	%st, %st(3)
	fsubr	%st(3), %st
	fsubr	%st, %st(3)
	fdiv	%st(3), %st
	fdiv	%st, %st(3)
	fdivr	%st(3), %st
	fdivr	%st, %st(3)
	faddp	%st, %st(3)
	fmulp	%st, %st(3)
	fsubp	%st, %st(3)
	fsubrp	%st, %st(3)
	fdivp	%st, %st(3)
	fdivrp	%st, %st(3)

	flds	(%eax)
	fldl	(%eax)
	fldt	(%eax)
	fsts	(%eax)
	fstl	(%eax)
	fstps	(%eax)
	fstpl	(%eax)
	fstpt	(%eax)
	filds	(%eax)
	fildl	(%eax)
	fildll	(%eax)
	fists	(%eax)
	fistl	(%eax)
	fistps	(%eax)
	fistpl	(%eax)
	fistpll	(%eax)
	fisttps	(%eax)
	fisttpl	(%eax)
	fisttpll	(%eax)
	fbld	(%eax)
	fbstp	(%eax)
	fadds	(%eax)
	faddl	(%eax)
	fiadds	(%eax)
	fiaddl	(%eax)
	fimull	(%eax)
	fcoms	(%eax)
	ficoml	(%eax)
	fcompl	(%eax)
	ficoms	(%eax)
	ficompl	(%eax)
	fsubs	(%eax)
	fsubrl	(%eax)
	fisubs	(%eax)
	fisubl	(%eax)
	fisubrl	(%eax)
	fdivs	(%eax)
	fdivrl	(%eax)
	fidivs	(%eax)
	fidivrl	(%eax)
	fldcw	(%eax)
	fnstcw	(%eax)
	fstcw	(%eax)
	fnstsw	(%eax)
	fstsw	(%eax)
	fnstenv	(%eax)
	fnstenvs	(%eax)
	fnstenvl	(%eax)
	fstenv	(%eax)
	fstenvs	(%eax)
	fstenvl	(%eax)
	fldenv	(%eax)
	fldenvs	(%eax)
	fldenvl	(%eax)
	fnsave	(%eax)
	fnsaves	(%eax)
	fnsavel	(%eax)
	fsave	(%eax)
	fsaves	(%eax)
	fsavel	(%eax)
	frstor	(%eax)
	frstors	(%eax)
	frstorl	(%eax)
