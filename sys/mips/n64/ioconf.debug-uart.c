#include "sys/types.h"
#include "sys/kconfig.h"

#define C (char *)

extern struct driver n64cartdriver;

struct conf_ctlr conf_ctlr_init[] = {
   /* driver,		unit,	addr,		pri,	flags */
    { 0 }
};

struct conf_device conf_device_init[] = {
   /* driver,		ctlr driver,	unit,	ctlr,	drive,	flags,	pins */
    { &n64cartdriver,	0,		-2,	0,	-2,	0x0,	{0} },
    { 0 }
};
#ifdef NINTENDO_ENABLED
void nintendoattach(int);
#endif
#ifdef PTY_ENABLED
void ptyattach(int);
#endif
#ifdef ETHER_ENABLED
void etherattach(int);
#endif
#ifdef INET_ENABLED
void inetattach(int);
#endif
#ifdef UNIXDOMAIN_ENABLED
void unixdomainattach(int);
#endif

struct conf_service conf_service_init[] = {
#ifdef NINTENDO_ENABLED
    { nintendoattach },
#endif
#ifdef PTY_ENABLED
    { ptyattach },
#endif
#ifdef ETHER_ENABLED
    { etherattach },
#endif
#ifdef INET_ENABLED
    { inetattach },
#endif
#ifdef UNIXDOMAIN_ENABLED
    { unixdomainattach },
#endif
    { 0 }
};
