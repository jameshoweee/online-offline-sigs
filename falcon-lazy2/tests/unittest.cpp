#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <chrono>
#include <tuple>
#include <fstream>

#define restrict
extern "C" {
#include "../inner.h"
}


#define EXPORT extern "C"

#define REQUIRE_DRAMATICALLY(req_contition, error_msg)                                                        \
  do {                                                                                                        \
    if (!(req_contition)) {                                                                                   \
      std::cerr << "REQUIREMENT FAILED at " << __FILE__ << ":" << __LINE__ << ": " << error_msg << std::endl; \
      abort();                                                                                                \
    }                                                                                                         \
  } while (0)

#define F_Q 12289

// from falcon.h
EXPORT void shake256_init_prng_from_seed(inner_shake256_context *sc,
                                  const void *seed, size_t seed_len);



int64_t posmod(int64_t a, int64_t q) {
    int64_t t = a%q;
    return t<0?t+q:t;
}
int64_t centermod(int64_t a, int64_t q) {
    int64_t t = a%q;
    if (t>q/2) return t-q;
    if (t<-q/2) return t+q;
    return t;
}

struct modQ {
    int64_t v;
    modQ(int64_t x=0): v(posmod(x,F_Q)) {}
};

modQ operator+(modQ a, modQ b) { return posmod(a.v+b.v, F_Q); }
modQ operator-(modQ a, modQ b) { return posmod(a.v-b.v, F_Q); }
modQ operator*(modQ a, modQ b) { return posmod(a.v*b.v, F_Q); }
modQ& operator+=(modQ& a, modQ b) { a.v = posmod(a.v+b.v, F_Q); return a; }
modQ& operator-=(modQ& a, modQ b) { a.v = posmod(a.v-b.v, F_Q); return a; }
modQ& operator*=(modQ& a, modQ b) { a.v = posmod(a.v*b.v, F_Q); return a; }
bool operator==(modQ& a, modQ b) { return a.v == b.v; }
std::ostream& operator<<(std::ostream& out, modQ x) { return out << x.v; }

void convert(modQ* res, const int8_t* x, uint64_t n) {
    for (uint64_t k=0; k<n; ++k) {
        res[k]=modQ(x[k]);
    }
}
void convert(modQ* res, const int16_t* x, uint64_t n) {
    for (uint64_t k=0; k<n; ++k) {
        res[k]=modQ(x[k]);
    }
}
void convert(modQ* res, const uint16_t* x, uint64_t n) {
    for (uint64_t k=0; k<n; ++k) {
        res[k]=modQ(x[k]);
    }
}
void convert(modQ* res, const int32_t* x, uint64_t n) {
    for (uint64_t k=0; k<n; ++k) {
        res[k]=modQ(x[k]);
    }
}
void starproduct(modQ* res, const modQ* x, const modQ* y, uint64_t n) {
    for (uint64_t k=0; k<n; ++k) {
        modQ rk = 0;
        for (uint64_t i=0; i<=k; ++i) {
            rk += x[k-i] * y[i];
        }
        for (uint64_t i=k+1; i<n; ++i) {
            rk -= x[n+k-i] * y[i];
        }
        res[k]=rk;
    }
}

