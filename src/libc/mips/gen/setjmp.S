/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * C library -- setjmp, longjmp
 *
 *	longjmp(a,v)
 * will generate a "return(v)" from
 * the last call to
 *	setjmp(a)
 * by restoring registers from the stack,
 * and a struct sigcontext, see <signal.h>
 *
 * From: Chris Dearman <cjwdearman@gmail.com>
 */
		.set	noreorder

		.type	setjmp, @function
setjmp:		.globl 	setjmp

		subu	$sp, 24
		sw	$ra, 20($sp)
		sw	$a0, 24($sp)

		/* Get the signal mask. */
		addu	$a2, $a0, 13*4		# &omask
		li	$a0, 1			# SIG_BLOCK
		jal	sigprocmask		# get current signal mask
		move	$a1, $0			# BDS: NULL

		lw	$a0, 24($sp)
		lw	$ra, 20($sp)
		addu	$sp, 24

		li	$t0, 1
		sw	$t0, (12 * 4)($a0)

		sw	$s0, (0 * 4) ($a0)	# save register variables s0-s8
		sw	$s1, (1 * 4) ($a0)
		sw	$s2, (2 * 4) ($a0)
		sw	$s3, (3 * 4) ($a0)
		sw	$s4, (4 * 4) ($a0)
		sw	$s5, (5 * 4) ($a0)
		sw	$s6, (6 * 4) ($a0)
		sw	$s7, (7 * 4) ($a0)
		sw	$s8, (8 * 4) ($a0)	# frame pointer
		sw	$ra, (9 * 4) ($a0)	# return address
		sw	$gp, (10 * 4) ($a0)	# global data pointer
		sw	$sp, (11 * 4) ($a0)	# stack pointer
#ifdef __mips_hard_float
		swc1	$f0,  (14 * 4) ($a0)
		swc1	$f1,  (15 * 4) ($a0)
		swc1	$f2,  (16 * 4) ($a0)
		swc1	$f3,  (17 * 4) ($a0)
		swc1	$f4,  (18 * 4) ($a0)
		swc1	$f5,  (19 * 4) ($a0)
		swc1	$f6,  (20 * 4) ($a0)
		swc1	$f7,  (21 * 4) ($a0)
		swc1	$f8,  (22 * 4) ($a0)
		swc1	$f9,  (23 * 4) ($a0)
		swc1	$f10, (24 * 4) ($a0)
		swc1	$f11, (25 * 4) ($a0)
		swc1	$f12, (26 * 4) ($a0)
		swc1	$f13, (27 * 4) ($a0)
		swc1	$f14, (28 * 4) ($a0)
		swc1	$f15, (29 * 4) ($a0)
		swc1	$f16, (30 * 4) ($a0)
		swc1	$f17, (31 * 4) ($a0)
		swc1	$f18, (32 * 4) ($a0)
		swc1	$f19, (33 * 4) ($a0)
		swc1	$f20, (34 * 4) ($a0)
		swc1	$f21, (35 * 4) ($a0)
		swc1	$f22, (36 * 4) ($a0)
		swc1	$f23, (37 * 4) ($a0)
		swc1	$f24, (38 * 4) ($a0)
		swc1	$f25, (39 * 4) ($a0)
		swc1	$f26, (40 * 4) ($a0)
		swc1	$f27, (41 * 4) ($a0)
		swc1	$f28, (42 * 4) ($a0)
		swc1	$f29, (43 * 4) ($a0)
		swc1	$f30, (44 * 4) ($a0)
		swc1	$f31, (45 * 4) ($a0)
		cfc1	$t0, $31
		sw	$t0,  (46 * 4) ($a0)
#endif

		j	$ra
		move	$v0, $zero		# BDS: return a zero for the setjmp call


		.type	longjmp, @function
longjmp:	.globl	longjmp

		subu	$sp, 24
		sw	$ra, 20($sp)
		sw	$a0, 24($sp)
		sw	$a1, 28($sp)

		/* Restore signal mask. */
		addu	$a1, $a0, 13*4		# &omask
		move	$a2, $0			# NULL
		jal	sigprocmask		# set current signal mask
		li	$a0, 3			# BDS: SIG_SETMASK

		lw	$a1, 28($sp)
		lw	$a0, 24($sp)
		lw	$ra, 20($sp)
		addu	$sp, 24

#ifdef __mips_hard_float
		lw	$t0,  (46 * 4) ($a0)
		ctc1	$t0, $31
		lwc1	$f0,  (14 * 4) ($a0)
		lwc1	$f1,  (15 * 4) ($a0)
		lwc1	$f2,  (16 * 4) ($a0)
		lwc1	$f3,  (17 * 4) ($a0)
		lwc1	$f4,  (18 * 4) ($a0)
		lwc1	$f5,  (19 * 4) ($a0)
		lwc1	$f6,  (20 * 4) ($a0)
		lwc1	$f7,  (21 * 4) ($a0)
		lwc1	$f8,  (22 * 4) ($a0)
		lwc1	$f9,  (23 * 4) ($a0)
		lwc1	$f10, (24 * 4) ($a0)
		lwc1	$f11, (25 * 4) ($a0)
		lwc1	$f12, (26 * 4) ($a0)
		lwc1	$f13, (27 * 4) ($a0)
		lwc1	$f14, (28 * 4) ($a0)
		lwc1	$f15, (29 * 4) ($a0)
		lwc1	$f16, (30 * 4) ($a0)
		lwc1	$f17, (31 * 4) ($a0)
		lwc1	$f18, (32 * 4) ($a0)
		lwc1	$f19, (33 * 4) ($a0)
		lwc1	$f20, (34 * 4) ($a0)
		lwc1	$f21, (35 * 4) ($a0)
		lwc1	$f22, (36 * 4) ($a0)
		lwc1	$f23, (37 * 4) ($a0)
		lwc1	$f24, (38 * 4) ($a0)
		lwc1	$f25, (39 * 4) ($a0)
		lwc1	$f26, (40 * 4) ($a0)
		lwc1	$f27, (41 * 4) ($a0)
		lwc1	$f28, (42 * 4) ($a0)
		lwc1	$f29, (43 * 4) ($a0)
		lwc1	$f30, (44 * 4) ($a0)
		lwc1	$f31, (45 * 4) ($a0)
#endif
		lw	$s0, (0 * 4) ($a0)	# restore register variables s0-s8
		lw	$s1, (1 * 4) ($a0)
		lw	$s2, (2 * 4) ($a0)
		lw	$s3, (3 * 4) ($a0)
		lw	$s4, (4 * 4) ($a0)
		lw	$s5, (5 * 4) ($a0)
		lw	$s6, (6 * 4) ($a0)
		lw	$s7, (7 * 4) ($a0)
		lw	$s8, (8 * 4) ($a0)	# frame pointer
		lw	$ra, (9 * 4) ($a0)	# return address
		lw	$gp, (10 * 4) ($a0)	# global data pointer
		lw	$sp, (11 * 4) ($a0)	# stack pointer

		j	$ra
		move	$v0, $a1		# BDS: return value from longjmp
