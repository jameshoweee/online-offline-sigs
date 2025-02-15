/*
 * Falcon signature generation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2017-2019  Falcon Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   Thomas Pornin <thomas.pornin@nccgroup.com>
 */

#include "stdio.h"
#include "inner.h"
#include "math.h"

#include <inttypes.h>
#include <string.h>
#include <assert.h>

/* =================================================================== */

/*
 * Compute degree N from logarithm 'logn'.
 */
#define MKN(logn)   ((size_t)1 << (logn))

/* =================================================================== */
/*
 * Binary case:
 *   N = 2^logn
 *   phi = X^N+1
 */

/// ONLINE OFFLINE DEFINING SOME FUNCTIONS

//#define NEAREST_PLANE_ENABLED

// static inline uint8_t *
// align_u16(void *tmp)
// {
// 	uint8_t *atmp;

// 	atmp = tmp;
// 	if (((uintptr_t)atmp & 1u) != 0) {
// 		atmp ++;
// 	}
// 	return atmp;
// }

void v_add(const fpr a[], const fpr b[], fpr result[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        result[i] = fpr_add(a[i], b[i]);
    }
}

void v_sub(const fpr a[], const fpr b[], fpr result[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        result[i] = fpr_sub(a[i], b[i]);
    }
}

void v_mul(const fpr a[], const fpr b[], fpr result[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        result[i] = fpr_mul(a[i], b[i]);
    }
}

void v_neg(const fpr a[], fpr result[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        result[i] = fpr_neg(a[i]);
    }
}

void v_inv(const fpr a[], fpr result[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        result[i] = fpr_inv(a[i]);
    }
}

// void v_round(const fpr a[], fpr result[], unsigned logn, size_t size) {
//     memcpy(result, a, size*sizeof(fpr));
//     Zf(iFFT)(result, logn);
//     for (size_t i = 0; i < size; i++) {
//         result[i] = fpr_of(fpr_rint(result[i]));
//     }
//     Zf(FFT)(result, logn);
// }

void v_round(const fpr a[], fpr result[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        result[i] = fpr_of(fpr_rint(a[i]));
    }
}

#define Q     12289

uint32_t mq_conv_small(int x);

/** converts a signed small polynomial to mod q */
void mq_conv_poly_small_sign(size_t n, const int8_t* in, uint16_t* res)
{
    for (size_t i=0; i<n; ++i) {
        res[i] = mq_conv_small(in[i]);
    }
}


uint32_t
mq_sub_sign(uint32_t x, uint32_t y)
{
	/*
	 * As in mq_add(), we use a conditional addition to ensure the
	 * result is in the 0..q-1 range.
	 */
	uint32_t d;

	d = x - y;
	d += Q & -(d >> 31);
	return d;
}

uint32_t
mq_add_sign(uint32_t x, uint32_t y)
{
	/*
	 * We compute x + y - q. If the result is negative, then the
	 * high bit will be set, and 'd >> 31' will be equal to 1;
	 * thus '-(d >> 31)' will be an all-one pattern. Otherwise,
	 * it will be an all-zero pattern. In other words, this
	 * implements a conditional addition of q.
	 */
	uint32_t d;

	d = x + y - Q;
	d += Q & -(d >> 31);
	return d;
}

// static inline int64_t
// fpr_print(fpr x)
// {
// 	int64_t r;

// 	r = (int64_t)x.v;
// 	return r;
// }

// void make_matrix(fpr A[2][2], const fpr arr1[], const fpr arr2[], const fpr arr3[], const fpr arr4[], size_t size) {
//     A[0][0] = arr1[0];
//     A[0][1] = arr2[0];
//     A[1][0] = arr3[0];
//     A[1][1] = arr4[0];
// }

// void make_vector(fpr A[], const fpr arr1[1], const fpr arr2[1], size_t size) {
//     A[0] = arr1[0];
//     A[1] = arr2[0];
// }

// rewriting this to just take 6 arrays as inputs instead of a 2x2 matrix of arrays and 2 arrays
// void mat_mul(const fpr A[2][2], const fpr x[2], fpr y[2], size_t size) {
//     fpr temp[2];
//     for (size_t i = 0; i < size; i++) {
//         v_mul(A[i], &x[i], temp, size);
//         v_add(y, temp, y, size);
//     }
// }

void mat_mul(const fpr A00[], const fpr A01[], const fpr A10[], const fpr A11[], const fpr x1[], const fpr x2[], fpr y1[], fpr y2[], size_t size) {
    for (size_t i = 0; i < size; i++) {
        fpr temp1 = fpr_add(fpr_mul(A00[i], x1[i]), fpr_mul(A01[i], x2[i]));
        fpr temp2 = fpr_add(fpr_mul(A10[i], x1[i]), fpr_mul(A11[i], x2[i]));
        y1[i] = fpr_add(y1[i], temp1);
        y2[i] = fpr_add(y2[i], temp2);
    }
}

// scalar is set to a float as this is what we need currently
void v_scalar_mul(const fpr a[], const fpr s, fpr result[], size_t size) {
	for (size_t i = 0; i < size; i++) {
		result[i] = fpr_mul(s, a[i]);
	}
}

int randint(int min, int max) {
    return min + rand() % (max - min + 1);
}

#define IMAX_BITS(m) ((m)/((m)%255+1) / 255%255*8 + 7-86/((m)%255+12))
#define RAND_MAX_WIDTH IMAX_BITS(RAND_MAX)

uint32_t rand32(void) {
  uint32_t r = 0;
  for (int i = 0; i < 32; i += RAND_MAX_WIDTH) {
    r <<= RAND_MAX_WIDTH;
    r ^= (unsigned) rand();
  }
  return r;
}

int randombytes(uint8_t *obuf, size_t len)
{
	static uint32_t fibo_a = 0xDEADBEEF, fibo_b = 0x01234567;
	size_t i;
	for (i = 0; i < len; i++) {
		fibo_a += fibo_b;
		fibo_b += fibo_a;
		obuf[i] = (fibo_a >> 24) ^ (fibo_b >> 16);
	}
	return 0;
}

#define LSBMASK(c)	(-((c)&1))
#define	CMUX(x,y,c)	(((x)&(LSBMASK(c)))^((y)&(~LSBMASK(c))))
#define CFLIP(x,c)	CMUX(x,-(x),c)
#define CABS(x)		CFLIP(x,x>=0)

uint64_t exp_scaled(uint64_t x)
{
    assert(x<=64082*256);

    uint64_t r;
    r = (( 809438661408LL    )*x) >> 28;
    r = (( 869506949331LL - r)*x) >> 28;
    r = (( 640044208952LL - r)*x) >> 27;
    r = (( 793458686015LL - r)*x) >> 27;
    r = (( 839743192604LL - r)*x) >> 27;
    r = (( 740389683060LL - r)*x) >> 26;
    r = ((1044449863563LL - r)*x) >> 27;
    r = (( 552517269260LL - r)*x) >> 25;
    r = (( 779422325990LL - r)*x) >> 23;
    r =  (2199023255552LL - r);

    return r;
}

static uint32_t sample_berncdt(uint64_t utop, uint64_t ubot)
{
    uint32_t x = 0;
    uint32_t b = 1;
    uint32_t r, s, t;
    t  = (utop >  11881272476311950404ULL);
    r  = (utop == 11881272476311950404ULL);
    s  = (ubot >   2232598800125794762ULL);
    b  = (r && s) || t;
    x += b;
    t  = (utop >  17729174351313943813ULL);
    r  = (utop == 17729174351313943813ULL);
    s  = (ubot >  17046599807202850264ULL);
    b  = (r && s) || t;
    x += b;
    t  = (utop >  18426461144592799266ULL);
    r  = (utop == 18426461144592799266ULL);
    s  = (ubot >   9031501729263515114ULL);
    b  = (r && s) || t;
    x += b;
    t  = (utop >  18446602887327906610ULL);
    r  = (utop == 18446602887327906610ULL);
    s  = (ubot >  11817852927693963396ULL);
    b  = (r && s) || t;
    x += b;
    t  = (utop >  18446743834670245612ULL);
    r  = (utop == 18446743834670245612ULL);
    s  = (ubot >   7306021935394802834ULL);
    b  = (r && s) || t;
    x += b;
    t  = (utop >  18446744073611412414ULL);
    r  = (utop == 18446744073611412414ULL);
    s  = (ubot >  17880792342251759005ULL);
    b  = (r && s) || t;
    x += b;
    t  = (utop >  18446744073709541852ULL);
    r  = (utop == 18446744073709541852ULL);
    s  = (ubot >  14689009182029885173ULL);
    b  = (r && s) || t;
    x += b;
    r  = (utop == 18446744073709551615ULL);
    s  = (ubot >  14106032229701791861ULL);
    b  = (r && s);
    x += b;
    s  = (ubot >  18446718728838181855ULL);
    b &= s;
    x += b;
    s  = (ubot >  18446744073673701140ULL);
    b &= s;
    x += b;
    
    return x;
}


static inline uint32_t div16404853(uint32_t x) {
    uint32_t y, z;
    y = (97488647 * (uint64_t) x) >> 32;
    z = (x - y) >> 1;
    return (z + y) >> 23;
}

#define BERN_RANDMULT	1664

void sample_gaussian_poly_bern(int8_t *sample1, int8_t *sample2, size_t n) 
{
    size_t i = 0, pos = 0;
    uint32_t x, y, t, k;
    int32_t z;

    uint64_t *us, utop, ubot, v, w;
    unsigned char *cs, c;
    unsigned char buf[(3*sizeof(uint64_t) + 2)*BERN_RANDMULT];
    uint32_t coeffs[2*n];

    randombytes(buf, sizeof buf);

    cs = buf;
    us = (uint64_t*)(buf+2*BERN_RANDMULT);

    while(i<2*n) {
	if(pos++ >= BERN_RANDMULT) {
	    randombytes(buf, sizeof buf);
	    pos = 0;
	    cs = buf;
	    us = (uint64_t*)(buf+2*BERN_RANDMULT);
	}

	utop = *us++;
	ubot = *us++;
	w    = *us++;
	w  >>= 1;

	x = sample_berncdt(utop, ubot) << 8;
	y = *cs++;
	z = x + y;

	y = y*(y + 2*x) << 8;  
	k = div16404853(y);
	t = y - 16404853*k;
	v = exp_scaled(t) << (22-k);
	
	c = *cs++;
	if((w > v) || (c & (z==0)))
	   continue;

	c >>= 1;
	coeffs[i++] = CFLIP(z,(int32_t)c);
    }

	for (size_t j=0; j<n; j++) {
		sample1[j] = coeffs[j];
	}
	for (size_t ii=0; ii<n; ii++) {
		sample2[ii] = coeffs[n+ii];
	}
    
}

