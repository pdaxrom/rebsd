/*	$Id$	*/

/*
 * Various settings that controls how the C compiler works for ReBSD/MIPS.
 */

/* common cpp predefines */
#define CPPADD	{ \
	"-T", \
	"-D__ReBSD__", "-D__REBSD__", "-D__rebsd__", \
	"-D__RETROBSD__", "-D__retrobsd__", \
	"-D__BSD__", "-D__unix__", "-Dunix", \
	NULL, \
}

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
	int mips_cpu = MIPS_CPU_DEFAULT;

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

#define PCC_HANDLE_MFLAG { \
	if (match(argp, "-march=vr4300") || match(argp, "-mips3")) { \
		mips_cpu = MIPS_CPU_VR4300; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-march=mips32r2") || match(argp, "-mips32r2") || \
	    match(argp, "-march=mips32")) { \
		mips_cpu = MIPS_CPU_MIPS32R2; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mbig-endian")) { \
		PCC_REBSD_CHECK_BIG_ENDIAN(); \
		bigendian = 1; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
	if (match(argp, "-mlittle-endian")) { \
		PCC_REBSD_CHECK_LITTLE_ENDIAN(); \
		bigendian = 0; \
		strlist_append(&compiler_flags, argp); \
		break; \
	} \
}

#define PCC_SETUP_CPP_ARGS { \
	if (mips_cpu == MIPS_CPU_MIPS32R2) { \
		strlist_prepend(&preprocessor_flags, "-D__mips=32"); \
		strlist_prepend(&preprocessor_flags, "-D__mips_isa_rev=2"); \
		strlist_prepend(&preprocessor_flags, "-D__mips32"); \
		strlist_prepend(&preprocessor_flags, "-D__mips32r2"); \
	} else { \
		strlist_prepend(&preprocessor_flags, "-D__mips=3"); \
		strlist_prepend(&preprocessor_flags, "-D__mips3"); \
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

#ifdef REBSD_TOOLCHAIN_ELF_DEFAULT
#define PCC_REBSD_EXEC_FORMAT "--elf"
#else
#define PCC_REBSD_EXEC_FORMAT "--aout"
#endif

#define PCC_SETUP_AS_ARGS { \
	strlist_append(&assembler_flags, PCC_REBSD_EXEC_FORMAT); \
	strlist_append(&assembler_flags, bigendian ? "-EB" : "-EL"); \
	strlist_append(&assembler_flags, \
	    mips_cpu == MIPS_CPU_MIPS32R2 ? "-march=mips32r2" : "-march=vr4300"); \
}

#define PCC_SETUP_LD_ARGS { \
	strlist_append(&early_linker_flags, PCC_REBSD_EXEC_FORMAT); \
	strlist_append(&early_linker_flags, bigendian ? "-EB" : "-EL"); \
	strlist_append(&early_linker_flags, "-X"); \
	if (softfloat && !nostdlib) \
		strlist_append(&early_linker_flags, \
		    cat("-L", cat_sysroot(sysroot, SOFTFLOATLIBDIR))); \
}

#define PCC_SIZE_TYPE		"unsigned int"
#define PCC_PTRDIFF_TYPE	"int"
#define PCC_WINT_TYPE		"unsigned int"
