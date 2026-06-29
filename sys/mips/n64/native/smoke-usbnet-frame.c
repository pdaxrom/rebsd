#include <stdio.h>
#include <string.h>

#include "n64usbnet_proto.h"

static unsigned char frame[N64USBNET_FRAME_MAX];
static unsigned char packet[N64USBNET_USB_PACKET];

static int
die(const char *msg)
{
    fprintf(stderr, "smoke-usbnet-frame: %s\n", msg);
    return 1;
}

static void
fill_frame(unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++)
        frame[i] = (unsigned char)((i * 37u + len) & 0xffu);
}

static int
run_len(unsigned len, unsigned seq)
{
    struct n64usbnet_tx tx;
    struct n64usbnet_rx rx;
    unsigned packet_len;
    int ret, done;

    fill_frame(len);
    n64usbnet_rx_reset(&rx);
    if (n64usbnet_tx_begin(&tx, frame, len, seq) != N64USBNET_MORE)
        return die("tx_begin failed");
    done = 0;
    while (!done) {
        ret = n64usbnet_tx_next(&tx, packet, &packet_len);
        if (ret == N64USBNET_ERROR)
            return die("tx_next failed");
        if (packet_len > N64USBNET_USB_PACKET)
            return die("tx packet too large");
        ret = n64usbnet_rx_push(&rx, packet, packet_len);
        if (ret == N64USBNET_ERROR)
            return die("rx_push failed");
        done = ret == N64USBNET_DONE;
    }
    if (rx.len != len || rx.seq != seq)
        return die("rx metadata mismatch");
    if (memcmp(rx.frame, frame, len) != 0)
        return die("rx frame mismatch");
    if (n64usbnet_tx_next(&tx, packet, &packet_len) != N64USBNET_DONE)
        return die("tx did not finish cleanly");
    return 0;
}

static int
run_bad_packets(void)
{
    struct n64usbnet_rx rx;
    unsigned i;

    n64usbnet_rx_reset(&rx);
    for (i = 0; i < sizeof(packet); i++)
        packet[i] = 0;
    if (n64usbnet_rx_push(&rx, packet, 1) != N64USBNET_ERROR)
        return die("accepted short header");
    packet[0] = 'B';
    packet[1] = 'D';
    if (n64usbnet_rx_push(&rx, packet, N64USBNET_HDR_LEN) !=
        N64USBNET_ERROR)
        return die("accepted bad magic");
    packet[0] = N64USBNET_MAGIC0;
    packet[1] = N64USBNET_MAGIC1;
    packet[2] = N64USBNET_VERSION;
    packet[3] = N64USBNET_TYPE_FRAME;
    packet[4] = 0xff;
    packet[5] = 0xff;
    if (n64usbnet_rx_push(&rx, packet, N64USBNET_HDR_LEN) !=
        N64USBNET_ERROR)
        return die("accepted oversize frame");
    return 0;
}

int
main(void)
{
    unsigned lens[] = {
        0, 1, 14, 52, 53, 63, 64, 65, 127, 512, 1500, 1518
    };
    unsigned i;

    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        if (run_len(lens[i], 0x10203040u + i) != 0)
            return 1;
    }
    if (run_bad_packets() != 0)
        return 1;
    printf("n64usbnet framing smoke ok\n");
    return 0;
}
