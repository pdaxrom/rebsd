/*
 * libmagic 5.47 feature configuration for RetroBSD.
 *
 * Keep this list honest: every HAVE_* below denotes a public system
 * interface, not a private compatibility shim in libmagic.
 */
#ifndef RETROBSD_LIBMAGIC_CONFIG_H
#define RETROBSD_LIBMAGIC_CONFIG_H

#define BUILTIN_ELF 1
#define ELFCORE 1

#define HAVE_ASCTIME_R 1
#define HAVE_ASPRINTF 1
#define HAVE_CTIME_R 1
#define HAVE_DPRINTF 1
#define HAVE_ERR_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FMTCHECK 1
#define HAVE_FORK 1
#define HAVE_FSEEKO 1
#define HAVE_GETLINE 1
#define HAVE_GETOPT_H 1
#define HAVE_GETOPT_LONG 1
#define HAVE_GETPAGESIZE 1
#define HAVE_GMTIME_R 1
#define HAVE_INTPTR_T 1
#define HAVE_INTTYPES_H 1
#define HAVE_LOCALTIME_R 1
#define HAVE_MBRTOWC 1
#define HAVE_MBSTATE_T 1
#define HAVE_MEMMEM 1
#define HAVE_MKSTEMP 1
#define HAVE_MMAP 1
#define HAVE_PREAD 1
#define HAVE_SIG_T 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCASESTR 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRLCAT 1
#define HAVE_STRLCPY 1
#define HAVE_STRNDUP 1
#define HAVE_STRTOF 1
#define HAVE_STRUCT_OPTION 1
#define HAVE_STRUCT_STAT_ST_RDEV 1
#define HAVE_STRUCT_TM_TM_GMTOFF 1
#define HAVE_STRUCT_TM_TM_ZONE 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TM_ISDST 1
#define HAVE_TM_ZONE 1
#define HAVE_UINTPTR_T 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIMES 1
#define HAVE_VASPRINTF 1
#define HAVE_VFORK 1
#define HAVE_VISIBILITY 0
#define HAVE_WCHAR_H 1
#define HAVE_WORKING_FORK 1
#define HAVE_WORKING_VFORK 1

#define PACKAGE "file"
#define PACKAGE_NAME "file"
#define PACKAGE_STRING "file 5.47"
#define PACKAGE_TARNAME "file"
#define PACKAGE_VERSION "5.47"
#define STDC_HEADERS 1
#define VERSION "5.47"

#endif
