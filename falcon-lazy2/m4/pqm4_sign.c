/* pqm4 crypto_sign shim for the online/offline Falcon (Falcon-OO-512).
 * keypair = Falcon keygen; sign = falcon_sign_dyn_lazy (offline+online);
 * open = falcon_verify. Signed message layout: sig(809) || message. */
#include "api.h"
#include "falcon.h"
#include "randombytes.h"
#include <string.h>

#define LOGN 9
#define PRIVLEN 1281  /* FALCON_PRIVKEY_SIZE(9) */
#define PUBLEN  897   /* FALCON_PUBKEY_SIZE(9)  */
#define SIGLEN  809   /* FALCON_SIG_CT_SIZE(9)  */

static void seed_rng(shake256_context *rng) {
    uint8_t seed[48];
    randombytes(seed, sizeof seed);
    shake256_init_prng_from_seed(rng, seed, sizeof seed);
}

int crypto_sign_keypair(uint8_t *pk, uint8_t *sk) {
    shake256_context rng;
    static uint8_t tmp[FALCON_TMPSIZE_KEYGEN(LOGN)];
    seed_rng(&rng);
    if (falcon_keygen_make(&rng, LOGN, sk, PRIVLEN, pk, PUBLEN, tmp, sizeof tmp) != 0)
        return -1;
    memcpy(sk + PRIVLEN, pk, PUBLEN); /* stash pubkey in sk for signing */
    return 0;
}

int crypto_sign(uint8_t *sm, size_t *smlen, const uint8_t *m, size_t mlen,
                const uint8_t *sk) {
    shake256_context rng;
    static uint8_t tmp[FALCON_TMPSIZE_SIGNDYN(LOGN)];
    const uint8_t *privkey = sk;
    const uint8_t *pubkey = sk + PRIVLEN;
    size_t siglen = SIGLEN;
    seed_rng(&rng);
    memmove(sm + SIGLEN, m, mlen);
    if (falcon_sign_dyn_lazy(&rng, sm, &siglen, FALCON_SIG_CT, pubkey, PUBLEN, privkey, PRIVLEN,
                             sm + SIGLEN, mlen, tmp, sizeof tmp) != 0)
        return -1;
    *smlen = SIGLEN + mlen;
    return 0;
}

int crypto_sign_open(uint8_t *m, size_t *mlen, const uint8_t *sm, size_t smlen,
                     const uint8_t *pk) {
    static uint8_t tmp[FALCON_TMPSIZE_VERIFY(LOGN)];
    size_t msglen;
    if (smlen < SIGLEN) return -1;
    msglen = smlen - SIGLEN;
    if (falcon_verify(sm, SIGLEN, FALCON_SIG_CT, pk, PUBLEN, sm + SIGLEN, msglen, tmp,
                      sizeof tmp) != 0)
        return -1;
    memmove(m, sm + SIGLEN, msglen);
    *mlen = msglen;
    return 0;
}