void gauss_sampler(sampler_context *sc, fpr mu, fpr isigma, int8_t* result, size_t n)
{
	int z;
	//long ctr, szlo, szhi;
	int c;

	/*
	 * We call the sampler 100000 times and check that each value is
	 * within +/-30 of the center. We also accumulate the values.
	 */
	c = (int)fpr_trunc(mu);

	for (size_t i = 0; i < n; i++) {
		z = Zf(sampler)(sc, mu, isigma);
		z -= c;
		// printf("Generated Gaussian value: %d\n", z);
		result[i] = z;
	}
}

void sample_gaussian(int8_t *res, 
                     sampler_context* spc,
					 fpr isigma,
                     unsigned logn)
{
	int z;
	size_t n;
	n = MKN(logn);

	for (size_t i = 0; i < n; i++) {
		z = Zf(sampler)(spc, fpr_zero, isigma);
		res[i] = z;
		res[i] = 0; // remove to get back masked version
	}
}

static inline uint32_t
mq_sub(uint32_t x, uint32_t y)
{
    /*
     * As in mq_add(), we use a conditional addition to ensure the
     * result is in the 0..q-1 range.
     */
    uint32_t d;

    d = x - y;
    d += Q & -(d >> 31);
    return d;
}

/*
static void
mq_poly_sub2(size_t logn, const uint16_t *a, const uint16_t* b, uint16_t* res)
{
    size_t u, n;
    n = (size_t)1 << logn;
    for (u = 0; u < n; u ++) {
        res[u] = (uint16_t)mq_sub(a[u], b[u]);
    }
}
*/

static void
mq_poly_small_sign_minus_mq(const int8_t *a, const uint16_t *b, uint16_t *res, size_t logn)
{
    size_t u, n;
    n = (size_t)1 << logn;
    for (u = 0; u < n; u ++) {
        res[u] = (uint16_t)mq_sub(mq_conv_small(a[u]), b[u]);
    }
}

/** computes res = x0 - h*x1 mod q
 *  h is already in NTT monty representation */
void compute_target(const uint16_t *h_monty, const int8_t *x0, const int8_t *x1, uint16_t *res, unsigned logn)
{
	uint64_t n;
	n = MKN(logn);
    // convert int8 to uint16
    mq_conv_poly_small_sign(n, x1, res);

	mq_NTT(res, logn);
	//Zf(to_ntt_monty)(h, logn);
	mq_poly_montymul_ntt(res, h_monty, logn);
	mq_iNTT(res, logn);

    mq_poly_small_sign_minus_mq(x0, res, res, logn);
}

void short_preimage(const uint16_t *target, //
                    const fpr *f_fft, const fpr *g_fft, // key
                    const fpr *F_fft, const fpr *G_fft, // key
                    int32_t *res1, int32_t *res2,
                    unsigned logn)
{
    size_t n;
    n = MKN(logn);

    // y1 = hm, y2 = 0, put into FFT
    fpr y1[n];
    fpr y2[n];

    for (size_t u = 0; u < n; u ++) {
        y1[u] = fpr_of(target[u]); // y1 = FFT(target)
        //y2[u] = (uint16_t)(0); // implicit
    }

    Zf(FFT)(y1, logn);
    //Zf(FFT)(y2, logn); implicitly the same, y2 == FFT(y2) == 0

    // (target,0) * [[F, -f][-G,g]]
    // simplified as two FFT mults:
    // (h * F,  -h * f)
    memcpy(y2, y1, n * sizeof(fpr));
    Zf(poly_neg)(y1, logn); // f == -f
    Zf(poly_mul_fft)(y1, F_fft, logn);
    Zf(poly_mul_fft)(y2, f_fft, logn);

    // multiple both polys by q_inv
    Zf(poly_mulconst)(y1, fpr_inverse_of_q, logn);
    Zf(poly_mulconst)(y2, fpr_inverse_of_q, logn);

    // round y1,y2
    // copy y1,y2, round it, subtract from original
    Zf(iFFT)(y1, logn);
    Zf(iFFT)(y2, logn);

    fpr y1_temp[n];
    fpr y2_temp[n];

    for (size_t u = 0; u < n; u ++) {
        y1_temp[u] = fpr_of(fpr_rint(y1[u]));
        y2_temp[u] = fpr_of(fpr_rint(y2[u]));
        y1[u] = fpr_sub(y1[u], y1_temp[u]);
        y2[u] = fpr_sub(y2[u], y2_temp[u]);
    }

    Zf(FFT)(y1, logn);
    Zf(FFT)(y2, logn);

    // mult by sk
    memcpy(y1_temp, y1, n * sizeof(fpr));
    memcpy(y2_temp, y2, n * sizeof(fpr));

    // first row of matrix mult
    // y[0] := g*y[0] + G*y[1]
    Zf(poly_mul_fft)(y1, g_fft, logn);
    Zf(poly_mul_fft)(y2_temp, G_fft, logn);
    Zf(poly_add)(y1, y2_temp, logn); // stored in y1

    // second row of matrix mult
    // y[1] := f*y[0] + F*y[1]
    Zf(poly_mul_fft)(y1_temp, f_fft, logn);
    Zf(poly_mul_fft)(y2, F_fft, logn);
    Zf(poly_add)(y2, y1_temp, logn); // stored in y2

    // round y1 and y2
    Zf(iFFT)(y1, logn);
    Zf(iFFT)(y2, logn);

    // round y1,y2 and write to res1,res2
    for (size_t u = 0; u < n; u ++) {
        res1[u] = fpr_rint(y1[u]);
        res2[u] = fpr_rint(y2[u]);
    }
}


double calc_norm(const double* array, size_t size) {
    double norm = 0.0;
    for (size_t i = 0; i < size; i++) {
		// printf("array[%d]: (%f),\n", i, array[i]);
        norm += array[i] * array[i];
		// printf("norm[%d]: (%f),\n", i, norm);

    }
    return sqrt(norm);
}

/*
 * Get the size of the LDL tree for an input with polynomials of size
 * 2^logn. The size is expressed in the number of elements.
 */
static inline unsigned
ffLDL_treesize(unsigned logn)
{
	/*
	 * For logn = 0 (polynomials are constant), the "tree" is a
	 * single element. Otherwise, the tree node has size 2^logn, and
	 * has two child trees for size logn-1 each. Thus, treesize s()
	 * must fulfill these two relations:
	 *
	 *   s(0) = 1
	 *   s(logn) = (2^logn) + 2*s(logn-1)
	 */
	return (logn + 1) << logn;
}

/*
 * Inner function for ffLDL_fft(). It expects the matrix to be both
 * auto-adjoint and quasicyclic; also, it uses the source operands
 * as modifiable temporaries.
 *
 * tmp[] must have room for at least one polynomial.
 */
static void
ffLDL_fft_inner(fpr *restrict tree,
	fpr *restrict g0, fpr *restrict g1, unsigned logn, fpr *restrict tmp)
{
	size_t n, hn;

	n = MKN(logn);
	if (n == 1) {
		tree[0] = g0[0];
		return;
	}
	hn = n >> 1;

	/*
	 * The LDL decomposition yields L (which is written in the tree)
	 * and the diagonal of D. Since d00 = g0, we just write d11
	 * into tmp.
	 */
	Zf(poly_LDLmv_fft)(tmp, tree, g0, g1, g0, logn);

	/*
	 * Split d00 (currently in g0) and d11 (currently in tmp). We
	 * reuse g0 and g1 as temporary storage spaces:
	 *   d00 splits into g1, g1+hn
	 *   d11 splits into g0, g0+hn
	 */
	Zf(poly_split_fft)(g1, g1 + hn, g0, logn);
	Zf(poly_split_fft)(g0, g0 + hn, tmp, logn);

	/*
	 * Each split result is the first row of a new auto-adjoint
	 * quasicyclic matrix for the next recursive step.
	 */
	ffLDL_fft_inner(tree + n,
		g1, g1 + hn, logn - 1, tmp);
	ffLDL_fft_inner(tree + n + ffLDL_treesize(logn - 1),
		g0, g0 + hn, logn - 1, tmp);
}

/*
 * Compute the ffLDL tree of an auto-adjoint matrix G. The matrix
 * is provided as three polynomials (FFT representation).
 *
 * The "tree" array is filled with the computed tree, of size
 * (logn+1)*(2^logn) elements (see ffLDL_treesize()).
 *
 * Input arrays MUST NOT overlap, except possibly the three unmodified
 * arrays g00, g01 and g11. tmp[] should have room for at least three
 * polynomials of 2^logn elements each.
 */
static void
ffLDL_fft(fpr *restrict tree, const fpr *restrict g00,
	const fpr *restrict g01, const fpr *restrict g11,
	unsigned logn, fpr *restrict tmp)
{
	size_t n, hn;
	fpr *d00, *d11;

	n = MKN(logn);
	if (n == 1) {
		tree[0] = g00[0];
		return;
	}
	hn = n >> 1;
	d00 = tmp;
	d11 = tmp + n;
	tmp += n << 1;

	memcpy(d00, g00, n * sizeof *g00);
	Zf(poly_LDLmv_fft)(d11, tree, g00, g01, g11, logn);

	Zf(poly_split_fft)(tmp, tmp + hn, d00, logn);
	Zf(poly_split_fft)(d00, d00 + hn, d11, logn);
	memcpy(d11, tmp, n * sizeof *tmp);
	ffLDL_fft_inner(tree + n,
		d11, d11 + hn, logn - 1, tmp);
	ffLDL_fft_inner(tree + n + ffLDL_treesize(logn - 1),
		d00, d00 + hn, logn - 1, tmp);
}

/*
 * Normalize an ffLDL tree: each leaf of value x is replaced with
 * sigma / sqrt(x).
 */
