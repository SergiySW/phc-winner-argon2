/*
 * Argon2 reference source code package - reference C implementations
 *
 * Copyright 2015
 * Daniel Dinu, Dmitry Khovratovich, Jean-Philippe Aumasson, and Samuel Neves
 *
 * You may use this work under the terms of a Creative Commons CC0 1.0
 * License/Waiver or the Apache Public License 2.0, at your option. The terms of
 * these licenses can be found at:
 *
 * - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
 * - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0
 *
 * You should have received a copy of both of these licenses along with this
 * software. If not, they may be obtained at the above URLs.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "blake2.h"
#include "blake2-impl.h"

static const uint64_t argon2_blake2b_IV[8] = {
    UINT64_C(0x6a09e667f3bcc908), UINT64_C(0xbb67ae8584caa73b),
    UINT64_C(0x3c6ef372fe94f82b), UINT64_C(0xa54ff53a5f1d36f1),
    UINT64_C(0x510e527fade682d1), UINT64_C(0x9b05688c2b3e6c1f),
    UINT64_C(0x1f83d9abfb41bd6b), UINT64_C(0x5be0cd19137e2179)};

static const unsigned int argon2_blake2b_sigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
};

static ARGON2_BLAKE2_INLINE void argon2_blake2b_set_lastnode(argon2_blake2b_state *S) {
    S->f[1] = (uint64_t)-1;
}

static ARGON2_BLAKE2_INLINE void argon2_blake2b_set_lastblock(argon2_blake2b_state *S) {
    if (S->last_node) {
        argon2_blake2b_set_lastnode(S);
    }
    S->f[0] = (uint64_t)-1;
}

static ARGON2_BLAKE2_INLINE void argon2_blake2b_increment_counter(argon2_blake2b_state *S,
                                                    uint64_t inc) {
    S->t[0] += inc;
    S->t[1] += (S->t[0] < inc);
}

static ARGON2_BLAKE2_INLINE void argon2_blake2b_invalidate_state(argon2_blake2b_state *S) {
    clear_internal_memory(S, sizeof(*S));      /* wipe */
    argon2_blake2b_set_lastblock(S); /* invalidate for further use */
}

static ARGON2_BLAKE2_INLINE void argon2_blake2b_init0(argon2_blake2b_state *S) {
    memset(S, 0, sizeof(*S));
    memcpy(S->h, argon2_blake2b_IV, sizeof(S->h));
}

int argon2_blake2b_init_param(argon2_blake2b_state *S, const argon2_blake2b_param *P) {
    const unsigned char *p = (const unsigned char *)P;
    unsigned int i;

    if (NULL == P || NULL == S) {
        return -1;
    }

    argon2_blake2b_init0(S);
    /* IV XOR Parameter Block */
    for (i = 0; i < 8; ++i) {
        S->h[i] ^= load64(&p[i * sizeof(S->h[i])]);
    }
    S->outlen = P->digest_length;
    return 0;
}

/* Sequential blake2b initialization */
int argon2_blake2b_init(argon2_blake2b_state *S, size_t outlen) {
    argon2_blake2b_param P;

    if (S == NULL) {
        return -1;
    }

    if ((outlen == 0) || (outlen > ARGON2_BLAKE2B_OUTBYTES)) {
        argon2_blake2b_invalidate_state(S);
        return -1;
    }

    /* Setup Parameter Block for unkeyed BLAKE2 */
    P.digest_length = (uint8_t)outlen;
    P.key_length = 0;
    P.fanout = 1;
    P.depth = 1;
    P.leaf_length = 0;
    P.node_offset = 0;
    P.node_depth = 0;
    P.inner_length = 0;
    memset(P.reserved, 0, sizeof(P.reserved));
    memset(P.salt, 0, sizeof(P.salt));
    memset(P.personal, 0, sizeof(P.personal));

    return argon2_blake2b_init_param(S, &P);
}

int argon2_blake2b_init_key(argon2_blake2b_state *S, size_t outlen, const void *key,
                     size_t keylen) {
    argon2_blake2b_param P;

    if (S == NULL) {
        return -1;
    }

    if ((outlen == 0) || (outlen > ARGON2_BLAKE2B_OUTBYTES)) {
        argon2_blake2b_invalidate_state(S);
        return -1;
    }

    if ((key == 0) || (keylen == 0) || (keylen > ARGON2_BLAKE2B_KEYBYTES)) {
        argon2_blake2b_invalidate_state(S);
        return -1;
    }

    /* Setup Parameter Block for keyed BLAKE2 */
    P.digest_length = (uint8_t)outlen;
    P.key_length = (uint8_t)keylen;
    P.fanout = 1;
    P.depth = 1;
    P.leaf_length = 0;
    P.node_offset = 0;
    P.node_depth = 0;
    P.inner_length = 0;
    memset(P.reserved, 0, sizeof(P.reserved));
    memset(P.salt, 0, sizeof(P.salt));
    memset(P.personal, 0, sizeof(P.personal));

    if (argon2_blake2b_init_param(S, &P) < 0) {
        argon2_blake2b_invalidate_state(S);
        return -1;
    }

    {
        uint8_t block[ARGON2_BLAKE2B_BLOCKBYTES];
        memset(block, 0, ARGON2_BLAKE2B_BLOCKBYTES);
        memcpy(block, key, keylen);
        argon2_blake2b_update(S, block, ARGON2_BLAKE2B_BLOCKBYTES);
        /* Burn the key from stack */
        clear_internal_memory(block, ARGON2_BLAKE2B_BLOCKBYTES);
    }
    return 0;
}

