/*
 * Copyright (c) 2026 ReBSD contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifndef _DEV_USB_USB_LIMITS_H_
#define _DEV_USB_USB_LIMITS_H_

#ifndef USB_MAX_DEVICES
#define USB_MAX_DEVICES             8
#endif
#ifndef USB_MAX_CORE_INTERFACES
#define USB_MAX_CORE_INTERFACES     16
#endif
#ifndef USB_MAX_CORE_ENDPOINTS
#define USB_MAX_CORE_ENDPOINTS      32
#endif
#ifndef USB_MAX_PIPES
#define USB_MAX_PIPES               16
#endif
#ifndef USB_MAX_XFERS
#define USB_MAX_XFERS               16
#endif
#ifndef USB_MAX_DRIVERS
#define USB_MAX_DRIVERS             8
#endif
#ifndef USB_MAX_TASKS
#define USB_MAX_TASKS               8
#endif
#ifndef USB_MAX_ROOT_PORTS
#define USB_MAX_ROOT_PORTS          8
#endif

#endif /* _DEV_USB_USB_LIMITS_H_ */
