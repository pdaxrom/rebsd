#include "n64usbnet_proto.h"

static void
n64usbnet_copy(unsigned char *dst, const unsigned char *src, unsigned len)
{
    while (len-- != 0)
        *dst++ = *src++;
}

static void
n64usbnet_put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
}

static unsigned
n64usbnet_get16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

static void
n64usbnet_put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static unsigned
n64usbnet_get32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
        ((unsigned)p[2] << 8) | (unsigned)p[3];
}

int
n64usbnet_tx_begin(struct n64usbnet_tx *tx, const unsigned char *frame,
    unsigned len, unsigned seq)
{
    if (tx == 0 || frame == 0 || len > N64USBNET_FRAME_MAX)
        return N64USBNET_ERROR;
    tx->frame = frame;
    tx->len = len;
    tx->pos = 0;
    tx->seq = seq;
    tx->flags = 0;
    tx->active = 1;
    return N64USBNET_MORE;
}

int
n64usbnet_tx_next(struct n64usbnet_tx *tx, unsigned char *packet,
    unsigned *packet_len)
{
    unsigned room, chunk;

    if (tx == 0 || packet == 0 || packet_len == 0)
        return N64USBNET_ERROR;
    if (!tx->active)
        return N64USBNET_DONE;

    if (tx->pos == 0) {
        packet[0] = N64USBNET_MAGIC0;
        packet[1] = N64USBNET_MAGIC1;
        packet[2] = N64USBNET_VERSION;
        packet[3] = N64USBNET_TYPE_FRAME;
        n64usbnet_put16(packet + 4, tx->len);
        n64usbnet_put16(packet + 6, tx->flags);
        n64usbnet_put32(packet + 8, tx->seq);
        room = N64USBNET_USB_PACKET - N64USBNET_HDR_LEN;
        chunk = tx->len < room ? tx->len : room;
        n64usbnet_copy(packet + N64USBNET_HDR_LEN, tx->frame, chunk);
        tx->pos = chunk;
        *packet_len = N64USBNET_HDR_LEN + chunk;
    } else {
        room = N64USBNET_USB_PACKET;
        chunk = tx->len - tx->pos;
        if (chunk > room)
            chunk = room;
        n64usbnet_copy(packet, tx->frame + tx->pos, chunk);
        tx->pos += chunk;
        *packet_len = chunk;
    }

    if (tx->pos == tx->len)
        tx->active = 0;
    return N64USBNET_MORE;
}

void
n64usbnet_rx_reset(struct n64usbnet_rx *rx)
{
    if (rx == 0)
        return;
    rx->len = 0;
    rx->pos = 0;
    rx->seq = 0;
    rx->flags = 0;
    rx->type = 0;
    rx->active = 0;
}

int
n64usbnet_rx_push(struct n64usbnet_rx *rx, const unsigned char *packet,
    unsigned packet_len)
{
    unsigned payload_len;
    const unsigned char *payload;

    if (rx == 0 || packet == 0 || packet_len > N64USBNET_USB_PACKET)
        return N64USBNET_ERROR;

    if (!rx->active) {
        if (packet_len < N64USBNET_HDR_LEN)
            return N64USBNET_ERROR;
        if (packet[0] != N64USBNET_MAGIC0 ||
            packet[1] != N64USBNET_MAGIC1 ||
            packet[2] != N64USBNET_VERSION ||
            packet[3] != N64USBNET_TYPE_FRAME)
            return N64USBNET_ERROR;
        rx->type = packet[3];
        rx->len = n64usbnet_get16(packet + 4);
        rx->flags = n64usbnet_get16(packet + 6);
        rx->seq = n64usbnet_get32(packet + 8);
        rx->pos = 0;
        if (rx->len > N64USBNET_FRAME_MAX)
            return N64USBNET_ERROR;
        payload = packet + N64USBNET_HDR_LEN;
        payload_len = packet_len - N64USBNET_HDR_LEN;
        rx->active = 1;
    } else {
        payload = packet;
        payload_len = packet_len;
    }

    if (rx->pos + payload_len > rx->len) {
        n64usbnet_rx_reset(rx);
        return N64USBNET_ERROR;
    }
    n64usbnet_copy(rx->frame + rx->pos, payload, payload_len);
    rx->pos += payload_len;
    if (rx->pos == rx->len) {
        rx->active = 0;
        return N64USBNET_DONE;
    }
    return N64USBNET_MORE;
}
