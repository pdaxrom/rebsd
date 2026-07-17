/*
 * ReBSD release identification shared by the kernel and userland.
 *
 * Keep the numeric release separate from the human-readable codename so
 * scripts can consume REBSD_RELEASE without having to parse display text.
 */
#ifndef _SYS_REBSD_VERSION_H_
#define _SYS_REBSD_VERSION_H_

#define REBSD_OSTYPE           "ReBSD"
#define REBSD_RELEASE          "0.1"
#define REBSD_CODENAME         "Resurgence"
#define REBSD_OSRELEASE        REBSD_RELEASE "-" REBSD_CODENAME
#define REBSD_RELEASE_NAME     REBSD_OSRELEASE

#endif /* _SYS_REBSD_VERSION_H_ */
