/*	$Id$	*/

/*-
 * Copyright (c) 2011 Joerg Sonnenberger <joerg@NetBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright(C) Caldera International Inc. 2001-2002. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code and documentation must retain the above
 * copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditionsand the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 * 	This product includes software developed or owned by Caldera
 *	International, Inc.
 * Neither the name of Caldera International, Inc. nor the names of other
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
 * INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OFLIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Front-end to the C compiler.
 *
 * Brief description of its syntax:
 * - Files that end with .c are passed via cpp->ccom->as->ld
 * - Files that end with .i are passed via ccom->as->ld
 * - Files that end with .S are passed via cpp->as->ld
 * - Files that end with .s are passed via as->ld
 * - Files that end with .o are passed directly to ld
 * - Multiple files may be given on the command line.
 * - Unrecognized options are all sent directly to ld.
 * -c or -S cannot be combined with -o if multiple files are given.
 *
 * This file should be rewritten readable.
 */
#include "config.h"

#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <time.h>

#ifdef  _WIN32
#include <windows.h>
#include <process.h>
#include <io.h>
#define F_OK	0x00
#define R_OK	0x04
#define W_OK	0x02
#define X_OK	R_OK
#endif

#include "compat.h"

#include "macdefs.h"

#include "xalloc.h"
#include "strlist.h"

#include "ccconfig.h"
/* C command */

#define CC_DRIVER
#include "softfloat.h"	/* for CPP floating point macros */

#define	MKS(x) _MKS(x)
#define _MKS(x) #x

/* default program names in pcc */
/* May be overridden if cross-compiler is generated */
#ifndef	CXXPROGNAME		/* name as C++ front end */
#define	CXXPROGNAME	"c++"
#endif
#ifndef CPPROGNAME
#define	CPPROGNAME	"cpp"	/* name as CPP front end */
#endif
#ifndef PREPROCESSOR
#define	PREPROCESSOR	"cpp"	/* "real" preprocessor name */
#endif
#ifndef COMPILER
#define COMPILER	"ccom"
#endif
#ifndef CXXCOMPILER
#define CXXCOMPILER	"cxxcom"
#endif
#ifndef ASSEMBLER
#define ASSEMBLER	"as"
#endif
#ifndef LINKER
#define LINKER		"ld"
#endif

#ifndef CC0
#define CC0	"cc0"
#endif
#ifndef CC1
#define CC1	"cc1"
#endif
#ifndef CC2
#define CC2	"cc2"
#endif
#ifndef CXX0
#define CXX0	"cxx0"
#endif
#ifndef CXX1
#define CXX1	"cxx1"
#endif

char	*passp = PREPROCESSOR;
char	*pass0 = COMPILER;
char	*passxx0 = CXXCOMPILER;
char	*as = ASSEMBLER;
char	*ld = LINKER;
char	*sysroot = "/", *isysroot;


/* crt files using pcc default names */
#ifndef CRTBEGIN_S
#define	CRTBEGIN_S	"crtbeginS.o"
#endif
#ifndef CRTEND_S
#define	CRTEND_S	"crtendS.o"
#endif
#ifndef CRTBEGIN_T
#define	CRTBEGIN_T	"crtbeginT.o"
#endif
#ifndef CRTEND_T
#define	CRTEND_T	"crtendT.o"
#endif
#ifndef CRTBEGIN
#define	CRTBEGIN	"crtbegin.o"
#endif
#ifndef CRTEND
#define	CRTEND		"crtend.o"
#endif
#ifndef CRTI
#define	CRTI		"crti.o"
#endif
#ifndef CRTN
#define	CRTN		"crtn.o"
#endif
#ifndef CRT0
#define	CRT0		"crt0.o"
#endif
#ifndef GCRT0
#define	GCRT0		"gcrt0.o"
#endif
#ifndef RCRT0
#define	RCRT0		"rcrt0.o"
#endif

/* preprocessor stuff */
#ifndef STDINC
#define	STDINC	  	"/usr/include/"
#endif
#define	STDINCS		{ STDINC, 0 }


char *cppadd[] = CPPADD;
char *cppmdadd[] = CPPMDADD;

/* Default libraries and search paths */
#ifndef PCCLIBDIR	/* set by autoconf */
#define PCCLIBDIR	NULL
#endif
#ifndef LIBDIR
#define LIBDIR		"/usr/lib/"
#endif
#ifndef DEFLIBDIRS	/* default library search paths */
#ifdef MULTIARCH_PATH
#define DEFLIBDIRS	{ LIBDIR, LIBDIR MULTIARCH_PATH "/", 0 }
#else
#define DEFLIBDIRS	{ LIBDIR, 0 }
#endif
#endif
#ifndef DEFLIBS		/* default libraries included */
#define	DEFLIBS		{ "-lpcc", "-lc", "-lpcc", 0 }
#endif
#ifndef DEFPROFLIBS	/* default profiling libraries */
#define	DEFPROFLIBS	{ "-lpcc", "-lc_p", "-lpcc", 0 }
#endif
#ifndef DEFCXXLIBS	/* default c++ libraries */
#define	DEFCXXLIBS	{ "-lp++", "-lpcc", "-lc", "-lpcc", 0 }
#endif
#ifndef STARTLABEL
#define STARTLABEL	"__start"
#endif
#ifndef DYNLINKARG
#define DYNLINKARG	"-dynamic-linker"
#endif
#ifndef DYNLINKLIB
#define DYNLINKLIB	NULL
#endif

char *stdincs[] = STDINCS;
char *dynlinkarg = DYNLINKARG;
char *dynlinklib = DYNLINKLIB;
char *pcclibdir = PCCLIBDIR;
char *deflibdirs[] = DEFLIBDIRS;
char *deflibs[] = DEFLIBS;
char *defproflibs[] = DEFPROFLIBS;
char *defcxxlibs[] = DEFCXXLIBS;

char	*outfile, *MFfile, *fname;
static char **lav;
static int lac;
static char *find_file(const char *file, struct strlist *path, int mode);
static int preprocess_input(char *input, char *output, int dodep);
static int compile_input(char *input, char *output);
static int assemble_input(char *input, char *output);
static int run_linker(void);
static int strlist_exec(struct strlist *l);
static char *select_linker(char *);
#if defined(mach_mips) || defined(mach_mips64)
static int mips_postprocess_asm(char *);
static int mips_prepare_asm_input(char *, char **);
#endif

char *cat(const char *, const char *);
static char *cat_sysroot(const char *, const char *);
char *setsuf(char *, char);
int cxxsuf(char *);
int getsuf(char *);
char *getsufp(char *s);
int main(int, char *[]);
void errorx(int, char *, ...);
int cunlink(char *);
void exandrm(char *);
void dexit(int);
void idexit(int);
char *gettmp(void);
void oerror(char *);
char *argnxt(char *, char *);
char *nxtopt(char *o);
void setup_cpp_flags(void);
void setup_ccom_flags(void);
void setup_as_flags(void);
void setup_ld_flags(void);
static void expand_sysroot(void);
#ifdef  _WIN32
char *win32pathsubst(char *);
char *win32commandline(struct strlist *l);
#endif
int	sspflag;
int	freestanding;
int	Sflag;
int	cflag;
int	gflag;
int	rflag;
int	vflag;
int	noexec;	/* -### */
int	tflag;
int	Eflag;
int	Oflag;
int	kflag;	/* generate PIC/pic code */
#define F_PIC	1
#define F_pic	2
int	Mflag, needM, MDflag, MMDflag;	/* dependencies only */
int	pgflag;
int	pieflag;
int	omit_frame_pointer;
int	Xflag;
int	nostartfiles, Bstatic, shared;
int	nostdinc, nostdlib;
int	pthreads;
int	xgnu89, xgnu99, c89defs, c99defs, c11defs;
int 	ascpp;
#ifdef CHAR_UNSIGNED
int	xuchar = 1;
#else
int	xuchar = 0;
#endif
int	cxxflag;
int	cppflag;
int	printprogname, printfilename, printsearchdirs;
enum { SC11, STRAD, SC89, SGNU89, SC99, SGNU99 } cstd;

#ifdef SOFTFLOAT
int	softfloat = 1;
#else
int	softfloat = 0;
#endif

#ifdef TARGET_BIG_ENDIAN
int	bigendian = 1;
#else
int	bigendian = 0;
#endif

#ifdef TARGET_GLOBALS
TARGET_GLOBALS
#endif

#define	match(a,b)	(strcmp(a,b) == 0)

/* handle gcc warning emulations */
struct Wflags {
	char *name;
	int flags;
#define	INWALL		1
#define	INWEXTRA	2
} Wflags[] = {
	{ "truncate", 0 },
	{ "strict-prototypes", 0 },
	{ "missing-prototypes", INWEXTRA },
	{ "implicit-int", INWALL },
	{ "implicit-function-declaration", INWALL },
	{ "shadow", INWEXTRA },
	{ "pointer-sign", INWALL },
	{ "sign-compare", INWEXTRA },
	{ "unknown-pragmas", INWALL },
	{ "unreachable-code", INWEXTRA },
	{ "deprecated-declarations", INWEXTRA },
	{ "attributes", 0 },
	{ "uninitialized", INWEXTRA },
	{ "return-type", INWALL },
	{ "return-mismatch", 0 },
	{ NULL, 0 },
};

#ifndef USHORT
/* copied from mip/manifest.h */
#define	USHORT		5
#define	INT		6
#define	UNSIGNED	7
#endif

/*
 * Wide char defines.
 */
#if WCHAR_TYPE == USHORT
#define	WCT "short unsigned int"
#define WCM "65535U"
#if WCHAR_SIZE != 2
#error WCHAR_TYPE vs. WCHAR_SIZE mismatch
#endif
#elif WCHAR_TYPE == INT
#define WCT "int"
#define WCM "2147483647"
#if WCHAR_SIZE != 4
#error WCHAR_TYPE vs. WCHAR_SIZE mismatch
#endif
#elif WCHAR_TYPE == UNSIGNED
#define WCT "unsigned int"
#define WCM "4294967295U"
#if WCHAR_SIZE != 4
#error WCHAR_TYPE vs. WCHAR_SIZE mismatch
#endif
#else
#error WCHAR_TYPE not defined or invalid
#endif

#ifdef GCC_COMPAT
#ifndef REGISTER_PREFIX
#define REGISTER_PREFIX ""
#endif
#ifndef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""
#endif
#endif

#ifndef PCC_WINT_TYPE
#define PCC_WINT_TYPE "unsigned int"
#endif

#ifndef PCC_SIZE_TYPE
#define PCC_SIZE_TYPE "unsigned long"
#endif

#ifndef PCC_PTRDIFF_TYPE
#define PCC_PTRDIFF_TYPE "long int"
#endif


struct strlist preprocessor_flags;
struct strlist depflags;
struct strlist incdirs;
struct strlist user_sysincdirs;
struct strlist includes;
struct strlist sysincdirs;
struct strlist dirafterdirs;
struct strlist crtdirs;
struct strlist libdirs;
struct strlist progdirs;
struct strlist early_linker_flags;
struct strlist middle_linker_flags;
struct strlist late_linker_flags;
struct strlist inputs;
struct strlist assembler_flags;
struct strlist temp_outputs;
struct strlist compiler_flags;