static void
ffLDL_binary_normalize(fpr *tree, unsigned orig_logn, unsigned logn)
{
	/*
	 * TODO: make an iterative version.
	 */
	size_t n;

	n = MKN(logn);
	if (n == 1) {
		/*
		 * We actually store in the tree leaf the inverse of
		 * the value mandated by the specification: this
		 * saves a division both here and in the sampler.
		 */
		tree[0] = fpr_mul(fpr_sqrt(tree[0]), fpr_inv_sigma[orig_logn]);
	} else {
		ffLDL_binary_normalize(tree + n, orig_logn, logn - 1);
		ffLDL_binary_normalize(tree + n + ffLDL_treesize(logn - 1),
			orig_logn, logn - 1);
	}
}

/* =================================================================== */

/*
 * Convert an integer polynomial (with small values) into the
 * representation with complex numbers.
 */
static void
smallints_to_fpr(fpr *r, const int8_t *t, unsigned logn)
{
	size_t n, u;

	n = MKN(logn);
	for (u = 0; u < n; u ++) {
		r[u] = fpr_of(t[u]);
	}
}

/*
 * The expanded private key contains:
 *  - The B0 matrix (four elements)
 *  - The ffLDL tree
 */

static inline size_t
skoff_b00(unsigned logn)
{
	(void)logn;
	return 0;
}

static inline size_t
skoff_b01(unsigned logn)
{
	return MKN(logn);
}

static inline size_t
skoff_b10(unsigned logn)
{
	return 2 * MKN(logn);
}

static inline size_t
skoff_b11(unsigned logn)
{
	return 3 * MKN(logn);
}

static inline size_t
skoff_tree(unsigned logn)
{
	return 4 * MKN(logn);
}

/* see inner.h */
void
Zf(expand_privkey)(fpr *restrict expanded_key,
	const int8_t *f, const int8_t *g,
	const int8_t *F, const int8_t *G,
	unsigned logn, uint8_t *restrict tmp)
{
	size_t n;
	fpr *rf, *rg, *rF, *rG;
	fpr *b00, *b01, *b10, *b11;
	fpr *g00, *g01, *g11, *gxx;
	fpr *tree;

	n = MKN(logn);
	b00 = expanded_key + skoff_b00(logn);
	b01 = expanded_key + skoff_b01(logn);
	b10 = expanded_key + skoff_b10(logn);
	b11 = expanded_key + skoff_b11(logn);
	tree = expanded_key + skoff_tree(logn);

	/*
	 * We load the private key elements directly into the B0 matrix,
	 * since B0 = [[g, -f], [G, -F]].
	 */
	rf = b01;
	rg = b00;
	rF = b11;
	rG = b10;

	smallints_to_fpr(rf, f, logn);
	smallints_to_fpr(rg, g, logn);
	smallints_to_fpr(rF, F, logn);
	smallints_to_fpr(rG, G, logn);

	/*
	 * Compute the FFT for the key elements, and negate f and F.
	 */
	Zf(FFT)(rf, logn);
	Zf(FFT)(rg, logn);
	Zf(FFT)(rF, logn);
	Zf(FFT)(rG, logn);
	Zf(poly_neg)(rf, logn);
	Zf(poly_neg)(rF, logn);

	/*
	 * The Gram matrix is G = B·B*. Formulas are:
	 *   g00 = b00*adj(b00) + b01*adj(b01)
	 *   g01 = b00*adj(b10) + b01*adj(b11)
	 *   g10 = b10*adj(b00) + b11*adj(b01)
	 *   g11 = b10*adj(b10) + b11*adj(b11)
	 *
	 * For historical reasons, this implementation uses
	 * g00, g01 and g11 (upper triangle).
	 */
	g00 = (fpr *)tmp;
	g01 = g00 + n;
	g11 = g01 + n;
	gxx = g11 + n;

	memcpy(g00, b00, n * sizeof *b00);
	Zf(poly_mulselfadj_fft)(g00, logn);
	memcpy(gxx, b01, n * sizeof *b01);
	Zf(poly_mulselfadj_fft)(gxx, logn);
	Zf(poly_add)(g00, gxx, logn);

	memcpy(g01, b00, n * sizeof *b00);
	Zf(poly_muladj_fft)(g01, b10, logn);
	memcpy(gxx, b01, n * sizeof *b01);
	Zf(poly_muladj_fft)(gxx, b11, logn);
	Zf(poly_add)(g01, gxx, logn);

	memcpy(g11, b10, n * sizeof *b10);
	Zf(poly_mulselfadj_fft)(g11, logn);
	memcpy(gxx, b11, n * sizeof *b11);
	Zf(poly_mulselfadj_fft)(gxx, logn);
	Zf(poly_add)(g11, gxx, logn);

	/*
	 * Compute the Falcon tree.
	 */
	ffLDL_fft(tree, g00, g01, g11, logn, gxx);

	/*
	 * Normalize tree.
	 */
	ffLDL_binary_normalize(tree, logn, logn);
}

typedef int (*samplerZ)(void *ctx, fpr mu, fpr sigma);

/*
 * Perform Fast Fourier Sampling for target vector t. The Gram matrix
 * is provided (G = [[g00, g01], [adj(g01), g11]]). The sampled vector
 * is written over (t0,t1). The Gram matrix is modified as well. The
 * tmp[] buffer must have room for four polynomials.
 */
TARGET_AVX2
static void
ffSampling_fft_dyntree(samplerZ samp, void *samp_ctx,
	fpr *restrict t0, fpr *restrict t1,
	fpr *restrict g00, fpr *restrict g01, fpr *restrict g11,
	unsigned orig_logn, unsigned logn, fpr *restrict tmp)
{
	size_t n, hn;
	fpr *z0, *z1;

	/*
	 * Deepest level: the LDL tree leaf value is just g00 (the
	 * array has length only 1 at this point); we normalize it
	 * with regards to sigma, then use it for sampling.
	 */
	if (logn == 0) {
		fpr leaf;

		leaf = g00[0];
		leaf = fpr_mul(fpr_sqrt(leaf), fpr_inv_sigma[orig_logn]);
		t0[0] = fpr_of(samp(samp_ctx, t0[0], leaf));
		t1[0] = fpr_of(samp(samp_ctx, t1[0], leaf));
		return;
	}

	n = (size_t)1 << logn;
	hn = n >> 1;

	/*
	 * Decompose G into LDL. We only need d00 (identical to g00),
	 * d11, and l10; we do that in place.
	 */
	Zf(poly_LDL_fft)(g00, g01, g11, logn);

	/*
	 * Split d00 and d11 and expand them into half-size quasi-cyclic
	 * Gram matrices. We also save l10 in tmp[].
	 */
	Zf(poly_split_fft)(tmp, tmp + hn, g00, logn);
	memcpy(g00, tmp, n * sizeof *tmp);
	Zf(poly_split_fft)(tmp, tmp + hn, g11, logn);
	memcpy(g11, tmp, n * sizeof *tmp);
	memcpy(tmp, g01, n * sizeof *g01);
	memcpy(g01, g00, hn * sizeof *g00);
	memcpy(g01 + hn, g11, hn * sizeof *g00);

	/*
	 * The half-size Gram matrices for the recursive LDL tree
	 * building are now:
	 *   - left sub-tree: g00, g00+hn, g01
	 *   - right sub-tree: g11, g11+hn, g01+hn
	 * l10 is in tmp[].
	 */

	/*
	 * We split t1 and use the first recursive call on the two
	 * halves, using the right sub-tree. The result is merged
	 * back into tmp + 2*n.
	 */
	z1 = tmp + n;
	Zf(poly_split_fft)(z1, z1 + hn, t1, logn);
	ffSampling_fft_dyntree(samp, samp_ctx, z1, z1 + hn,
		g11, g11 + hn, g01 + hn, orig_logn, logn - 1, z1 + n);
	Zf(poly_merge_fft)(tmp + (n << 1), z1, z1 + hn, logn);

	/*
	 * Compute tb0 = t0 + (t1 - z1) * l10.
	 * At that point, l10 is in tmp, t1 is unmodified, and z1 is
	 * in tmp + (n << 1). The buffer in z1 is free.
	 *
	 * In the end, z1 is written over t1, and tb0 is in t0.
	 */
	memcpy(z1, t1, n * sizeof *t1);
	Zf(poly_sub)(z1, tmp + (n << 1), logn);
	memcpy(t1, tmp + (n << 1), n * sizeof *tmp);
	Zf(poly_mul_fft)(tmp, z1, logn);
	Zf(poly_add)(t0, tmp, logn);

	/*
	 * Second recursive invocation, on the split tb0 (currently in t0)
	 * and the left sub-tree.
	 */
	z0 = tmp;
	Zf(poly_split_fft)(z0, z0 + hn, t0, logn);
	ffSampling_fft_dyntree(samp, samp_ctx, z0, z0 + hn,
		g00, g00 + hn, g01, orig_logn, logn - 1, z0 + n);
	Zf(poly_merge_fft)(t0, z0, z0 + hn, logn);
}

/*
 * Perform Fast Fourier Sampling for target vector t and LDL tree T.
 * tmp[] must have size for at least two polynomials of size 2^logn.
 */
