/*
 * Repetition benchmark for the online/offline Falcon and the comparison
 * schemes. Unlike bench_lazy_falcon.cpp (google-benchmark, one fixed key and
 * message reused across all iterations), this harness runs N independent
 * repetitions (default 1000, like the paper), each with FRESH randomness:
 * a new key, a new message, a new offline token. It times keygen, sign
 * (offline + online separately), the reference full signer sign_dyn, and
 * verify, then the same operations for ed25519 and Dilithium2 (ML-DSA-44).
 *
 * Portable wall-clock time is always reported (works on Mac arm64 and the
 * x86 box); on x86_64 invariant-TSC cycles are reported in addition.
 *
 *   ./falcon_bench_reps [reps]     (reps defaults to 1000)
 */
#include "testlib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#if defined(__x86_64__)
#include <x86intrin.h>
#define HAVE_CYCLES 1
static inline uint64_t cycles_now() {
    unsigned aux;
    _mm_lfence();
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}
#else
#define HAVE_CYCLES 0
#endif

using clk = std::chrono::steady_clock;

// One measurement sample: nanoseconds and (on x86) cycles.
struct Sample {
    double ns;
    double cyc;
};

struct Stats {
    double median, mean, minv, stdv;
    size_t n;
};

static Stats summarize(std::vector<double> v) {
    Stats s{};
    s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.minv = v.front();
    s.median = v[v.size() / 2];
    double sum = 0, sumsq = 0;
    for (double x : v) { sum += x; sumsq += x * x; }
    s.mean = sum / v.size();
    double var = sumsq / v.size() - s.mean * s.mean;
    s.stdv = var > 0 ? std::sqrt(var) : 0;
    return s;
}

