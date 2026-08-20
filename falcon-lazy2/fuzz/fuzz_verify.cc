/*
 * libFuzzer harness: falcon_verify() with attacker-controlled signature bytes
 * against a fixed, valid public key. Exercises the signature header/nonce
 * parsing and all three payload decoders (compressed / padded / CT) plus the
 * verify math. Build with fuzz/build.sh (clang -fsanitize=fuzzer,address,undefined).
 */
#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "falcon.h"
}

#define LOGN 9

static int inited = 0;
static uint8_t pk[FALCON_PUBKEY_SIZE(LOGN)];
static uint8_t vtmp[FALCON_TMPSIZE_VERIFY(LOGN)];

static void setup(void) {
    static const unsigned char seed[16] = {1, 2, 3, 4, 5, 6, 7, 8,
                                           9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t sk[FALCON_PRIVKEY_SIZE(LOGN)];
    uint8_t ktmp[FALCON_TMPSIZE_KEYGEN(LOGN)];
    shake256_context rng;
    shake256_init_prng_from_seed(&rng, seed, sizeof seed);
    falcon_keygen_make(&rng, LOGN, sk, sizeof sk, pk, sizeof pk, ktmp, sizeof ktmp);
    inited = 1;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!inited) setup();
    const char *msg = "fuzz";
    /* try each declared signature type so every decoder is exercised */
    for (int t = FALCON_SIG_COMPRESSED; t <= FALCON_SIG_CT; ++t) {
        (void)falcon_verify(data, size, t, pk, sizeof pk, msg, 4, vtmp, sizeof vtmp);
    }
    return 0;
}