TARGET_AVX2
static void
ffSampling_fft(samplerZ samp, void *samp_ctx,
	fpr *restrict z0, fpr *restrict z1,
	const fpr *restrict tree,
	const fpr *restrict t0, const fpr *restrict t1, unsigned logn,
	fpr *restrict tmp)
{
	size_t n, hn;
	const fpr *tree0, *tree1;

	/*
	 * When logn == 2, we inline the last two recursion levels.
	 */
	if (logn == 2) {
#if FALCON_AVX2  // yyyAVX2+1
		fpr w0, w1, w2, w3, sigma;
		__m128d ww0, ww1, wa, wb, wc, wd;
		__m128d wy0, wy1, wz0, wz1;
		__m128d half, invsqrt8, invsqrt2, neghi, neglo;
		int si0, si1, si2, si3;

		tree0 = tree + 4;
		tree1 = tree + 8;

		half = _mm_set1_pd(0.5);
		invsqrt8 = _mm_set1_pd(0.353553390593273762200422181052);
		invsqrt2 = _mm_set1_pd(0.707106781186547524400844362105);
		neghi = _mm_set_pd(-0.0, 0.0);
		neglo = _mm_set_pd(0.0, -0.0);

		/*
		 * We split t1 into w*, then do the recursive invocation,
		 * with output in w*. We finally merge back into z1.
		 */
		ww0 = _mm_loadu_pd(&t1[0].v);
		ww1 = _mm_loadu_pd(&t1[2].v);
		wa = _mm_unpacklo_pd(ww0, ww1);
		wb = _mm_unpackhi_pd(ww0, ww1);
		wc = _mm_add_pd(wa, wb);
		ww0 = _mm_mul_pd(wc, half);
		wc = _mm_sub_pd(wa, wb);
		wd = _mm_xor_pd(_mm_permute_pd(wc, 1), neghi);
		ww1 = _mm_mul_pd(_mm_add_pd(wc, wd), invsqrt8);

		w2.v = _mm_cvtsd_f64(ww1);
		w3.v = _mm_cvtsd_f64(_mm_permute_pd(ww1, 1));
		wa = ww1;
		sigma = tree1[3];
		si2 = samp(samp_ctx, w2, sigma);
		si3 = samp(samp_ctx, w3, sigma);
		ww1 = _mm_set_pd((double)si3, (double)si2);
		wa = _mm_sub_pd(wa, ww1);
		wb = _mm_loadu_pd(&tree1[0].v);
		wc = _mm_mul_pd(wa, wb);
		wd = _mm_mul_pd(wa, _mm_permute_pd(wb, 1));
		wa = _mm_unpacklo_pd(wc, wd);
		wb = _mm_unpackhi_pd(wc, wd);
		ww0 = _mm_add_pd(ww0, _mm_add_pd(wa, _mm_xor_pd(wb, neglo)));
		w0.v = _mm_cvtsd_f64(ww0);
		w1.v = _mm_cvtsd_f64(_mm_permute_pd(ww0, 1));
		sigma = tree1[2];
		si0 = samp(samp_ctx, w0, sigma);
		si1 = samp(samp_ctx, w1, sigma);
		ww0 = _mm_set_pd((double)si1, (double)si0);

		wc = _mm_mul_pd(
			_mm_set_pd((double)(si2 + si3), (double)(si2 - si3)),
			invsqrt2);
		wa = _mm_add_pd(ww0, wc);
		wb = _mm_sub_pd(ww0, wc);
		ww0 = _mm_unpacklo_pd(wa, wb);
		ww1 = _mm_unpackhi_pd(wa, wb);
		_mm_storeu_pd(&z1[0].v, ww0);
		_mm_storeu_pd(&z1[2].v, ww1);

		/*
		 * Compute tb0 = t0 + (t1 - z1) * L. Value tb0 ends up in w*.
		 */
		wy0 = _mm_sub_pd(_mm_loadu_pd(&t1[0].v), ww0);
		wy1 = _mm_sub_pd(_mm_loadu_pd(&t1[2].v), ww1);
		wz0 = _mm_loadu_pd(&tree[0].v);
		wz1 = _mm_loadu_pd(&tree[2].v);
		ww0 = _mm_sub_pd(_mm_mul_pd(wy0, wz0), _mm_mul_pd(wy1, wz1));
		ww1 = _mm_add_pd(_mm_mul_pd(wy0, wz1), _mm_mul_pd(wy1, wz0));
		ww0 = _mm_add_pd(ww0, _mm_loadu_pd(&t0[0].v));
		ww1 = _mm_add_pd(ww1, _mm_loadu_pd(&t0[2].v));

		/*
		 * Second recursive invocation.
		 */
		wa = _mm_unpacklo_pd(ww0, ww1);
		wb = _mm_unpackhi_pd(ww0, ww1);
		wc = _mm_add_pd(wa, wb);
		ww0 = _mm_mul_pd(wc, half);
		wc = _mm_sub_pd(wa, wb);
		wd = _mm_xor_pd(_mm_permute_pd(wc, 1), neghi);
		ww1 = _mm_mul_pd(_mm_add_pd(wc, wd), invsqrt8);

		w2.v = _mm_cvtsd_f64(ww1);
		w3.v = _mm_cvtsd_f64(_mm_permute_pd(ww1, 1));
		wa = ww1;
		sigma = tree0[3];
		si2 = samp(samp_ctx, w2, sigma);
		si3 = samp(samp_ctx, w3, sigma);
		ww1 = _mm_set_pd((double)si3, (double)si2);
		wa = _mm_sub_pd(wa, ww1);
		wb = _mm_loadu_pd(&tree0[0].v);
		wc = _mm_mul_pd(wa, wb);
		wd = _mm_mul_pd(wa, _mm_permute_pd(wb, 1));
		wa = _mm_unpacklo_pd(wc, wd);
		wb = _mm_unpackhi_pd(wc, wd);
		ww0 = _mm_add_pd(ww0, _mm_add_pd(wa, _mm_xor_pd(wb, neglo)));
		w0.v = _mm_cvtsd_f64(ww0);
		w1.v = _mm_cvtsd_f64(_mm_permute_pd(ww0, 1));
		sigma = tree0[2];
		si0 = samp(samp_ctx, w0, sigma);
		si1 = samp(samp_ctx, w1, sigma);
		ww0 = _mm_set_pd((double)si1, (double)si0);

		wc = _mm_mul_pd(
			_mm_set_pd((double)(si2 + si3), (double)(si2 - si3)),
			invsqrt2);
		wa = _mm_add_pd(ww0, wc);
		wb = _mm_sub_pd(ww0, wc);
		ww0 = _mm_unpacklo_pd(wa, wb);
		ww1 = _mm_unpackhi_pd(wa, wb);
		_mm_storeu_pd(&z0[0].v, ww0);
		_mm_storeu_pd(&z0[2].v, ww1);

		return;
#else  // yyyAVX2+0
		fpr x0, x1, y0, y1, w0, w1, w2, w3, sigma;
		fpr a_re, a_im, b_re, b_im, c_re, c_im;

		tree0 = tree + 4;
		tree1 = tree + 8;

		/*
		 * We split t1 into w*, then do the recursive invocation,
		 * with output in w*. We finally merge back into z1.
		 */
		a_re = t1[0];
		a_im = t1[2];
		b_re = t1[1];
		b_im = t1[3];
		c_re = fpr_add(a_re, b_re);
		c_im = fpr_add(a_im, b_im);
		w0 = fpr_half(c_re);
		w1 = fpr_half(c_im);
		c_re = fpr_sub(a_re, b_re);
		c_im = fpr_sub(a_im, b_im);
		w2 = fpr_mul(fpr_add(c_re, c_im), fpr_invsqrt8);
		w3 = fpr_mul(fpr_sub(c_im, c_re), fpr_invsqrt8);

		x0 = w2;
		x1 = w3;
		sigma = tree1[3];
		w2 = fpr_of(samp(samp_ctx, x0, sigma));
		w3 = fpr_of(samp(samp_ctx, x1, sigma));
		a_re = fpr_sub(x0, w2);
		a_im = fpr_sub(x1, w3);
		b_re = tree1[0];
		b_im = tree1[1];
		c_re = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		c_im = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		x0 = fpr_add(c_re, w0);
		x1 = fpr_add(c_im, w1);
		sigma = tree1[2];
		w0 = fpr_of(samp(samp_ctx, x0, sigma));
		w1 = fpr_of(samp(samp_ctx, x1, sigma));

		a_re = w0;
		a_im = w1;
		b_re = w2;
		b_im = w3;
		c_re = fpr_mul(fpr_sub(b_re, b_im), fpr_invsqrt2);
		c_im = fpr_mul(fpr_add(b_re, b_im), fpr_invsqrt2);
		z1[0] = w0 = fpr_add(a_re, c_re);
		z1[2] = w2 = fpr_add(a_im, c_im);
		z1[1] = w1 = fpr_sub(a_re, c_re);
		z1[3] = w3 = fpr_sub(a_im, c_im);

		/*
		 * Compute tb0 = t0 + (t1 - z1) * L. Value tb0 ends up in w*.
		 */
		w0 = fpr_sub(t1[0], w0);
		w1 = fpr_sub(t1[1], w1);
		w2 = fpr_sub(t1[2], w2);
		w3 = fpr_sub(t1[3], w3);

		a_re = w0;
		a_im = w2;
		b_re = tree[0];
		b_im = tree[2];
		w0 = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		w2 = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		a_re = w1;
		a_im = w3;
		b_re = tree[1];
		b_im = tree[3];
		w1 = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		w3 = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));

		w0 = fpr_add(w0, t0[0]);
		w1 = fpr_add(w1, t0[1]);
		w2 = fpr_add(w2, t0[2]);
		w3 = fpr_add(w3, t0[3]);

		/*
		 * Second recursive invocation.
		 */
		a_re = w0;
		a_im = w2;
		b_re = w1;
		b_im = w3;
		c_re = fpr_add(a_re, b_re);
		c_im = fpr_add(a_im, b_im);
		w0 = fpr_half(c_re);
		w1 = fpr_half(c_im);
		c_re = fpr_sub(a_re, b_re);
		c_im = fpr_sub(a_im, b_im);
		w2 = fpr_mul(fpr_add(c_re, c_im), fpr_invsqrt8);
		w3 = fpr_mul(fpr_sub(c_im, c_re), fpr_invsqrt8);

		x0 = w2;
		x1 = w3;
		sigma = tree0[3];
		w2 = y0 = fpr_of(samp(samp_ctx, x0, sigma));
		w3 = y1 = fpr_of(samp(samp_ctx, x1, sigma));
		a_re = fpr_sub(x0, y0);
		a_im = fpr_sub(x1, y1);
		b_re = tree0[0];
		b_im = tree0[1];
		c_re = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		c_im = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		x0 = fpr_add(c_re, w0);
		x1 = fpr_add(c_im, w1);
		sigma = tree0[2];
		w0 = fpr_of(samp(samp_ctx, x0, sigma));
		w1 = fpr_of(samp(samp_ctx, x1, sigma));

		a_re = w0;
		a_im = w1;
		b_re = w2;
		b_im = w3;
		c_re = fpr_mul(fpr_sub(b_re, b_im), fpr_invsqrt2);
		c_im = fpr_mul(fpr_add(b_re, b_im), fpr_invsqrt2);
		z0[0] = fpr_add(a_re, c_re);
		z0[2] = fpr_add(a_im, c_im);
		z0[1] = fpr_sub(a_re, c_re);
		z0[3] = fpr_sub(a_im, c_im);

		return;
