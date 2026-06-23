/*
 * N64 uses the shared MIPS machdep sysctl ABI.  Keep this wrapper so existing
 * <machine/cpu.h> includes work while the numeric definitions live in one
 * place.
 */
#include "../cpu.h"