int
main(int argc, char *argv[])
{
	struct Wflags *Wf;
	struct string *s;
	char *t, *u, *argp;
	char *msuffix;
	int ninput, j;

	lav = argv;
	lac = argc;
	ninput = 0;

	strlist_init(&crtdirs);
	strlist_init(&libdirs);
	strlist_init(&progdirs);
	strlist_init(&preprocessor_flags);
	strlist_init(&incdirs);
	strlist_init(&user_sysincdirs);
	strlist_init(&includes);
	strlist_init(&sysincdirs);
	strlist_init(&dirafterdirs);
	strlist_init(&depflags);
	strlist_init(&early_linker_flags);
	strlist_init(&middle_linker_flags);
	strlist_init(&late_linker_flags);
	strlist_init(&inputs);
	strlist_init(&assembler_flags);
	strlist_init(&temp_outputs);
	strlist_init(&compiler_flags);

	if ((t = strrchr(argv[0], '/')))
		t++;
	else
		t = argv[0];

	if (match(t, CXXPROGNAME)) {
		cxxflag = 1;
	} else if (match(t, CPPROGNAME)) {
		Eflag = cppflag = 1;
	}

#ifdef PCC_EARLY_SETUP
	PCC_EARLY_SETUP
#endif

#ifdef _WIN32
	/* have to prefix path early.  -B may override */
	incdir = win32pathsubst(incdir);
	altincdir = win32pathsubst(altincdir);
	libdir = win32pathsubst(libdir);
#ifdef PCCINCDIR
	pccincdir = win32pathsubst(pccincdir);
	pxxincdir = win32pathsubst(pxxincdir);
#endif
#ifdef PCCLIBDIR
	pcclibdir = win32pathsubst(pcclibdir);
#endif
	passp = win32pathsubst(passp);
	pass0 = win32pathsubst(pass0);
#ifdef STARTFILES
	for (i = 0; startfiles[i] != NULL; i++)
		startfiles[i] = win32pathsubst(startfiles[i]);
	for (i = 0; endfiles[i] != NULL; i++)
		endfiles[i] = win32pathsubst(endfiles[i]);
#endif
#ifdef STARTFILES_T
	for (i = 0; startfiles_T[i] != NULL; i++)
		startfiles_T[i] = win32pathsubst(startfiles_T[i]);
	for (i = 0; endfiles_T[i] != NULL; i++)
		endfiles_T[i] = win32pathsubst(endfiles_T[i]);
#endif
#ifdef STARTFILES_S
	for (i = 0; startfiles_S[i] != NULL; i++)
		startfiles_S[i] = win32pathsubst(startfiles_S[i]);
	for (i = 0; endfiles_S[i] != NULL; i++)
		endfiles_S[i] = win32pathsubst(endfiles_S[i]);
#endif
#endif

	while (--lac) {
		++lav;
		argp = *lav;

#ifdef PCC_EARLY_ARG_CHECK
		PCC_EARLY_ARG_CHECK
#endif

		if (*argp != '-' || match(argp, "-")) {
			/* Check for duplicate .o files. */
			if (getsuf(argp) == 'o') {
				j = 0;
				STRLIST_FOREACH(s, &inputs)
					if (match(argp, s->value))
						j++;
				if (j)
					continue; /* skip it */
			}
			strlist_append(&inputs, argp);
			ninput++;
			continue;
		}

		switch (argp[1]) {
		default:
			oerror(argp);
			break;

		case '#':
			if (match(argp, "-###")) {
				printf("%s\n", VERSSTR);
				vflag++;
				noexec++;
			} else
				oerror(argp);
			break;

		case '-': /* double -'s */
			if (match(argp, "--version")) {
				printf("%s\n", VERSSTR);
				return 0;
			} else if (strncmp(argp, "--sysroot=", 10) == 0) {
				sysroot = argp + 10;
			} else if (strncmp(argp, "--sysroot", 9) == 0) {
				sysroot = nxtopt(argp);
			} else if (strcmp(argp, "--param") == 0) {
				/* NOTHING YET */;
				(void)nxtopt(0); /* ignore arg */
			} else
				oerror(argp);
			break;

		case 'a':	/* only -ansi switch for now */
			if (match(argp, "-ansi"))
				cstd = SC89;
			else
				oerror(argp);
			break;

		case 'B': /* other search paths for binaries */
			t = nxtopt("-B");
			strlist_append(&crtdirs, t);
			strlist_append(&libdirs, t);
			strlist_append(&progdirs, t);
			break;

		case 'C':
			if (match(argp, "-C") || match(argp, "-CC"))
				strlist_append(&preprocessor_flags, argp);
			else
				oerror(argp);
			break;

		case 'c':
			cflag++;
			break;

		case 'd': /* debug options */
			if (match(argp, "-dumpmachine")) {
 				/* Print target and immediately exit */
 				puts(TARGSTR);
 				exit(0);
 			}
 			if (match(argp, "-dumpversion")) {
 				/* Print claimed gcc level, immediately exit */
 				puts("4.3.1");
 				exit(0);
 			}
			for (t = &argp[2]; *t; t++) {
				if (*t == 'M')
					strlist_append(&preprocessor_flags, "-dM");

				/* ignore others */
			}
			break;

		case 'E':
			Eflag++;
			break;

		case 'f': /* GCC compatibility flags */
			u = &argp[2];
			j = 0;
			if (strncmp(u, "no-", 3) == 0)
				j = 1, u += 3;
			if (match(u, "PIC") || match(u, "pic")) {
				kflag = j ? 0 : *u == 'P' ? F_PIC : F_pic;
			} else if (match(u, "PIE") || match(u, "pie")) {
				kflag = j ? 0 : *u == 'P' ? F_PIC : F_pic;
			} else if (match(u, "freestanding")) {
				freestanding = j ? 0 : 1;
			} else if (match(u, "omit-frame-pointer")) {
				omit_frame_pointer = j ? 0 : 1;
			} else if (match(u, "signed-char")) {
				xuchar = j ? 1 : 0;
			} else if (match(u, "unsigned-char")) {
				xuchar = j ? 0 : 1;
			} else if (match(u, "stack-protector") ||
			    match(u, "stack-protector-all")) {
				sspflag = j ? 0 : 1;
			} else if (match(u, "use-ld=")) {
				/* ignore nonsense -fno-use-ld=* command */
				if (j)
					break;
				u += 7;
				ld = select_linker(u);
			}
			/* silently ignore the rest */
			break;

		case 'g': /* create debug output */
			if (argp[2] == '0')
				gflag = 0;
			else
				gflag++;
			break;


		case 'X':
			Xflag++;
			break;

		case 'D':
		case 'U':
			strlist_append(&preprocessor_flags, argp);
			if (argp[2] != 0)
				break;
			strlist_append(&preprocessor_flags, nxtopt(argp));
			break;

		case 'I': /* Add include dirs */
			strlist_append(&incdirs, nxtopt("-I"));
			break;

		case 'i':
			if (match(argp, "-isystem")) {
				strlist_append(&user_sysincdirs, nxtopt(0));
			} else if (match(argp, "-include")) {
				strlist_append(&includes, nxtopt(0));
			} else if (match(argp, "-isysroot")) {
				isysroot = nxtopt(0);
			} else if (strcmp(argp, "-idirafter") == 0) {
				strlist_append(&dirafterdirs, nxtopt(0));
			} else
				oerror(argp);
			break;

		case 'k': /* generate PIC code */
			kflag = argp[2] ? argp[2] - '0' : F_pic;
			break;

		case 'l':
		case 'L':
			if (argp[2] == 0)
				argp = cat(argp, nxtopt(0));
			strlist_append(&inputs, argp);
			break;

		case 'm': /* target-dependent options */
#ifdef PCC_HANDLE_MFLAG
			PCC_HANDLE_MFLAG
#endif
			if (strncmp(argp, "-march=", 6) == 0) {
				strlist_append(&compiler_flags, argp);
				break;
			}
#if defined(mach_arm) || defined(mach_mips) || defined(mach_mips64)
			if (match(argp, "-mbig-endian")) {
				bigendian = 1;
				strlist_append(&compiler_flags, argp);
				break;
			}
			if (match(argp, "-mlittle-endian")) {
				bigendian = 0;
				strlist_append(&compiler_flags, argp);
				break;
			}
			if (match(argp, "-msoft-float")) {
				softfloat = 1;
				strlist_append(&compiler_flags, argp);
				break;
			}
#endif
#if defined(mach_mips) || defined(mach_mips64)
			if (match(argp, "-mhard-float")) {
				softfloat = 0;
				strlist_append(&compiler_flags, argp);
				break;
			}
#endif
#if defined(os_sunos)
			/* Ignore -m64 and -m32 for now.
			 * TODO set up PCC as a multiarch compiler on Solaris.
			 */
			if (match(argp, "-m64") || match(argp, "-m32"))
				break;
#endif

			strlist_append(&middle_linker_flags, argp);
			if (argp[2] == 0) {
				t = nxtopt(0);
				strlist_append(&middle_linker_flags, t);
			}
			break;

		case 'n': /* handle -n flags */
			if (strcmp(argp, "-nostdinc") == 0)
				nostdinc++;
			else if (strcmp(argp, "-nostdlib") == 0) {
				nostdlib++;
				nostartfiles++;
			} else if (strcmp(argp, "-nostartfiles") == 0)
				nostartfiles = 1;
			else if (strcmp(argp, "-nodefaultlibs") == 0)
				nostdlib++;
			else if (strcmp(argp, "-nopie") == 0) {
				strlist_append(&middle_linker_flags, argp);
				pieflag = 0;
			} else
				oerror(argp);
			break;

		case 'p':
			if (strcmp(argp, "-pg") == 0 ||
			    strcmp(argp, "-p") == 0)
				pgflag++;
			else if (strcmp(argp, "-pthread") == 0)
				pthreads++;
			else if (strcmp(argp, "-pipe") == 0)
				/* NOTHING YET */;
			else if (strcmp(argp, "-pedantic") == 0)
				/* NOTHING YET */;
			else if ((t = argnxt(argp, "-print-prog-name="))) {
				fname = t;
				printprogname = 1;
			} else if ((t = argnxt(argp, "-print-file-name="))) {
				fname = t;
				printfilename = 1;
			} else if (match(argp, "-print-libgcc-file-name")) {
				fname = "libpcc.a";
				printfilename = 1;
			} else if (match(argp, "-print-search-dirs")) {
				printsearchdirs = 1;
			} else if (match(argp, "-pie")) {
				strlist_append(&middle_linker_flags, argp);
				pieflag = 1;
			} else
				oerror(argp);
			break;

		case 'R':
			if (argp[2] == 0)
				argp = cat(argp, nxtopt(0));
			strlist_append(&middle_linker_flags, argp);
			break;

		case 'r':
			if (match(argp, "-rdynamic")) {
				strlist_append(&middle_linker_flags,
				    "--export-dynamic");
			} else if (match(argp, "-r")) {
				rflag = 1;
			} else
				oerror(argp);
			break;

		case 'T':
			strlist_append(&inputs, argp);
			if (argp[2] == 0 ||
			    strcmp(argp, "-Ttext") == 0 ||
			    strcmp(argp, "-Tdata") == 0 ||
			    strcmp(argp, "-Tbss") == 0)
				strlist_append(&inputs, nxtopt(0));
			break;

		case 's':
			if (match(argp, "-shared")) {
				shared = 1;
			} else if (match(argp, "-static")) {
				Bstatic = 1;
			} else if (match(argp, "-symbolic")) {
				strlist_append(&middle_linker_flags,
				    "-Bsymbolic");
			} else if (strncmp(argp, "-std=", 5) == 0) {
				if (strcmp(&argp[5], "c11") == 0)
					cstd = SC11;
				else if (strcmp(&argp[5], "gnu99") == 0 ||
				    strcmp(&argp[5], "gnu9x") == 0)
					cstd = SGNU99;
				else if (strcmp(&argp[5], "c89") == 0)
					cstd = SC89;
				else if (strcmp(&argp[5], "gnu89") == 0)
					cstd = SGNU89;
				else if (strcmp(&argp[5], "c99") == 0)
					cstd = SC99;
				else
					oerror(argp);
			} else if (match(argp, "-s")) {
				strlist_append(&middle_linker_flags, argp);
		 	} else
				oerror(argp);
			break;

		case 'S':
			Sflag++;
			cflag++;
			break;

		case 't':
			tflag++;
			cstd = STRAD;
			break;

		case 'o':
			if (outfile)
				errorx(8, "too many -o");
			outfile = nxtopt("-o");
			break;

		case 'O':
			if (argp[2] == '\0')
				/* gcc does -O1, clang does -O2 */
				Oflag = 1;	/* do what gcc does */
			else if (argp[3] == '\0' &&
			    isdigit((unsigned char)argp[2]))
				Oflag = argp[2] - '0';
			else if (argp[3] == '\0' && argp[2] == 's')
				Oflag = 1;	/* optimize for space only */
			else
				oerror(argp);
			break;

		case 'P':
			strlist_append(&preprocessor_flags, argp);
			break;

		case 'M':
			needM = 1;
			if (match(argp, "-M")) {
				Mflag++;
				strlist_append(&depflags, argp);
			} else if (match(argp, "-MP")) {
				strlist_append(&depflags, "-xMP");
			} else if (match(argp, "-MF")) {
				MFfile = nxtopt("-MF");
			} else if (match(argp, "-MT") || match(argp, "-MQ")) {
				t = cat("-xMT,", nxtopt("-MT"));
				t[3] = argp[2];
				strlist_append(&depflags, t);
			} else if (match(argp, "-MD")) {
				MDflag++;
				needM = 0;
				strlist_append(&depflags, "-M");
			} else if (match(argp, "-MMD")) {
				MMDflag++;
				needM = 0;
				strlist_append(&depflags, "-M");
				strlist_append(&depflags, "-xMMD");
			} else
				oerror(argp);
			break;

		case 'v':
			printf("%s\n", VERSSTR);
			vflag++;
			break;

		case 'w': /* no warnings at all emitted */
			strlist_append(&compiler_flags, "-w");
			break;

		case 'W': /* Ignore (most of) W-flags */
			if ((t = argnxt(argp, "-Wl,"))) {
				u = strtok(t, ",");
				do {
					strlist_append(&inputs, u);
				} while ((u = strtok(NULL, ",")) != NULL);
			} else if ((t = argnxt(argp, "-Wa,"))) {
				u = strtok(t, ",");
				do {
					strlist_append(&assembler_flags, u);
				} while ((u = strtok(NULL, ",")) != NULL);
			} else if ((t = argnxt(argp, "-Wc,"))) {
				u = strtok(t, ",");
				do {
					strlist_append(&compiler_flags, u);
				} while ((u = strtok(NULL, ",")) != NULL);
			} else if ((t = argnxt(argp, "-Wp,"))) {
				u = strtok(t, ",");
				do {
					strlist_append(&preprocessor_flags, u);
				} while ((u = strtok(NULL, ",")) != NULL);
			} else if (strcmp(argp, "-Werror") == 0) {
				strlist_append(&compiler_flags, "-Werror");
				strlist_append(&preprocessor_flags, "-E");
			} else if (strcmp(argp, "-Wall") == 0) {
				for (Wf = Wflags; Wf->name; Wf++)
					if (Wf->flags & INWALL)
						strlist_append(&compiler_flags,
						    cat("-W", Wf->name));
			} else if (strcmp(argp, "-Wextra") == 0 ||
				   strcmp(argp, "-W") == 0) {
				for (Wf = Wflags; Wf->name; Wf++)
					if (Wf->flags & INWEXTRA)
						strlist_append(&compiler_flags,
						    cat("-W", Wf->name));
			} else if (strcmp(argp, "-WW") == 0) {
				for (Wf = Wflags; Wf->name; Wf++)
					strlist_append(&compiler_flags,
					    cat("-W", Wf->name));
			} else {
				/* pass through, if supported */
				t = &argp[2];
				if (strncmp(t, "no-", 3) == 0)
					t += 3;
				if (strncmp(t, "error=", 6) == 0)
					t += 6;
				for (Wf = Wflags; Wf->name; Wf++) {
					if (strcmp(t, Wf->name) == 0)
						strlist_append(&compiler_flags,
						    argp);
				}
			}
			break;

		case 'x':
			t = nxtopt("-x");
			if (match(t, "none"))
				strlist_append(&inputs, ")");
			else if (match(t, "c"))
				strlist_append(&inputs, ")c");
			else if (match(t, "assembler"))
				strlist_append(&inputs, ")s");
			else if (match(t, "assembler-with-cpp"))
				strlist_append(&inputs, ")S");
			else if (match(t, "c++"))
				strlist_append(&inputs, ")c++");
			else {
				strlist_append(&compiler_flags, "-x");
				strlist_append(&compiler_flags, t);
			}
			break;

		case 'z':
			argp = cat(argp, nxtopt(0));
			strlist_append(&middle_linker_flags, argp);
			break;

		}
		continue;

	}

	/* Sanity checking */
	if (cppflag) {
		if (ninput == 0) {
			strlist_append(&inputs, "-");
			ninput++;
		} else if (ninput > 2 || (ninput == 2 && outfile)) {
			errorx(8, "too many files");
		} else if (ninput == 2) {
			outfile = STRLIST_NEXT(STRLIST_FIRST(&inputs))->value;
			STRLIST_FIRST(&inputs)->next = NULL;
			ninput--;
		}
	}
	if (tflag && Eflag == 0)
		errorx(8,"-t only allowed fi -E given");

	/* Correct C standard */
	switch (cstd) {
	case STRAD: break;
	case SC89: c89defs = 1; break;
	case SGNU89: xgnu89 = c89defs = 1; break;
	case SC99: c89defs = c99defs = 1; break;
	case SGNU99: c89defs = c99defs = xgnu99 = 1; break;
	case SC11: c89defs = c11defs = 1; break;
	}

	if (ninput == 0 && !(printprogname || printfilename || printsearchdirs)) {
		if (vflag && outfile == NULL && cflag == 0 && Sflag == 0 &&
		    Eflag == 0 && Mflag == 0 && MDflag == 0 && MMDflag == 0 &&
		    needM == 0)
			return 0;
		errorx(8, "no input files");
	}
	if (outfile && (cflag || Sflag || Eflag) && ninput > 1)
		errorx(8, "-o given with -c || -E || -S and more than one file");
#if 0
	if (outfile && clist[0] && strcmp(outfile, clist[0]) == 0)
		errorx(8, "output file will be clobbered");
#endif

	if (needM && !Mflag && !MDflag && !MMDflag)
		errorx(8, "to make dependencies needs -M");


	if (signal(SIGINT, SIG_IGN) != SIG_IGN)	/* interrupt */
		signal(SIGINT, idexit);
	if (signal(SIGTERM, SIG_IGN) != SIG_IGN)	/* terminate */
		signal(SIGTERM, idexit);

	/* after arg parsing */
	strlist_append(&progdirs, LIBEXECDIR);
	if (pcclibdir)
		strlist_append(&crtdirs, pcclibdir);
	for (j = 0; deflibdirs[j]; j++) {
		if (sysroot && *sysroot)
			deflibdirs[j] = cat_sysroot(sysroot, deflibdirs[j]);
		strlist_append(&crtdirs, deflibdirs[j]);
	}

	setup_cpp_flags();
	setup_ccom_flags();
	setup_as_flags();

	if (isysroot == NULL)
		isysroot = sysroot;
	expand_sysroot();

	if (printprogname) {
		printf("%s\n", find_file(fname, &progdirs, X_OK));
		return 0;
	} else if (printfilename) {
		printf("%s\n", find_file(fname, &crtdirs, R_OK));
		return 0;
	} else if (printsearchdirs) {
		printf("install: %s\n", LIBEXECDIR);
		printf("programs: =");
		strlist_print(&progdirs, stdout, 0, ":");
		printf("\n");
		printf("libraries: =");
		strlist_print(&crtdirs, stdout, 0, ":");
		printf("\n");
		return 0;
	}

	msuffix = NULL;
	STRLIST_FOREACH(s, &inputs) {
		char *suffix;
		char *ifile, *ofile = NULL;

		ifile = s->value;
		if (ifile[0] == ')') { /* -x source type given */
			msuffix = ifile[1] ? &ifile[1] : NULL;
			continue;
		}
		if (ifile[0] == '-' && ifile[1] == 0)
			suffix = msuffix ? msuffix : "c";
		else if (ifile[0] == '-')
			suffix = "o"; /* source files cannot begin with - */
		else if (msuffix)
			suffix = msuffix;
		else
			suffix = getsufp(ifile);
		/*
		 * C preprocessor
		 */
		ascpp = match(suffix, "S");
		if (ascpp || cppflag || match(suffix, "c") || cxxsuf(suffix)) {
			/* find out next output file */
			if (Mflag || MDflag || MMDflag) {
				char *Mofile = NULL;

				if (MFfile)
					Mofile = MFfile;
				else if (outfile)
					Mofile = setsuf(outfile, 'd');
				else if (MDflag || MMDflag)
					Mofile = setsuf(ifile, 'd');
				if (preprocess_input(ifile, Mofile, 1))
					exandrm(Mofile);
			}
			if (Mflag)
				continue;
			if (Eflag) {
				/* last pass */
				ofile = outfile;
			} else {
				/* to temp file */
				strlist_append(&temp_outputs, ofile = gettmp());
			}
			if (preprocess_input(ifile, ofile, 0))
				exandrm(ofile);
			if (Eflag)
				continue;
			ifile = ofile;
			suffix = match(suffix, "S") ? "s" : "i";
		}

		/*
		 * C compiler
		 */
		if (match(suffix, "i")) {
			/* find out next output file */
			if (Sflag) {
				ofile = outfile;
				if (outfile == NULL)
					ofile = setsuf(s->value, 's');
			} else
				strlist_append(&temp_outputs, ofile = gettmp());
			if (compile_input(ifile, ofile))
				exandrm(ofile);
#if defined(mach_mips) || defined(mach_mips64)
			if (mips_postprocess_asm(ofile))
				exandrm(ofile);
#endif
			if (Sflag)
				continue;
			ifile = ofile;
			suffix = "s";
		}

		/*
		 * Assembler
		 */
		if (match(suffix, "s")) {
			if (cflag) {
				ofile = outfile;
				if (ofile == NULL)
					ofile = setsuf(s->value, 'o');
			} else {
				strlist_append(&temp_outputs, ofile = gettmp());
				/* strlist_append linker */
			}
#if defined(mach_mips) || defined(mach_mips64)
			if (mips_prepare_asm_input(ifile, &ifile))
				exandrm(0);
#endif
			if (assemble_input(ifile, ofile))
				exandrm(ofile);
			ifile = ofile;
		}

		strlist_append(&middle_linker_flags, ifile);
	}

	if (cflag || Eflag || Mflag)
		dexit(0);

	/*
	 * Linker
	 */
	setup_ld_flags();
	if (run_linker())
		exandrm(0);

#ifdef notdef
	strlist_free(&crtdirs);
	strlist_free(&libdirs);
	strlist_free(&progdirs);
	strlist_free(&incdirs);
	strlist_free(&preprocessor_flags);
	strlist_free(&user_sysincdirs);
	strlist_free(&includes);
	strlist_free(&sysincdirs);
	strlist_free(&dirafterdirs);
	strlist_free(&depflags);
	strlist_free(&early_linker_flags);
	strlist_free(&middle_linker_flags);
	strlist_free(&late_linker_flags);
	strlist_free(&inputs);
	strlist_free(&assembler_flags);
	strlist_free(&temp_outputs);
	strlist_free(&compiler_flags);
#endif
	dexit(0);
	return 0;
}