#endif  // yyyAVX2-
	}

	/*
	 * Case logn == 1 is reachable only when using Falcon-2 (the
	 * smallest size for which Falcon is mathematically defined, but
	 * of course way too insecure to be of any use).
	 */
	if (logn == 1) {
		fpr x0, x1, y0, y1, sigma;
		fpr a_re, a_im, b_re, b_im, c_re, c_im;

		x0 = t1[0];
		x1 = t1[1];
		sigma = tree[3];
		z1[0] = y0 = fpr_of(samp(samp_ctx, x0, sigma));
		z1[1] = y1 = fpr_of(samp(samp_ctx, x1, sigma));
		a_re = fpr_sub(x0, y0);
		a_im = fpr_sub(x1, y1);
		b_re = tree[0];
		b_im = tree[1];
		c_re = fpr_sub(fpr_mul(a_re, b_re), fpr_mul(a_im, b_im));
		c_im = fpr_add(fpr_mul(a_re, b_im), fpr_mul(a_im, b_re));
		x0 = fpr_add(c_re, t0[0]);
		x1 = fpr_add(c_im, t0[1]);
		sigma = tree[2];
		z0[0] = fpr_of(samp(samp_ctx, x0, sigma));
		z0[1] = fpr_of(samp(samp_ctx, x1, sigma));

		return;
	}

	/*
	 * Normal end of recursion is for logn == 0. Since the last
	 * steps of the recursions were inlined in the blocks above
	 * (when logn == 1 or 2), this case is not reachable, and is
	 * retained here only for documentation purposes.

	if (logn == 0) {
		fpr x0, x1, sigma;

		x0 = t0[0];
		x1 = t1[0];
		sigma = tree[0];
		z0[0] = fpr_of(samp(samp_ctx, x0, sigma));
		z1[0] = fpr_of(samp(samp_ctx, x1, sigma));
		return;
	}

	 */

	/*
	 * General recursive case (logn >= 3).
	 */

	n = (size_t)1 << logn;
	hn = n >> 1;
	tree0 = tree + n;
	tree1 = tree + n + ffLDL_treesize(logn - 1);

	/*
	 * We split t1 into z1 (reused as temporary storage), then do
	 * the recursive invocation, with output in tmp. We finally
	 * merge back into z1.
	 */
	Zf(poly_split_fft)(z1, z1 + hn, t1, logn);
	ffSampling_fft(samp, samp_ctx, tmp, tmp + hn,
		tree1, z1, z1 + hn, logn - 1, tmp + n);
	Zf(poly_merge_fft)(z1, tmp, tmp + hn, logn);

	/*
	 * Compute tb0 = t0 + (t1 - z1) * L. Value tb0 ends up in tmp[].
	 */
	memcpy(tmp, t1, n * sizeof *t1);
	Zf(poly_sub)(tmp, z1, logn);
	Zf(poly_mul_fft)(tmp, tree, logn);
	Zf(poly_add)(tmp, t0, logn);

	/*
	 * Second recursive invocation.
	 */
	Zf(poly_split_fft)(z0, z0 + hn, tmp, logn);
	ffSampling_fft(samp, samp_ctx, tmp, tmp + hn,
		tree0, z0, z0 + hn, logn - 1, tmp + n);
	Zf(poly_merge_fft)(z0, tmp, tmp + hn, logn);
}

/*
 * Compute a signature: the signature contains two vectors, s1 and s2.
 * The s1 vector is not returned. The squared norm of (s1,s2) is
 * computed, and if it is short enough, then s2 is returned into the
 * s2[] buffer, and 1 is returned; otherwise, s2[] is untouched and 0 is
 * returned; the caller should then try again. This function uses an
 * expanded key.
 *
 * tmp[] must have room for at least six polynomials.
 */
static int
do_sign_tree(samplerZ samp, void *samp_ctx, int16_t *s2,
	const fpr *restrict expanded_key,
	const uint16_t *hm,
	unsigned logn, fpr *restrict tmp)
{
	size_t n, u;
	fpr *t0, *t1, *tx, *ty;
	const fpr *b00, *b01, *b10, *b11, *tree;
	fpr ni;
	uint32_t sqn, ng;
	int16_t *s1tmp, *s2tmp;

	n = MKN(logn);
	t0 = tmp;
	t1 = t0 + n;
	b00 = expanded_key + skoff_b00(logn);
	b01 = expanded_key + skoff_b01(logn);
	b10 = expanded_key + skoff_b10(logn);
	b11 = expanded_key + skoff_b11(logn);
	tree = expanded_key + skoff_tree(logn);

	/*
	 * Set the target vector to [hm, 0] (hm is the hashed message).
	 */
	for (u = 0; u < n; u ++) {
		t0[u] = fpr_of(hm[u]);
		/* This is implicit.
		t1[u] = fpr_zero;
		*/
	}

	/*
	 * Apply the lattice basis to obtain the real target
	 * vector (after normalization with regards to modulus).
	 */
	Zf(FFT)(t0, logn);
	ni = fpr_inverse_of_q;
	memcpy(t1, t0, n * sizeof *t0);
	Zf(poly_mul_fft)(t1, b01, logn);
	Zf(poly_mulconst)(t1, fpr_neg(ni), logn);
	Zf(poly_mul_fft)(t0, b11, logn);
	Zf(poly_mulconst)(t0, ni, logn);

	tx = t1 + n;
	ty = tx + n;

	/*
	 * Apply sampling. Output is written back in [tx, ty].
	 */
	ffSampling_fft(samp, samp_ctx, tx, ty, tree, t0, t1, logn, ty + n);

	/*
	 * Get the lattice point corresponding to that tiny vector.
	 */
	memcpy(t0, tx, n * sizeof *tx);
	memcpy(t1, ty, n * sizeof *ty);
	Zf(poly_mul_fft)(tx, b00, logn);
	Zf(poly_mul_fft)(ty, b10, logn);
	Zf(poly_add)(tx, ty, logn);
	memcpy(ty, t0, n * sizeof *t0);
	Zf(poly_mul_fft)(ty, b01, logn);

	memcpy(t0, tx, n * sizeof *tx);
	Zf(poly_mul_fft)(t1, b11, logn);
	Zf(poly_add)(t1, ty, logn);

	Zf(iFFT)(t0, logn);
	Zf(iFFT)(t1, logn);

	/*
	 * Compute the signature.
	 */
	s1tmp = (int16_t *)tx;
	sqn = 0;
	ng = 0;
	for (u = 0; u < n; u ++) {
		int32_t z;

		z = (int32_t)hm[u] - (int32_t)fpr_rint(t0[u]);
		sqn += (uint32_t)(z * z);
		ng |= sqn;
		s1tmp[u] = (int16_t)z;
	}
	sqn |= -(ng >> 31);

	/*
	 * With "normal" degrees (e.g. 512 or 1024), it is very
	 * improbable that the computed vector is not short enough;
	 * however, it may happen in practice for the very reduced
	 * versions (e.g. degree 16 or below). In that case, the caller
	 * will loop, and we must not write anything into s2[] because
	 * s2[] may overlap with the hashed message hm[] and we need
	 * hm[] for the next iteration.
	 */
	s2tmp = (int16_t *)tmp;
	for (u = 0; u < n; u ++) {
		s2tmp[u] = (int16_t)-fpr_rint(t1[u]);
	}
	if (Zf(is_short_half)(sqn, s2tmp, logn)) {
		memcpy(s2, s2tmp, n * sizeof *s2);
		memcpy(tmp, s1tmp, n * sizeof *s1tmp);
		return 1;
	}
	return 0;
}

/*
 * Compute a signature: the signature contains two vectors, s1 and s2.
 * The s1 vector is not returned. The squared norm of (s1,s2) is
 * computed, and if it is short enough, then s2 is returned into the
 * s2[] buffer, and 1 is returned; otherwise, s2[] is untouched and 0 is
 * returned; the caller should then try again.
 *
 * tmp[] must have room for at least nine polynomials.
 */