typedef std::vector<modQ> vec_modQ;
vec_modQ to_vec_modQ(const std::vector<int8_t>& v) {
    vec_modQ res(v.size());
    convert(res.data(), v.data(), v.size());
    return res;
}
vec_modQ to_vec_modQ(const std::vector<uint16_t>& v) {
    vec_modQ res(v.size());
    convert(res.data(), v.data(), v.size());
    return res;
}
vec_modQ to_vec_modQ(const std::vector<int16_t>& v) {
    vec_modQ res(v.size());
    convert(res.data(), v.data(), v.size());
    return res;
}
vec_modQ to_vec_modQ(const std::vector<int32_t>& v) {
    vec_modQ res(v.size());
    convert(res.data(), v.data(), v.size());
    return res;
}
vec_modQ starproduct(const vec_modQ& x, const vec_modQ& y) {
    uint64_t n = x.size();
    REQUIRE_DRAMATICALLY(y.size()==n, "wrong size");
    vec_modQ res(n);
    starproduct(res.data(), x.data(), y.data(), n);
    return res;
}
vec_modQ operator-(const vec_modQ& x, const vec_modQ& y) {
    uint64_t n = x.size();
    REQUIRE_DRAMATICALLY(y.size()==n, "wrong size");
    vec_modQ res(n);
    for (uint16_t i=0; i<n; ++i) {
        res[i] = x[i] - y[i];
    }
    return res;
}
vec_modQ operator+(const vec_modQ& x, const vec_modQ& y) {
    uint64_t n = x.size();
    REQUIRE_DRAMATICALLY(y.size()==n, "wrong size");
    vec_modQ res(n);
    for (uint16_t i=0; i<n; ++i) {
        res[i] = x[i] + y[i];
    }
    return res;
}
bool operator==(const vec_modQ& x, const vec_modQ& y) {
    uint64_t n = x.size();
    REQUIRE_DRAMATICALLY(y.size()==n, "wrong size");
    for (uint64_t i=0; i<n; ++i) {
        if (x[i].v != y[i].v) return false;
    }
    return true;
}
std::default_random_engine& randgen() {
    static std::default_random_engine eng;
    return eng;
}
uint64_t random_u64() {
    static std::uniform_int_distribution<uint64_t> dist;
    return dist(randgen());
}
uint64_t random_i64() {
    static std::uniform_int_distribution<int64_t> dist;
    return dist(randgen());
}

template<typename T>
double print_statistics(const std::vector<T>& vec) {
    uint64_t n = vec.size();
    double minv = 1./0.;
    double maxv = -1./0.;
    double sumv = 0;
    double sumsq = 0;
    for (uint64_t i=0; i<n; ++i) {
        double v = vec[i];
        if (v > maxv) maxv = v;
        if (v < minv) minv = v;
        sumv += v;
        sumsq += v * v;
    }
    double mean = sumv/n;
    double var = sumsq/n - mean*mean;
    std::cout
            << "min: " << minv << "\n"
            << "max: " << maxv << "\n"
            << "mean: " << mean << "\n"
            << "stdv: " << sqrt(var) << "\n"
            << "norm: " << sqrt(sumsq) << std::endl;
    return sqrt(sumsq);
}

template<typename T>
std::tuple<double, double> cal_statistics(const std::vector<T>& vec) {
    uint64_t n = vec.size();
    double maxv = -1./0.;
    double sumsq = 0;
    for (uint64_t i=0; i<n; ++i) {
        double v = std::abs(vec[i]);
        if (v > maxv) maxv = v;
        sumsq += v * v;
    }
    return std::make_tuple(std::sqrt(sumsq), maxv);
}

struct falcon_key_t {
    std::vector<int8_t> f;
    std::vector<int8_t> g;
    std::vector<int8_t> F;
    std::vector<int8_t> G;
    std::vector<uint16_t> h;
};

falcon_key_t keygen(uint64_t logn, inner_shake256_context* rng) {
    falcon_key_t res;
    const uint64_t n = 1<<logn;
    uint8_t* tmp = (uint8_t*) aligned_alloc(64, 1024*1024);
    res.f.resize(n);
    res.g.resize(n);
    res.F.resize(n);
    res.G.resize(n);
    res.h.resize(n);
    Zf(keygen)(
            rng,
            res.f.data(),res.g.data(),
            res.F.data(),res.G.data(),
            res.h.data(),logn, tmp);
    free(tmp);
    return res;
}

TEST(falcon, keygen) {
    const uint64_t logn = 9;
    //const uint64_t n = 1 << logn;
    inner_shake256_context rng;
    inner_shake256_init(&rng);
    falcon_key_t key = keygen(logn, &rng);
    // verify that fh=g mod q
    // verify that Fh=G mod q
    vec_modQ fq = to_vec_modQ(key.f);
    vec_modQ gq = to_vec_modQ(key.g);
    vec_modQ Fq = to_vec_modQ(key.F);
    vec_modQ Gq = to_vec_modQ(key.G);
    vec_modQ hq = to_vec_modQ(key.h);
    ASSERT_EQ(starproduct(fq, hq), gq);
    ASSERT_EQ(starproduct(Fq, hq), Gq);
}