// A timed op collects two vectors so we can summarize time and cycles apart.
struct Timer {
    std::vector<double> ns, cyc;
    void reserve(size_t n) { ns.reserve(n); cyc.reserve(n); }
    template <class F> void run(F&& f) {
#if HAVE_CYCLES
        uint64_t c0 = cycles_now();
#endif
        auto t0 = clk::now();
        f();
        auto t1 = clk::now();
#if HAVE_CYCLES
        uint64_t c1 = cycles_now();
        cyc.push_back((double)(c1 - c0));
#endif
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
};

static void print_header(const char* title, int reps) {
    printf("\n=== %s (%d reps, fresh randomness each rep) ===\n", title, reps);
#if HAVE_CYCLES
    printf("%-18s %10s %10s %10s %10s | %12s %12s %12s\n",
           "operation", "med(us)", "mean(us)", "min(us)", "stdv(us)",
           "med(cyc)", "mean(cyc)", "min(cyc)");
#else
    printf("%-18s %10s %10s %10s %10s\n",
           "operation", "med(us)", "mean(us)", "min(us)", "stdv(us)");
#endif
}

static void print_row(const char* op, const Timer& t) {
    Stats ns = summarize(t.ns);
#if HAVE_CYCLES
    Stats cy = summarize(t.cyc);
    printf("%-18s %10.3f %10.3f %10.3f %10.3f | %12.0f %12.0f %12.0f\n",
           op, ns.median / 1e3, ns.mean / 1e3, ns.minv / 1e3, ns.stdv / 1e3,
           cy.median, cy.mean, cy.minv);
#else
    printf("%-18s %10.3f %10.3f %10.3f %10.3f\n",
           op, ns.median / 1e3, ns.mean / 1e3, ns.minv / 1e3, ns.stdv / 1e3);
#endif
}

// -------------------------------------------------------------------------
// Falcon online/offline (the novel scheme). logn = 9 (Falcon-512, NIST-L1).
// -------------------------------------------------------------------------
static void bench_falcon_oo(int reps) {
    const unsigned logn = 9;
    const size_t n = (size_t)1 << logn;

    std::random_device rd;

    std::vector<int8_t> f(n), g(n), F(n), G(n), s1(n), s2(n);
    std::vector<uint16_t> h(n), hmonty(n), starget(n), hm(n);
    std::vector<fpr> ff(n), gf(n), Ff(n), Gf(n);
    std::vector<int16_t> sig(n), sig_dyn(n);
    uint8_t* tmp = (uint8_t*)aligned_alloc(64, 1024 * 1024);

    Timer t_keygen, t_offline, t_online, t_signdyn, t_verify;
    t_keygen.reserve(reps); t_offline.reserve(reps); t_online.reserve(reps);
    t_signdyn.reserve(reps); t_verify.reserve(reps);
    size_t verify_ok = 0;

    for (int rep = 0; rep < reps; ++rep) {
        // fresh 48-byte seed -> fresh SHAKE256 PRNG for this repetition
        uint8_t seed[48];
        for (size_t i = 0; i < sizeof seed; ++i) seed[i] = (uint8_t)rd();
        inner_shake256_context rng;
        inner_shake256_init(&rng);
        inner_shake256_inject(&rng, seed, sizeof seed);
        inner_shake256_flip(&rng);

        t_keygen.run([&] {
            Zf(keygen)(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn, tmp);
        });

        // public key in NTT+Montgomery for verify
        memcpy(hmonty.data(), h.data(), n * sizeof(uint16_t));
        Zf(to_ntt_monty)(hmonty.data(), logn);

        // fresh random message hash for this repetition
        for (size_t i = 0; i < n; ++i) hm[i] = (uint16_t)(rd() % F_Q);

        t_offline.run([&] {
            sign_dyn_lazy_offline(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn,
                                  s1.data(), s2.data(), starget.data(),
                                  ff.data(), gf.data(), Ff.data(), Gf.data());
        });
        t_online.run([&] {
            sign_dyn_lazy_online(s1.data(), s2.data(), starget.data(), sig.data(),
                                 ff.data(), gf.data(), Ff.data(), Gf.data(), hm.data(), logn, nullptr);
        });

        t_signdyn.run([&] {
            Zf(sign_dyn)(sig_dyn.data(), &rng, f.data(), g.data(), F.data(), G.data(),
                         hm.data(), logn, tmp);
        });

        // produce an in-bound lazy signature (retry the one-time offline token), then time verify
        int ok = 0;
        for (int a = 0; a < 64 && !ok; ++a) {
            sign_dyn_lazy_offline(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn,
                                  s1.data(), s2.data(), starget.data(),
                                  ff.data(), gf.data(), Ff.data(), Gf.data());
            ok = sign_dyn_lazy_online(s1.data(), s2.data(), starget.data(), sig.data(),
                                      ff.data(), gf.data(), Ff.data(), Gf.data(), hm.data(), logn, nullptr);
        }
        int r = 0;
        t_verify.run([&] {
            r = Zf(verify_raw)(hm.data(), sig.data(), hmonty.data(), logn, tmp);
        });
        if (r == 1) ++verify_ok;
    }
    free(tmp);

    print_header("Falcon online/offline (logn=9, Falcon-512)", reps);
    print_row("keygen", t_keygen);
    print_row("sign_offline", t_offline);
    print_row("sign_online", t_online);
    print_row("sign_dyn(orig)", t_signdyn);
    print_row("verify", t_verify);
    printf("verify_ok: %zu/%d\n", verify_ok, reps);
}

// -------------------------------------------------------------------------
// ed25519 comparison
// -------------------------------------------------------------------------
#include "ed25519.h"
static void bench_ed25519(int reps) {
    const size_t MSGBYTES = 64;
    std::random_device rd;
    unsigned char pk[32], sk[64], seed[32], sig[64];
    unsigned char msg[MSGBYTES];

    Timer t_keygen, t_sign, t_verify;
    t_keygen.reserve(reps); t_sign.reserve(reps); t_verify.reserve(reps);
    size_t verify_ok = 0;

    for (int rep = 0; rep < reps; ++rep) {
        ed25519_create_seed(seed);          // fresh OS entropy
        t_keygen.run([&] { ed25519_create_keypair(pk, sk, seed); });
        for (size_t i = 0; i < MSGBYTES; ++i) msg[i] = (uint8_t)rd();
        t_sign.run([&] { ed25519_sign(sig, msg, MSGBYTES, pk, sk); });
        int r = 0;
        t_verify.run([&] { r = ed25519_verify(sig, msg, MSGBYTES, pk); });
        if (r == 1) ++verify_ok;
    }

    print_header("ed25519", reps);
    print_row("keygen", t_keygen);
    print_row("sign", t_sign);
    print_row("verify", t_verify);
    printf("verify_ok: %zu/%d\n", verify_ok, reps);
}

// -------------------------------------------------------------------------
// Dilithium2 / ML-DSA-44 comparison (reference C, and AVX2 on x86)
// -------------------------------------------------------------------------
extern "C" {
#include "dilithium/ref/sign.h"
}
static void bench_dilithium_ref(int reps) {
    const size_t MSGBYTES = 64;
    std::random_device rd;
    uint8_t pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES], sig[CRYPTO_BYTES];
    uint8_t msg[MSGBYTES];
    size_t siglen = 0;