/*
 * exit and cleanup after interrupt.
 */
void
idexit(int arg)
{
	dexit(100);
}

/*
 * exit and cleanup.
 */
void
dexit(int eval)
{
	struct string *s;

	if (!Xflag) {
		STRLIST_FOREACH(s, &temp_outputs)
			cunlink(s->value);
	}
	exit(eval);
}

/*
 * Called when something failed.
 */
void
exandrm(char *s)
{
	if (s && *s)
		strlist_append(&temp_outputs, s);
	dexit(1);
}

/*
 * complain and exit.
 */
void
errorx(int eval, char *s, ...)
{
	va_list ap;

	va_start(ap, s);
	fputs("error: ", stderr);
	vfprintf(stderr, s, ap);
	putc('\n', stderr);
	va_end(ap);
	dexit(eval);
}

static char *
find_file(const char *file, struct strlist *path, int mode)
{
	struct string *s;
	char *f;
	size_t lf, lp;
	int need_sep;

	lf = strlen(file);
	if (lf > 0 && file[0] == '/')
		return xstrdup(file);
	STRLIST_FOREACH(s, path) {
		lp = strlen(s->value);
		need_sep = (lp && s->value[lp - 1] != '/') ? 1 : 0;
		f = xmalloc(lp + lf + need_sep + 1);
		memcpy(f, s->value, lp);
		if (need_sep)
			f[lp] = '/';
		memcpy(f + lp + need_sep, file, lf + 1);
		if (access(f, mode) == 0)
			return f;
		free(f);
	}
	return xstrdup(file);
}

#ifdef HAVE_CC2
#ifdef NEED_CC2
#define	C2check	1
#else
#define	C2check	Oflag
#endif
#else
#define	C2check	0
#endif

#ifdef TWOPASS
static int
compile_input(char *input, char *output)
{
	struct strlist args;
	char *tfile;
	int retval;

	strlist_append(&temp_outputs, tfile = gettmp());

	strlist_init(&args);
	strlist_append_list(&args, &compiler_flags);
	strlist_append(&args, input);
	strlist_append(&args, tfile);
	strlist_prepend(&args,
	    find_file(cxxflag ? CXX0 : CC0, &progdirs, X_OK));
	retval = strlist_exec(&args);
	strlist_free(&args);
	if (retval)
		return retval;

	strlist_init(&args);
	strlist_append_list(&args, &compiler_flags);
	strlist_append(&args, tfile);
	strlist_append(&args, output);
	strlist_prepend(&args,
	    find_file(cxxflag ? CXX1: CC1, &progdirs, X_OK));
	retval = strlist_exec(&args);
	strlist_free(&args);
	return retval;
}
#else
static int
compile_input(char *input, char *output)
{
	struct strlist args;
	char *tfile;
	int retval;

	tfile = output;
	if (C2check)
		strlist_append(&temp_outputs, tfile = gettmp());

	strlist_init(&args);
	strlist_append_list(&args, &compiler_flags);
	strlist_append(&args, input);
	strlist_append(&args, tfile);
	strlist_prepend(&args,
	    find_file(cxxflag ? passxx0 : pass0, &progdirs, X_OK));
	retval = strlist_exec(&args);
	strlist_free(&args);
	if (retval)
		return retval;
	if (C2check) {
		strlist_init(&args);
		strlist_append(&args, tfile);
		strlist_append(&args, output);
		strlist_prepend(&args, find_file(CC2, &progdirs, X_OK));
		retval = strlist_exec(&args);
		strlist_free(&args);
	}
	return retval;
}
#endif

#if defined(mach_mips) || defined(mach_mips64)
#define MIPS_LOAD_NONE	0
#define MIPS_LOAD_GPR	1
#define MIPS_LOAD_FPR	2

struct mips_load_dest {
	int kind;
	unsigned long long regs;
};

static const char *
mips_skip_space(const char *s)
{
	while (*s == ' ' || *s == '\t')
		++s;
	return s;
}

static int
mips_parse_opcode(const char *line, char *op, size_t opsz)
{
	const char *s;
	size_t len;

	s = mips_skip_space(line);
	if (*s == '\0' || *s == '\n' || *s == '#' || *s == '.')
		return 0;
	len = 0;
	while (s[len] != '\0' && s[len] != '\n' && s[len] != ' ' &&
	    s[len] != '\t' && s[len] != ',') {
		if (s[len] == ':')
			return 0;
		if (len + 1 < opsz)
			op[len] = s[len];
		++len;
	}
	if (len == 0 || len >= opsz)
		return 0;
	op[len] = '\0';
	return 1;
}

static int
mips_is_int_load(const char *op)
{
	return strcmp(op, "lw") == 0 || strcmp(op, "lh") == 0 ||
	    strcmp(op, "lhu") == 0 || strcmp(op, "lb") == 0 ||
	    strcmp(op, "lbu") == 0 || strcmp(op, "lwl") == 0 ||
	    strcmp(op, "lwr") == 0;
}

static int
mips_is_fpu_load(const char *op)
{
	return strcmp(op, "l.s") == 0 || strcmp(op, "l.d") == 0 ||
	    strcmp(op, "lwc1") == 0 || strcmp(op, "ldc1") == 0;
}

static int
mips_alias_gpr(const char *name, size_t len)
{
	static const struct {
		const char *name;
		int reg;
	} aliases[] = {
		{ "zero", 0 }, { "at", 1 },
		{ "v0", 2 }, { "v1", 3 },
		{ "a0", 4 }, { "a1", 5 }, { "a2", 6 }, { "a3", 7 },
		{ "a4", 8 }, { "a5", 9 }, { "a6", 10 }, { "a7", 11 },
		{ "t0", 8 }, { "t1", 9 }, { "t2", 10 }, { "t3", 11 },
		{ "t4", 12 }, { "t5", 13 }, { "t6", 14 }, { "t7", 15 },
		{ "s0", 16 }, { "s1", 17 }, { "s2", 18 }, { "s3", 19 },
		{ "s4", 20 }, { "s5", 21 }, { "s6", 22 }, { "s7", 23 },
		{ "t8", 24 }, { "t9", 25 },
		{ "k0", 26 }, { "k1", 27 },
		{ "gp", 28 }, { "sp", 29 }, { "fp", 30 },
		{ "s8", 30 }, { "ra", 31 },
	};
	size_t i;

	for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++)
		if (strlen(aliases[i].name) == len &&
		    strncmp(aliases[i].name, name, len) == 0)
			return aliases[i].reg;
	return -1;
}

static int
mips_parse_gpr(const char *s, const char **endp)
{
	unsigned int reg;
	const char *name;
	size_t len;

	if (*s != '$')
		return -1;
	++s;
	if (*s == 'f' && s[1] >= '0' && s[1] <= '9') {
		++s;
		while (*s >= '0' && *s <= '9')
			++s;
		if (endp != NULL)
			*endp = s;
		return -1;
	}
	if (*s >= '0' && *s <= '9') {
		reg = 0;
		while (*s >= '0' && *s <= '9') {
			reg = reg * 10 + (unsigned int)(*s - '0');
			++s;
		}
		if (endp != NULL)
			*endp = s;
		return reg <= 31 ? (int)reg : -1;
	}
	name = s;
	while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
	    (*s >= '0' && *s <= '9') || *s == '_')
		++s;
	len = (size_t)(s - name);
	if (endp != NULL)
		*endp = s;
	if (len == 0)
		return -1;
	return mips_alias_gpr(name, len);
}

static int
mips_parse_fpr(const char *s, const char **endp)
{
	unsigned int reg;

	if (*s != '$') {
		if (endp != NULL)
			*endp = s;
		return -1;
	}
	++s;
	if (*s != 'f') {
		if (endp != NULL)
			*endp = s;
		return -1;
	}
	++s;
	if (*s < '0' || *s > '9') {
		if (endp != NULL)
			*endp = s;
		return -1;
	}
	reg = 0;
	while (*s >= '0' && *s <= '9') {
		reg = reg * 10 + (unsigned int)(*s - '0');
		++s;
	}
	if (endp != NULL)
		*endp = s;
	return reg <= 31 ? (int)reg : -1;
}

static struct mips_load_dest
mips_no_load_dest(void)
{
	struct mips_load_dest dest;

	dest.kind = MIPS_LOAD_NONE;
	dest.regs = 0;
	return dest;
}

static struct mips_load_dest
mips_load_dest_for_reg(int kind, int reg, int pair)
{
	struct mips_load_dest dest;
	int other;

	dest.kind = kind;
	dest.regs = 1ULL << reg;
	if (kind == MIPS_LOAD_FPR && pair) {
		other = (reg & 1) ? reg - 1 : reg + 1;
		if (other >= 0 && other <= 31)
			dest.regs |= 1ULL << other;
	}
	return dest;
}

static struct mips_load_dest
mips_load_dest(const char *line)
{
	char op[16];
	const char *s;
	int reg;

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return mips_no_load_dest();
	s = mips_skip_space(line);
	s += strlen(op);
	s = mips_skip_space(s);
	if (mips_is_int_load(op)) {
		reg = mips_parse_gpr(s, NULL);
		if (reg >= 0)
			return mips_load_dest_for_reg(MIPS_LOAD_GPR, reg, 0);
	} else if (mips_is_fpu_load(op)) {
		reg = mips_parse_fpr(s, NULL);
		if (reg >= 0)
			return mips_load_dest_for_reg(MIPS_LOAD_FPR, reg,
			    strcmp(op, "l.d") == 0 || strcmp(op, "ldc1") == 0);
	}
	return mips_no_load_dest();
}

static int
mips_is_instruction(const char *line)
{
	char op[16];

	return mips_parse_opcode(line, op, sizeof(op));
}

static int
mips_is_nop(const char *line)
{
	char op[16];

	return mips_parse_opcode(line, op, sizeof(op)) && strcmp(op, "nop") == 0;
}

static int
mips_label_start_char(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	    c == '_' || c == '$';
}

static int
mips_label_char(int c)
{
	return mips_label_start_char(c) || (c >= '0' && c <= '9') ||
	    c == '.';
}

static int
mips_is_load_gap_skip_line(const char *line)
{
	const char *s;

	s = mips_skip_space(line);
	if (*s == '\0' || *s == '\n' || *s == '#')
		return 1;
	if (!mips_label_start_char((unsigned char)*s))
		return 0;
	while (mips_label_char((unsigned char)*s))
		++s;
	if (*s++ != ':')
		return 0;
	s = mips_skip_space(s);
	return *s == '\0' || *s == '\n' || *s == '#';
}

static int
mips_is_vr4300_fp_mul(const char *line)
{
	char op[16];

	return mips_parse_opcode(line, op, sizeof(op)) &&
	    (strcmp(op, "mul.s") == 0 || strcmp(op, "mul.d") == 0);
}

static int
mips_is_multiply(const char *line)
{
	char op[16];

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	return strcmp(op, "mul.s") == 0 || strcmp(op, "mul.d") == 0 ||
	    strcmp(op, "mult") == 0 || strcmp(op, "multu") == 0 ||
	    strcmp(op, "dmult") == 0 || strcmp(op, "dmultu") == 0;
}

static int
mips_is_hilo_write(const char *line)
{
	char op[16];

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	return strcmp(op, "mult") == 0 || strcmp(op, "multu") == 0 ||
	    strcmp(op, "dmult") == 0 || strcmp(op, "dmultu") == 0 ||
	    strcmp(op, "div") == 0 || strcmp(op, "divu") == 0 ||
	    strcmp(op, "ddiv") == 0 || strcmp(op, "ddivu") == 0 ||
	    strcmp(op, "mthi") == 0 || strcmp(op, "mtlo") == 0 ||
	    strcmp(op, "mul") == 0 || strcmp(op, "madd") == 0 ||
	    strcmp(op, "maddu") == 0 || strcmp(op, "msub") == 0 ||
	    strcmp(op, "msubu") == 0;
}

static int
mips_is_fpu_compare(const char *line)
{
	char op[16];

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	return strcmp(op, "c.eq.s") == 0 || strcmp(op, "c.eq.d") == 0 ||
	    strcmp(op, "c.lt.s") == 0 || strcmp(op, "c.lt.d") == 0 ||
	    strcmp(op, "c.le.s") == 0 || strcmp(op, "c.le.d") == 0;
}

static int
mips_is_fpu_branch(const char *line)
{
	char op[16];

	return mips_parse_opcode(line, op, sizeof(op)) &&
	    (strcmp(op, "bc1t") == 0 || strcmp(op, "bc1f") == 0);
}