TEST(falcon, original_sig) {
    const uint64_t logn = 9;
    const uint64_t n = 1 << logn;
    inner_shake256_context rng;
    inner_shake256_init(&rng);
    falcon_key_t key = keygen(logn, &rng);
    vec_modQ hq = to_vec_modQ(key.h);
    std::vector<int16_t> sig(n);
    std::vector<uint16_t> hm(n);
    // use a random hash of message
    for (uint64_t i=0; i<n; ++i) {
        hm[i]=rand()%F_Q;
    }
    uint8_t* tmp = (uint8_t*) aligned_alloc(64, 1024*1024);
    falcon_inner_sign_dyn(
            sig.data(),
            &rng,
            key.f.data(), key.g.data(),
            key.F.data(), key.G.data(),
            hm.data(),
            logn, tmp);
    free(tmp);
    // compute the full uncompressed signature
    vec_modQ sigq = to_vec_modQ(sig);
    vec_modQ hsigq = starproduct(hq, sigq);
    std::vector<double> full_sig(2*n);
    for (uint64_t i=0; i<n; i++) {
        full_sig[i] = centermod(hm[i]-hsigq[i].v, F_Q);
        full_sig[i+n] = sig[i];
    }
    // print statistics about this vector and check the norm
    double norm = print_statistics(full_sig);
    ASSERT_LE(norm, 6000);
}

EXPORT int sign_dyn_lazy_online(
        int8_t* sample1, int8_t* sample2, uint16_t* sample_target,
        int16_t *s2,
        const fpr *restrict f_fft, const fpr *restrict g_fft,
        const fpr *restrict F_fft, const fpr *restrict G_fft,
        const uint16_t *hm, unsigned logn, fpr *restrict tmp __attribute((unused)));

EXPORT void sign_dyn_lazy_offline(
        // inputs
        inner_shake256_context *rng,
        const int8_t *restrict f, const int8_t *restrict g,
        const int8_t *restrict F, const int8_t *restrict G,
        const uint16_t *h,
        unsigned logn,
        //outputs
        int8_t* sample1, int8_t* sample2, uint16_t* sample_target,
        fpr *restrict f_fft, fpr *restrict g_fft,
        fpr *restrict F_fft, fpr *restrict G_fft
);