static int
do_sign_dyn(samplerZ samp, void *samp_ctx, int16_t *s2,
	const int8_t *restrict f, const int8_t *restrict g,
	const int8_t *restrict F, const int8_t *restrict G,
	const uint16_t *hm, unsigned logn, fpr *restrict tmp)
{
	size_t n, u;
	fpr *t0, *t1, *tx, *ty;
	fpr *b00, *b01, *b10, *b11, *g00, *g01, *g11;
	fpr ni;
	uint32_t sqn, ng;
	int16_t *s1tmp, *s2tmp;

	n = MKN(logn);

	/*
	 * Lattice basis is B = [[g, -f], [G, -F]]. We convert it to FFT.
	 */
	b00 = tmp;
	b01 = b00 + n;
	b10 = b01 + n;
	b11 = b10 + n;
	smallints_to_fpr(b01, f, logn);
	smallints_to_fpr(b00, g, logn);
	smallints_to_fpr(b11, F, logn);
	smallints_to_fpr(b10, G, logn);
	Zf(FFT)(b01, logn);
	Zf(FFT)(b00, logn);
	Zf(FFT)(b11, logn);
	Zf(FFT)(b10, logn);
	Zf(poly_neg)(b01, logn);
	Zf(poly_neg)(b11, logn);

	/*
	 * Compute the Gram matrix G = B·B*. Formulas are:
	 *   g00 = b00*adj(b00) + b01*adj(b01)
	 *   g01 = b00*adj(b10) + b01*adj(b11)
	 *   g10 = b10*adj(b00) + b11*adj(b01)
	 *   g11 = b10*adj(b10) + b11*adj(b11)
	 *
	 * For historical reasons, this implementation uses
	 * g00, g01 and g11 (upper triangle). g10 is not kept
	 * since it is equal to adj(g01).
	 *
	 * We _replace_ the matrix B with the Gram matrix, but we
	 * must keep b01 and b11 for computing the target vector.
	 */
	t0 = b11 + n;
	t1 = t0 + n;

	memcpy(t0, b01, n * sizeof *b01);
	Zf(poly_mulselfadj_fft)(t0, logn);    // t0 <- b01*adj(b01)

	memcpy(t1, b00, n * sizeof *b00);
	Zf(poly_muladj_fft)(t1, b10, logn);   // t1 <- b00*adj(b10)
	Zf(poly_mulselfadj_fft)(b00, logn);   // b00 <- b00*adj(b00)
	Zf(poly_add)(b00, t0, logn);      // b00 <- g00
	memcpy(t0, b01, n * sizeof *b01);
	Zf(poly_muladj_fft)(b01, b11, logn);  // b01 <- b01*adj(b11)
	Zf(poly_add)(b01, t1, logn);      // b01 <- g01

	Zf(poly_mulselfadj_fft)(b10, logn);   // b10 <- b10*adj(b10)
	memcpy(t1, b11, n * sizeof *b11);
	Zf(poly_mulselfadj_fft)(t1, logn);    // t1 <- b11*adj(b11)
	Zf(poly_add)(b10, t1, logn);      // b10 <- g11

	/*
	 * We rename variables to make things clearer. The three elements
	 * of the Gram matrix uses the first 3*n slots of tmp[], followed
	 * by b11 and b01 (in that order).
	 */
	g00 = b00;
	g01 = b01;
	g11 = b10;
	b01 = t0;
	t0 = b01 + n;
	t1 = t0 + n;

	/*
	 * Memory layout at that point:
	 *   g00 g01 g11 b11 b01 t0 t1
	 */

	/*
	 * Set the target vector to [hm, 0] (hm is the hashed message).
	 */
	for (u = 0; u < n; u ++) {
		t0[u] = fpr_of(hm[u]);
		/* This is implicit.
		t1[u] = fpr_zero;
		*/
	}

	/*
	 * Apply the lattice basis to obtain the real target
	 * vector (after normalization with regards to modulus).
	 */
	Zf(FFT)(t0, logn);
	ni = fpr_inverse_of_q;
	memcpy(t1, t0, n * sizeof *t0);
	Zf(poly_mul_fft)(t1, b01, logn);
	Zf(poly_mulconst)(t1, fpr_neg(ni), logn);
	Zf(poly_mul_fft)(t0, b11, logn);
	Zf(poly_mulconst)(t0, ni, logn);

	/*
	 * b01 and b11 can be discarded, so we move back (t0,t1).
	 * Memory layout is now:
	 *      g00 g01 g11 t0 t1
	 */
	memcpy(b11, t0, n * 2 * sizeof *t0);
	t0 = g11 + n;
	t1 = t0 + n;

	/*
	 * Apply sampling; result is written over (t0,t1).
	 */
	ffSampling_fft_dyntree(samp, samp_ctx,
		t0, t1, g00, g01, g11, logn, logn, t1 + n);

	/*
	 * We arrange the layout back to:
	 *     b00 b01 b10 b11 t0 t1
	 *
	 * We did not conserve the matrix basis, so we must recompute
	 * it now.
	 */
	b00 = tmp;
	b01 = b00 + n;
	b10 = b01 + n;
	b11 = b10 + n;
	memmove(b11 + n, t0, n * 2 * sizeof *t0);
	t0 = b11 + n;
	t1 = t0 + n;
	smallints_to_fpr(b01, f, logn);
	smallints_to_fpr(b00, g, logn);
	smallints_to_fpr(b11, F, logn);
	smallints_to_fpr(b10, G, logn);
	Zf(FFT)(b01, logn);
	Zf(FFT)(b00, logn);
	Zf(FFT)(b11, logn);
	Zf(FFT)(b10, logn);
	Zf(poly_neg)(b01, logn);
	Zf(poly_neg)(b11, logn);
	tx = t1 + n;
	ty = tx + n;

	/*
	 * Get the lattice point corresponding to that tiny vector.
	 */
	memcpy(tx, t0, n * sizeof *t0);
	memcpy(ty, t1, n * sizeof *t1);
	Zf(poly_mul_fft)(tx, b00, logn); // t0 * g
	Zf(poly_mul_fft)(ty, b10, logn); // t1 * G
	Zf(poly_add)(tx, ty, logn); // computes t0*g + t1*G, stores in tx
	memcpy(ty, t0, n * sizeof *t0); // ty = t0
	Zf(poly_mul_fft)(ty, b01, logn); // ty := t0*b01

	memcpy(t0, tx, n * sizeof *tx); // stores t_*_B[0] in t0
	Zf(poly_mul_fft)(t1, b11, logn); // t1 := t0*b01
	Zf(poly_add)(t1, ty, logn); // final compute t0*-f + t1*-F, stores in t1
	Zf(iFFT)(t0, logn);
	Zf(iFFT)(t1, logn);

	s1tmp = (int16_t *)tx;
	sqn = 0;
	ng = 0;
	for (u = 0; u < n; u ++) {
		int32_t z;

		z = (int32_t)hm[u] - (int32_t)fpr_rint(t0[u]); // z0/z1 := t0/t1 - z0/z1
		sqn += (uint32_t)(z * z);
		ng |= sqn;
		s1tmp[u] = (int16_t)z;
	}
	sqn |= -(ng >> 31);

	/*
	 * With "normal" degrees (e.g. 512 or 1024), it is very
	 * improbable that the computed vector is not short enough;
	 * however, it may happen in practice for the very reduced
	 * versions (e.g. degree 16 or below). In that case, the caller
	 * will loop, and we must not write anything into s2[] because
	 * s2[] may overlap with the hashed message hm[] and we need
	 * hm[] for the next iteration.
	 */
	s2tmp = (int16_t *)tmp;
	for (u = 0; u < n; u ++) {
		s2tmp[u] = (int16_t)-fpr_rint(t1[u]);
	}
	if (Zf(is_short_half)(sqn, s2tmp, logn)) {
		memcpy(s2, s2tmp, n * sizeof *s2);
		memcpy(tmp, s1tmp, n * sizeof *s1tmp);
		return 1;
	}
	return 0;
}

/// ONLINE OFFLINE DO SIGN FUNCTION
static int
do_sign_dyn_lazy(int8_t* sample1, int8_t* sample2, uint16_t* sample_target,
                 int16_t *s2,
    const fpr *restrict f_fft, const fpr *restrict g_fft,
    const fpr *restrict F_fft, const fpr *restrict G_fft,
    const uint16_t *hm, unsigned logn, fpr *restrict tmp __attribute((unused)))
{
    size_t n, u;
    int loop __attribute((unused));

    n = MKN(logn);

	// for(loop = 0; loop < 10; loop++)
	// 	printf("target x3[%d]: (%d),\n", loop, (int) x3[loop]);

	// START ONLINE
    int32_t res1[n];
    int32_t res2[n];
    // "real" target = hm + x3
    falcon_inner_mq_poly_addto(sample_target, hm, logn);
    short_preimage(sample_target, //
                   f_fft, g_fft, //
                   F_fft, G_fft, //
                   res1, res2,  //
                   logn);
	
    // remove the gaussian sample
    for (size_t i=0; i<n; i++) {
        res1[i] -= sample1[i];
        res2[i] -= sample2[i];
    }

	// for(loop = 0; loop < 10; loop++)
	// 	printf("uncompressed_sig[%d]: (%d, %d),\n", loop, res1[loop], res2[loop]);

	/*
	 * Compute the signature.
	 */
	uint32_t sqn, ng;
	sqn = 0;
	ng = 0;
	for (u = 0; u < n; u ++) {
		int32_t z;
		z = (int32_t)res1[u];
		sqn += (uint32_t)(z * z);
		ng |= sqn;
		//y1tmp[u] = (int16_t)z;
	}
	sqn |= -(ng >> 31);

	// for(loop = 0; loop < 10; loop++)
	// 	printf("compressed_y1[%d]: (%d, %d),\n", loop, y1tmp[loop], y2_sig[loop]);	

    int16_t y2tmp[n];
	for (u = 0; u < n; u ++) {
		y2tmp[u] = (int16_t)-res2[u];
	}

	if (Zf(is_short_half)(sqn, y2tmp, logn)) {
		memcpy(s2, y2tmp, n * sizeof *s2);
        return 1;
	}
    // signature size is probably not ok. let's still output it.
    memcpy(s2, y2tmp, n * sizeof *s2);
    return 0;
}


/*
 * Sample an integer value along a half-gaussian distribution centered
 * on zero and standard deviation 1.8205, with a precision of 72 bits.
 */
