#include "sys/types.h"
#include "sys/kconfig.h"

#define C (char *)

extern struct driver ci20_uartdriver;

struct conf_ctlr conf_ctlr_init[] = {
   /* driver,		unit,	addr,		pri,	flags */
    { 0 }
};

struct conf_device conf_device_init[] = {
   /* driver,		ctlr driver,	unit,	ctlr,	drive,	flags,	pins */
    { &ci20_uartdriver,	0,		-2,	0,	-2,	0x0,	{0} },
    { 0 }
};
void creatorattach(int);

struct conf_service conf_service_init[] = {
    { creatorattach },
    { 0 }
};
