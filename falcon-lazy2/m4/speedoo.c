/* Custom pqm4 speed harness for the online/offline Falcon: measures keygen,
 * sign_offline, sign_online, sign_dyn (reference full signer) and verify
 * SEPARATELY, on the real M4 at CLOCK_FAST. Only meaningful for the
 * falcon-oo-* scheme (uses its inner API); build with:
 *   make PLATFORM=stm32f4discovery elf/crypto_sign_falcon-oo-512_ref_speedoo.elf
 */
#include "hal.h"
#include "sendfn.h"
#include "randombytes.h"

#include <stdint.h>
#include <string.h>

#define restrict
#include "inner.h"

#ifndef OO_ITER
#define OO_ITER 3
#endif
#define LOGN 9
#define N 512

/* defined in the scheme's sign.c, not prototyped in inner.h */
void sign_dyn_lazy_offline(inner_shake256_context *rng, const int8_t *f, const int8_t *g,
                           const int8_t *F, const int8_t *G, const uint16_t *h, unsigned logn,
                           int8_t *s1, int8_t *s2, uint16_t *st, fpr *ff, fpr *gf, fpr *Ff,
                           fpr *Gf);
int sign_dyn_lazy_online(int8_t *s1, int8_t *s2, uint16_t *st, int16_t *sig, const fpr *ff,
                         const fpr *gf, const fpr *Ff, const fpr *Gf, const uint16_t *hm,
                         unsigned logn, fpr *tmp);

static int8_t f[N], g[N], F[N], G[N], s1[N], s2s[N];
static uint16_t h[N], hmonty[N], starget[N], hm[N];
static fpr ff[N], gf[N], Ff[N], Gf[N];
static int16_t sig[N];
static uint8_t tmp[40000]; /* max of keygen(15879)/sign_dyn(39943)/verify(4097) */

int main(void) {
    hal_setup(CLOCK_FAST);
    hal_send_str("==========================");

    inner_shake256_context rng;
    uint8_t seed[48];
    unsigned long long t0, t1;

    for (int it = 0; it < OO_ITER; it++) {
        randombytes(seed, sizeof seed);
        inner_shake256_init(&rng);
        inner_shake256_inject(&rng, seed, sizeof seed);
        inner_shake256_flip(&rng);

        hal_spraystack();
        t0 = hal_get_time();
        falcon_inner_keygen(&rng, f, g, F, G, h, LOGN, tmp);
        t1 = hal_get_time();
        send_unsignedll("keygen cycles:", t1 - t0);
        send_unsignedll("keygen stack:", (unsigned long long)hal_checkstack());

        memcpy(hmonty, h, sizeof h);
        falcon_inner_to_ntt_monty(hmonty, LOGN);
        randombytes((uint8_t *)hm, sizeof hm);
        for (int i = 0; i < N; i++) hm[i] %= 12289;

        hal_spraystack();
        t0 = hal_get_time();
        sign_dyn_lazy_offline(&rng, f, g, F, G, h, LOGN, s1, s2s, starget, ff, gf, Ff, Gf);
        t1 = hal_get_time();
        send_unsignedll("sign_offline cycles:", t1 - t0);
        send_unsignedll("sign_offline stack:", (unsigned long long)hal_checkstack());

        hal_spraystack();
        t0 = hal_get_time();
        sign_dyn_lazy_online(s1, s2s, starget, sig, ff, gf, Ff, Gf, hm, LOGN, NULL);
        t1 = hal_get_time();
        send_unsignedll("sign_online cycles:", t1 - t0);
        send_unsignedll("sign_online stack:", (unsigned long long)hal_checkstack());

        t0 = hal_get_time();
        falcon_inner_sign_dyn(sig, &rng, f, g, F, G, hm, LOGN, tmp);
        t1 = hal_get_time();
        send_unsignedll("sign_dyn_orig cycles:", t1 - t0);

        /* produce a valid lazy signature (retry the one-time offline on over-bound) */
        int ok = 0;
        for (int a = 0; a < 64 && !ok; a++) {
            sign_dyn_lazy_offline(&rng, f, g, F, G, h, LOGN, s1, s2s, starget, ff, gf, Ff, Gf);
            ok = sign_dyn_lazy_online(s1, s2s, starget, sig, ff, gf, Ff, Gf, hm, LOGN, NULL);
        }
        t0 = hal_get_time();
        int r = falcon_inner_verify_raw(hm, sig, hmonty, LOGN, tmp);
        t1 = hal_get_time();
        send_unsignedll("verify cycles:", t1 - t0);
        send_unsignedll("verify_ok:", (unsigned long long)(r == 1));
    }

    hal_send_str("#");
    while (1) {
    }
    return 0;
}