TARGET_AVX2
int
Zf(gaussian0_sampler)(prng *p)
{
#if FALCON_AVX2 // yyyAVX2+1

	/*
	 * High words.
	 */
	static const union {
		uint16_t u16[16];
		__m256i ymm[1];
	} rhi15 = {
		{
			0x51FB, 0x2A69, 0x113E, 0x0568,
			0x014A, 0x003B, 0x0008, 0x0000,
			0x0000, 0x0000, 0x0000, 0x0000,
			0x0000, 0x0000, 0x0000, 0x0000
		}
	};

	static const union {
		uint64_t u64[20];
		__m256i ymm[5];
	} rlo57 = {
		{
			0x1F42ED3AC391802, 0x12B181F3F7DDB82,
			0x1CDD0934829C1FF, 0x1754377C7994AE4,
			0x1846CAEF33F1F6F, 0x14AC754ED74BD5F,
			0x024DD542B776AE4, 0x1A1FFDC65AD63DA,
			0x01F80D88A7B6428, 0x001C3FDB2040C69,
			0x00012CF24D031FB, 0x00000949F8B091F,
			0x0000003665DA998, 0x00000000EBF6EBB,
			0x0000000002F5D7E, 0x000000000007098,
			0x0000000000000C6, 0x000000000000001,
			0x000000000000000, 0x000000000000000
		}
	};

	uint64_t lo;
	unsigned hi;
	__m256i xhi, rhi, gthi, eqhi, eqm;
	__m256i xlo, gtlo0, gtlo1, gtlo2, gtlo3, gtlo4;
	__m128i t, zt;
	int r;

	/*
	 * Get a 72-bit random value and split it into a low part
	 * (57 bits) and a high part (15 bits)
	 */
	lo = prng_get_u64(p);
	hi = prng_get_u8(p);
	hi = (hi << 7) | (unsigned)(lo >> 57);
	lo &= 0x1FFFFFFFFFFFFFF;

	/*
	 * Broadcast the high part and compare it with the relevant
	 * values. We need both a "greater than" and an "equal"
	 * comparisons.
	 */
	xhi = _mm256_broadcastw_epi16(_mm_cvtsi32_si128(hi));
	rhi = _mm256_loadu_si256(&rhi15.ymm[0]);
	gthi = _mm256_cmpgt_epi16(rhi, xhi);
	eqhi = _mm256_cmpeq_epi16(rhi, xhi);

	/*
	 * The result is the number of 72-bit values (among the list of 19)
	 * which are greater than the 72-bit random value. We first count
	 * all non-zero 16-bit elements in the first eight of gthi. Such
	 * elements have value -1 or 0, so we first negate them.
	 */
	t = _mm_srli_epi16(_mm256_castsi256_si128(gthi), 15);
	zt = _mm_setzero_si128();
	t = _mm_hadd_epi16(t, zt);
	t = _mm_hadd_epi16(t, zt);
	t = _mm_hadd_epi16(t, zt);
	r = _mm_cvtsi128_si32(t);

	/*
	 * We must look at the low bits for all values for which the
	 * high bits are an "equal" match; values 8-18 all have the
	 * same high bits (0).
	 * On 32-bit systems, 'lo' really is two registers, requiring
	 * some extra code.
	 */
#if defined(__x86_64__) || defined(_M_X64)
	xlo = _mm256_broadcastq_epi64(_mm_cvtsi64_si128(*(int64_t *)&lo));
#else
	{
		uint32_t e0, e1;
		int32_t f0, f1;

		e0 = (uint32_t)lo;
		e1 = (uint32_t)(lo >> 32);
		f0 = *(int32_t *)&e0;
		f1 = *(int32_t *)&e1;
		xlo = _mm256_set_epi32(f1, f0, f1, f0, f1, f0, f1, f0);
	}
#endif
	gtlo0 = _mm256_cmpgt_epi64(_mm256_loadu_si256(&rlo57.ymm[0]), xlo); 
	gtlo1 = _mm256_cmpgt_epi64(_mm256_loadu_si256(&rlo57.ymm[1]), xlo); 
	gtlo2 = _mm256_cmpgt_epi64(_mm256_loadu_si256(&rlo57.ymm[2]), xlo); 
	gtlo3 = _mm256_cmpgt_epi64(_mm256_loadu_si256(&rlo57.ymm[3]), xlo); 
	gtlo4 = _mm256_cmpgt_epi64(_mm256_loadu_si256(&rlo57.ymm[4]), xlo); 

	/*
	 * Keep only comparison results that correspond to the non-zero
	 * elements in eqhi.
	 */
	gtlo0 = _mm256_and_si256(gtlo0, _mm256_cvtepi16_epi64(
		_mm256_castsi256_si128(eqhi)));
	gtlo1 = _mm256_and_si256(gtlo1, _mm256_cvtepi16_epi64(
		_mm256_castsi256_si128(_mm256_bsrli_epi128(eqhi, 8))));
	eqm = _mm256_permute4x64_epi64(eqhi, 0xFF);
	gtlo2 = _mm256_and_si256(gtlo2, eqm);
	gtlo3 = _mm256_and_si256(gtlo3, eqm);
	gtlo4 = _mm256_and_si256(gtlo4, eqm);

	/*
	 * Add all values to count the total number of "-1" elements.
	 * Since the first eight "high" words are all different, only
	 * one element (at most) in gtlo0:gtlo1 can be non-zero; however,
	 * if the high word of the random value is zero, then many
	 * elements of gtlo2:gtlo3:gtlo4 can be non-zero.
	 */
	gtlo0 = _mm256_or_si256(gtlo0, gtlo1);
	gtlo0 = _mm256_add_epi64(
		_mm256_add_epi64(gtlo0, gtlo2),
		_mm256_add_epi64(gtlo3, gtlo4));
	t = _mm_add_epi64(
		_mm256_castsi256_si128(gtlo0),
		_mm256_extracti128_si256(gtlo0, 1));
	t = _mm_add_epi64(t, _mm_srli_si128(t, 8));
	r -= _mm_cvtsi128_si32(t);

	return r;

#else // yyyAVX2+0

	static const uint32_t dist[] = {
		10745844u,  3068844u,  3741698u,
		 5559083u,  1580863u,  8248194u,
		 2260429u, 13669192u,  2736639u,
		  708981u,  4421575u, 10046180u,
		  169348u,  7122675u,  4136815u,
		   30538u, 13063405u,  7650655u,
		    4132u, 14505003u,  7826148u,
		     417u, 16768101u, 11363290u,
		      31u,  8444042u,  8086568u,
		       1u, 12844466u,   265321u,
		       0u,  1232676u, 13644283u,
		       0u,    38047u,  9111839u,
		       0u,      870u,  6138264u,
		       0u,       14u, 12545723u,
		       0u,        0u,  3104126u,
		       0u,        0u,    28824u,
		       0u,        0u,      198u,
		       0u,        0u,        1u
	};

	uint32_t v0, v1, v2, hi;
	uint64_t lo;
	size_t u;
	int z;

	/*
	 * Get a random 72-bit value, into three 24-bit limbs v0..v2.
	 */
	lo = prng_get_u64(p);
	hi = prng_get_u8(p);
	v0 = (uint32_t)lo & 0xFFFFFF;
	v1 = (uint32_t)(lo >> 24) & 0xFFFFFF;
	v2 = (uint32_t)(lo >> 48) | (hi << 16);

	/*
	 * Sampled value is z, such that v0..v2 is lower than the first
	 * z elements of the table.
	 */
	z = 0;
	for (u = 0; u < (sizeof dist) / sizeof(dist[0]); u += 3) {
		uint32_t w0, w1, w2, cc;

		w0 = dist[u + 2];
		w1 = dist[u + 1];
		w2 = dist[u + 0];
		cc = (v0 - w0) >> 31;
		cc = (v1 - w1 - cc) >> 31;
		cc = (v2 - w2 - cc) >> 31;
		z += (int)cc;
	}
	return z;

#endif // yyyAVX2-
}

/*
 * Sample a bit with probability exp(-x) for some x >= 0.
 */
TARGET_AVX2
static int
BerExp(prng *p, fpr x, fpr ccs)
{
	int s, i;
	fpr r;
	uint32_t sw, w;
	uint64_t z;

	/*
	 * Reduce x modulo log(2): x = s*log(2) + r, with s an integer,
	 * and 0 <= r < log(2). Since x >= 0, we can use fpr_trunc().
	 */
	s = (int)fpr_trunc(fpr_mul(x, fpr_inv_log2));
	r = fpr_sub(x, fpr_mul(fpr_of(s), fpr_log2));

	/*
	 * It may happen (quite rarely) that s >= 64; if sigma = 1.2
	 * (the minimum value for sigma), r = 0 and b = 1, then we get
	 * s >= 64 if the half-Gaussian produced a z >= 13, which happens
	 * with probability about 0.000000000230383991, which is
	 * approximatively equal to 2^(-32). In any case, if s >= 64,
	 * then BerExp will be non-zero with probability less than
	 * 2^(-64), so we can simply saturate s at 63.
	 */
	sw = (uint32_t)s;
	sw ^= (sw ^ 63) & -((63 - sw) >> 31);
	s = (int)sw;

	/*
	 * Compute exp(-r); we know that 0 <= r < log(2) at this point, so
	 * we can use fpr_expm_p63(), which yields a result scaled to 2^63.
	 * We scale it up to 2^64, then right-shift it by s bits because
	 * we really want exp(-x) = 2^(-s)*exp(-r).
	 *
	 * The "-1" operation makes sure that the value fits on 64 bits
	 * (i.e. if r = 0, we may get 2^64, and we prefer 2^64-1 in that
	 * case). The bias is negligible since fpr_expm_p63() only computes
	 * with 51 bits of precision or so.
	 */
	z = ((fpr_expm_p63(r, ccs) << 1) - 1) >> s;

	/*
	 * Sample a bit with probability exp(-x). Since x = s*log(2) + r,
	 * exp(-x) = 2^-s * exp(-r), we compare lazily exp(-x) with the
	 * PRNG output to limit its consumption, the sign of the difference
	 * yields the expected result.
	 */
	i = 64;
	do {
		i -= 8;
		w = prng_get_u8(p) - ((uint32_t)(z >> i) & 0xFF);
	} while (!w && i > 0);
	return (int)(w >> 31);
}

/*
 * The sampler produces a random integer that follows a discrete Gaussian
 * distribution, centered on mu, and with standard deviation sigma. The
 * provided parameter isigma is equal to 1/sigma.
 *
 * The value of sigma MUST lie between 1 and 2 (i.e. isigma lies between
 * 0.5 and 1); in Falcon, sigma should always be between 1.2 and 1.9.
 */
TARGET_AVX2
int
Zf(sampler)(void *ctx, fpr mu, fpr isigma)
{
	sampler_context *spc;
	int s;
	fpr r, dss, ccs;

	spc = ctx;

	/*
	 * Center is mu. We compute mu = s + r where s is an integer
	 * and 0 <= r < 1.
	 */
	s = (int)fpr_floor(mu);
	r = fpr_sub(mu, fpr_of(s));

	/*
	 * dss = 1/(2*sigma^2) = 0.5*(isigma^2).
	 */
	dss = fpr_half(fpr_sqr(isigma));

	/*
	 * ccs = sigma_min / sigma = sigma_min * isigma.
	 */
	ccs = fpr_mul(isigma, spc->sigma_min);

	/*
	 * We now need to sample on center r.
	 */
	for (;;) {
		int z0, z, b;
		fpr x;

		/*
		 * Sample z for a Gaussian distribution. Then get a
		 * random bit b to turn the sampling into a bimodal
		 * distribution: if b = 1, we use z+1, otherwise we
		 * use -z. We thus have two situations:
		 *
		 *  - b = 1: z >= 1 and sampled against a Gaussian
		 *    centered on 1.
		 *  - b = 0: z <= 0 and sampled against a Gaussian
		 *    centered on 0.
		 */
		z0 = Zf(gaussian0_sampler)(&spc->p);
		b = (int)prng_get_u8(&spc->p) & 1;
		z = b + ((b << 1) - 1) * z0;

		/*
		 * Rejection sampling. We want a Gaussian centered on r;
		 * but we sampled against a Gaussian centered on b (0 or
		 * 1). But we know that z is always in the range where
		 * our sampling distribution is greater than the Gaussian
		 * distribution, so rejection works.
		 *
		 * We got z with distribution:
		 *    G(z) = exp(-((z-b)^2)/(2*sigma0^2))
		 * We target distribution:
		 *    S(z) = exp(-((z-r)^2)/(2*sigma^2))
		 * Rejection sampling works by keeping the value z with
		 * probability S(z)/G(z), and starting again otherwise.
		 * This requires S(z) <= G(z), which is the case here.
		 * Thus, we simply need to keep our z with probability:
		 *    P = exp(-x)
		 * where:
		 *    x = ((z-r)^2)/(2*sigma^2) - ((z-b)^2)/(2*sigma0^2)
		 *
		 * Here, we scale up the Bernouilli distribution, which
		 * makes rejection more probable, but makes rejection
		 * rate sufficiently decorrelated from the Gaussian
		 * center and standard deviation that the whole sampler
		 * can be said to be constant-time.
		 */
		x = fpr_mul(fpr_sqr(fpr_sub(fpr_of(z), r)), dss);
		x = fpr_sub(x, fpr_mul(fpr_of(z0 * z0), fpr_inv_2sqrsigma0));
		if (BerExp(&spc->p, x, ccs)) {
			/*
			 * Rejection sampling was centered on r, but the
			 * actual center is mu = s + r.
			 */
			return s + z;
		}
	}
}

