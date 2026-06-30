#ifndef _MIPS_COMMON_IF_USBN_H_
#define _MIPS_COMMON_IF_USBN_H_

void usbnattach(int unit);
void usbnpoll(void);
void usbn_input(int unit, const unsigned char *frame, unsigned len);
void usbn_input_error(int unit);
void usbn_link_reset(int unit);
void usbn_tx_done(int unit, int error);

/*
 * Board-specific transport hooks.  The N64 implementation will map these to
 * the N64cart USB device controller; Malta can provide a fake transport for
 * QEMU-only validation.
 */
int usbn_hw_init(int unit, unsigned char *enaddr);
int usbn_hw_send(int unit, const unsigned char *frame, unsigned len);
void usbn_hw_poll(void);

#endif
