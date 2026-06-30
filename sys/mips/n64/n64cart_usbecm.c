/*
 * N64cart USB CDC ECM backend for usbn(4).
 *
 * Keep this as a separate configured driver from n64cart_usbnet.c.  The shared
 * implementation is compiled in CDC ECM mode here; n64cart_usbnet.c by itself
 * remains the vendor-specific bridge protocol backend.
 */
#define N64USB_CDC_ECM 1
#include "n64cart_usbnet.c"
