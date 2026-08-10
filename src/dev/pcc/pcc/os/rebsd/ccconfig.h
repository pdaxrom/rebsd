/*	$Id$	*/

/*
 * Various settings that control how the C compiler works for ReBSD.
 */

/* common cpp predefines */
#define PCC_REBSD_CPP_FORMAT "-D__ELF__",

#define CPPADD	{ \
	"-T", \
	PCC_REBSD_CPP_FORMAT \
	"-D__ReBSD__", "-D__REBSD__", "-D__rebsd__", \
	"-D__RETROBSD__", "-D__retrobsd__", \
	"-D__BSD__", "-D__unix__", "-Dunix", \
	NULL, \
}

#if defined(mach_mips)

#ifdef TARGET_BIG_ENDIAN
#define CPPMD_ENDIAN "-D__MIPSEB__", "-D__MIPSEB", "-DMIPSEB", "-D_MIPSEB",
#else
#define CPPMD_ENDIAN \
	"-D__MIPSEL__", "-D__MIPSEL", "-DMIPSEL", "-D_MIPSEL", \
	"-D__mipsel__", "-D__mipsel",
#endif

#define	CPPMDADD { \
	"-D__mips__", "-Dmips", \
	CPPMD_ENDIAN \
	"-D__mips_o32", \
	NULL, \
}

#define TARGET_GLOBALS \
	struct mips_target mips_target = MIPS_TARGET_INITIALIZER; \
	int mips_fix4300_explicit; \
	int mips_tune_explicit;

#ifdef TARGET_BIG_ENDIAN
#define PCC_REBSD_CHECK_BIG_ENDIAN() ((void)0)
#define PCC_REBSD_CHECK_LITTLE_ENDIAN() \
	errorx(8, "-mlittle-endian is not supported by big-endian mips-rebsd")
#else
#define PCC_REBSD_CHECK_BIG_ENDIAN() \
	errorx(8, "-mbig-endian is not supported by little-endian mipsel-rebsd")
#define PCC_REBSD_CHECK_LITTLE_ENDIAN() ((void)0)
#endif

/*
 * The PCC inline pass currently corrupts trees in ReBSD/MIPS optimized
 * soft-float builds. Keep explicit -Wc,-xinline available, but do not enable
 * it automatically as part of -O/-O2 for this target.
 */
#define PCC_DISABLE_AUTO_XINLINE

#define PCC_REBSD_DEFAULT_FIX4300() \
	if (!mips_fix4300_explicit) \
		mips_target.fix_vr4300 = (mips_target.capabilities & \
		    MIPS_CAP_VR4300_FMUL_ERRATUM) != 0

#define PCC_REBSD_SELECT_ISA(isa, tune) { \
	mips_target_set_isa(&mips_target, (isa)); \
	if (!mips_tune_explicit) \
		mips_target_set_tune(&mips_target, (tune)); \
	PCC_REBSD_DEFAULT_FIX4300(); \
}

#define PCC_REBSD_SELECT_TUNE(tune) { \
	mips_target_set_tune(&mips_target, (tune)); \
	mips_tune_explicit = 1; \
	PCC_REBSD_DEFAULT_FIX4300(); \
}

#define PCC_HANDLE_MFLAG { \
	if (match(argp, "-march=vr4300") || match(argp, "-mips3")) { \
		PCC_REBSD_SELECT_ISA(MIPS_ISA_III, MIPS_TUNE_VR4300); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-march=mips3")) { \
		PCC_REBSD_SELECT_ISA(MIPS_ISA_III, MIPS_TUNE_GENERIC); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-march=mips32r2") || match(argp, "-mips32r2")) { \
		PCC_REBSD_SELECT_ISA(MIPS_ISA_MIPS32R2, MIPS_TUNE_GENERIC); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-march=mips32")) \
		errorx(8, "-march=mips32 is unsupported; use -march=mips32r2"); \
	if (match(argp, "-mtune=generic")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_GENERIC); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mtune=vr4300")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_VR4300); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mtune=r4000")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_R4000); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mtune=24kc")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_24KC); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mtune=34kc")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_34KC); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mtune=74kc")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_74KC); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mtune=jz4780")) { \
		PCC_REBSD_SELECT_TUNE(MIPS_TUNE_JZ4780); \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (strncmp(argp, "-mtune=", 7) == 0) \
		errorx(8, "unsupported MIPS tuning '%s'", argp + 7); \
	if (match(argp, "-mfix4300") || match(argp, "-mno-fix4300")) { \
		mips_target.fix_vr4300 = match(argp, "-mfix4300"); \
		mips_fix4300_explicit = 1; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mbig-endian")) { \
		PCC_REBSD_CHECK_BIG_ENDIAN(); \
		bigendian = 1; \
		mips_target.little_endian = 0; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mlittle-endian")) { \
		PCC_REBSD_CHECK_LITTLE_ENDIAN(); \
		bigendian = 0; \
		mips_target.little_endian = 1; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
}

