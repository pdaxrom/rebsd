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
void nintendoattach(int);
void ptyattach(int);
void etherattach(int);
void inetattach(int);
void unixdomainattach(int);

struct conf_service conf_service_init[] = {
    { nintendoattach },
    { ptyattach },
    { etherattach },
    { inetattach },
    { unixdomainattach },
    { 0 }
};