    Timer t_keygen, t_sign, t_verify;
    t_keygen.reserve(reps); t_sign.reserve(reps); t_verify.reserve(reps);
    size_t verify_ok = 0;

    for (int rep = 0; rep < reps; ++rep) {
        t_keygen.run([&] { pqcrystals_dilithium2_ref_keypair(pk, sk); });
        for (size_t i = 0; i < MSGBYTES; ++i) msg[i] = (uint8_t)rd();
        t_sign.run([&] { pqcrystals_dilithium2_ref_signature(sig, &siglen, msg, MSGBYTES, sk); });
        int r = 0;
        t_verify.run([&] { r = pqcrystals_dilithium2_ref_verify(sig, siglen, msg, MSGBYTES, pk); });
        if (r == 0) ++verify_ok;   // dilithium verify returns 0 on success
    }

    print_header("Dilithium2 / ML-DSA-44 (reference C)", reps);
    print_row("keygen", t_keygen);
    print_row("sign", t_sign);
    print_row("verify", t_verify);
    printf("verify_ok: %zu/%d\n", verify_ok, reps);
}

#ifdef __x86_64__
extern "C" typeof(pqcrystals_dilithium2_ref_keypair) pqcrystals_dilithium2_avx2_keypair;
extern "C" typeof(pqcrystals_dilithium2_ref_signature) pqcrystals_dilithium2_avx2_signature;
extern "C" typeof(pqcrystals_dilithium2_ref_verify) pqcrystals_dilithium2_avx2_verify;

static void bench_dilithium_avx(int reps) {
    const size_t MSGBYTES = 64;
    std::random_device rd;
    uint8_t pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES], sig[CRYPTO_BYTES];
    uint8_t msg[MSGBYTES];
    size_t siglen = 0;

    Timer t_keygen, t_sign, t_verify;
    t_keygen.reserve(reps); t_sign.reserve(reps); t_verify.reserve(reps);
    size_t verify_ok = 0;

    for (int rep = 0; rep < reps; ++rep) {
        t_keygen.run([&] { pqcrystals_dilithium2_avx2_keypair(pk, sk); });
        for (size_t i = 0; i < MSGBYTES; ++i) msg[i] = (uint8_t)rd();
        t_sign.run([&] { pqcrystals_dilithium2_avx2_signature(sig, &siglen, msg, MSGBYTES, sk); });
        int r = 0;
        t_verify.run([&] { r = pqcrystals_dilithium2_avx2_verify(sig, siglen, msg, MSGBYTES, pk); });
        if (r == 0) ++verify_ok;
    }

    print_header("Dilithium2 / ML-DSA-44 (AVX2)", reps);
    print_row("keygen", t_keygen);
    print_row("sign", t_sign);
    print_row("verify", t_verify);
    printf("verify_ok: %zu/%d\n", verify_ok, reps);
}
#endif

int main(int argc, char** argv) {
    int reps = 1000;
    if (argc > 1) reps = atoi(argv[1]);
    if (reps < 1) reps = 1;

#if HAVE_CYCLES
    printf("platform: x86_64 (wall-clock time + invariant-TSC cycles)\n");
#else
    printf("platform: non-x86 (wall-clock time only)\n");
#endif
    printf("repetitions per operation: %d\n", reps);

    bench_falcon_oo(reps);
    bench_ed25519(reps);
    bench_dilithium_ref(reps);
#ifdef __x86_64__
    bench_dilithium_avx(reps);
#endif
    return 0;
}