static int
mips_has_delay_slot(const char *line)
{
	char op[16];

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	if (strcmp(op, "j") == 0 || strcmp(op, "jal") == 0 ||
	    strcmp(op, "jr") == 0 || strcmp(op, "jalr") == 0)
		return 1;
	return op[0] == 'b' && strcmp(op, "break") != 0;
}

static int
mips_is_load_gap_insn(const char *line, const struct mips_load_dest *dest)
{
	char op[16];

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	if (strcmp(op, "nop") == 0)
		return 0;
	return 1;
}

static int
mips_line_touches_gpr(const char *line, unsigned long long regs)
{
	char op[16];
	const char *s;
	int r;

	if (mips_parse_opcode(line, op, sizeof(op)) &&
	    (strcmp(op, "jal") == 0 || strcmp(op, "jalr") == 0) &&
	    (regs & (1ULL << 31)) != 0)
		return 1;
	for (s = line; *s != '\0' && *s != '\n' && *s != '#'; ++s) {
		if (*s != '$')
			continue;
		r = mips_parse_gpr(s, &s);
		if (r >= 0 && (regs & (1ULL << r)) != 0)
			return 1;
		if (*s == '\0' || *s == '\n' || *s == '#')
			break;
		--s;
	}
	return 0;
}

static int
mips_line_touches_fpr(const char *line, unsigned long long regs)
{
	const char *s;
	int r;

	for (s = line; *s != '\0' && *s != '\n' && *s != '#'; ++s) {
		if (*s != '$')
			continue;
		r = mips_parse_fpr(s, &s);
		if (r >= 0 && (regs & (1ULL << r)) != 0)
			return 1;
		if (*s == '\0' || *s == '\n' || *s == '#')
			break;
		--s;
	}
	return 0;
}

static int
mips_line_touches_load(const char *line, const struct mips_load_dest *dest)
{
	if (dest->kind == MIPS_LOAD_GPR)
		return mips_line_touches_gpr(line, dest->regs);
	if (dest->kind == MIPS_LOAD_FPR)
		return mips_line_touches_fpr(line, dest->regs);
	return 1;
}

static int
mips_is_control_transfer(const char *line)
{
	char op[16];

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	return op[0] == 'b' || strcmp(op, "j") == 0 ||
	    strcmp(op, "jr") == 0 || strcmp(op, "jal") == 0 ||
	    strcmp(op, "jalr") == 0;
}

static int
mips_reg_token_char(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	    (c >= '0' && c <= '9') || c == '_';
}

static int
mips_parse_gpr_operand(const char **sp, char *tok, size_t toksz, int *regp)
{
	const char *s, *end, *rend;
	size_t len;
	int reg;

	s = mips_skip_space(*sp);
	if (*s != '$')
		return 0;
	end = s + 1;
	while (mips_reg_token_char((unsigned char)*end))
		++end;
	len = (size_t)(end - s);
	if (len <= 1 || len >= toksz)
		return 0;
	reg = mips_parse_gpr(s, &rend);
	if (reg < 0 || rend != end)
		return 0;
	memcpy(tok, s, len);
	tok[len] = '\0';
	*sp = end;
	*regp = reg;
	return 1;
}

static int
mips_skip_comma(const char **sp)
{
	const char *s;

	s = mips_skip_space(*sp);
	if (*s != ',')
		return 0;
	*sp = s + 1;
	return 1;
}

static int
mips_line_ends_after_operands(const char *s)
{
	s = mips_skip_space(s);
	return *s == '\0' || *s == '\n' || *s == '#';
}

static int
mips_parse_move_gprs(const char *line, char *dsttok, size_t dstsz,
    char *srctok, size_t srcsz, int *dstp, int *srcp)
{
	char op[16];
	const char *s;

	if (!mips_parse_opcode(line, op, sizeof(op)) || strcmp(op, "move") != 0)
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_gpr_operand(&s, dsttok, dstsz, dstp) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, srctok, srcsz, srcp) ||
	    !mips_line_ends_after_operands(s))
		return 0;
	return 1;
}

static int
mips_parse_tail_operand(const char *s, char *tok, size_t toksz,
    const char **commentp)
{
	const char *start, *end, *comment;
	size_t len;

	start = mips_skip_space(s);
	end = start;
	while (*end != '\0' && *end != '\n' && *end != '#')
		++end;
	comment = *end == '#' ? end : NULL;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
		--end;
	len = (size_t)(end - start);
	if (len == 0 || len >= toksz)
		return 0;
	memcpy(tok, start, len);
	tok[len] = '\0';
	*commentp = comment;
	return 1;
}

static int
mips_fold_move_shift_line(const char *move, const char *shift, char *out,
    size_t outsz)
{
	char op[16], mdst[16], msrc[16], sdst[16], ssrc[16], imm[64];
	const char *s, *comment;
	int mdstreg, msrcreg, sdstreg, ssrcreg;
	int n;

	if (!mips_parse_move_gprs(move, mdst, sizeof(mdst), msrc, sizeof(msrc),
	    &mdstreg, &msrcreg))
		return 0;
	if (!mips_parse_opcode(shift, op, sizeof(op)) ||
	    (strcmp(op, "sll") != 0 && strcmp(op, "srl") != 0 &&
	    strcmp(op, "sra") != 0))
		return 0;
	s = mips_skip_space(shift);
	s += strlen(op);
	if (!mips_parse_gpr_operand(&s, sdst, sizeof(sdst), &sdstreg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, ssrc, sizeof(ssrc), &ssrcreg) ||
	    !mips_skip_comma(&s))
		return 0;
	if (sdstreg != mdstreg || ssrcreg != mdstreg)
		return 0;
	if (!mips_parse_tail_operand(s, imm, sizeof(imm), &comment))
		return 0;
	if (comment != NULL)
		n = snprintf(out, outsz, "\t%s %s,%s,%s\t%s",
		    op, sdst, msrc, imm, comment);
	else
		n = snprintf(out, outsz, "\t%s %s,%s,%s\n",
		    op, sdst, msrc, imm);
	return n > 0 && (size_t)n < outsz;
}

static int
mips_fold_move_addiu_line(const char *move, const char *add, char *out,
    size_t outsz)
{
	char mdst[16], msrc[16], adst[16], asrc[16], imm[64];
	const char *s, *comment;
	int mdstreg, msrcreg, adstreg, asrcreg;
	int n;

	if (!mips_parse_move_gprs(move, mdst, sizeof(mdst), msrc, sizeof(msrc),
	    &mdstreg, &msrcreg))
		return 0;
	if (!mips_parse_opcode(add, imm, sizeof(imm)) ||
	    strcmp(imm, "addiu") != 0)
		return 0;
	s = mips_skip_space(add);
	s += strlen(imm);
	if (!mips_parse_gpr_operand(&s, adst, sizeof(adst), &adstreg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, asrc, sizeof(asrc), &asrcreg) ||
	    !mips_skip_comma(&s))
		return 0;
	if (adstreg != mdstreg || asrcreg != mdstreg)
		return 0;
	if (!mips_parse_tail_operand(s, imm, sizeof(imm), &comment))
		return 0;
	if (comment != NULL)
		n = snprintf(out, outsz, "\taddiu %s,%s,%s\t%s",
		    adst, msrc, imm, comment);
	else
		n = snprintf(out, outsz, "\taddiu %s,%s,%s\n",
		    adst, msrc, imm);
	return n > 0 && (size_t)n < outsz;
}

static int
mips_parse_signed_imm16(const char *tok, int *imm)
{
	char *end;
	long val;

	if (strcmp(tok, "0xffffffff") == 0 || strcmp(tok, "0xFFFFFFFF") == 0) {
		*imm = -1;
		return 1;
	}
	errno = 0;
	val = strtol(tok, &end, 0);
	if (errno != 0 || *end != '\0' || val < -32768 || val > 32767)
		return 0;
	*imm = (int)val;
	return 1;
}

static int
mips_fold_li_addiu_line(const char *line, char *out, size_t outsz)
{
	char op[16], dst[16], imm[64];
	const char *s, *comment;
	int dstreg, immval;
	int n;

	if (!mips_parse_opcode(line, op, sizeof(op)) || strcmp(op, "li") != 0)
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_gpr_operand(&s, dst, sizeof(dst), &dstreg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_tail_operand(s, imm, sizeof(imm), &comment) ||
	    !mips_parse_signed_imm16(imm, &immval))
		return 0;
	if (comment != NULL)
		n = snprintf(out, outsz, "\taddiu %s,$zero,%d\t%s",
		    dst, immval, comment);
	else
		n = snprintf(out, outsz, "\taddiu %s,$zero,%d\n",
		    dst, immval);
	return n > 0 && (size_t)n < outsz;
}

static int
mips_parse_shift_imm_gprs(const char *line, unsigned long long *regsp)
{
	char op[16], tok[16], imm[64];
	const char *s, *comment;
	char *end;
	long val;
	int dstreg, srcreg;

	if (!mips_parse_opcode(line, op, sizeof(op)) ||
	    (strcmp(op, "sll") != 0 && strcmp(op, "srl") != 0 &&
	    strcmp(op, "sra") != 0))
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_gpr_operand(&s, tok, sizeof(tok), &dstreg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, tok, sizeof(tok), &srcreg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_tail_operand(s, imm, sizeof(imm), &comment))
		return 0;
	errno = 0;
	val = strtol(imm, &end, 0);
	if (errno != 0 || *end != '\0' || val < 0 || val > 31)
		return 0;
	*regsp = (1ULL << dstreg) | (1ULL << srcreg);
	return 1;
}

static int
mips_parse_move_gprs_regs(const char *line, unsigned long long *regsp)
{
	char dst[16], src[16];
	int dstreg, srcreg;

	if (!mips_parse_move_gprs(line, dst, sizeof(dst), src, sizeof(src),
	    &dstreg, &srcreg))
		return 0;
	*regsp = (1ULL << dstreg) | (1ULL << srcreg);
	return 1;
}

static int
mips_parse_gpr_alu_regs(const char *line, unsigned long long *regsp)
{
	char op[16], tok[16];
	const char *s, *comment;
	int dstreg, src1reg, src2reg;

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (strcmp(op, "addiu") == 0) {
		if (!mips_parse_gpr_operand(&s, tok, sizeof(tok), &dstreg) ||
		    !mips_skip_comma(&s) ||
		    !mips_parse_gpr_operand(&s, tok, sizeof(tok), &src1reg) ||
		    !mips_skip_comma(&s) ||
		    !mips_parse_tail_operand(s, tok, sizeof(tok), &comment))
			return 0;
		*regsp = (1ULL << dstreg) | (1ULL << src1reg);
		return 1;
	}
	if (strcmp(op, "addu") != 0 && strcmp(op, "subu") != 0)
		return 0;
	if (!mips_parse_gpr_operand(&s, tok, sizeof(tok), &dstreg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, tok, sizeof(tok), &src1reg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, tok, sizeof(tok), &src2reg) ||
	    !mips_line_ends_after_operands(s))
		return 0;
	*regsp = (1ULL << dstreg) | (1ULL << src1reg) |
	    (1ULL << src2reg);
	return 1;
}

static int
mips_parse_simple_gpr_regs(const char *line, unsigned long long *regsp)
{
	return mips_parse_gpr_alu_regs(line, regsp) ||
	    mips_parse_shift_imm_gprs(line, regsp) ||
	    mips_parse_move_gprs_regs(line, regsp);
}

static int
mips_is_lw_load(const char *line, struct mips_load_dest *destp)
{
	char op[16];
	struct mips_load_dest dest;

	if (!mips_parse_opcode(line, op, sizeof(op)) || strcmp(op, "lw") != 0)
		return 0;
	dest = mips_load_dest(line);
	if (dest.kind != MIPS_LOAD_GPR)
		return 0;
	*destp = dest;
	return 1;
}

static int
mips_is_fpu_load_line(const char *line, struct mips_load_dest *destp)
{
	struct mips_load_dest dest;

	dest = mips_load_dest(line);
	if (dest.kind != MIPS_LOAD_FPR)
		return 0;
	*destp = dest;
	return 1;
}

static int
mips_can_fill_shift_load_delay(const char *shift, const char *load,
    const char *next)
{
	struct mips_load_dest dest;
	unsigned long long shift_regs;

	if (!mips_parse_shift_imm_gprs(shift, &shift_regs) ||
	    !mips_is_lw_load(load, &dest))
		return 0;
	if (!mips_is_load_gap_insn(next, &dest) ||
	    !mips_line_touches_load(next, &dest))
		return 0;
	if (mips_line_touches_gpr(load, shift_regs) ||
	    mips_line_touches_load(shift, &dest))
		return 0;
	return 1;
}

static int
mips_can_fill_move_load_delay(const char *move, const char *load,
    const char *next)
{
	struct mips_load_dest dest;
	unsigned long long move_regs;

	if (!mips_parse_move_gprs_regs(move, &move_regs) ||
	    !mips_is_lw_load(load, &dest))
		return 0;
	if (!mips_is_load_gap_insn(next, &dest) ||
	    !mips_line_touches_load(next, &dest))
		return 0;
	if (mips_line_touches_gpr(load, move_regs) ||
	    mips_line_touches_load(move, &dest))
		return 0;
	return 1;
}

static int
mips_can_fill_li_load_delay(const char *li, const char *load,
    const char *next, char *delay, size_t delaysz)
{
	struct mips_load_dest dest;
	unsigned long long li_regs;

	if (!mips_fold_li_addiu_line(li, delay, delaysz) ||
	    !mips_parse_gpr_alu_regs(delay, &li_regs) ||
	    !mips_is_lw_load(load, &dest))
		return 0;
	if ((li_regs & (1ULL << 29)) != 0)
		return 0;
	if (mips_is_control_transfer(next))
		return 0;
	if (!mips_is_load_gap_insn(next, &dest) ||
	    !mips_line_touches_load(next, &dest))
		return 0;
	if (mips_line_touches_gpr(load, li_regs) ||
	    mips_line_touches_load(delay, &dest))
		return 0;
	return 1;
}

static int
mips_can_fill_gpr_alu_fpu_load_delay(const char *alu, const char *load,
    const char *next)
{
	struct mips_load_dest dest;
	unsigned long long alu_regs;

	if (!mips_parse_gpr_alu_regs(alu, &alu_regs) ||
	    !mips_is_fpu_load_line(load, &dest))
		return 0;
	if (!mips_is_load_gap_insn(next, &dest) ||
	    !mips_line_touches_load(next, &dest))
		return 0;
	if (mips_line_touches_gpr(load, alu_regs) ||
	    mips_line_touches_load(alu, &dest))
		return 0;
	return 1;
}

static int
mips_parse_int_mult_hilo(const char *line, unsigned long long *regsp)
{
	char op[16], tok[16];
	const char *s;
	int reg1, reg2;

	if (!mips_parse_opcode(line, op, sizeof(op)) ||
	    (strcmp(op, "mult") != 0 && strcmp(op, "multu") != 0 &&
	    strcmp(op, "dmult") != 0 && strcmp(op, "dmultu") != 0))
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg1) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg2) ||
	    !mips_line_ends_after_operands(s))
		return 0;
	if (regsp != NULL)
		*regsp = (1ULL << reg1) | (1ULL << reg2);
	return 1;
}

static int
mips_parse_mfhilo_dest(const char *line, int *regp)
{
	char op[16], tok[16];
	const char *s;

	if (!mips_parse_opcode(line, op, sizeof(op)) ||
	    (strcmp(op, "mflo") != 0 && strcmp(op, "mfhi") != 0))
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	return mips_parse_gpr_operand(&s, tok, sizeof(tok), regp) &&
	    mips_line_ends_after_operands(s);
}

