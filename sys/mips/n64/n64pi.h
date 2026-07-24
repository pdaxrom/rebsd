#ifndef _N64_N64PI_H_
#define _N64_N64PI_H_

/*
 * The N64 has one PI bus and one CPU.  A bus owner keeps interrupts masked
 * until it leaves, so a timer or cartridge interrupt cannot enter a second
 * PI client while the first client is using the bus.
 */
void n64pi_init(void);
int n64pi_bus_enter(const void *);
void n64pi_bus_leave(const void *);
int n64pi_is_busy(void);

#endif
