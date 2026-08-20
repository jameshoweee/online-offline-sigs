/*
 * libFuzzer harness: falcon_verify() with attacker-controlled public-key bytes
 * and a fixed, valid signature. Exercises the public-key decoder (modq_decode)
 * and the length/format checks. Build with fuzz/build.sh.
 */
#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "falcon.h"
}

#define LOGN 9

static int inited = 0;
static uint8_t sig[FALCON_SIG_COMPRESSED_MAXSIZE(LOGN)];
static size_t sig_len;
static uint8_t vtmp[FALCON_TMPSIZE_VERIFY(LOGN)];

static void setup(void) {
    static const unsigned char seed[16] = {1, 2, 3, 4, 5, 6, 7, 8,
                                           9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t sk[FALCON_PRIVKEY_SIZE(LOGN)];
    uint8_t pk[FALCON_PUBKEY_SIZE(LOGN)];
    uint8_t tmp[FALCON_TMPSIZE_SIGNDYN(LOGN)];
    uint8_t ktmp[FALCON_TMPSIZE_KEYGEN(LOGN)];
    shake256_context rng;
    shake256_init_prng_from_seed(&rng, seed, sizeof seed);
    falcon_keygen_make(&rng, LOGN, sk, sizeof sk, pk, sizeof pk, ktmp, sizeof ktmp);
    sig_len = sizeof sig;
    falcon_sign_dyn(&rng, sig, &sig_len, FALCON_SIG_COMPRESSED, sk, sizeof sk, "fuzz", 4, tmp,
                    sizeof tmp);
    inited = 1;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!inited) setup();
    (void)falcon_verify(sig, sig_len, FALCON_SIG_COMPRESSED, data, size, "fuzz", 4, vtmp,
                        sizeof vtmp);
    return 0;
}