static int
mips_can_trim_mfhilo_post_nops(const char *mf, const char *next1,
    const char *next2)
{
	int reg;

	if (mips_cpu != MIPS_CPU_VR4300 && mips_cpu != MIPS_CPU_MIPS32R2)
		return 0;
	if (!mips_parse_mfhilo_dest(mf, &reg) ||
	    !mips_is_instruction(next1) ||
	    !mips_is_instruction(next2))
		return 0;
	return !mips_is_hilo_write(next1) && !mips_is_hilo_write(next2);
}

static int
mips_parse_fpr_operand(const char **sp, int *regp)
{
	const char *s, *end;
	int reg;

	s = mips_skip_space(*sp);
	reg = mips_parse_fpr(s, &end);
	if (reg < 0)
		return 0;
	*sp = end;
	*regp = reg;
	return 1;
}

static int
mips_parse_mtc1_regs(const char *line, int *gprp, int *fprp)
{
	char op[16], tok[16];
	const char *s;

	if (!mips_parse_opcode(line, op, sizeof(op)) ||
	    strcmp(op, "mtc1") != 0)
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	return mips_parse_gpr_operand(&s, tok, sizeof(tok), gprp) &&
	    mips_skip_comma(&s) &&
	    mips_parse_fpr_operand(&s, fprp) &&
	    mips_line_ends_after_operands(s);
}

static int
mips_parse_cvt_w_regs(const char *line, int *dstp, int *srcp, int *doublep)
{
	char op[16];
	const char *s;

	if (!mips_parse_opcode(line, op, sizeof(op)) ||
	    (strcmp(op, "cvt.s.w") != 0 && strcmp(op, "cvt.d.w") != 0))
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_fpr_operand(&s, dstp) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_fpr_operand(&s, srcp) ||
	    !mips_line_ends_after_operands(s))
		return 0;
	if (doublep != NULL)
		*doublep = strcmp(op, "cvt.d.w") == 0;
	return 1;
}

static int
mips_can_trim_mtc1_cvt_nop(const char *mtc1, const char *cvt)
{
	int mtc1_gpr, mtc1_fpr, cvt_dst, cvt_src;

	if (mips_cpu != MIPS_CPU_MIPS32R2)
		return 0;
	if (!mips_parse_mtc1_regs(mtc1, &mtc1_gpr, &mtc1_fpr) ||
	    !mips_parse_cvt_w_regs(cvt, &cvt_dst, &cvt_src, NULL))
		return 0;
	return mtc1_fpr == cvt_src;
}

static int
mips_parse_fpu_binary_src_regs(const char *line, unsigned long long *srcp)
{
	char op[16];
	const char *s;
	int dst, src1, src2, pair;

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	pair = 0;
	if (strcmp(op, "add.s") == 0 || strcmp(op, "sub.s") == 0 ||
	    strcmp(op, "mul.s") == 0 || strcmp(op, "div.s") == 0)
		pair = 0;
	else if (strcmp(op, "add.d") == 0 || strcmp(op, "sub.d") == 0 ||
	    strcmp(op, "mul.d") == 0 || strcmp(op, "div.d") == 0)
		pair = 1;
	else
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_fpr_operand(&s, &dst) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_fpr_operand(&s, &src1) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_fpr_operand(&s, &src2) ||
	    !mips_line_ends_after_operands(s))
		return 0;
	*srcp = mips_load_dest_for_reg(MIPS_LOAD_FPR, src1, pair).regs |
	    mips_load_dest_for_reg(MIPS_LOAD_FPR, src2, pair).regs;
	return 1;
}

static int
mips_can_fill_hilo_mtc1_delay(const char *hilo, const char *mf,
    const char *mtc1, const char *cvt)
{
	int mfreg, mtc1_gpr, mtc1_fpr, cvt_dst, cvt_src;

	if (!mips_parse_int_mult_hilo(hilo, NULL) ||
	    !mips_parse_mfhilo_dest(mf, &mfreg) ||
	    !mips_parse_mtc1_regs(mtc1, &mtc1_gpr, &mtc1_fpr) ||
	    !mips_parse_cvt_w_regs(cvt, &cvt_dst, &cvt_src, NULL))
		return 0;
	if (mtc1_gpr == mfreg)
		return 0;
	return cvt_dst == mtc1_fpr && cvt_src == mtc1_fpr;
}

static int
mips_can_fill_hilo_lw_delay(const char *hilo, const char *mf,
    const char *use, const char *load)
{
	struct mips_load_dest load_dest;
	unsigned long long hilo_regs, use_regs;
	int mfreg;

	if (!mips_parse_int_mult_hilo(hilo, &hilo_regs) ||
	    !mips_parse_mfhilo_dest(mf, &mfreg) ||
	    !mips_is_lw_load(load, &load_dest))
		return 0;
	if (!mips_parse_simple_gpr_regs(use, &use_regs))
		return 0;
	if ((use_regs & (1ULL << mfreg)) == 0)
		return 0;
	if (mips_line_touches_load(use, &load_dest))
		return 0;
	if (mips_line_touches_gpr(load, use_regs))
		return 0;
	if (mips_line_touches_gpr(load, hilo_regs | (1ULL << mfreg)))
		return 0;
	return 1;
}

static int
mips_can_fill_hilo_lw_delay2(const char *hilo, const char *mf,
    const char *use1, const char *use2, const char *load)
{
	struct mips_load_dest load_dest;
	unsigned long long hilo_regs, use1_regs, use2_regs;
	int mfreg;

	if (!mips_parse_int_mult_hilo(hilo, &hilo_regs) ||
	    !mips_parse_mfhilo_dest(mf, &mfreg) ||
	    !mips_is_lw_load(load, &load_dest))
		return 0;
	if (!mips_parse_simple_gpr_regs(use1, &use1_regs) ||
	    !mips_parse_simple_gpr_regs(use2, &use2_regs))
		return 0;
	if ((use1_regs & (1ULL << mfreg)) == 0 ||
	    (use2_regs & (1ULL << mfreg)) == 0)
		return 0;
	if (mips_line_touches_load(use1, &load_dest) ||
	    mips_line_touches_load(use2, &load_dest))
		return 0;
	if (mips_line_touches_gpr(load, use1_regs | use2_regs))
		return 0;
	if (mips_line_touches_gpr(load, hilo_regs | (1ULL << mfreg)))
		return 0;
	return 1;
}

static int
mips_can_fill_hilo_post_mflo_lw_delay(const char *hilo, const char *mf,
    const char *load, const char *use)
{
	struct mips_load_dest load_dest;
	unsigned long long hilo_regs, use_regs;
	int mfreg;

	if (!mips_parse_int_mult_hilo(hilo, &hilo_regs) ||
	    !mips_parse_mfhilo_dest(mf, &mfreg) ||
	    !mips_is_lw_load(load, &load_dest) ||
	    !mips_parse_simple_gpr_regs(use, &use_regs))
		return 0;
	if (mips_line_touches_gpr(load, hilo_regs | (1ULL << mfreg)))
		return 0;
	return 1;
}

static int
mips_can_fill_mtc1_cvt_fpu_load_delay(const char *mtc1, const char *cvt,
    const char *load, const char *fp)
{
	struct mips_load_dest load_dest;
	unsigned long long cvt_regs, fp_src;
	int mtc1_gpr, mtc1_fpr, cvt_dst, cvt_src, cvt_double;

	if (!mips_parse_mtc1_regs(mtc1, &mtc1_gpr, &mtc1_fpr) ||
	    !mips_parse_cvt_w_regs(cvt, &cvt_dst, &cvt_src, &cvt_double) ||
	    !mips_is_fpu_load_line(load, &load_dest) ||
	    !mips_parse_fpu_binary_src_regs(fp, &fp_src))
		return 0;
	(void)mtc1_gpr;
	if (cvt_dst != mtc1_fpr || cvt_src != mtc1_fpr)
		return 0;
	cvt_regs = mips_load_dest_for_reg(MIPS_LOAD_FPR, cvt_dst,
	    cvt_double).regs | (1ULL << cvt_src);
	if ((load_dest.regs & cvt_regs) != 0)
		return 0;
	return (fp_src & load_dest.regs) != 0;
}

static int
mips_is_plain_jump(const char *line)
{
	char op[16];

	return mips_parse_opcode(line, op, sizeof(op)) && strcmp(op, "j") == 0;
}

static int
mips_is_plain_call(const char *line)
{
	char op[16];

	return mips_parse_opcode(line, op, sizeof(op)) && strcmp(op, "jal") == 0;
}

static int
mips_is_jump_delay_gpr_alu(const char *line)
{
	char op[16], tok[16];
	const char *s, *comment;
	int reg;

	if (!mips_parse_opcode(line, op, sizeof(op)) ||
	    (strcmp(op, "addiu") != 0 && strcmp(op, "addu") != 0))
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (!mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg) ||
	    !mips_skip_comma(&s) ||
	    !mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg) ||
	    !mips_skip_comma(&s))
		return 0;
	if (strcmp(op, "addiu") == 0)
		return mips_parse_tail_operand(s, tok, sizeof(tok), &comment);
	return mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg) &&
	    mips_line_ends_after_operands(s);
}

static int
mips_parse_base_offset_operand_reg(const char **sp, int *basep)
{
	char imm[64];
	const char *s, *start, *end;
	size_t len;
	int immval, reg;

	s = mips_skip_space(*sp);
	start = s;
	while (*s != '\0' && *s != '\n' && *s != '#' && *s != '(')
		++s;
	if (*s != '(')
		return 0;
	end = s;
	while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
		--end;
	len = (size_t)(end - start);
	if (len == 0 || len >= sizeof(imm))
		return 0;
	memcpy(imm, start, len);
	imm[len] = '\0';
	if (!mips_parse_signed_imm16(imm, &immval))
		return 0;
	++s;
	reg = mips_parse_gpr(s, &s);
	if (reg < 0 || *s != ')')
		return 0;
	*sp = s + 1;
	if (basep != NULL)
		*basep = reg;
	return 1;
}

static int
mips_parse_base_offset_operand(const char **sp)
{
	return mips_parse_base_offset_operand_reg(sp, NULL);
}

static int
mips_is_jump_delay_store(const char *line)
{
	char op[16];
	char tok[16];
	const char *s;
	int isfp, reg;

	if (!mips_parse_opcode(line, op, sizeof(op)))
		return 0;
	isfp = strcmp(op, "s.s") == 0 || strcmp(op, "s.d") == 0 ||
	    strcmp(op, "swc1") == 0 || strcmp(op, "sdc1") == 0;
	if (!isfp && strcmp(op, "sb") != 0 && strcmp(op, "sh") != 0 &&
	    strcmp(op, "sw") != 0 && strcmp(op, "sd") != 0)
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	if (isfp) {
		s = mips_skip_space(s);
		reg = mips_parse_fpr(s, &s);
		if (reg < 0)
			return 0;
	} else if (!mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg))
		return 0;
	return mips_skip_comma(&s) && mips_parse_base_offset_operand(&s) &&
	    mips_line_ends_after_operands(s);
}

static int
mips_is_jr_ra(const char *line)
{
	char op[16], tok[16];
	const char *s;
	int reg;

	if (!mips_parse_opcode(line, op, sizeof(op)) || strcmp(op, "jr") != 0)
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	return mips_parse_gpr_operand(&s, tok, sizeof(tok), &reg) &&
	    reg == 31 && mips_line_ends_after_operands(s);
}

static int
mips_is_frame_load_for_jr_delay(const char *line)
{
	char op[16], tok[16];
	const char *s;
	int dstreg, basereg;

	if (mips_cpu != MIPS_CPU_MIPS32R2)
		return 0;
	if (!mips_parse_opcode(line, op, sizeof(op)) || strcmp(op, "lw") != 0)
		return 0;
	s = mips_skip_space(line);
	s += strlen(op);
	return mips_parse_gpr_operand(&s, tok, sizeof(tok), &dstreg) &&
	    dstreg == 30 &&
	    mips_skip_comma(&s) &&
	    mips_parse_base_offset_operand_reg(&s, &basereg) &&
	    basereg == 29 &&
	    mips_line_ends_after_operands(s);
}

static int
mips_can_move_to_plain_jump_delay(const char *line)
{
	char dst[16], src[16];
	int dstreg, srcreg;

	return mips_parse_move_gprs(line, dst, sizeof(dst), src, sizeof(src),
	    &dstreg, &srcreg) || mips_is_jump_delay_gpr_alu(line) ||
	    (mips_cpu == MIPS_CPU_VR4300 && mips_is_jump_delay_store(line));
}

static int
mips_can_move_to_plain_control_delay(const char *line, const char *control)
{
	if (MIPS_FIX4300_ACTIVE && mips_is_vr4300_fp_mul(line))
		return 0;
	if (mips_is_jr_ra(control))
		return mips_is_frame_load_for_jr_delay(line);
	if (!mips_can_move_to_plain_jump_delay(line))
		return 0;
	if (mips_is_plain_jump(control))
		return 1;
	if (mips_is_plain_call(control))
		return !mips_line_touches_gpr(line, 1ULL << 31);
	return 0;
}

static int
mips_can_trim_load_nop(FILE *in, const char *next,
    const struct mips_load_dest *dest)
{
	char look[4096];
	long pos;
	int i;

	if (!mips_is_load_gap_insn(next, dest))
		return 0;
	if (mips_line_touches_load(next, dest)) {
		if (mips_cpu == MIPS_CPU_MIPS32R2)
			return 1;
		if (mips_cpu == MIPS_CPU_VR4300)
			return 1;
		return 0;
	}
	if (mips_cpu == MIPS_CPU_MIPS32R2)
		return 1;
	if (dest->kind == MIPS_LOAD_GPR)
		return 1;
	pos = ftell(in);
	if (pos == -1)
		return 0;
	for (i = 1; i < 2; i++) {
		if (fgets(look, sizeof(look), in) == NULL) {
			if (ferror(in))
				return 0;
			clearerr(in);
			if (fseek(in, pos, SEEK_SET) == -1)
				return 0;
			return 0;
		}
		if (!mips_is_instruction(look)) {
			if (fseek(in, pos, SEEK_SET) == -1)
				return 0;
			return 0;
		}
		if (mips_line_touches_load(look, dest) &&
		    (mips_cpu != MIPS_CPU_VR4300 ||
		    !mips_has_delay_slot(next))) {
			if (fseek(in, pos, SEEK_SET) == -1)
				return 0;
			return 0;
		}
	}
	if (fseek(in, pos, SEEK_SET) == -1)
		return 0;
	return 1;
}

static char *
mips_asm_temp_name(const char *path)
{
#ifdef _WIN32
	(void)path;
	return NULL;
#else
	const char *slash;
	char *tmp;
	size_t dirlen;
	int fd;

	slash = strrchr(path, '/');
	dirlen = slash != NULL ? (size_t)(slash - path + 1) : 0;
	tmp = xmalloc(dirlen + sizeof(".mipsasm.XXXXXX"));
	if (dirlen != 0)
		memcpy(tmp, path, dirlen);
	memcpy(tmp + dirlen, ".mipsasm.XXXXXX", sizeof(".mipsasm.XXXXXX"));
	fd = mkstemp(tmp);
	if (fd == -1) {
		free(tmp);
		return NULL;
	}
	close(fd);
	return tmp;
#endif
}

