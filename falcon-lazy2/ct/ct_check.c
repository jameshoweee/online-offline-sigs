/*
 * ctgrind-style constant-time DIAGNOSIS (report only, no hardening).
 *
 * Marks the secret key (f,g,F,G) and the offline Gaussian samples as
 * "uninitialised" via Valgrind memcheck client requests, then runs the
 * offline and online phases. Under `valgrind --tool=memcheck`, any
 * "Conditional jump or move depends on uninitialised value" (or a
 * secret-indexed memory access) pinpoints a secret-dependent branch, i.e. a
 * potential timing leak. No output of that kind means no secret-dependent
 * control flow was observed on the exercised path.
 *
 * Build: gcc -O2 -I.. ct/ct_check.c ../{codec,common,falcon,fft,fpr,keygen,rng,shake,sign,vrfy}.c -lm -o ct_check
 * Run:   valgrind -q --error-exitcode=0 ./ct_check
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/memcheck.h>

#define restrict
#include "inner.h"

/* defined in sign.c (not prototyped in inner.h) */
void sign_dyn_lazy_offline(inner_shake256_context *rng, const int8_t *f, const int8_t *g,
                           const int8_t *F, const int8_t *G, const uint16_t *h, unsigned logn,
                           int8_t *sample1, int8_t *sample2, uint16_t *sample_target, fpr *f_fft,
                           fpr *g_fft, fpr *F_fft, fpr *G_fft);
int sign_dyn_lazy_online(int8_t *sample1, int8_t *sample2, uint16_t *sample_target, int16_t *s2,
                         const fpr *f_fft, const fpr *g_fft, const fpr *F_fft, const fpr *G_fft,
                         const uint16_t *hm, unsigned logn, fpr *tmp);

int main(void) {
    const unsigned logn = 9;
    const size_t n = (size_t)1 << logn;

    inner_shake256_context rng;
    inner_shake256_init(&rng);
    uint8_t *tmp = (uint8_t *)malloc(2u << 20);

    int8_t f[1024], g[1024], F[1024], G[1024];
    uint16_t h[1024];
    falcon_inner_keygen(&rng, f, g, F, G, h, logn, tmp);

    /* mark the private key as secret */
    VALGRIND_MAKE_MEM_UNDEFINED(f, n * sizeof f[0]);
    VALGRIND_MAKE_MEM_UNDEFINED(g, n * sizeof g[0]);
    VALGRIND_MAKE_MEM_UNDEFINED(F, n * sizeof F[0]);
    VALGRIND_MAKE_MEM_UNDEFINED(G, n * sizeof G[0]);

    int8_t s1[1024], s2[1024];
    uint16_t starget[1024];
    fpr ff[1024], gf[1024], Ff[1024], Gf[1024];
    int16_t sig[1024];
    uint16_t hm[1024];
    for (size_t i = 0; i < n; ++i) hm[i] = (uint16_t)(i * 7 + 1) % 12289; /* public */

    sign_dyn_lazy_offline(&rng, f, g, F, G, h, logn, s1, s2, starget, ff, gf, Ff, Gf);

    /* mark the offline Gaussian samples as secret too */
    VALGRIND_MAKE_MEM_UNDEFINED(s1, n * sizeof s1[0]);
    VALGRIND_MAKE_MEM_UNDEFINED(s2, n * sizeof s2[0]);

    int r = sign_dyn_lazy_online(s1, s2, starget, sig, ff, gf, Ff, Gf, hm, logn, NULL);

    /* Prevent the result (secret-tainted) from being optimised away without
     * branching on it: reveal definedness only for a checksum print count. */
    size_t defined = VALGRIND_MAKE_MEM_DEFINED(sig, n * sizeof sig[0]);
    (void)defined;
    printf("ct_check done, online returned %d, sig[0]=%d\n", r, (int)sig[0]);
    free(tmp);
    return 0;
}
