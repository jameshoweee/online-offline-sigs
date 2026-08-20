#ifndef API_H
#define API_H

#include <stddef.h>
#include <stdint.h>

/* Falcon-512 (logn=9), CT (constant-time / fixed-size) signature format.
 * The secret key stores the Falcon private key followed by the public key,
 * because the online/offline signer (falcon_sign_dyn_lazy) needs both. */
#define CRYPTO_SECRETKEYBYTES 2178   /* 1281 privkey + 897 pubkey */
#define CRYPTO_PUBLICKEYBYTES 897
#define CRYPTO_BYTES 809             /* FALCON_SIG_CT_SIZE(9) */
#define CRYPTO_ALGNAME "Falcon-OO-512"

int crypto_sign_keypair(uint8_t *pk, uint8_t *sk);

int crypto_sign(uint8_t *sm, size_t *smlen,
                const uint8_t *m, size_t mlen,
                const uint8_t *sk);

int crypto_sign_open(uint8_t *m, size_t *mlen,
                     const uint8_t *sm, size_t smlen,
                     const uint8_t *pk);

#endif