static int
mips_trim_load_delay_nops(char *path)
{
	char line[4096], next[4096], skipped[4][4096];
	FILE *in, *out;
	char *tmp;
	struct mips_load_dest pending_load;
	int have_next;
	int i;
	int nskipped;
	int overflow;
	int changed;
	int failed;

	tmp = mips_asm_temp_name(path);
	if (tmp == NULL)
		return 1;
	in = fopen(path, "r");
	if (in == NULL) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	out = fopen(tmp, "w");
	if (out == NULL) {
		fclose(in);
		unlink(tmp);
		free(tmp);
		return 1;
	}

	pending_load = mips_no_load_dest();
	changed = 0;
	while (fgets(line, sizeof(line), in) != NULL) {
		if (pending_load.kind != MIPS_LOAD_NONE && mips_is_nop(line)) {
			nskipped = 0;
			overflow = 0;
			if (fgets(next, sizeof(next), in) == NULL) {
				fputs(line, out);
				pending_load = mips_no_load_dest();
				break;
			}
			have_next = 1;
			while (mips_is_load_gap_skip_line(next)) {
				if (nskipped >=
				    (int)(sizeof(skipped) / sizeof(skipped[0]))) {
					overflow = 1;
					break;
				}
				memcpy(skipped[nskipped++], next,
				    sizeof(skipped[0]));
				if (fgets(next, sizeof(next), in) == NULL) {
					have_next = 0;
					break;
				}
			}
			if (!have_next || overflow) {
				fputs(line, out);
				for (i = 0; i < nskipped; i++)
					fputs(skipped[i], out);
				if (overflow)
					fputs(next, out);
				pending_load = mips_no_load_dest();
				if (!have_next)
					break;
				continue;
			}
			if (!mips_can_trim_load_nop(in, next, &pending_load)) {
				fputs(line, out);
			} else {
				changed = 1;
			}
			for (i = 0; i < nskipped; i++)
				fputs(skipped[i], out);
			fputs(next, out);
			pending_load = mips_load_dest(next);
			continue;
		}
		fputs(line, out);
		pending_load = mips_load_dest(line);
	}

	failed = ferror(in) || ferror(out);
	if (fclose(out) == EOF)
		failed = 1;
	if (fclose(in) == EOF)
		failed = 1;
	if (failed) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	if (changed) {
		if (rename(tmp, path) == -1) {
			unlink(tmp);
			free(tmp);
			return 1;
		}
	} else {
		unlink(tmp);
	}
	free(tmp);
	return 0;
}

static int
mips_repair_vr4300_multiply_errata(char *path, int warn_delay_slot)
{
	char line[4096];
	FILE *in, *out;
	char *tmp;
	int prev_delay_slot;
	int pending_fp_mul;
	int changed;
	int failed;
	int lineno;

	if (!MIPS_FIX4300_ACTIVE)
		return 0;

	tmp = mips_asm_temp_name(path);
	if (tmp == NULL)
		return 1;
	in = fopen(path, "r");
	if (in == NULL) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	out = fopen(tmp, "w");
	if (out == NULL) {
		fclose(in);
		unlink(tmp);
		free(tmp);
		return 1;
	}

	prev_delay_slot = 0;
	pending_fp_mul = 0;
	changed = 0;
	lineno = 0;
	while (fgets(line, sizeof(line), in) != NULL) {
		++lineno;
		if (!mips_is_instruction(line)) {
			fputs(line, out);
			continue;
		}
		if (prev_delay_slot && mips_is_vr4300_fp_mul(line) &&
		    warn_delay_slot)
			fprintf(stderr, "%s:%d: warning: VR4300 erratum: "
			    "mul.s/mul.d in branch delay slot may need a "
			    "separating instruction before a following "
			    "multiply\n", path, lineno);
		if (pending_fp_mul && mips_is_multiply(line)) {
			fputs("\tnop\t# VR4300 fp multiply erratum\n", out);
			changed = 1;
		}
		fputs(line, out);
		pending_fp_mul = mips_is_vr4300_fp_mul(line);
		prev_delay_slot = mips_has_delay_slot(line);
	}

	failed = ferror(in) || ferror(out);
	if (fclose(out) == EOF)
		failed = 1;
	if (fclose(in) == EOF)
		failed = 1;
	if (failed) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	if (changed) {
		if (rename(tmp, path) == -1) {
			unlink(tmp);
			free(tmp);
			return 1;
		}
	} else {
		unlink(tmp);
	}
	free(tmp);
	return 0;
}