TEST(falcon, lazy_sig) {
    const uint64_t logn = 9;
    const uint64_t n = 1 << logn;
    inner_shake256_context rng;
    inner_shake256_init(&rng);
    falcon_key_t key = keygen(logn, &rng);
    vec_modQ hq = to_vec_modQ(key.h);
    std::vector<int16_t> sig(n);
    // use a random hash of message
    std::vector<uint16_t> hm(n);
    for (uint64_t i=0; i<n; ++i) {
        hm[i]=rand()%F_Q;
    }
    uint8_t* tmp = (uint8_t*) aligned_alloc(64, 1024*1024);
    /*
    falcon_inner_sign_dyn_lazy(
            sig.data(),
            &rng,
            key.f.data(), key.g.data(),
            key.F.data(), key.G.data(),
            key.h.data(),
            hm.data(),
            logn, tmp);
            */
    std::vector<int8_t> sample1(n);
    std::vector<int8_t> sample2(n);
    std::vector<uint16_t> sample_target(n);
    std::vector<fpr> f_FFT(n);
    std::vector<fpr> g_FFT(n);
    std::vector<fpr> F_FFT(n);
    std::vector<fpr> G_FFT(n);
    uint64_t t0_off = std::chrono::steady_clock::now().time_since_epoch().count();
    for (uint64_t i=0; i<1000; ++i) {
        sign_dyn_lazy_offline(&rng, key.f.data(), key.g.data(), key.F.data(), key.G.data(), key.h.data(), logn,
                              sample1.data(), sample2.data(), sample_target.data(), f_FFT.data(), g_FFT.data(),
                              F_FFT.data(), G_FFT.data());
    }
    uint64_t t1_off = std::chrono::steady_clock::now().time_since_epoch().count();
    std::vector<uint16_t> orig_sample_target = sample_target;
    uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();
    for (uint64_t i=0; i<1000; ++i) {
        sign_dyn_lazy_online(sample1.data(), sample2.data(), sample_target.data(), sig.data(),
                             f_FFT.data(), g_FFT.data(), F_FFT.data(), G_FFT.data(), hm.data(), logn,
                             nullptr);
    }
    uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    double time_offline = (t1_off - t0_off)/1e9/1000.*1e6;
    double time_online = (t1 - t0)/1e9/1000.*1e6;
    std::cout << "offline time (us): " << time_offline << std::endl;
    std::cout << "online time (us): " << time_online << std::endl;
    sign_dyn_lazy_online(sample1.data(), sample2.data(), orig_sample_target.data(), sig.data(),
                         f_FFT.data(), g_FFT.data(), F_FFT.data(), G_FFT.data(), hm.data(), logn,
                         nullptr);
    free(tmp);
    // compute the full uncompressed signature
    vec_modQ sigq = to_vec_modQ(sig);
    vec_modQ hsigq = starproduct(hq, sigq);
    std::vector<double> full_sig(2*n);
    for (uint64_t i=0; i<n; i++) {
        full_sig[i] = centermod(hm[i]-hsigq[i].v, F_Q);
        full_sig[i+n] = sig[i];
    }
    // print statistics about this vector and check the norm
    double norm = print_statistics(full_sig);
    ASSERT_LE(norm, 6000);
}


EXPORT void sample_gaussian(int8_t *res,
                     sampler_context* spc,
                     fpr isigma,
                     unsigned logn);

TEST(falcon, sample_gaussian) {
    for (const uint64_t logn: {9,10}) {
        const uint64_t n = 1 << logn;
        inner_shake256_context rng;
        inner_shake256_init(&rng);
        double sigma = 2.; // between 1 and 2
        fpr isigma = FPR(1./sigma);
        sampler_context sc;
        Zf(prng_init)(&sc.p, &rng);
        sc.sigma_min = fpr_sigma_min[logn];
        std::vector<int8_t> res(n);
        sample_gaussian(res.data(), &sc, isigma, logn);
        std::vector<double> st(n);
        for (uint64_t i=0; i<n; ++i) st[i]=res[i];
        double norm = print_statistics(st);
        ASSERT_LE(norm, 3*n);
    }
}

/** x0 - h.x1 */
EXPORT void compute_target(uint16_t *h, int8_t *x0, int8_t *x1, uint16_t *res, unsigned logn);

TEST(falcon, mul_by_h) {
    for (const uint64_t logn: {9,10}) {
        const uint64_t n = 1 << logn;
        std::vector<uint16_t> actual(n);
        std::vector<uint16_t> h(n);
        std::vector<uint16_t> x0(n);
        for (uint16_t i=0; i<n; ++i) {
            h[i] = posmod(random_u64(), F_Q);
            x0[i] = posmod(random_u64(), F_Q);
        }
        vec_modQ hq = to_vec_modQ(h);
        vec_modQ x0q = to_vec_modQ(x0);
        vec_modQ expect = starproduct(x0q , hq);
        // we need to pass h_monty
        std::vector<uint16_t> h_monty = h;
        falcon_inner_to_ntt_monty(h_monty.data(), logn);
        //
        std::vector<uint16_t> res = x0;
        mq_NTT(res.data(), logn);
        mq_poly_montymul_ntt(res.data(), h_monty.data(), logn);
        mq_iNTT(res.data(), logn);

        ASSERT_EQ(to_vec_modQ(res), expect);
    }
}


