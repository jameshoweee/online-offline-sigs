/* Emit one valid signature and public key as fuzzing seed corpus. */
#include <stdio.h>
#include <stdlib.h>
#include "falcon.h"

#define LOGN 9

static void wr(const char *path, const void *buf, size_t n) {
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf, 1, n, f); fclose(f); }
}

int main(void) {
    static const unsigned char seed[16] = {1, 2, 3, 4, 5, 6, 7, 8,
                                           9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t sk[FALCON_PRIVKEY_SIZE(LOGN)];
    uint8_t pk[FALCON_PUBKEY_SIZE(LOGN)];
    uint8_t sig[FALCON_SIG_COMPRESSED_MAXSIZE(LOGN)];
    uint8_t ktmp[FALCON_TMPSIZE_KEYGEN(LOGN)];
    uint8_t stmp[FALCON_TMPSIZE_SIGNDYN(LOGN)];
    shake256_context rng;
    shake256_init_prng_from_seed(&rng, seed, sizeof seed);
    falcon_keygen_make(&rng, LOGN, sk, sizeof sk, pk, sizeof pk, ktmp, sizeof ktmp);
    size_t sl = sizeof sig;
    falcon_sign_dyn(&rng, sig, &sl, FALCON_SIG_COMPRESSED, sk, sizeof sk, "fuzz", 4, stmp,
                    sizeof stmp);
    wr("corpus_verify/sig.bin", sig, sl);
    wr("corpus_pubkey/pk.bin", pk, sizeof pk);
    wr("corpus_comp/sig.bin", sig, sl);
    printf("seeds written: sig_len=%zu pk_len=%zu\n", sl, sizeof pk);
    return 0;
}
