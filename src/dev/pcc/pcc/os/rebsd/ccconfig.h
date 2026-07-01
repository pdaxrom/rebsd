/*	$Id$	*/

/*
 * Various settings that controls how the C compiler works for ReBSD/MIPS.
 */

/* common cpp predefines */
#define CPPADD	{ \
	"-D__ReBSD__", "-D__REBSD__", "-D__rebsd__", \
	"-D__RETROBSD__", "-D__retrobsd__", \
	"-D__BSD__", "-D__unix__", "-Dunix", \
	NULL, \
}

#define	CPPMDADD { \
	"-D__mips__", "-Dmips", "-D__mips=32", \
	"-D__MIPSEB__", "-D__MIPSEB", "-DMIPSEB", "-D_MIPSEB", \
	"-D__mips_o32", \
	NULL, \
}

#define CRTBEGIN	0
#define CRTEND		0
#define CRTI		0
#define CRTN		0
#undef PCCLIBDIR
#define PCCLIBDIR	NULL

#define CRT0		LIBDIR "crt0.o"

#define DEFLIBDIRS	{ NULL }
#define DEFLIBS		{ LIBDIR "libc.a", NULL }
#define DEFPROFLIBS	{ LIBDIR "libc.a", NULL }
#define DEFCXXLIBS	{ LIBDIR "libc.a", NULL }

#define STARTLABEL	"_start"
#define TARGET_NO_ABICALLS

#define PCC_SETUP_LD_ARGS \
	strlist_append(&early_linker_flags, "-X");

#define PCC_SIZE_TYPE		"unsigned int"
#define PCC_PTRDIFF_TYPE	"int"
#define PCC_WINT_TYPE		"unsigned int"
