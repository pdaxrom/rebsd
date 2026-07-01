/*
 * Small encrypted transport helper for RetroBSD telnet tools.
 *
 * This is a pre-shared-key stream layer for lab use.  It is not TELNET
 * STARTTLS and it is not a replacement for a real SSH port.
 */

#ifndef _TELCRYPTO_H_
#define _TELCRYPTO_H_

#define RTEL_NONCE_LEN 16
#define RTEL_PROOF_LEN 32

struct rtel_stream {
    unsigned char key[32];
    unsigned char nonce[12];
    unsigned long counter;
    unsigned char block[64];
    int used;
};

struct rtel_session {
    int enabled;
    struct rtel_stream rx;
    struct rtel_stream tx;
};

int rtel_client_handshake(int, char *, struct rtel_session *);
int rtel_server_handshake(int, char *, struct rtel_session *);
int rtel_read(int, struct rtel_session *, unsigned char *, int);
int rtel_write(int, struct rtel_session *, unsigned char *, int);

#endif