#define PCC_SETUP_CPP_ARGS { \
	const char *mips_error = mips_target_error(&mips_target); \
	if (mips_error != NULL) \
		errorx(8, "%s", mips_error); \
	mips_target.little_endian = !bigendian; \
	mips_target.hard_float = !softfloat; \
	if (mips_target.isa == MIPS_ISA_MIPS32R2) { \
		strlist_prepend(&preprocessor_flags, "-D__mips=32"); \
		strlist_prepend(&preprocessor_flags, "-D__mips_isa_rev=2"); \
		strlist_prepend(&preprocessor_flags, "-D__mips32"); \
		strlist_prepend(&preprocessor_flags, "-D__mips32r2"); \
	} else { \
		strlist_prepend(&preprocessor_flags, "-D__mips=3"); \
		strlist_prepend(&preprocessor_flags, "-D__mips3"); \
		if (mips_target.tune == MIPS_TUNE_VR4300) \
			strlist_prepend(&preprocessor_flags, "-D__vr4300__"); \
	} \
	if (softfloat) { \
		strlist_prepend(&preprocessor_flags, "-D__mips_soft_float"); \
		strlist_append(&compiler_flags, "-msoft-float"); \
	} else { \
		strlist_prepend(&preprocessor_flags, "-D__mips_hard_float"); \
		strlist_append(&compiler_flags, "-mhard-float"); \
	} \
}

#define CRTBEGIN	0
#define CRTEND		0
#define CRTI		0
#define CRTN		0
#undef PCCLIBDIR
#define PCCLIBDIR	NULL

#define CRT0		LIBDIR "crt0.o"

#define DEFLIBDIRS	{ LIBDIR, NULL }
#define SOFTFLOATLIBDIR	LIBDIR "softfloat/"
#define DEFLIBS		{ "-lpcc", "-lc", "-lpcc", NULL }
#define DEFPROFLIBS	{ "-lpcc", "-lc", "-lpcc", NULL }
#define DEFCXXLIBS	{ "-lpcc", "-lc", "-lpcc", NULL }

#define STARTLABEL	"_start"
#define TARGET_NO_ABICALLS

#define PCC_REBSD_EXEC_FORMAT "--elf"

#define PCC_SETUP_AS_ARGS { \
	strlist_append(&assembler_flags, PCC_REBSD_EXEC_FORMAT); \
	strlist_append(&assembler_flags, bigendian ? "-EB" : "-EL"); \
	strlist_append(&assembler_flags, \
	    mips_target.isa == MIPS_ISA_MIPS32R2 ? \
	    "-march=mips32r2" : "-march=vr4300"); \
}

#define PCC_SETUP_LD_ARGS { \
	strlist_append(&early_linker_flags, PCC_REBSD_EXEC_FORMAT); \
	strlist_append(&early_linker_flags, bigendian ? "-EB" : "-EL"); \
	strlist_append(&early_linker_flags, "-X"); \
	if (softfloat && !nostdlib) \
		strlist_append(&early_linker_flags, \
		    cat("-L", cat_sysroot(sysroot, SOFTFLOATLIBDIR))); \
}

#elif defined(mach_i386)

#define CPPMDADD { \
	"-D__i386__", "-D__i386", "-Di386", \
	NULL, \
}

#define CRTBEGIN	0
#define CRTEND		0
#define CRTI		0
#define CRTN		0
#undef PCCLIBDIR
#define PCCLIBDIR	NULL

#define CRT0		LIBDIR "crt0.o"
#define DEFLIBDIRS	{ LIBDIR, NULL }
#define DEFLIBS		{ "-lpcc", "-lc", "-lpcc", NULL }
#define DEFPROFLIBS	{ "-lpcc", "-lc", "-lpcc", NULL }
#define DEFCXXLIBS	{ "-lpcc", "-lc", "-lpcc", NULL }

#define STARTLABEL	"_start"

#define PCC_SETUP_AS_ARGS { \
	strlist_append(&assembler_flags, "--32"); \
}

#define PCC_SETUP_LD_ARGS { \
	strlist_append(&early_linker_flags, "--elf"); \
	strlist_append(&early_linker_flags, "-X"); \
	if (!rflag) { \
		strlist_append(&early_linker_flags, "-T"); \
		strlist_append(&early_linker_flags, \
		    cat_sysroot(sysroot, LIBDIR "ldscripts/elf32-i386.ld")); \
	} \
}

#else
#error ReBSD PCC target architecture is not configured
#endif

#define PCC_SIZE_TYPE		"unsigned int"
#define PCC_PTRDIFF_TYPE	"int"
#define PCC_WINT_TYPE		"unsigned int"