TEST(falcon, compute_target) {
    for (const uint64_t logn: {9,10}) {
        const uint64_t n = 1 << logn;
        std::vector<uint16_t> actual(n);
        std::vector<uint16_t> h(n);
        std::vector<int8_t> x0(n);
        std::vector<int8_t> x1(n);
        for (uint16_t i=0; i<n; ++i) {
            h[i] = posmod(random_u64(), F_Q);
            x0[i] = centermod(random_u64(), 65);
            x1[i] = centermod(random_u64(), 65);
        }
        vec_modQ hq = to_vec_modQ(h);
        vec_modQ x0q = to_vec_modQ(x0);
        vec_modQ x1q = to_vec_modQ(x1);
        vec_modQ expect = x0q - starproduct(x1q , hq);
        // we need to pass h_monty
        std::vector<uint16_t> h_monty = h;
        falcon_inner_to_ntt_monty(h_monty.data(), logn);

        compute_target(h_monty.data(), x0.data(), x1.data(), actual.data(), logn);
        ASSERT_EQ(to_vec_modQ(actual), expect);
    }
}

/** find a short lattice point in a coset
 *  short (res0,res1) s.t.  res0 - h.res1 = target mod q
 */
EXPORT void short_preimage(const uint16_t *target, //
                           const fpr *f_fft, const fpr *g_fft, // key
                           const fpr *F_fft, const fpr *G_fft, // key
                           int32_t *res1, int32_t *res2,
                           unsigned logn);

TEST(falcon, short_preimage) {
    for (const uint64_t logn: {9,10}) {
        const uint64_t n = 1 << logn;
        inner_shake256_context rng;
        inner_shake256_init(&rng);
        falcon_key_t key = keygen(logn, &rng);
        std::vector<uint16_t> target(n);
        std::vector<int32_t> res1(n);
        std::vector<int32_t> res2(n);
        for (uint16_t i=0; i<n; ++i) {
            target[i] = posmod(random_u64(), F_Q);
        }
        std::vector<fpr> f_FFT(n);
        std::vector<fpr> g_FFT(n);
        std::vector<fpr> F_FFT(n);
        std::vector<fpr> G_FFT(n);
        for (uint16_t i=0; i<n; ++i) {
            f_FFT[i] = fpr_of(key.f[i]);
            g_FFT[i] = fpr_of(key.g[i]);
            F_FFT[i] = fpr_of(key.F[i]);
            G_FFT[i] = fpr_of(key.G[i]);
        }
        Zf(FFT)(f_FFT.data(), logn);
        Zf(FFT)(g_FFT.data(), logn);
        Zf(FFT)(F_FFT.data(), logn);
        Zf(FFT)(G_FFT.data(), logn);
        //
        short_preimage(target.data(), //
                       f_FFT.data(), g_FFT.data(), //
                       F_FFT.data(), G_FFT.data(), //
                       res1.data(), res2.data(), //
                       logn);
        // verify: the norm of res1, res2
        print_statistics(res1);
        print_statistics(res2);

        // verify that res1-h*res2=target mod q
        vec_modQ res1q = to_vec_modQ(res1);
        vec_modQ res2q = to_vec_modQ(res2);
        vec_modQ hq = to_vec_modQ(key.h);
        vec_modQ targetq = to_vec_modQ(target);
        vec_modQ actual = res1q - starproduct(res2q, hq);
        ASSERT_EQ(actual, targetq);
    }
}

EXPORT void
shake256_extract(inner_shake256_context *sc, void *out, size_t len);

void uniform_random_modq(uint16_t* res, inner_shake256_context* rng, uint64_t n) {
    static const uint64_t BOUND = UINT64_C(18446744073709545952);
    uint64_t r;
    for (uint64_t i=0; i<n; ++i) {
        for (shake256_extract(rng, &r, 8);
             r>=BOUND;
             shake256_extract(rng, &r, 8)) {}
        res[i] = r % F_Q;
    }
}