static void argon2_blake2b_compress(argon2_blake2b_state *S, const uint8_t *block) {
    uint64_t m[16];
    uint64_t v[16];
    unsigned int i, r;

    for (i = 0; i < 16; ++i) {
        m[i] = load64(block + i * sizeof(m[i]));
    }

    for (i = 0; i < 8; ++i) {
        v[i] = S->h[i];
    }

    v[8] = argon2_blake2b_IV[0];
    v[9] = argon2_blake2b_IV[1];
    v[10] = argon2_blake2b_IV[2];
    v[11] = argon2_blake2b_IV[3];
    v[12] = argon2_blake2b_IV[4] ^ S->t[0];
    v[13] = argon2_blake2b_IV[5] ^ S->t[1];
    v[14] = argon2_blake2b_IV[6] ^ S->f[0];
    v[15] = argon2_blake2b_IV[7] ^ S->f[1];

#define G(r, i, a, b, c, d)                                                    \
    do {                                                                       \
        a = a + b + m[argon2_blake2b_sigma[r][2 * i + 0]];                            \
        d = rotr64(d ^ a, 32);                                                 \
        c = c + d;                                                             \
        b = rotr64(b ^ c, 24);                                                 \
        a = a + b + m[argon2_blake2b_sigma[r][2 * i + 1]];                            \
        d = rotr64(d ^ a, 16);                                                 \
        c = c + d;                                                             \
        b = rotr64(b ^ c, 63);                                                 \
    } while ((void)0, 0)

#define ROUND(r)                                                               \
    do {                                                                       \
        G(r, 0, v[0], v[4], v[8], v[12]);                                      \
        G(r, 1, v[1], v[5], v[9], v[13]);                                      \
        G(r, 2, v[2], v[6], v[10], v[14]);                                     \
        G(r, 3, v[3], v[7], v[11], v[15]);                                     \
        G(r, 4, v[0], v[5], v[10], v[15]);                                     \
        G(r, 5, v[1], v[6], v[11], v[12]);                                     \
        G(r, 6, v[2], v[7], v[8], v[13]);                                      \
        G(r, 7, v[3], v[4], v[9], v[14]);                                      \
    } while ((void)0, 0)

    for (r = 0; r < 12; ++r) {
        ROUND(r);
    }

    for (i = 0; i < 8; ++i) {
        S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
    }

#undef G
#undef ROUND
}

int argon2_blake2b_update(argon2_blake2b_state *S, const void *in, size_t inlen) {
    const uint8_t *pin = (const uint8_t *)in;

    if (inlen == 0) {
        return 0;
    }

    /* Sanity check */
    if (S == NULL || in == NULL) {
        return -1;
    }

    /* Is this a reused state? */
    if (S->f[0] != 0) {
        return -1;
    }

    if (S->buflen + inlen > ARGON2_BLAKE2B_BLOCKBYTES) {
        /* Complete current block */
        size_t left = S->buflen;
        size_t fill = ARGON2_BLAKE2B_BLOCKBYTES - left;
        memcpy(&S->buf[left], pin, fill);
        argon2_blake2b_increment_counter(S, ARGON2_BLAKE2B_BLOCKBYTES);
        argon2_blake2b_compress(S, S->buf);
        S->buflen = 0;
        inlen -= fill;
        pin += fill;
        /* Avoid buffer copies when possible */
        while (inlen > ARGON2_BLAKE2B_BLOCKBYTES) {
            argon2_blake2b_increment_counter(S, ARGON2_BLAKE2B_BLOCKBYTES);
            argon2_blake2b_compress(S, pin);
            inlen -= ARGON2_BLAKE2B_BLOCKBYTES;
            pin += ARGON2_BLAKE2B_BLOCKBYTES;
        }
    }
    memcpy(&S->buf[S->buflen], pin, inlen);
    S->buflen += (unsigned int)inlen;
    return 0;
}