static int
mips_fill_shift_load_delay_nops(char *path)
{
	char line[4096], load[4096], nop[4096], next[4096];
	char delay[4096];
	FILE *in, *out;
	char *tmp;
	const char *fill;
	long pos;
	int can_fill;
	int prev_delay_slot;
	int changed;
	int failed;

	tmp = mips_asm_temp_name(path);
	if (tmp == NULL)
		return 1;
	in = fopen(path, "r");
	if (in == NULL) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	out = fopen(tmp, "w");
	if (out == NULL) {
		fclose(in);
		unlink(tmp);
		free(tmp);
		return 1;
	}

	prev_delay_slot = 0;
	changed = 0;
	while (fgets(line, sizeof(line), in) != NULL) {
		if (!prev_delay_slot && mips_is_instruction(line)) {
			pos = ftell(in);
			if (pos != -1 && fgets(load, sizeof(load), in) != NULL) {
				if (fgets(nop, sizeof(nop), in) != NULL &&
				    fgets(next, sizeof(next), in) != NULL &&
				    mips_is_nop(nop)) {
					fill = line;
					can_fill =
					    mips_can_fill_shift_load_delay(line,
					    load, next) ||
					    mips_can_fill_move_load_delay(line,
					    load, next) ||
					    mips_can_fill_gpr_alu_fpu_load_delay(
					    line, load, next);
					if (!can_fill &&
					    mips_can_fill_li_load_delay(line,
					    load, next, delay, sizeof(delay))) {
						fill = delay;
						can_fill = 1;
					}
					if (can_fill) {
						fputs(load, out);
						fputs(fill, out);
						fputs(next, out);
						prev_delay_slot =
						    mips_has_delay_slot(next);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos, SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
			} else if (pos != -1) {
				if (ferror(in)) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
			}
		}
		fputs(line, out);
		if (mips_is_instruction(line))
			prev_delay_slot = mips_has_delay_slot(line);
	}

	failed = ferror(in) || ferror(out);
	if (fclose(out) == EOF)
		failed = 1;
	if (fclose(in) == EOF)
		failed = 1;
	if (failed) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	if (changed) {
		if (rename(tmp, path) == -1) {
			unlink(tmp);
			free(tmp);
			return 1;
		}
	} else {
		unlink(tmp);
	}
	free(tmp);
	return 0;
}

static int
mips_fold_late_peepholes(char *path)
{
	char line[4096], next[4096], after[4096], mf[4096], mtc1[4096];
	char mtc1nop[4096], cvt[4096], folded[4096];
	FILE *in, *out;
	const char *delay;
	char *tmp;
	long pos;
	long pos2;
	int prev_control;
	int changed;
	int failed;

	tmp = mips_asm_temp_name(path);
	if (tmp == NULL)
		return 1;
	in = fopen(path, "r");
	if (in == NULL) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	out = fopen(tmp, "w");
	if (out == NULL) {
		fclose(in);
		unlink(tmp);
		free(tmp);
		return 1;
	}

	prev_control = 0;
	changed = 0;
	while (fgets(line, sizeof(line), in) != NULL) {
		if (!prev_control) {
			pos = ftell(in);
			if (pos != -1 && fgets(next, sizeof(next), in) != NULL) {
				if (mips_cpu == MIPS_CPU_MIPS32R2 &&
				    mips_is_fpu_compare(line) &&
				    mips_is_nop(next)) {
					if (fgets(after, sizeof(after), in) != NULL) {
						if (mips_is_fpu_branch(after)) {
							fputs(line, out);
							fputs(after, out);
							prev_control = 1;
							changed = 1;
							continue;
						}
					} else {
						if (ferror(in)) {
							fclose(out);
							fclose(in);
							unlink(tmp);
							free(tmp);
							return 1;
						}
						clearerr(in);
					}
					if (fseek(in, pos, SEEK_SET) == -1) {
						fclose(out);
						fclose(in);
						unlink(tmp);
						free(tmp);
						return 1;
					}
				}
				if (mips_fold_move_shift_line(line, next,
				    folded, sizeof(folded)) ||
				    mips_fold_move_addiu_line(line, next,
				    folded, sizeof(folded))) {
					fputs(folded, out);
					prev_control = mips_is_control_transfer(folded);
					changed = 1;
					continue;
				}
				pos2 = ftell(in);
				if (pos2 != -1 &&
				    fgets(after, sizeof(after), in) != NULL) {
					if (mips_is_nop(next) &&
					    mips_can_trim_mtc1_cvt_nop(line,
					    after)) {
						fputs(line, out);
						fputs(after, out);
						prev_control =
						    mips_is_control_transfer(
						    after);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos2 != -1 ? pos2 : pos,
				    SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
				pos2 = ftell(in);
				if (pos2 != -1 &&
				    fgets(after, sizeof(after), in) != NULL &&
				    fgets(mf, sizeof(mf), in) != NULL &&
				    fgets(mtc1, sizeof(mtc1), in) != NULL) {
					if (mips_is_nop(next) &&
					    mips_is_nop(after) &&
					    mips_can_trim_mfhilo_post_nops(line,
					    mf, mtc1)) {
						fputs(line, out);
						fputs(mf, out);
						fputs(mtc1, out);
						prev_control =
						    mips_is_control_transfer(
						    mtc1);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos2 != -1 ? pos2 : pos,
				    SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
				pos2 = ftell(in);
				if (pos2 != -1 &&
				    fgets(after, sizeof(after), in) != NULL &&
				    fgets(mf, sizeof(mf), in) != NULL &&
				    fgets(mtc1, sizeof(mtc1), in) != NULL &&
				    fgets(mtc1nop, sizeof(mtc1nop), in) != NULL &&
				    fgets(cvt, sizeof(cvt), in) != NULL) {
					if (mips_is_nop(next) &&
					    mips_is_nop(after) &&
					    mips_is_nop(mtc1nop) &&
					    mips_can_fill_hilo_mtc1_delay(line,
					    mf, mtc1, cvt)) {
						fputs(line, out);
						fputs(mtc1, out);
						fputs(next, out);
						fputs(mf, out);
						fputs(cvt, out);
						prev_control =
						    mips_is_control_transfer(cvt);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos2 != -1 ? pos2 : pos,
				    SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
				pos2 = ftell(in);
				if (pos2 != -1 &&
				    fgets(after, sizeof(after), in) != NULL &&
				    fgets(mf, sizeof(mf), in) != NULL &&
				    fgets(mtc1, sizeof(mtc1), in) != NULL &&
				    fgets(mtc1nop, sizeof(mtc1nop), in) != NULL &&
				    fgets(cvt, sizeof(cvt), in) != NULL) {
					if (mips_is_nop(next) &&
					    mips_is_nop(after) &&
					    mips_can_fill_hilo_lw_delay2(line,
					    mf, mtc1, mtc1nop, cvt)) {
						fputs(line, out);
						fputs(cvt, out);
						fputs(after, out);
						fputs(mf, out);
						fputs(mtc1, out);
						fputs(mtc1nop, out);
						prev_control =
						    mips_is_control_transfer(
						    mtc1nop);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos2 != -1 ? pos2 : pos,
				    SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
				pos2 = ftell(in);
				if (pos2 != -1 &&
				    fgets(after, sizeof(after), in) != NULL &&
				    fgets(mf, sizeof(mf), in) != NULL &&
				    fgets(mtc1, sizeof(mtc1), in) != NULL &&
				    fgets(mtc1nop, sizeof(mtc1nop), in) != NULL) {
					if (mips_is_nop(next) &&
					    mips_is_nop(after) &&
					    mips_can_fill_hilo_lw_delay(line,
					    mf, mtc1, mtc1nop)) {
						fputs(line, out);
						fputs(mtc1nop, out);
						fputs(after, out);
						fputs(mf, out);
						fputs(mtc1, out);
						prev_control =
						    mips_is_control_transfer(
						    mtc1);
						changed = 1;
						continue;
					}
					if (mips_is_nop(next) &&
					    mips_is_nop(after) &&
					    mips_can_fill_hilo_post_mflo_lw_delay(
					    line, mf, mtc1, mtc1nop)) {
						fputs(line, out);
						fputs(mtc1, out);
						fputs(after, out);
						fputs(mf, out);
						fputs(mtc1nop, out);
						prev_control =
						    mips_is_control_transfer(
						    mtc1nop);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos2 != -1 ? pos2 : pos,
				    SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
				pos2 = ftell(in);
				if (pos2 != -1 &&
				    fgets(after, sizeof(after), in) != NULL &&
				    fgets(mf, sizeof(mf), in) != NULL &&
				    fgets(mtc1, sizeof(mtc1), in) != NULL &&
				    fgets(mtc1nop, sizeof(mtc1nop), in) != NULL) {
					if (mips_is_nop(next) &&
					    mips_is_nop(mtc1) &&
					    mips_can_fill_mtc1_cvt_fpu_load_delay(
					    line, after, mf, mtc1nop)) {
						fputs(line, out);
						fputs(mf, out);
						fputs(after, out);
						fputs(mtc1nop, out);
						prev_control =
						    mips_is_control_transfer(
						    mtc1nop);
						changed = 1;
						continue;
					}
				}
				if (ferror(in) ||
				    fseek(in, pos2 != -1 ? pos2 : pos,
				    SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
				delay = NULL;
				if (mips_can_move_to_plain_control_delay(line,
				    next))
					delay = line;
				else if (mips_fold_li_addiu_line(line, folded,
				    sizeof(folded)) &&
				    mips_can_move_to_plain_control_delay(folded,
				    next))
					delay = folded;
				if (delay != NULL) {
					pos2 = ftell(in);
					if (pos2 != -1 &&
					    fgets(after, sizeof(after), in) != NULL) {
						if (mips_is_nop(after)) {
							fputs(next, out);
							fputs(delay, out);
							prev_control = 0;
							changed = 1;
							continue;
						}
					} else if (pos2 != -1) {
						if (ferror(in)) {
							fclose(out);
							fclose(in);
							unlink(tmp);
							free(tmp);
							return 1;
						}
						clearerr(in);
					}
				}
				if (fseek(in, pos, SEEK_SET) == -1) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
			} else if (pos != -1) {
				if (ferror(in)) {
					fclose(out);
					fclose(in);
					unlink(tmp);
					free(tmp);
					return 1;
				}
				clearerr(in);
			}
		}
		fputs(line, out);
		if (mips_is_instruction(line))
			prev_control = mips_is_control_transfer(line);
	}

	failed = ferror(in) || ferror(out);
	if (fclose(out) == EOF)
		failed = 1;
	if (fclose(in) == EOF)
		failed = 1;
	if (failed) {
		unlink(tmp);
		free(tmp);
		return 1;
	}
	if (changed) {
		if (rename(tmp, path) == -1) {
			unlink(tmp);
			free(tmp);
			return 1;
		}
	} else {
		unlink(tmp);
	}
	free(tmp);
	return 0;
}

static int
mips_postprocess_asm(char *path)
{
	if (mips_cpu == MIPS_CPU_VR4300 && mips_fill_shift_load_delay_nops(path))
		return 1;
	if (mips_trim_load_delay_nops(path))
		return 1;
	if (mips_fold_late_peepholes(path))
		return 1;
	if (mips_cpu == MIPS_CPU_VR4300 && mips_trim_load_delay_nops(path))
		return 1;
	return mips_repair_vr4300_multiply_errata(path, 1);
}

static int
mips_copy_file(char *input, char *output)
{
	char buf[8192];
	FILE *in, *out;
	size_t n;
	int failed;

	in = fopen(input, "r");
	if (in == NULL)
		return 1;
	out = fopen(output, "w");
	if (out == NULL) {
		fclose(in);
		return 1;
	}

	failed = 0;
	while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
		if (fwrite(buf, 1, n, out) != n) {
			failed = 1;
			break;
		}
	if (ferror(in))
		failed = 1;
	if (fclose(out) == EOF)
		failed = 1;
	if (fclose(in) == EOF)
		failed = 1;
	return failed;
}

static int
mips_prepare_asm_input(char *input, char **outputp)
{
	char *fixed;

	if (!MIPS_FIX4300_ACTIVE)
		return 0;
	fixed = gettmp();
	strlist_append(&temp_outputs, fixed);
	if (mips_copy_file(input, fixed))
		return 1;
	if (mips_repair_vr4300_multiply_errata(fixed, 1))
		return 1;
	*outputp = fixed;
	return 0;
}
#endif

static int
assemble_input(char *input, char *output)
{
	struct strlist args;
	int retval;

	strlist_init(&args);
#ifdef PCC_EARLY_AS_ARGS
	PCC_EARLY_AS_ARGS
#endif
	strlist_append_list(&args, &assembler_flags);
	strlist_append(&args, "-o");
	strlist_append(&args, output);
	strlist_append(&args, input);
	strlist_prepend(&args,
	    find_file(as, &progdirs, X_OK));
#ifdef PCC_LATE_AS_ARGS
	PCC_LATE_AS_ARGS
#endif
	retval = strlist_exec(&args);
	strlist_free(&args);
	return retval;
}

static int
preprocess_input(char *input, char *output, int dodep)
{
	struct strlist args;
	struct string *s;
	int retval;

	strlist_init(&args);
	strlist_append_list(&args, &preprocessor_flags);
	if (ascpp) {
		strlist_append(&args, "-A");
		strlist_append(&args, "-D__ASSEMBLER__"); 
	}
	STRLIST_FOREACH(s, &includes) {
		strlist_append(&args, "-i");
		strlist_append(&args, s->value);
	}
	STRLIST_FOREACH(s, &incdirs) {
		strlist_append(&args, "-I");
		strlist_append(&args, s->value);
	}
	STRLIST_FOREACH(s, &user_sysincdirs) {
		strlist_append(&args, "-S");
		strlist_append(&args, s->value);
	}
	if (!nostdinc) {
		STRLIST_FOREACH(s, &sysincdirs) {
			strlist_append(&args, "-S");
			strlist_append(&args, s->value);
		}
	}
	STRLIST_FOREACH(s, &dirafterdirs) {
		strlist_append(&args, "-S");
		strlist_append(&args, s->value);
	}
	if (dodep)
		strlist_append_list(&args, &depflags);
	strlist_append(&args, input);
	if (output)
		strlist_append(&args, output);

	strlist_prepend(&args, find_file(passp, &progdirs, X_OK));
	retval = strlist_exec(&args);
	strlist_free(&args);
	return retval;
}

static int
run_linker(void)
{
	struct strlist linker_flags;
	int retval;

#ifdef PCC_EARLY_LD_ARGS
	PCC_EARLY_LD_ARGS
#endif

	if (outfile) {
		strlist_prepend(&early_linker_flags, outfile);
		strlist_prepend(&early_linker_flags, "-o");
	}

	strlist_init(&linker_flags);
	strlist_append_list(&linker_flags, &early_linker_flags);
	strlist_append_list(&linker_flags, &middle_linker_flags);
	strlist_append_list(&linker_flags, &late_linker_flags);
	strlist_prepend(&linker_flags, find_file(ld, &progdirs, X_OK));

	retval = strlist_exec(&linker_flags);

	strlist_free(&linker_flags);
	return retval;
}

static char *
select_linker(char *name)
{
	static char ld_name[8];
 
	/* Short names first.  */
	if (strcmp(name, "bfd") == 0 ||
	    strcmp(name, "gold") == 0 ||
	    strcmp(name, "lld") == 0) {
		snprintf(ld_name, sizeof ld_name, "ld.%s", name);
		return ld_name;
	}
 
	/* Must be absolute path otherwise.  */
	if (name[0] != '/')
		return LINKER;
 
	return name;
}



static char *cxxt[] = { "cc", "cp", "cxx", "cpp", "CPP", "c++", "C" };
int
cxxsuf(char *s)
{
	unsigned i;
	for (i = 0; i < sizeof(cxxt)/sizeof(cxxt[0]); i++)
		if (strcmp(s, cxxt[i]) == 0)
			return 1;
	return 0;
}

char *
getsufp(char *s)
{
	register char *p;

	if ((p = strrchr(s, '.')) && p[1] != '\0')
		return &p[1];
	return "";
}

int
getsuf(char *s)
{
	register char *p;

	if ((p = strrchr(s, '.')) && p[1] != '\0' && p[2] == '\0')
		return p[1];
	return(0);
}

/*
 * Get basename of string s, copy it and change its suffix to ch.
 */
char *
setsuf(char *s, char ch)
{
	char *e, *p, *rp;

	e = NULL;
	for (p = s; *p; p++) {
		if (*p == '/')
			s = p + 1;
		if (*p == '.')
			e = p;
	}
	if (s > e)
		e = p;

	rp = p = xmalloc(e - s + 3);
	while (s < e)
		*p++ = *s++;

	*p++ = '.';
	*p++ = ch;
	*p = '\0';
	return rp;
}

#ifdef _WIN32

static int
strlist_exec(struct strlist *l)
{
	char *cmd;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD exitCode;
	BOOL ok;

	cmd = win32commandline(l);
	if (vflag)
		printf("%s\n", cmd);
	if (noexec)
		return 0;

	ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

	ok = CreateProcess(NULL,  // the executable program
		cmd,   // the command line arguments
		NULL,       // ignored
		NULL,       // ignored
		TRUE,       // inherit handles
		HIGH_PRIORITY_CLASS,
		NULL,       // ignored
		NULL,       // ignored
		&si,
		&pi);

	if (!ok)
		errorx(100, "Can't find %s\n", STRLIST_FIRST(l)->value);

	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return (exitCode != 0);
}

#else

static int
strlist_exec(struct strlist *l)
{
	sig_atomic_t exit_now = 0;
	sig_atomic_t child;
	char **argv;
	size_t argc;
	ssize_t result;
	int rv;
	int status;

	strlist_make_array(l, &argv, &argc);
	if (vflag) {
		printf("Calling ");
		strlist_print(l, stdout, noexec, " ");
		printf("\n");
	}
	if (noexec)
		return 0;

	switch ((child = fork())) {
	case 0:
		execvp(argv[0], argv);
		result = write(STDERR_FILENO, "Exec of ", 8);
		result = write(STDERR_FILENO, argv[0], strlen(argv[0]));
		result = write(STDERR_FILENO, " failed\n", 8);
		(void)result;
		_exit(127);
	case -1:
		errorx(1, "fork failed");
	default:
		while ((rv = waitpid(child, &status, 0)) == -1 && errno == EINTR)
			/* nothing */(void)0;
		if (rv == -1)
			errorx(1, "waitpid failed");
		if (WIFSIGNALED(status))
			errorx(1, "%s terminated with signal %d",
			    argv[0], WTERMSIG(status));
		if (!WIFEXITED(status))
			errorx(1, "%s terminated abnormally", argv[0]);
		rv = WEXITSTATUS(status);
		if (rv)
			errorx(1, "%s terminated with status %d", argv[0], rv);
		while (argc-- > 0)
			free(argv[argc]);
		free(argv);
		break;
	}
	return exit_now;
}

#endif

/*
 * Catenate two (optional) strings together
 */
char *
cat(const char *a, const char *b)
{
	size_t len;
	char *rv;

	len = (a ? strlen(a) : 0) + (b ? strlen(b) : 0) + 1;
	rv = xmalloc(len);
	snprintf(rv, len, "%s%s", (a ? a : ""), (b ? b : ""));
	return rv;
}

static char *
cat_sysroot(const char *root, const char *path)
{
	size_t root_len, path_len, copy_path_len;
	int need_slash, skip_path_slash;
	char *rv;

	if (root == NULL || root[0] == '\0')
		root = "/";
	if (path == NULL)
		path = "";

	root_len = strlen(root);
	path_len = strlen(path);
	need_slash = 0;
	skip_path_slash = 0;

	if (root_len == 1 && root[0] == '/' && path[0] == '/') {
		root_len = 0;
	} else if (root_len != 0 && root[root_len - 1] == '/' &&
	    path[0] == '/') {
		skip_path_slash = 1;
	} else if (root_len != 0 && root[root_len - 1] != '/' &&
	    path[0] != '\0' && path[0] != '/') {
		need_slash = 1;
	}

	copy_path_len = path_len - skip_path_slash;
	rv = xmalloc(root_len + need_slash + copy_path_len + 1);
	memcpy(rv, root, root_len);
	if (need_slash)
		rv[root_len++] = '/';
	memcpy(rv + root_len, path + skip_path_slash, copy_path_len);
	rv[root_len + copy_path_len] = '\0';
	return rv;
}

int
cunlink(char *f)
{
	if (f==0 || Xflag)
		return(0);
	return (unlink(f));
}

#ifdef _WIN32
char *
gettmp(void)
{
	DWORD pathSize;
	char pathBuffer[MAX_PATH + 1];
	char tempFilename[MAX_PATH];
	UINT uniqueNum;

	pathSize = GetTempPath(sizeof(pathBuffer), pathBuffer);
	if (pathSize == 0 || pathSize > sizeof(pathBuffer))
		pathBuffer[0] = '\0';
	uniqueNum = GetTempFileName(pathBuffer, "ctm", 0, tempFilename);
	if (uniqueNum == 0)
		errorx(8, "GetTempFileName failed: path \"%s\"", pathBuffer);

	return xstrdup(tempFilename);
}

#else

char *
gettmp(void)
{
	const char *tmpdir;
	size_t len;
	char *sfn;
	int fd = -1;

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || tmpdir[0] == '\0')
		tmpdir = "/tmp";
	len = strlen(tmpdir);
	sfn = xmalloc(len + sizeof("/ctm.XXXXXX"));
	snprintf(sfn, len + sizeof("/ctm.XXXXXX"), "%s%sctm.XXXXXX",
	    tmpdir, tmpdir[len - 1] == '/' ? "" : "/");
	if ((fd = mkstemp(sfn)) == -1)
		errorx(8, "%s: %s\n", sfn, strerror(errno));
	close(fd);
	return sfn;
}
#endif

static void
expand_sysroot(void)
{
	struct string *s;
	struct strlist *lists[] = { &crtdirs, &sysincdirs, &incdirs,
	    &user_sysincdirs, &libdirs, &progdirs, &dirafterdirs, NULL };
	const char *sysroots[] = { sysroot, isysroot, isysroot, isysroot,
	    sysroot, sysroot, isysroot, NULL };
	size_t i;
	char *path;

	assert(sizeof(lists) / sizeof(lists[0]) ==
	       sizeof(sysroots) / sizeof(sysroots[0]));

	for (i = 0; lists[i] != NULL; ++i) {
		STRLIST_FOREACH(s, lists[i]) {
			if (s->value[0] != '=')
				continue;
			path = cat_sysroot(sysroots[i], s->value + 1);
			free(s->value);
			s->value = path;
		}
	}
}

void
oerror(char *s)
{
	errorx(8, "unknown option '%s'", s);
}

/*
 * See if m matches the beginning of string str, if it does return the
 * remaining of str, otherwise NULL.
 */
char *
argnxt(char *str, char *m)
{
	if (strncmp(str, m, strlen(m)))
		return NULL; /* No match */
	return str + strlen(m);
}

/*
 * Return next argument to option, or complain.
 */
char *
nxtopt(char *o)
{
	size_t l;

	if (o != NULL) {
		l = strlen(o);
		if (lav[0][l] != 0)
			return &lav[0][l];
	}
	if (lac == 1)
		errorx(8, "missing argument to '%s'", o);
	lav++;
	lac--;
	return lav[0];
}

struct flgcheck {
	int *flag;
	int set;
	char *def;
} cppflgcheck[] = {
	{ &vflag, 1, "-v" },
	{ &c99defs, 1, "-D__STDC_VERSION__=199901L" },
	{ &c11defs, 1, "-D__STDC_VERSION__=201112L" },
	{ &c89defs, 1, "-D__STDC__=1" },
	{ &freestanding, 1, "-D__STDC_HOSTED__=0" },
	{ &freestanding, 0, "-D__STDC_HOSTED__=1" },
	{ &cxxflag, 1, "-D__cplusplus" },
	{ &xuchar, 1, "-D__CHAR_UNSIGNED__" },
	{ &sspflag, 1, "-D__SSP__" },
	{ &pthreads, 1, "-D_PTHREADS" },
	{ &Oflag, 1, "-D__OPTIMIZE__" },
	{ &tflag, 1, "-t" },
	{ &kflag, 1, "-D__PIC__" },
	{ 0 },
};

static void
cksetflags(struct flgcheck *fs, struct strlist *sl, int which)
{
	void (*fn)(struct strlist *, const char *);

	fn = which == 'p' ? strlist_prepend : strlist_append;
	for (; fs->flag; fs++) {
		if (fs->set && *fs->flag)
			fn(sl, fs->def);
		if (!fs->set && !*fs->flag)
			fn(sl, fs->def);
	}
}

#ifndef TARGET_LE
#define TARGET_LE       1
#define TARGET_BE       2
#define TARGET_PDP      3
#define TARGET_ANY      4
#endif

static char *defflags[] = {
	"-D__PCC__=" MKS(PCC_MAJOR),
	"-D__PCC_MINOR__=" MKS(PCC_MINOR),
	"-D__PCC_MINORMINOR__=" MKS(PCC_MINORMINOR),
	"-D__VERSION__=" MKS(VERSSTR),
	"-D__SCHAR_MAX__=" MKS(MAX_CHAR),
	"-D__SHRT_MAX__=" MKS(MAX_SHORT),
	"-D__INT_MAX__=" MKS(MAX_INT),
	"-D__LONG_MAX__=" MKS(MAX_LONG),
	"-D__LONG_LONG_MAX__=" MKS(MAX_LONGLONG),

	"-D__STDC_ISO_10646__=200009L",
	"-D__WCHAR_TYPE__=" WCT,
	"-D__SIZEOF_WCHAR_T__=" MKS(WCHAR_SIZE),
	"-D__WCHAR_MAX__=" WCM,
	"-D__WINT_TYPE__=" PCC_WINT_TYPE,
	"-D__SIZE_TYPE__=" PCC_SIZE_TYPE,
	"-D__PTRDIFF_TYPE__=" PCC_PTRDIFF_TYPE,
	"-D__SIZEOF_WINT_T__=4",
	"-D__ORDER_LITTLE_ENDIAN__=1234",
	"-D__ORDER_BIG_ENDIAN__=4321",
	"-D__ORDER_PDP_ENDIAN__=3412",
#ifndef NO_C11
	"-D__STDC_UTF_16__=1",
	"-D__STDC_UTF_32__=1",
	"-D__STDC_NO_ATOMICS__=1",
	"-D__STDC_NO_THREADS__=1",
#endif

/*
 * These should probably be changeable during runtime...
 */
#if TARGET_ENDIAN == TARGET_BE
	"-D__FLOAT_WORD_ORDER__=__ORDER_BIG_ENDIAN__",
	"-D__BYTE_ORDER__=__ORDER_BIG_ENDIAN__",
#elif TARGET_ENDIAN == TARGET_PDP
	"-D__FLOAT_WORD_ORDER__=__ORDER_PDP_ENDIAN__",
	"-D__BYTE_ORDER__=__ORDER_PDP_ENDIAN__",
#elif TARGET_ENDIAN == TARGET_LE
	"-D__FLOAT_WORD_ORDER__=__ORDER_LITTLE_ENDIAN__",
	"-D__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__",
#else
#error Unknown endian...
#endif
};

static char *gcppflags[] = {
#ifndef os_win32
#ifdef GCC_COMPAT
	"-D__GNUC__=4",
	"-D__GNUC_MINOR__=3",
	"-D__GNUC_PATCHLEVEL__=1",
	"-D__REGISTER_PREFIX__=" REGISTER_PREFIX,
	"-D__USER_LABEL_PREFIX__=" USER_LABEL_PREFIX,
#if SZLONG == 64
	"-D__SIZEOF_LONG__=8",
#elif SZLONG == 32
	"-D__SIZEOF_LONG__=4",
#endif
#if SZPOINT(CHAR) == 64
	"-D__SIZEOF_POINTER__=8",
#elif SZPOINT(CHAR) == 32
	"-D__SIZEOF_POINTER__=4",
#endif
#endif
#endif
	NULL
};

/* Use floating point definitions form softfloat.h */

static char *fpflags[] = {
#ifdef TARGET_FLT_EVAL_METHOD
	"-D__FLT_EVAL_METHOD__=" MKS(TARGET_FLT_EVAL_METHOD),
#endif
#ifdef mach_i386
	"-D__STDC_IEC_559__",
#endif
#ifdef FLT_PREFIX
	"-D__FLT_RADIX__=" MKS(C(FLT_PREFIX,_RADIX)),
	"-D__FLT_DIG__=" MKS(C(FLT_PREFIX,_DIG)),
	"-D__FLT_EPSILON__=" MKS(C(FLT_PREFIX,_EPSILON)),
	"-D__FLT_MANT_DIG__=" MKS(C(FLT_PREFIX,_MANT_DIG)),
	"-D__FLT_MAX_10_EXP__=" MKS(C(FLT_PREFIX,_MAX_10_EXP)),
	"-D__FLT_MAX_EXP__=" MKS(C(FLT_PREFIX,_MAX_EXP)),
	"-D__FLT_MAX__=" MKS(C(FLT_PREFIX,_MAX)),
	"-D__FLT_MIN_10_EXP__=" MKS(C(FLT_PREFIX,_MIN_10_EXP)),
	"-D__FLT_MIN_EXP__=" MKS(C(FLT_PREFIX,_MIN_EXP)),
	"-D__FLT_MIN__=" MKS(C(FLT_PREFIX,_MIN)),
	"-D__FLT_HAS_SUBNORM__=" MKS(C(FLT_PREFIX,_HAS_SUBNORM)),
	"-D__FLT_TRUE_MIN__=" MKS(C(FLT_PREFIX,_TRUE_MIN)),
#endif
#ifdef DBL_PREFIX
	"-D__DBL_DIG__=" MKS(C(DBL_PREFIX,_DIG)),
	"-D__DBL_EPSILON__=" MKS(C(DBL_PREFIX,_EPSILON)),
	"-D__DBL_MANT_DIG__=" MKS(C(DBL_PREFIX,_MANT_DIG)),
	"-D__DBL_MAX_10_EXP__=" MKS(C(DBL_PREFIX,_MAX_10_EXP)),
	"-D__DBL_MAX_EXP__=" MKS(C(DBL_PREFIX,_MAX_EXP)),
	"-D__DBL_MAX__=" MKS(C(DBL_PREFIX,_MAX)),
	"-D__DBL_MIN_10_EXP__=" MKS(C(DBL_PREFIX,_MIN_10_EXP)),
	"-D__DBL_MIN_EXP__=" MKS(C(DBL_PREFIX,_MIN_EXP)),
	"-D__DBL_MIN__=" MKS(C(DBL_PREFIX,_MIN)),
	"-D__DBL_HAS_SUBNORM__=" MKS(C(DBL_PREFIX,_HAS_SUBNORM)),
	"-D__DBL_TRUE_MIN__=" MKS(C(DBL_PREFIX,_TRUE_MIN)),
#endif
#ifdef LDBL_PREFIX
	"-D__LDBL_DIG__=" MKS(C(LDBL_PREFIX,_DIG)),
	"-D__LDBL_EPSILON__=" MKS(C(LDBL_PREFIX,_EPSILON)),
	"-D__LDBL_MANT_DIG__=" MKS(C(LDBL_PREFIX,_MANT_DIG)),
	"-D__LDBL_MAX_10_EXP__=" MKS(C(LDBL_PREFIX,_MAX_10_EXP)),
	"-D__LDBL_MAX_EXP__=" MKS(C(LDBL_PREFIX,_MAX_EXP)),
	"-D__LDBL_MAX__=" MKS(C(LDBL_PREFIX,_MAX)),
	"-D__LDBL_MIN_10_EXP__=" MKS(C(LDBL_PREFIX,_MIN_10_EXP)),
	"-D__LDBL_MIN_EXP__=" MKS(C(LDBL_PREFIX,_MIN_EXP)),
	"-D__LDBL_MIN__=" MKS(C(LDBL_PREFIX,_MIN)),
	"-D__LDBL_HAS_SUBNORM__=" MKS(C(LDBL_PREFIX,_HAS_SUBNORM)),
	"-D__LDBL_TRUE_MIN__=" MKS(C(LDBL_PREFIX,_TRUE_MIN)),
#endif
	NULL
};

/*
 * Configure the standard cpp flags.
 */
void
setup_cpp_flags(void)
{
	int i;

	/* a bunch of misc defines */
	for (i = 0; i < (int)sizeof(defflags)/(int)sizeof(char *); i++)
		strlist_prepend(&preprocessor_flags, defflags[i]);

	for (i = 0; gcppflags[i]; i++)
		strlist_prepend(&preprocessor_flags, gcppflags[i]);
	strlist_prepend(&preprocessor_flags, xgnu89 ?
	    "-D__GNUC_GNU_INLINE__" : "-D__GNUC_STDC_INLINE__");

	cksetflags(cppflgcheck, &preprocessor_flags, 'p');

	/* Create time and date defines */
	if (tflag == 0) {
		char buf[100]; /* larger than needed */
		time_t t = time(NULL);
		char *n = ctime(&t);
	
		n[19] = 0;
		snprintf(buf, sizeof buf, "-D__TIME__=\"%s\"", n+11);
		strlist_prepend(&preprocessor_flags, xstrdup(buf));

		n[24] = n[11] = 0;
		snprintf(buf, sizeof buf, "-D__DATE__=\"%s%s\"", n+4, n+20);
		strlist_prepend(&preprocessor_flags, xstrdup(buf));
	}

	for (i = 0; fpflags[i]; i++)
		strlist_prepend(&preprocessor_flags, fpflags[i]);

	for (i = 0; cppadd[i]; i++)
		strlist_prepend(&preprocessor_flags, cppadd[i]);
	for (i = 0; cppmdadd[i]; i++)
		strlist_prepend(&preprocessor_flags, cppmdadd[i]);
#ifdef PCC_SETUP_CPP_ARGS
	PCC_SETUP_CPP_ARGS
#endif

	/* Include dirs */
	strlist_append(&sysincdirs, "=" INCLUDEDIR "pcc/");
#ifdef PCCINCDIR
	if (cxxflag)
		strlist_append(&sysincdirs, "=" PCCINCDIR "/c++");
	strlist_append(&sysincdirs, "=" PCCINCDIR);
#endif
	for (i = 0; stdincs[i]; i++)
		strlist_append(&sysincdirs, stdincs[i]);
}

struct flgcheck ccomflgcheck[] = {
	{ &Oflag, 1, "-xtemps" },
	{ &Oflag, 1, "-xdeljumps" },
#ifndef PCC_DISABLE_AUTO_XINLINE
	{ &Oflag, 1, "-xinline" },
#endif
	{ &Oflag, 1, "-xdce" },
	{ &Oflag, 1, "-xssa" },
	{ &freestanding, 1, "-ffreestanding" },
	{ &pgflag, 1, "-p" },
	{ &gflag, 1, "-g" },
	{ &xgnu89, 1, "-xgnu89" },
	{ &xgnu99, 1, "-xgnu99" },
	{ &xuchar, 1, "-xuchar" },
	{ &omit_frame_pointer, 1, "-fomit-frame-pointer" },
#if !defined(os_sunos) && !defined(mach_i386)
	{ &vflag, 1, "-v" },
#endif
#if defined(os_darwin)
	{ &Bstatic, 0, "-k" },
#elif defined(os_sunos) && defined(mach_i386)
	{ &kflag, 1, "-K" },
	{ &kflag, 1, "pic" },
#else
	{ &kflag, 1, "-k" },
#endif
	{ &sspflag, 1, "-fstack-protector" },
	{ 0 }
};

void
setup_ccom_flags(void)
{

	cksetflags(ccomflgcheck, &compiler_flags, 'a');
}

int one = 1;

struct flgcheck asflgcheck[] = {
#if defined(USE_YASM)
	{ &one, 1, "-w" },
	{ &one, 1, "-p" },
	{ &one, 1, "gnu" },
	{ &one, 1, "-f" },
#if defined(os_win32)
	{ &one, 1, "win32" },
#elif defined(os_darwin)
	{ &one, 1, "macho" },
#else
#if defined(mach_amd64)
	{ &one, 1, "elf64" },
#else
	{ &one, 1, "elf" },
#endif
#endif
#endif
#if defined(os_sunos) && defined(mach_sparc64)
	{ &one, 1, "-m64" },
#endif
#if defined(os_darwin)
	{ &Bstatic, 1, "-static" },
#endif
#if !defined(USE_YASM) && !defined(NO_AS_V)
	{ &vflag, 1, "-v" },
#endif
#if defined(os_openbsd) && defined(mach_mips64)
	{ &kflag, 1, "-KPIC" },
#else
#if !defined(USE_YASM)
	{ &kflag, 1, "-k" },
#endif
#endif
#ifdef TARGET_ASFLAGS
	TARGET_ASFLAGS
#endif
	{ 0 }
};
void
setup_as_flags(void)
{
#ifdef PCC_SETUP_AS_ARGS
	PCC_SETUP_AS_ARGS
#endif
	cksetflags(asflgcheck, &assembler_flags, 'a');
}

struct flgcheck ldflgcheck[] = {
#if !defined(MSLINKER) && !defined(os_sunos)
	{ &vflag, 1, "-v" },
#endif
#ifdef os_darwin
	{ &shared, 1, "-dylib" },
#elif defined(os_win32)
	{ &shared, 1, "-Bdynamic" },
#else
	{ &shared, 1, "-shared" },
#endif
#if !defined(os_sunos) && !defined(os_win32)
#ifndef os_darwin
	{ &shared, 0, "-d" },
#endif
#endif
#ifdef os_darwin
	{ &Bstatic, 1, "-static" },
#else
	{ &Bstatic, 1, "-Bstatic" },
#endif
#if !defined(os_darwin) && !defined(os_sunos)
	{ &gflag, 1, "-g" },
#endif
	{ &pthreads, 1, "-lpthread" },
	{ 0 },
};

static void
strap(struct strlist *sh, struct strlist *cd, char *n, int where)
{
	void (*fn)(struct strlist *, const char *);
	char *fil;

	if (n == 0)
		return; /* no crtfile */

	fn = where == 'p' ? strlist_prepend : strlist_append;
	fil = find_file(n, cd, R_OK);
	(*fn)(sh, fil);
}

void
setup_ld_flags(void)
{
	char *b, *e;
	int i;

#ifdef PCC_SETUP_LD_ARGS
	PCC_SETUP_LD_ARGS
#endif

	cksetflags(ldflgcheck, &early_linker_flags, 'a');
	if (Bstatic == 0 && shared == 0 && rflag == 0) {
		if (dynlinklib) {
			strlist_append(&early_linker_flags, dynlinkarg);
			strlist_append(&early_linker_flags, dynlinklib);
		}
#ifndef os_darwin
		strlist_append(&early_linker_flags, "-e");
		strlist_append(&early_linker_flags, STARTLABEL);
#endif
	}
	if (shared == 0 && rflag)
		strlist_append(&early_linker_flags, "-r");
#ifdef STARTLABEL_S
	if (shared == 1) {
		strlist_append(&early_linker_flags, "-e");
		strlist_append(&early_linker_flags, STARTLABEL_S);
	}
#endif
	if (sysroot && *sysroot)
		strlist_append(&early_linker_flags, cat("--sysroot=", sysroot));
	if (!nostdlib) {
		/* library search paths */
		if (pcclibdir)
			strlist_append(&late_linker_flags,
			    cat("-L", pcclibdir));
		for (i = 0; deflibdirs[i]; i++)
			strlist_append(&late_linker_flags,
			    cat("-L", deflibdirs[i]));
		/* standard libraries */
		if (pgflag) {
			for (i = 0; defproflibs[i]; i++)
				strlist_append(&late_linker_flags,
				     defproflibs[i]);
		} else if (cxxflag) {
			for (i = 0; defcxxlibs[i]; i++)
				strlist_append(&late_linker_flags,
				    defcxxlibs[i]);
		} else {
			for (i = 0; deflibs[i]; i++)
				strlist_append(&late_linker_flags, deflibs[i]);
		}
	}
	if (!nostartfiles) {
		if (Bstatic) {
			b = CRTBEGIN_T;
			e = CRTEND_T;
		} else if (shared /* || pieflag */) {
			b = CRTBEGIN_S;
			e = CRTEND_S;
		} else {
			b = CRTBEGIN;
			e = CRTEND;
		}
		strap(&middle_linker_flags, &crtdirs, b, 'p');
		strap(&late_linker_flags, &crtdirs, e, 'a');
		strap(&middle_linker_flags, &crtdirs, CRTI, 'p');
		strap(&late_linker_flags, &crtdirs, CRTN, 'a');
#ifdef os_win32
		/*
		 * On Win32 Cygwin/MinGW runtimes, the profiling code gcrtN.o
		 * comes in addition to crtN.o or dllcrtN.o
		 */
		if (pgflag)
			strap(&middle_linker_flags, &crtdirs, GCRT0, 'p');
		if (shared == 0)
			b = CRT0;
		else
			b = CRT0_S;     /* dllcrtN.o */
		strap(&middle_linker_flags, &crtdirs, b, 'p');
#else
		if (shared == 0) {
			if (pgflag)
				b = GCRT0;
			else if (pieflag)
				b = RCRT0;
			else
				b = CRT0;
#ifndef os_darwin
			strap(&middle_linker_flags, &crtdirs, b, 'p');
#endif
		}
#endif
	}
}

#ifdef _WIN32
char *
win32pathsubst(char *s)
{
	char env[1024];
	DWORD len;

	len = ExpandEnvironmentStrings(s, env, sizeof(env));
	if (len == 0 || len > sizeof(env))
		errorx(8, "ExpandEnvironmentStrings failed, len %lu", len);

	len--;	/* skip nil */
	while (len-- > 0 && (env[len] == '/' || env[len] == '\\'))
		env[len] = '\0';

	return xstrdup(env);
}

char *
win32commandline(struct strlist *l)
{
	const struct string *s;
	char *cmd;
	char *p;
	int len;
	int j, k;

	len = 0;
	STRLIST_FOREACH(s, l) {
		len++;
		for (j = 0; s->value[j] != '\0'; j++) {
			if (s->value[j] == '\"') {
				for (k = j-1; k >= 0 && s->value[k] == '\\'; k--)
					len++;
				len++;
			}
			len++;
		}
		for (k = j-1; k >= 0 && s->value[k] == '\\'; k--)
			len++;
		len++;
		len++;
	}

	p = cmd = xmalloc(len);

	STRLIST_FOREACH(s, l) {
		*p++ = '\"';
		for (j = 0; s->value[j] != '\0'; j++) {
			if (s->value[j] == '\"') {
				for (k = j-1; k >= 0 && s->value[k] == '\\'; k--)
					*p++ = '\\';
				*p++ = '\\';
			}
			*p++ = s->value[j];
		}
		for (k = j-1; k >= 0 && s->value[k] == '\\'; k--)
			*p++ = '\\';
		*p++ = '\"';
		*p++ = ' ';
	}
	p[-1] = '\0';

	return cmd;
}
#endif
