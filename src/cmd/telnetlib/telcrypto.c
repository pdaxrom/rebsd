/*
 * Pre-shared-key encrypted stream for the small RetroBSD telnet tools.
 * Uses SHA-256 for key derivation/proof and ChaCha20 for the byte stream.
 */

#include <sys/types.h>

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "telcrypto.h"

#define RTEL_MAGIC "RTEL1"
#define RTEL_MAGIC_LEN 5
#define SHA256_BLOCK 64

#define ROR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROL32(x,n) (((x) << (n)) | ((x) >> (32 - (n))))

struct sha256_ctx {
    unsigned long h[8];
    unsigned long bits_hi;
    unsigned long bits_lo;
    unsigned char block[SHA256_BLOCK];
    int used;
};

static unsigned long k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static unsigned long
load_be32(p)
    unsigned char *p;
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
        ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static void
store_be32(p, v)
    unsigned char *p;
    unsigned long v;
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static unsigned long
load_le32(p)
    unsigned char *p;
{
    return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) |
        ((unsigned long)p[1] << 8) | (unsigned long)p[0];
}

static void
store_le32(p, v)
    unsigned char *p;
    unsigned long v;
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static void
sha256_transform(ctx, data)
    struct sha256_ctx *ctx;
    unsigned char *data;
{
    unsigned long w[64];
    unsigned long a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = load_be32(data + i * 4);
    for (; i < 64; i++) {
        unsigned long s0, s1;
        s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^
            (w[i - 15] >> 3);
        s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^
            (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
    e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];

    for (i = 0; i < 64; i++) {
        unsigned long s1, ch, s0, maj;
        s1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        ch = (e & f) ^ ((~e) & g);
        t1 = h + s1 + ch + k256[i] + w[i];
        s0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void
sha256_init(ctx)
    struct sha256_ctx *ctx;
{
    ctx->h[0] = 0x6a09e667; ctx->h[1] = 0xbb67ae85;
    ctx->h[2] = 0x3c6ef372; ctx->h[3] = 0xa54ff53a;
    ctx->h[4] = 0x510e527f; ctx->h[5] = 0x9b05688c;
    ctx->h[6] = 0x1f83d9ab; ctx->h[7] = 0x5be0cd19;
    ctx->bits_hi = 0;
    ctx->bits_lo = 0;
    ctx->used = 0;
}

static void
sha256_update(ctx, data, len)
    struct sha256_ctx *ctx;
    unsigned char *data;
    int len;
{
    unsigned long old;

    while (len-- > 0) {
        old = ctx->bits_lo;
        ctx->bits_lo += 8;
        if (ctx->bits_lo < old)
            ctx->bits_hi++;
        ctx->block[ctx->used++] = *data++;
        if (ctx->used == SHA256_BLOCK) {
            sha256_transform(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

static void
sha256_final(ctx, out)
    struct sha256_ctx *ctx;
    unsigned char *out;
{
    int i;

    ctx->block[ctx->used++] = 0x80;
    if (ctx->used > 56) {
        while (ctx->used < 64)
            ctx->block[ctx->used++] = 0;
        sha256_transform(ctx, ctx->block);
        ctx->used = 0;
    }
    while (ctx->used < 56)
        ctx->block[ctx->used++] = 0;
    store_be32(ctx->block + 56, ctx->bits_hi);
    store_be32(ctx->block + 60, ctx->bits_lo);
    sha256_transform(ctx, ctx->block);
    for (i = 0; i < 8; i++)
        store_be32(out + i * 4, ctx->h[i]);
}

static void
hash3(key, cnonce, snonce, label, out)
    char *key;
    unsigned char *cnonce;
    unsigned char *snonce;
    char *label;
    unsigned char *out;
{
    struct sha256_ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, (unsigned char *)key, strlen(key));
    sha256_update(&ctx, (unsigned char *)":", 1);
    sha256_update(&ctx, cnonce, RTEL_NONCE_LEN);
    sha256_update(&ctx, snonce, RTEL_NONCE_LEN);
    sha256_update(&ctx, (unsigned char *)label, strlen(label));
    sha256_final(&ctx, out);
}

static void
make_nonce(key, out)
    char *key;
    unsigned char *out;
{
    static unsigned long serial;
    struct sha256_ctx ctx;
    unsigned long v;
    time_t now;

    now = time((time_t *)0);
    serial++;
    sha256_init(&ctx);
    sha256_update(&ctx, (unsigned char *)key, strlen(key));
    v = (unsigned long)now;
    sha256_update(&ctx, (unsigned char *)&v, sizeof(v));
    v = (unsigned long)getpid();
    sha256_update(&ctx, (unsigned char *)&v, sizeof(v));
    v = (unsigned long)&ctx;
    sha256_update(&ctx, (unsigned char *)&v, sizeof(v));
    sha256_update(&ctx, (unsigned char *)&serial, sizeof(serial));
    sha256_final(&ctx, out);
}

static int
raw_write_all(fd, buf, len)
    int fd;
    unsigned char *buf;
    int len;
{
    int n;

    while (len > 0) {
        n = write(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static int
raw_read_all(fd, buf, len)
    int fd;
    unsigned char *buf;
    int len;
{
    int n;

    while (len > 0) {
        n = read(fd, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

static void
qround(x, a, b, c, d)
    unsigned long *x;
    int a, b, c, d;
{
    x[a] += x[b]; x[d] ^= x[a]; x[d] = ROL32(x[d], 16);
    x[c] += x[d]; x[b] ^= x[c]; x[b] = ROL32(x[b], 12);
    x[a] += x[b]; x[d] ^= x[a]; x[d] = ROL32(x[d], 8);
    x[c] += x[d]; x[b] ^= x[c]; x[b] = ROL32(x[b], 7);
}

static void
chacha_block(st)
    struct rtel_stream *st;
{
    static unsigned char sigma[16] = "expand 32-byte k";
    unsigned long x[16], orig[16];
    int i;

    x[0] = load_le32(sigma + 0);
    x[1] = load_le32(sigma + 4);
    x[2] = load_le32(sigma + 8);
    x[3] = load_le32(sigma + 12);
    for (i = 0; i < 8; i++)
        x[4 + i] = load_le32(st->key + i * 4);
    x[12] = st->counter++;
    x[13] = load_le32(st->nonce + 0);
    x[14] = load_le32(st->nonce + 4);
    x[15] = load_le32(st->nonce + 8);
    for (i = 0; i < 16; i++)
        orig[i] = x[i];

    for (i = 0; i < 10; i++) {
        qround(x, 0, 4, 8, 12);
        qround(x, 1, 5, 9, 13);
        qround(x, 2, 6, 10, 14);
        qround(x, 3, 7, 11, 15);
        qround(x, 0, 5, 10, 15);
        qround(x, 1, 6, 11, 12);
        qround(x, 2, 7, 8, 13);
        qround(x, 3, 4, 9, 14);
    }
    for (i = 0; i < 16; i++)
        store_le32(st->block + i * 4, x[i] + orig[i]);
    st->used = 0;
}

static void
stream_init(st, key, nonce)
    struct rtel_stream *st;
    unsigned char *key;
    unsigned char *nonce;
{
    memcpy(st->key, key, 32);
    memcpy(st->nonce, nonce, 12);
    st->counter = 1;
    st->used = 64;
}

static void
stream_xor(st, buf, len)
    struct rtel_stream *st;
    unsigned char *buf;
    int len;
{
    while (len-- > 0) {
        if (st->used >= 64)
            chacha_block(st);
        *buf++ ^= st->block[st->used++];
    }
}

static void
derive(key, cnonce, snonce, client, ses)
    char *key;
    unsigned char *cnonce;
    unsigned char *snonce;
    int client;
    struct rtel_session *ses;
{
    unsigned char ckey[32], skey[32], cnonce2[32], snonce2[32];

    hash3(key, cnonce, snonce, "c2s-key", ckey);
    hash3(key, cnonce, snonce, "s2c-key", skey);
    hash3(key, cnonce, snonce, "c2s-nonce", cnonce2);
    hash3(key, cnonce, snonce, "s2c-nonce", snonce2);
    if (client) {
        stream_init(&ses->tx, ckey, cnonce2);
        stream_init(&ses->rx, skey, snonce2);
    } else {
        stream_init(&ses->tx, skey, snonce2);
        stream_init(&ses->rx, ckey, cnonce2);
    }
    ses->enabled = 1;
}

static int
write_hello(fd, nonce)
    int fd;
    unsigned char *nonce;
{
    unsigned char buf[RTEL_MAGIC_LEN + RTEL_NONCE_LEN];

    memcpy(buf, RTEL_MAGIC, RTEL_MAGIC_LEN);
    memcpy(buf + RTEL_MAGIC_LEN, nonce, RTEL_NONCE_LEN);
    return raw_write_all(fd, buf, sizeof(buf));
}

static int
read_hello(fd, nonce)
    int fd;
    unsigned char *nonce;
{
    unsigned char buf[RTEL_MAGIC_LEN + RTEL_NONCE_LEN];

    if (raw_read_all(fd, buf, sizeof(buf)) < 0)
        return -1;
    if (memcmp(buf, RTEL_MAGIC, RTEL_MAGIC_LEN) != 0) {
        errno = EINVAL;
        return -1;
    }
    memcpy(nonce, buf + RTEL_MAGIC_LEN, RTEL_NONCE_LEN);
    return 0;
}

int
rtel_client_handshake(fd, key, ses)
    int fd;
    char *key;
    struct rtel_session *ses;
{
    unsigned char cnonce[32], snonce[RTEL_NONCE_LEN];
    unsigned char expect[RTEL_PROOF_LEN], got[RTEL_PROOF_LEN];

    memset(ses, 0, sizeof(*ses));
    make_nonce(key, cnonce);
    if (write_hello(fd, cnonce) < 0 || read_hello(fd, snonce) < 0)
        return -1;
    hash3(key, cnonce, snonce, "client-proof", expect);
    if (raw_write_all(fd, expect, RTEL_PROOF_LEN) < 0)
        return -1;
    hash3(key, cnonce, snonce, "server-proof", expect);
    if (raw_read_all(fd, got, RTEL_PROOF_LEN) < 0)
        return -1;
    if (memcmp(expect, got, RTEL_PROOF_LEN) != 0) {
        errno = EACCES;
        return -1;
    }
    derive(key, cnonce, snonce, 1, ses);
    return 0;
}

int
rtel_server_handshake(fd, key, ses)
    int fd;
    char *key;
    struct rtel_session *ses;
{
    unsigned char cnonce[RTEL_NONCE_LEN], snonce[32];
    unsigned char expect[RTEL_PROOF_LEN], got[RTEL_PROOF_LEN];

    memset(ses, 0, sizeof(*ses));
    if (read_hello(fd, cnonce) < 0)
        return -1;
    make_nonce(key, snonce);
    if (write_hello(fd, snonce) < 0)
        return -1;
    hash3(key, cnonce, snonce, "client-proof", expect);
    if (raw_read_all(fd, got, RTEL_PROOF_LEN) < 0)
        return -1;
    if (memcmp(expect, got, RTEL_PROOF_LEN) != 0) {
        errno = EACCES;
        return -1;
    }
    hash3(key, cnonce, snonce, "server-proof", expect);
    if (raw_write_all(fd, expect, RTEL_PROOF_LEN) < 0)
        return -1;
    derive(key, cnonce, snonce, 0, ses);
    return 0;
}

int
rtel_read(fd, ses, buf, len)
    int fd;
    struct rtel_session *ses;
    unsigned char *buf;
    int len;
{
    int n;

    n = read(fd, buf, len);
    if (n > 0 && ses && ses->enabled)
        stream_xor(&ses->rx, buf, n);
    return n;
}

int
rtel_write(fd, ses, buf, len)
    int fd;
    struct rtel_session *ses;
    unsigned char *buf;
    int len;
{
    unsigned char tmp[128];
    int n;

    if (!ses || !ses->enabled)
        return raw_write_all(fd, buf, len);
    while (len > 0) {
        n = len > sizeof(tmp) ? sizeof(tmp) : len;
        memcpy(tmp, buf, n);
        stream_xor(&ses->tx, tmp, n);
        if (raw_write_all(fd, tmp, n) < 0)
            return -1;
        buf += n;
        len -= n;
    }
    return 0;
}