int argon2_blake2b_final(argon2_blake2b_state *S, void *out, size_t outlen) {
    uint8_t buffer[ARGON2_BLAKE2B_OUTBYTES] = {0};
    unsigned int i;

    /* Sanity checks */
    if (S == NULL || out == NULL || outlen < S->outlen) {
        return -1;
    }

    /* Is this a reused state? */
    if (S->f[0] != 0) {
        return -1;
    }

    argon2_blake2b_increment_counter(S, S->buflen);
    argon2_blake2b_set_lastblock(S);
    memset(&S->buf[S->buflen], 0, ARGON2_BLAKE2B_BLOCKBYTES - S->buflen); /* Padding */
    argon2_blake2b_compress(S, S->buf);

    for (i = 0; i < 8; ++i) { /* Output full hash to temp buffer */
        store64(buffer + sizeof(S->h[i]) * i, S->h[i]);
    }

    memcpy(out, buffer, S->outlen);
    clear_internal_memory(buffer, sizeof(buffer));
    clear_internal_memory(S->buf, sizeof(S->buf));
    clear_internal_memory(S->h, sizeof(S->h));
    return 0;
}

int argon2_blake2b(void *out, size_t outlen, const void *in, size_t inlen,
            const void *key, size_t keylen) {
    argon2_blake2b_state S;
    int ret = -1;

    /* Verify parameters */
    if (NULL == in && inlen > 0) {
        goto fail;
    }

    if (NULL == out || outlen == 0 || outlen > ARGON2_BLAKE2B_OUTBYTES) {
        goto fail;
    }

    if ((NULL == key && keylen > 0) || keylen > ARGON2_BLAKE2B_KEYBYTES) {
        goto fail;
    }

    if (keylen > 0) {
        if (argon2_blake2b_init_key(&S, outlen, key, keylen) < 0) {
            goto fail;
        }
    } else {
        if (argon2_blake2b_init(&S, outlen) < 0) {
            goto fail;
        }
    }

    if (argon2_blake2b_update(&S, in, inlen) < 0) {
        goto fail;
    }
    ret = argon2_blake2b_final(&S, out, outlen);

fail:
    clear_internal_memory(&S, sizeof(S));
    return ret;
}

/* Argon2 Team - Begin Code */
int argon2_blake2b_long(void *pout, size_t outlen, const void *in, size_t inlen) {
    uint8_t *out = (uint8_t *)pout;
    argon2_blake2b_state blake_state;
    uint8_t outlen_bytes[sizeof(uint32_t)] = {0};
    int ret = -1;

    if (outlen > UINT32_MAX) {
        goto fail;
    }

    /* Ensure little-endian byte order! */
    store32(outlen_bytes, (uint32_t)outlen);

#define TRY(statement)                                                         \
    do {                                                                       \
        ret = statement;                                                       \
        if (ret < 0) {                                                         \
            goto fail;                                                         \
        }                                                                      \
    } while ((void)0, 0)

    if (outlen <= ARGON2_BLAKE2B_OUTBYTES) {
        TRY(argon2_blake2b_init(&blake_state, outlen));
        TRY(argon2_blake2b_update(&blake_state, outlen_bytes, sizeof(outlen_bytes)));
        TRY(argon2_blake2b_update(&blake_state, in, inlen));
        TRY(argon2_blake2b_final(&blake_state, out, outlen));
    } else {
        uint32_t toproduce;
        uint8_t out_buffer[ARGON2_BLAKE2B_OUTBYTES];
        uint8_t in_buffer[ARGON2_BLAKE2B_OUTBYTES];
        TRY(argon2_blake2b_init(&blake_state, ARGON2_BLAKE2B_OUTBYTES));
        TRY(argon2_blake2b_update(&blake_state, outlen_bytes, sizeof(outlen_bytes)));
        TRY(argon2_blake2b_update(&blake_state, in, inlen));
        TRY(argon2_blake2b_final(&blake_state, out_buffer, ARGON2_BLAKE2B_OUTBYTES));
        memcpy(out, out_buffer, ARGON2_BLAKE2B_OUTBYTES / 2);
        out += ARGON2_BLAKE2B_OUTBYTES / 2;
        toproduce = (uint32_t)outlen - ARGON2_BLAKE2B_OUTBYTES / 2;

        while (toproduce > ARGON2_BLAKE2B_OUTBYTES) {
            memcpy(in_buffer, out_buffer, ARGON2_BLAKE2B_OUTBYTES);
            TRY(argon2_blake2b(out_buffer, ARGON2_BLAKE2B_OUTBYTES, in_buffer,
                        ARGON2_BLAKE2B_OUTBYTES, NULL, 0));
            memcpy(out, out_buffer, ARGON2_BLAKE2B_OUTBYTES / 2);
            out += ARGON2_BLAKE2B_OUTBYTES / 2;
            toproduce -= ARGON2_BLAKE2B_OUTBYTES / 2;
        }

        memcpy(in_buffer, out_buffer, ARGON2_BLAKE2B_OUTBYTES);
        TRY(argon2_blake2b(out_buffer, toproduce, in_buffer, ARGON2_BLAKE2B_OUTBYTES, NULL,
                    0));
        memcpy(out, out_buffer, toproduce);
    }
fail:
    clear_internal_memory(&blake_state, sizeof(blake_state));
    return ret;
#undef TRY
}
/* Argon2 Team - End Code */
