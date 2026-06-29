#ifndef _MIPS_COMMON_N64USBNET_PROTO_H_
#define _MIPS_COMMON_N64USBNET_PROTO_H_

/*
 * Framing used by the first N64 cartridge USB Ethernet transport.
 *
 * The cartridge USB controller is a full-speed USB device endpoint pair.
 * One Ethernet frame is sent as a first 64-byte bulk packet with this header,
 * followed by raw continuation packets until hdr.len bytes are received.
 */
#define N64USBNET_MAGIC0        0x55u   /* 'U' */
#define N64USBNET_MAGIC1        0x4eu   /* 'N' */
#define N64USBNET_VERSION       1u
#define N64USBNET_TYPE_FRAME    1u

#define N64USBNET_HDR_LEN       12u
#define N64USBNET_USB_PACKET    64u
#define N64USBNET_FRAME_MAX     1518u

#define N64USBNET_MORE          0
#define N64USBNET_DONE          1
#define N64USBNET_ERROR         -1

struct n64usbnet_tx {
    const unsigned char *frame;
    unsigned len;
    unsigned pos;
    unsigned seq;
    unsigned flags;
    int active;
};

struct n64usbnet_rx {
    unsigned char frame[N64USBNET_FRAME_MAX];
    unsigned len;
    unsigned pos;
    unsigned seq;
    unsigned flags;
    unsigned type;
    int active;
};

int n64usbnet_tx_begin(struct n64usbnet_tx *tx,
    const unsigned char *frame, unsigned len, unsigned seq);
int n64usbnet_tx_next(struct n64usbnet_tx *tx,
    unsigned char *packet, unsigned *packet_len);

void n64usbnet_rx_reset(struct n64usbnet_rx *rx);
int n64usbnet_rx_push(struct n64usbnet_rx *rx,
    const unsigned char *packet, unsigned packet_len);

#endif
