/*
 * POSIX directory stream interface.
 *
 * The on-disk and historical BSD name for struct dirent is "direct".
 * Preserve that ABI while exposing the standard source-level spelling.
 */
#ifndef _DIRENT_H_
#define _DIRENT_H_

#include <sys/dir.h>

#define dirent direct

#endif