/* see inner.h */
void
Zf(sign_tree)(int16_t *sig, inner_shake256_context *rng,
	const fpr *restrict expanded_key,
	const uint16_t *hm, unsigned logn, uint8_t *tmp)
{
	fpr *ftmp;

	ftmp = (fpr *)tmp;
	for (;;) {
		/*
		 * Signature produces short vectors s1 and s2. The
		 * signature is acceptable only if the aggregate vector
		 * s1,s2 is short; we must use the same bound as the
		 * verifier.
		 *
		 * If the signature is acceptable, then we return only s2
		 * (the verifier recomputes s1 from s2, the hashed message,
		 * and the public key).
		 */
		sampler_context spc;
		samplerZ samp;
		void *samp_ctx;

		/*
		 * Normal sampling. We use a fast PRNG seeded from our
		 * SHAKE context ('rng').
		 */
		spc.sigma_min = fpr_sigma_min[logn];
		Zf(prng_init)(&spc.p, rng);
		samp = Zf(sampler);
		samp_ctx = &spc;

		/*
		 * Do the actual signature.
		 */
		if (do_sign_tree(samp, samp_ctx, sig,
			expanded_key, hm, logn, ftmp))
		{
			break;
		}
	}
}

/* see inner.h */
void
Zf(sign_dyn)(int16_t *sig, inner_shake256_context *rng,
	const int8_t *restrict f, const int8_t *restrict g,
	const int8_t *restrict F, const int8_t *restrict G,
	const uint16_t *hm, unsigned logn, uint8_t *tmp)
{
	fpr *ftmp;

	ftmp = (fpr *)tmp;
	for (;;) {
		/*
		 * Signature produces short vectors s1 and s2. The
		 * signature is acceptable only if the aggregate vector
		 * s1,s2 is short; we must use the same bound as the
		 * verifier.
		 *
		 * If the signature is acceptable, then we return only s2
		 * (the verifier recomputes s1 from s2, the hashed message,
		 * and the public key).
		 */
		sampler_context spc;
		samplerZ samp;
		void *samp_ctx;

		/*
		 * Normal sampling. We use a fast PRNG seeded from our
		 * SHAKE context ('rng').
		 */
		spc.sigma_min = fpr_sigma_min[logn];
		Zf(prng_init)(&spc.p, rng);
		samp = Zf(sampler);

		samp_ctx = &spc;

		/*
		 * Do the actual signature.
		 */
		if (do_sign_dyn(samp, samp_ctx, sig,
			f, g, F, G, hm, logn, ftmp))
		{
			break;
		}
	}
}

/* see inner.h */
void
Zf(sign_dyn_lazy)(int16_t *sig, inner_shake256_context *rng,
                  const int8_t *restrict f, const int8_t *restrict g,
                  const int8_t *restrict F, const int8_t *restrict G,
                  const uint16_t *h,
                  const uint16_t *hm, unsigned logn, uint8_t *tmp)
                  __attribute((noinline));
void
Zf(sign_dyn_lazy)(int16_t *sig, inner_shake256_context *rng,
	const int8_t *restrict f, const int8_t *restrict g,
	const int8_t *restrict F, const int8_t *restrict G,
	const uint16_t *h,
	const uint16_t *hm, unsigned logn, uint8_t *tmp)
{
    const size_t n = MKN(logn);
	fpr *ftmp;
    fpr f_fft[n], g_fft[n], F_fft[n], G_fft[n];


    // START OFFLINE
    /*
     * Lattice basis is B = [[g, f], [G, F]]. We convert it to FFT.
     */
    smallints_to_fpr(f_fft, f, logn);
    smallints_to_fpr(g_fft, g, logn);
    smallints_to_fpr(F_fft, F, logn);
    smallints_to_fpr(G_fft, G, logn);
    Zf(FFT)(f_fft, logn); // f
    Zf(FFT)(g_fft, logn); // g
    Zf(FFT)(F_fft, logn); // F
    Zf(FFT)(G_fft, logn); // G

    uint16_t h_monty[n];
    memcpy(h_monty, h, n*sizeof(uint16_t));
    falcon_inner_to_ntt_monty(h_monty, logn);

    // compute the Gaussian blinding sample
    // gaussian
    sampler_context sc;
    fpr isigma __attribute((unused)), mu __attribute((unused));
    fpr muinc __attribute((unused)); // TODO check if really unused?;
    Zf(prng_init)(&sc.p, rng);
    sc.sigma_min = fpr_sigma_min[9];

    isigma = fpr_div(fpr_of(10), fpr_of(17));
    mu = fpr_neg(fpr_one);
    muinc = fpr_div(fpr_one, fpr_of(10));

    // modulo_lattice:  2n coords -> n coords mod q
    //   any lattice point: modulo_lattice = 0
    //   (x,0)              modulo_lattice(x,0) = x mod q
    //                      modulo_lattice(q,0)
    //                      modulo_lattice(h,1)
    //   g = hf mod q?
    //                      modulo_lattice(G,g)
    //                      modulo_lattice(F,G)

    // (int_x3, int_x4) are the small gaussian vector
    int8_t sample1[n];
    int8_t sample2[n];

    // gauss_sampler(&sc, mu, isigma, sample1, n);
    // gauss_sampler(&sc, mu, isigma, sample2, n);

	sample_gaussian_poly_bern(sample1, sample2, n);

    // for(int loop = 0; loop < 10; loop++)
    // 	printf("gauss_x3x4[%d]: (%d, %d),\n", loop, sample1[loop], sample2[loop]);

    // x3 = int_x3 - h * int_x4 mod q the target
    uint16_t sample_target[n];
    compute_target(h_monty, sample1, sample2, sample_target, logn);



    ftmp = (fpr *)tmp;
    /*
	 * Do the actual signature.
	 */
	do_sign_dyn_lazy(
        sample1,sample2,sample_target,
        sig,
	    f_fft, g_fft, F_fft, G_fft,
        hm, logn, ftmp);
}

int
sign_dyn_lazy_online(
        int8_t* sample1, int8_t* sample2, uint16_t* sample_target,
                int16_t *s2,
                 const fpr *restrict f_fft, const fpr *restrict g_fft,
                 const fpr *restrict F_fft, const fpr *restrict G_fft,
                 const uint16_t *hm, unsigned logn, fpr *restrict tmp __attribute((unused))) {
	// add/remove to trigger denoised or noised (with Gaussian noise) respectively
	// const size_t n = MKN(logn);
    // memset(sample1, 0, n*sizeof(int8_t));
    // memset(sample2, 0, n*sizeof(int8_t));
	// memset(sample_target, 0, n*sizeof(uint16_t));
    return do_sign_dyn_lazy(sample1, sample2, sample_target, s2, f_fft, g_fft, F_fft, G_fft, hm, logn, tmp);
}

void sign_dyn_lazy_offline(
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
                                   ) {
    const size_t n = MKN(logn);

    // START OFFLINE
    /*
     * Lattice basis is B = [[g, f], [G, F]]. We convert it to FFT.
     */
    smallints_to_fpr(f_fft, f, logn);
    smallints_to_fpr(g_fft, g, logn);
    smallints_to_fpr(F_fft, F, logn);
    smallints_to_fpr(G_fft, G, logn);
    Zf(FFT)(f_fft, logn); // f
    Zf(FFT)(g_fft, logn); // g
    Zf(FFT)(F_fft, logn); // F
    Zf(FFT)(G_fft, logn); // G

    uint16_t h_monty[n];
    memcpy(h_monty, h, n * sizeof(uint16_t));
    falcon_inner_to_ntt_monty(h_monty, logn);

    // compute the Gaussian blinding sample
    // gaussian
    sampler_context sc;
    fpr isigma __attribute((unused)), mu __attribute((unused));
    fpr muinc __attribute((unused)); // TODO check if really unused?;
    Zf(prng_init)(&sc.p, rng);
    sc.sigma_min = fpr_sigma_min[9];

    isigma = fpr_div(fpr_of(10), fpr_of(17));
    mu = fpr_neg(fpr_one);
    muinc = fpr_div(fpr_one, fpr_of(10));

    // modulo_lattice:  2n coords -> n coords mod q
    //   any lattice point: modulo_lattice = 0
    //   (x,0)              modulo_lattice(x,0) = x mod q
    //                      modulo_lattice(q,0)
    //                      modulo_lattice(h,1)
    //   g = hf mod q?
    //                      modulo_lattice(G,g)
    //                      modulo_lattice(F,G)

    // (int_x3,int_x4) are the small gaussian vector

	// remove below for falcon sampler
    // gauss_sampler(&sc, mu, isigma, sample1, n);
    // gauss_sampler(&sc, mu, isigma, sample2, n);

	// bliss-like gaussian sampler
	sample_gaussian_poly_bern(sample1, sample2, n);

    // x3 = int_x3 - h * int_x4 mod q the target
    compute_target(h_monty, sample1, sample2, sample_target, logn);
}