TEST(falcon, lazy_sig_norm) {
    const uint64_t logn = 9;
    const uint64_t n = 1 << logn;

    uint64_t seed = 42;
    inner_shake256_context rng;
    shake256_init_prng_from_seed(&rng, &seed, 8);

    double sum = 0.0;
    double max_sum = 0.0;
    uint64_t num_iterations = std::pow(2, 5);
    //double num_iter = (double) num_iterations;

    //uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();
    for (uint64_t j=0; j<num_iterations; ++j) {
        falcon_key_t key = keygen(logn, &rng);
        vec_modQ hq = to_vec_modQ(key.h);
        std::vector<int16_t> sig(n);
        // use a random hash of message
        std::vector<uint16_t> hm(n);
        uniform_random_modq(hm.data(), &rng, n);
        uint8_t* tmp = (uint8_t*) aligned_alloc(64, 1024*1024);
        std::vector<int8_t> sample1(n);
        std::vector<int8_t> sample2(n);
        std::vector<uint16_t> sample_target(n);
        std::vector<fpr> f_FFT(n);
        std::vector<fpr> g_FFT(n);
        std::vector<fpr> F_FFT(n);
        std::vector<fpr> G_FFT(n);

        sign_dyn_lazy_offline(&rng, key.f.data(), key.g.data(), key.F.data(), key.G.data(), key.h.data(), logn,
                                sample1.data(), sample2.data(), sample_target.data(), f_FFT.data(), g_FFT.data(),
                                F_FFT.data(), G_FFT.data());

        //uint64_t t1_off = std::chrono::steady_clock::now().time_since_epoch().count();
        std::vector<uint16_t> orig_sample_target = sample_target;
        //uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();

        sign_dyn_lazy_online(sample1.data(), sample2.data(), sample_target.data(), sig.data(),
                                f_FFT.data(), g_FFT.data(), F_FFT.data(), G_FFT.data(), hm.data(), logn,
                                nullptr);

        free(tmp);
        // compute the full uncompressed signature
        vec_modQ sigq = to_vec_modQ(sig);
        vec_modQ hsigq = starproduct(hq, sigq);
        std::vector<double> full_sig(2*n);
        for (uint64_t i=0; i<n; i++) {
            full_sig[i] = centermod(hm[i]-hsigq[i].v, F_Q);
            full_sig[i+n] = sig[i];
        }
        // print statistics about this vector and check the norm
        auto norm = cal_statistics(full_sig);
        double exract_norm = std::get<0>(norm);
        sum += exract_norm;
        double extract_max = std::get<1>(norm);
        max_sum += extract_max;
        //ASSERT_LE(norm, 6000);
        if ((j + 1) % 500 == 0) {
            std::cout << "iteration: " << j << std::endl;
        }
    
    }
    //uint64_t t1 = std::chrono::steady_clock::now().time_since_epoch().count();
    //double time_offline = (t1_off - t0_off)/1e9/num_iterations.*1e6;
    //double total_time = (t1 - t0)/1e9/num_iter*1e6;
    //std::cout << "total average time (us): " << total_time << std::endl;

    // calculate average norm
    double average = sum / num_iterations;
    double average_max = max_sum / num_iterations;
    std::cout << "average L2 norm: " << average << std::endl;
    std::cout << "average Linf norm: " << average_max << std::endl;

}

double get_time() {
    static uint64_t t0 = std::chrono::steady_clock::now().time_since_epoch().count();
    uint64_t t = std::chrono::steady_clock::now().time_since_epoch().count();
    return (t-t0)*1e-9;
}

