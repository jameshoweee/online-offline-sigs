/*
 * Standalone cycle + correctness harness for the online/offline Falcon.
 *
 * Why this exists: the target box is an AWS guest with no hardware PMU
 * (perf "cycles" is unsupported and google-benchmark's libpfm counters are
 * unavailable), and the distro libbenchmark is a DEBUG build. So we measure
 * with rdtscp (invariant TSC), the standard pqm4/SUPERCOP fallback: warmup,
 * then median-of-N single-op samples with MAD, pinned to one core with taskset.
 *
 * The reported "cycles" are invariant-TSC ticks (nominal 2.5 GHz on the
 * 8259CL), NOT core-clock cycles. They are consistent across before/after so
 * relative deltas are meaningful; absolute figures should be read as TSC ticks.
 *
 * It also doubles as the C4 correctness oracle: it checks that lazy signatures
 * verify under Falcon's real verify_raw() bound (tighter than the unit tests'
 * loose 6000 proxy), and reports the pass rate.
 *
 * Build: wired into CMakeLists as target `bench_cycles`.
 * Run:   taskset -c 1 ./bench_cycles [logn=9] [--csv]
 */

#include "testlib.h"

#include <x86intrin.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* ---- clock ---- */
static inline uint64_t cycles_now(void) {
    unsigned aux;
    _mm_lfence();
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

/* ---- deterministic message PRNG (splitmix64), independent of the sampler ---- */
static uint64_t sm_state = 0x9E3779B97F4A7C15ULL;
static uint64_t sm_next(void) {
    uint64_t z = (sm_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static void random_hm(std::vector<uint16_t>& hm) {
    for (size_t i = 0; i < hm.size(); ++i) hm[i] = (uint16_t)(sm_next() % F_Q);
}

/* ---- stats ---- */
struct Stat { double median, mad, minv; };
static Stat summarize(std::vector<uint64_t> s) {
    std::sort(s.begin(), s.end());
    size_t n = s.size();
    double med = s[n / 2];
    std::vector<uint64_t> dev(n);
    for (size_t i = 0; i < n; ++i) {
        double d = (double)s[i] - med;
        dev[i] = (uint64_t)(d < 0 ? -d : d);
    }
    std::sort(dev.begin(), dev.end());
    return Stat{med, (double)dev[n / 2], (double)s[0]};
}

static volatile uint64_t g_sink = 0;

template <class F>
static Stat bench(size_t warmup, size_t iters, F f) {
    for (size_t i = 0; i < warmup; ++i) f();
    std::vector<uint64_t> s;
    s.reserve(iters);
    for (size_t i = 0; i < iters; ++i) {
        uint64_t t0 = cycles_now();
        f();
        uint64_t t1 = cycles_now();
        s.push_back(t1 - t0);
    }
    return summarize(std::move(s));
}

int main(int argc, char** argv) {
    unsigned logn = 9;
    bool csv = false;
    size_t trials = 2000;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--csv") csv = true;
        else if (a.rfind("--trials=", 0) == 0) trials = (size_t)atoll(a.c_str() + 9);
        else logn = (unsigned)atoi(argv[i]);
    }
    const size_t n = (size_t)1 << logn;

    inner_shake256_context rng;
    inner_shake256_init(&rng);
    uint8_t* tmp = (uint8_t*)aligned_alloc(64, 2u << 20);

    std::vector<int8_t> f(n), g(n), F(n), G(n);
    std::vector<uint16_t> h(n);

    /* keygen */
    Stat kg = bench(10, 100, [&] {
        Zf(keygen)(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn, tmp);
        g_sink ^= (uint64_t)f[0];
    });
    /* fix one key for the remaining benchmarks */
    Zf(keygen)(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn, tmp);
    std::vector<uint16_t> hmonty = h;
    Zf(to_ntt_monty)(hmonty.data(), logn);

    std::vector<uint16_t> hm(n);
    random_hm(hm);

    std::vector<int8_t> s1(n), s2s(n);
    std::vector<uint16_t> starget(n);
    std::vector<fpr> ff(n), gf(n), Ff(n), Gf(n);
    std::vector<int16_t> sig(n);

    /* sign offline (message-independent) */
    Stat off = bench(20, 3000, [&] {
        sign_dyn_lazy_offline(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn,
                              s1.data(), s2s.data(), starget.data(), ff.data(), gf.data(),
                              Ff.data(), Gf.data());
        g_sink ^= (uint64_t)starget[0];
    });

    /* sign online (message-dependent). cost is data-independent, target stays in [0,q). */
    sign_dyn_lazy_offline(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn,
                          s1.data(), s2s.data(), starget.data(), ff.data(), gf.data(),
                          Ff.data(), Gf.data());
    std::vector<uint16_t> starget0 = starget;
    Stat on = bench(50, 8000, [&] {
        sign_dyn_lazy_online(s1.data(), s2s.data(), starget.data(), sig.data(), ff.data(),
                             gf.data(), Ff.data(), Gf.data(), hm.data(), logn, nullptr);
        g_sink ^= (uint64_t)sig[0];
    });

    /* original dynamic signer (full FP ffSampling, for reference) */
    Stat sd = bench(10, 400, [&] {
        Zf(sign_dyn)(sig.data(), &rng, f.data(), g.data(), F.data(), G.data(), hm.data(), logn, tmp);
        g_sink ^= (uint64_t)sig[0];
    });

    /* verify (produce a valid lazy sig for hm first) */
    starget = starget0;
    sign_dyn_lazy_online(s1.data(), s2s.data(), starget.data(), sig.data(), ff.data(), gf.data(),
                         Ff.data(), Gf.data(), hm.data(), logn, nullptr);
    Stat vf = bench(50, 8000, [&] {
        g_sink ^= (uint64_t)Zf(verify_raw)(hm.data(), sig.data(), hmonty.data(), logn, tmp);
    });

    /* C4 correctness oracle: fresh offline+online, does it verify under the REAL bound? */
    size_t pass = 0;
    for (size_t t = 0; t < trials; ++t) {
        random_hm(hm);
        sign_dyn_lazy_offline(&rng, f.data(), g.data(), F.data(), G.data(), h.data(), logn,
                              s1.data(), s2s.data(), starget.data(), ff.data(), gf.data(),
                              Ff.data(), Gf.data());
        sign_dyn_lazy_online(s1.data(), s2s.data(), starget.data(), sig.data(), ff.data(),
                             gf.data(), Ff.data(), Gf.data(), hm.data(), logn, nullptr);
        pass += (size_t)Zf(verify_raw)(hm.data(), sig.data(), hmonty.data(), logn, tmp);
    }

    free(tmp);

    if (csv) {
        printf("op,median_cyc,mad_cyc,min_cyc\n");
        printf("keygen,%.0f,%.0f,%.0f\n", kg.median, kg.mad, kg.minv);
        printf("sign_offline,%.0f,%.0f,%.0f\n", off.median, off.mad, off.minv);
        printf("sign_online,%.0f,%.0f,%.0f\n", on.median, on.mad, on.minv);
        printf("sign_dyn_orig,%.0f,%.0f,%.0f\n", sd.median, sd.mad, sd.minv);
        printf("verify,%.0f,%.0f,%.0f\n", vf.median, vf.mad, vf.minv);
        printf("# verify_raw pass rate: %zu/%zu\n", pass, trials);
        return 0;
    }

    printf("== online/offline Falcon, logn=%u (n=%zu), invariant-TSC ticks ==\n", logn, n);
    printf("%-16s %14s %12s %14s\n", "op", "median", "MAD", "min");
    auto row = [](const char* name, Stat s) {
        printf("%-16s %14.0f %12.0f %14.0f  (%.3f Mcyc)\n", name, s.median, s.mad, s.minv,
               s.median / 1e6);
    };
    row("keygen", kg);
    row("sign_offline", off);
    row("sign_online", on);
    row("sign_dyn_orig", sd);
    row("verify", vf);
    printf("\nC4 verify_raw (real Falcon bound) pass rate: %zu/%zu (%.2f%%)\n", pass, trials,
           100.0 * (double)pass / (double)trials);
    printf("(online speedup vs orig sign_dyn: %.1fx)\n", sd.median / on.median);
    return 0;
}