TEST(falcon, lazy_sig_norm_multikeys) {
    std::string signature_coords_filename = "sig_norm_coords.csv";

    const uint64_t logn = 9;
    const uint64_t n = 1 << logn;

    uint64_t seed = 42;

    double sum = 0.0;
    double max_sum = 0.0;
    uint64_t num_iterations = std::pow(2, 20);
    uint64_t num_keyreuse = std::pow(2, 10);
    int total_iterations = num_iterations*num_keyreuse;
    int total_ongoing_count = 0;
    double average = 0.0;
    double average_max = 0.0;

    std::vector<int16_t> sig(n);
    // use a random hash of message
    std::vector<uint16_t> hm(n, 0);
    uint8_t* tmp = (uint8_t*) aligned_alloc(64, 1024*1024);
    std::vector<int8_t> sample1(n);
    std::vector<int8_t> sample2(n);
    std::vector<uint16_t> sample_target(n);
    std::vector<fpr> f_FFT(n);
    std::vector<fpr> g_FFT(n);
    std::vector<fpr> F_FFT(n);
    std::vector<fpr> G_FFT(n);

    // write the header of the csv file
    std::ofstream ofs(signature_coords_filename);
    ofs << "key_seed,infty_norm,eucl_norm";
    // for (uint64_t i=0; i<2*n; ++i) {
    //     ofs << ",s" << i;
    // }
    ofs << std::endl;

    double last_update_time = 0;

    for (uint64_t j=0; j<num_iterations; ++j) {
        uint64_t key_seed = seed + j;
        inner_shake256_context rng;
        shake256_init_prng_from_seed(&rng, &key_seed, 8);
        falcon_key_t key = keygen(logn, &rng);
        vec_modQ hq = to_vec_modQ(key.h);
        // sign offline
        sign_dyn_lazy_offline(&rng, key.f.data(), key.g.data(), key.F.data(), key.G.data(), key.h.data(), logn,
                              sample1.data(), sample2.data(), sample_target.data(), f_FFT.data(), g_FFT.data(),
                              F_FFT.data(), G_FFT.data());
        // zero the gaussian sample
        memset(sample1.data(), 0, n*sizeof(int8_t));
        memset(sample2.data(), 0, n*sizeof(int8_t));
        memset(sample_target.data(), 0, n*sizeof(uint16_t));


        for (uint64_t k=0; k<num_keyreuse; ++k) {
            // make hm random
            uniform_random_modq(hm.data(), &rng, n);
            // we keep this one since the online phase modifies sample_target
            memset(sample_target.data(), 0, n*sizeof(uint16_t));
            sign_dyn_lazy_online(sample1.data(), sample2.data(), sample_target.data(), sig.data(),
                                    f_FFT.data(), g_FFT.data(), F_FFT.data(), G_FFT.data(), hm.data(), logn,
                                    nullptr);

            // compute the full uncompressed signature
            vec_modQ sigq = to_vec_modQ(sig);
            vec_modQ hsigq = starproduct(hq, sigq);
            std::vector<int16_t> full_sig(2*n);
            for (uint64_t i=0; i<n; i++) {
                full_sig[i] = centermod(hm[i]-hsigq[i].v, F_Q);
                full_sig[i+n] = sig[i];
            }
            // print statistics about this vector and check the norm
            auto norm = cal_statistics(full_sig);
            double exract_norm = std::get<0>(norm);
            sum += exract_norm;
            double extract_max = std::get<1>(norm);
            max_sum += extract_max;
            total_ongoing_count++;

            ofs << key_seed << "," << extract_max << "," << exract_norm;
            // for (uint64_t i=0; i<2*n; ++i) {
            //     ofs << "," << full_sig[i];
            // }
            ofs << std::endl;
            // ofs.write(reinterpret_cast<const char*>(&key_seed), sizeof(key_seed));
            // ofs.write(reinterpret_cast<const char*>(&extract_max), sizeof(extract_max));
            // ofs.write(reinterpret_cast<const char*>(&exract_norm), sizeof(exract_norm));
            // ofs.write(reinterpret_cast<const char*>(full_sig.data()), full_sig.size() * sizeof(double));
        }

        if (get_time() >= last_update_time + 60) {
            last_update_time = get_time();
            double percentage = (j * 100.) / num_iterations;
            std::cout << "We're " << percentage+1 << "% of the way through" << std::endl;
            std::cout << "---iteration: " << total_ongoing_count << std::endl;
            std::cout << "---ongoing avg l2 norm: " << sum/total_ongoing_count << std::endl;
            std::cout << "---ongoing avg linf norm: " << max_sum/total_ongoing_count << std::endl;
        }
    }
    // calculate average norm
    average = sum / total_iterations;
    average_max = max_sum / total_iterations;
    std::cout << "average L2 norm: " << average << std::endl;
    std::cout << "average Linf norm: " << average_max << std::endl;
    ofs.close();
    free(tmp);
}
