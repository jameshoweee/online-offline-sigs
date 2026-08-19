# Online/Offline Signatures: review, fixes, fuzzing, optimisation

Working document. Tracks the review of the online/offline Falcon
(`falcon-lazy` / `falcon-lazy2`), the fixes applied, fuzzing results, the
constant-time diagnosis, and before/after cycle and area numbers.

## Measurement setup

- Host: AWS Xeon Platinum 8259CL (c5), gcc 15 / clang 21, `-O3`.
- No hardware PMU in this guest (perf `cycles` unsupported, google-benchmark
  libpfm unavailable, distro libbenchmark is a DEBUG build). Cycles are measured
  with a dedicated rdtscp harness (`falcon-lazy2/tests/bench_cycles.cpp`):
  warmup, then median-of-N single-op samples with MAD, pinned via `taskset -c 1`.
- Reported "cycles" are invariant-TSC ticks (nominal 2.5 GHz), not core-clock
  cycles. They are consistent across before/after, so relative deltas are the
  headline; absolute numbers should be read as TSC ticks.
- Build: `cmake .. -DCMAKE_C_FLAGS="-O3 -g -fno-omit-frame-pointer"
  -DCMAKE_CXX_FLAGS="-O3 -g -fno-omit-frame-pointer"` then
  `taskset -c 1 ./bench_cycles <logn>`.

## Baseline (BEFORE), original code at -O3

invariant-TSC ticks, median (MAD):

| op | logn=9 (n=512) | logn=10 (n=1024) |
| --- | --- | --- |
| keygen | 20,836,316 (3,664,920) | 58,611,170 (7,143,628) |
| sign_offline | 308,160 (2,668) | 598,326 (5,810) |
| sign_online | 69,536 (162) | 141,824 (374) |
| sign_dyn_orig | 654,948 (8,526) | 1,317,112 (13,188) |
| verify | 27,490 (32) | 56,536 (50) |

- Online speedup vs original `sign_dyn`: 9.4x (n=512), 9.3x (n=1024).
- C4 correctness oracle: lazy signatures verify under Falcon's real
  `verify_raw` bound at **2000/2000 (100%)** for both logn 9 and 10. The scheme
  is functionally correct; the unit tests' 6000 norm proxy is looser than the
  real bound but no signature exceeded the real bound in 2000 trials per size.
- Unit tests: all 8 core tests pass (`keygen`, `original_sig`, `lazy_sig`,
  `sample_gaussian`, `sample_gaussian_poly_bern`, `mul_by_h`, `compute_target`,
  `short_preimage`).

## Confirmed findings (from reading the real source + runtime)

Severity uses CRIT/HIGH/MED/LOW/INFO.

- **C2 (HIGH, security; benchmark-neutral):** the offline Gaussian sampler
  `sample_gaussian_poly_bern` (sign.c) draws all randomness from a global
  fixed-seed Fibonacci generator `randombytes` (sign.c:221,
  `0xDEADBEEF/0x01234567`). The SHAKE `rng` passed to `sign_dyn_lazy_offline`
  is initialised into `sc.p` and then never used (the `gauss_sampler` calls are
  commented out; `isigma/mu/muinc` are unused). So the Gaussian blinding vector
  is deterministic across program runs. Fix: seed the sampler from the caller's
  SHAKE prng (as Falcon's own sampler does).
- **C1 (LOW, dead/test code):** `sample_gaussian()` (sign.c:396) sets
  `res[i] = 0` after sampling ("remove to get back masked version"), so its unit
  test measures norm 0 and passes vacuously. `sample_gaussian` is not on the
  signing path (offline uses `sample_gaussian_poly_bern`). Remove or fix + make
  the test non-vacuous.
- **C5 (MED, area/embedded):** large stack VLAs, e.g. `sample_gaussian_poly_bern`
  `buf[(3*8+2)*1664] = 43264 B` plus `coeffs[2n]`; `short_preimage` four
  `fpr[n]`; `do_sign_dyn_lazy` `res1[n]/res2[n]/y2tmp[n]`; `Zf(sign_dyn_lazy)`
  and the offline/online wrappers stack the FFT basis and samples. Fine on host,
  a stack-overflow risk on the M7. Route through caller-provided `tmp`.
- **C6 (LOW, hygiene):** dead vars/params under `-Wall -Wextra -Wshadow`
  (`muinc` "TODO check if really unused?", `isigma/mu`, unused `tmp` params,
  `int loop`); blocks a `-Werror` build.
- **C7 (LOW, embedded harness):** `main.cpp` times the combined
  `falcon_sign_dyn_lazy` (offline+online) as "lazy signing", so it does not
  isolate the online cost; also uses the fixed-seed `randombytes` and
  double-includes two `falcon.h` with clashing symbols. (No board this pass, so
  not measured; corrected for fidelity.)
- **C8 (INFO, hygiene):** committed build artifacts (`falcon-lazy/*.o`,
  `falcon-lazy2/build/`, `.DS_Store`) and a missing `falcon-lazy2/dilithium`
  symlink (the bench cannot build until it is created; `resync.sh` only links
  `*.c/*.h`).
- **Dropped C3:** the squared-norm accumulation `sqn += (uint32_t)(z*z);
  ng |= sqn; sqn |= -(ng>>31)` is Falcon's standard overflow-safe saturation,
  not a bug.

Dead code (0 references anywhere): `v_add/v_sub/v_mul/v_neg/v_inv/v_round/
v_scalar_mul/mat_mul`, `gauss_sampler`, `calc_norm`, `randint`, `rand32`.
Removing reduces code size and warning surface (area).

## Corrected baseline after C2 (real CSPRNG in the sampler)

Fixing C2 (sampler now draws from the SHAKE-seeded PRNG instead of the toy
fixed-seed Fibonacci generator) raises the **offline** cost, because a real
CSPRNG costs more per byte than the toy LCG. This is the honest baseline for the
optimisation deltas below. Everything else is unchanged; C4 stays 100%.

| op | logn=9 toy RNG | logn=9 post-C2 | logn=10 toy RNG | logn=10 post-C2 |
| --- | --- | --- | --- | --- |
| sign_offline | 308,160 | 477,518 | 598,326 | 977,064 |

## Optimisation results (AFTER)

invariant-TSC ticks, median. "scalar final" = C1+C2+I2+I4 (portable, the
M7-relevant path). "AVX2 final" additionally enables `FALCON_AVX2`+`FALCON_FMA`
(I0, x86 only). C4 = 100% and all unit tests pass in every column.

logn=9 (n=512):

| op | post-C2 scalar (base) | scalar final | AVX2 final | AVX2 vs base |
| --- | --- | --- | --- | --- |
| keygen | 20,836,316 | 21,856,318 | 20,482,776 | ~flat |
| sign_offline | 477,518 | 481,160 | 241,884 | **-49%** |
| sign_online | 69,728 | 69,760 | 64,040 | -8% |
| sign_dyn_orig | 654,948 | 675,310 | 476,762 | -27% |
| verify | 27,490 | 27,762 | 27,518 | ~flat |

logn=10 (n=1024):

| op | post-C2 scalar (base) | scalar final | AVX2 final | AVX2 vs base |
| --- | --- | --- | --- | --- |
| sign_offline | 977,064 | 967,542 | 496,280 | **-49%** |
| sign_online | 142,484 | 142,506 | 130,388 | -8% |
| sign_dyn_orig | 1,317,112 | 1,323,480 | 969,776 | -26% |
| verify | 56,536 | 56,560 | 56,844 | ~flat |

Online stays ~7.4x (AVX2) to ~9.4x faster than the reference `sign_dyn`.

Levers:
- **I0 (AVX2/FMA, host only):** `FALCON_AVX2` was OFF (config.h autodetect does
  not key off `-mavx2`; must set `-DFALCON_AVX2=1 -DFALCON_FMA=1`). Enabling it:
  offline -44% (on top of I2), sign_dyn -26/27%. Online only -8% because it is
  transform-count-bound, not butterfly-throughput-bound (profile: iFFT 34%,
  FFT 26%, short_preimage glue 29%). Does NOT apply to the M7.
- **I2 (offline sampler):** draw straight from the SHAKE PRNG instead of filling
  a 43 KB buffer. Offline -9% on AVX2, perf-neutral scalar, and a large area win
  (below). Profile before: prng_refill 22% + prng_fill 11% + sampler 20% +
  exp_scaled 19%.
- **I4 (dead code):** removed unused `v_*`, `mat_mul`, `gauss_sampler`,
  `calc_norm`, `randint`, `rand32`, `randombytes`. Code-size win (below).
- **I1/I3 (online transforms): assessed, not changed.** `short_preimage` runs
  the near-minimal 7 FFT/iFFT for a two-poly Babai nearest-plane (the rounding
  must happen in the coefficient domain, forcing an iFFT/FFT round-trip). Cutting
  below 7 is possible only with risky merging for little gain; left as-is.

## Area (host proxies)

- Code size: `sign.o` text 40,274 -> 36,469 B (**-9.4%**, I4);
  `libfalcon.a` text 257,018 -> 253,610 B.
- Stack (`-fstack-usage`): `sample_gaussian_poly_bern` **~48 KB VLA -> 128 B
  static** (I2). Remaining dynamic frames (VLAs, an embedded concern):
  `short_preimage` ~16 KB (n=512) / 32 KB (n=1024), `do_sign_dyn_lazy` ~6/12 KB,
  `sign_dyn_lazy_offline` ~10/20 KB. See C5 remaining.

## Cortex-M4 results (STM32F407, the paper-relevant embedded target)

Measured via pqm4 at `CLOCK_FAST` (168 MHz; counts include flash wait states,
same convention as the SDitH M4 tracker). M4 has a single-precision FPU, so
Falcon's doubles use `FPEMU` plus the auto-enabled `FALCON_ASM_CORTEXM4`
assembly. Harness + how-to: `falcon-lazy2/m4/`. Falcon-512, real hardware:

| op | cycles | stack (RAM) |
| --- | --- | --- |
| keygen | ~285M (variable 150M-311M) | ~1.1 KB (+15.9 KB tmp) |
| sign_offline | 7.24M | 2.2 KB |
| sign_online | 10.52M | 20.8 KB |
| sign_dyn (reference full signer) | 39.6M | (40 KB tmp) |
| verify | 174K | small |

Flash: ~107 KB (.bin); signatures verify (`verify_ok=1`).

Headline M4 insight (that x86 hid): **the online phase (10.5M) is more expensive
than the offline (7.2M)** here, the reverse of x86, because the online
`short_preimage` is FP-FFT-bound and the M4 emulates doubles. The online/offline
split still helps (online is 3.8x cheaper than the full signer's 39.6M) but the
online is not cheap in absolute terms (~63 ms at 168 MHz). The clear M4 lever is
to cut/replace the online FP FFT (fixed-point or fewer transforms). The board
also surfaced C5 and C9 (below) as real, not theoretical.

## Fuzzing (Phase 2)

Three libFuzzer harnesses under `falcon-lazy2/fuzz/` (clang,
`-fsanitize=fuzzer,address,undefined`), each ~45s with a seed corpus:

| harness | surface | execs | result |
| --- | --- | --- | --- |
| fuzz_verify | falcon_verify, all 3 sig decoders | 420,812 | no crash |
| fuzz_pubkey | public-key decode (modq_decode) | 1,268,464 | no crash |
| fuzz_comp_decode | comp/modq/trim decoders directly | 965,391 | no crash |

The attacker-facing decoders (stock `vrfy.c`/`codec.c`) are robust to malformed
input. Build/run: `sh fuzz/build.sh` then `./fuzz_verify corpus_verify -max_total_time=...`.

## C9 (MED): the lazy signer has no norm-retry

`do_sign_dyn_lazy` returns 1 when the signature is within Falcon's norm bound
and 0 when it is not, but in the 0 case it **still writes the (invalid)
signature** and returns 0. The benchmarks and `main.cpp` ignore the return, so
an over-bound (invalid) signature can escape. Measured rate with a fresh
one-time offline token and uniform messages: **0 / 100,000** at both sizes
(< 1e-5). But reusing a fixed offline sample across messages can exceed the
bound (observed L2 norm 6094.9 > real bound 5833.9), which is what made the
`rand()`-seeded `lazy_sig` unit test flaky. The M4 bring-up then hit it for real:
the standard pqm4 `stack` harness failed with "Signature did not verify
correctly" because the public `falcon_sign_dyn_lazy` emitted an over-bound sig.
Fixes applied: (1) the `lazy_sig` test regenerates the one-time offline token on
an over-bound signature and is order-independent; (2) the public
`Zf(sign_dyn_lazy)` now retries the one-time offline token (bounded at 128
attempts) instead of emitting an invalid signature.

## Constant-time diagnosis (Phase 4, report only, no hardening)

ctgrind-style check (`falcon-lazy2/ct/ct_check.c`, Valgrind memcheck with the
secret key and offline samples marked uninitialised):

- 3 secret-dependent conditional branches, all in `do_sign_dyn_lazy`, in the
  norm-acceptance region. This mirrors reference Falcon's rejection-on-norm; the
  accept/reject outcome is observable regardless, so it is generally deemed
  acceptable.
- 0 secret-indexed memory reads. The FFT / `short_preimage` path is
  data-oblivious (no branches on secret-derived values).
- Caveat (config.h): native `double` div/sqrt are not fully constant-time; this
  affects keygen/expand, not the lazy online path, and is negligible per Falcon.

## Remaining / recommendations

- **C5 (remaining VLAs, embedded) - now quantified on M4:** the online path
  uses 20.8 KB of stack (measured on the F407), and the combined public
  `crypto_sign` path additionally stacks the FFT basis + samples (~19 KB) plus a
  ~40 KB `tmp`, which strains the default 112 KB M4 RAM alongside `.bss`. Route
  `short_preimage`, `do_sign_dyn_lazy` and the sign wrappers' working buffers
  through the caller-provided `tmp` (already threaded, currently
  `__attribute((unused))`) for robust embedded use.
- **C6:** the offline/online wrappers still carry unused `isigma/mu/muinc`; drop
  them to build clean under `-Werror` (Release flags retain the warnings).
- **C7 (embedded `main.cpp`), not edited here (no mbed toolchain/board):** (a)
  time offline once + online in the loop instead of the combined
  `falcon_sign_dyn_lazy`; (b) replace the fixed-seed `randombytes`; (c) drop the
  double `falcon.h` include. Apply and flash on-device.
- **Embedded FP backend:** the `Makefile` builds `-DFALCON_FPEMU` with
  `-mfloat-abi=softfp` on an M7 that has a double-precision FPU (`fpv5-d16`,
  `+fp.dp`). `FALCON_FPNATIVE` + `-mfloat-abi=hard` would speed keygen/offline
  substantially, at the cost of FP constant-timeness. Your call.
- **Repo:** `git rm -r --cached` the 155 committed build artifacts (now covered
  by `.gitignore`); commit `resync.sh` which now also creates the `dilithium`
  symlink.

## Changelog

- (baseline) added `bench_cycles.cpp` rdtscp harness; fixed CMake `-O9`->`-O3`
  and `-Wl-no-undefined`->`-Wl,--no-undefined`; created `falcon-lazy2/dilithium`
  symlink so the bench builds.
- C1: removed the `res[i]=0` masking stub in `sample_gaussian()`.
- C2: `sample_gaussian_poly_bern()` takes a `prng*` and draws from the caller's
  SHAKE-seeded PRNG; fixed-seed Fibonacci path removed from the signing flow.
- C4: added permanent gtest `lazy_verify_realbound` (verify_raw at logn 9/10).
- C8: expanded `.gitignore`; `resync.sh` now idempotent and creates `dilithium`.
- I2: sampler draws directly from the PRNG; removed the 43 KB `buf` VLA and the
  `coeffs[]` VLA.
- I4: removed dead helper functions (code size -9.4%).
- C9: `lazy_sig` test now retries the one-time offline token on an over-bound
  signature and is order-independent.
- Phase 2: added `fuzz/` (libFuzzer verify/pubkey/comp_decode), no crashes.
- Phase 4: added `ct/ct_check.c` ctgrind diagnosis.
- C9 (public API): `Zf(sign_dyn_lazy)` now retries the one-time offline token
  (bounded at 128) instead of emitting an over-bound invalid signature. Surfaced
  by the M4 pqm4 stack harness.
- M4: added `falcon-lazy2/m4/` (pqm4 scheme + `speedoo.c` split harness); real
  STM32F407 cycles/flash/RAM captured (see the Cortex-M4 section).

## C10 (HIGH): the public online/offline signing API is incomplete

`falcon_sign_dyn_lazy` never produces a verifiable signature. In
`falcon_sign_dyn_lazy_finish` (falcon.c ~652) the code calls `Zf(sign_dyn_lazy)`
into `sv` then `return 0` WITHOUT encoding `sv` into the output `sig` buffer (no
nonce written, no CT/compressed encode, no `*sig_len`), unlike the working
`falcon_sign_dyn_finish`. The `dummy1/dummy2/dummy3 = ... "to get rid of
warnings"` stubs confirm the encoding was never implemented. Measured:
`falcon_sign_dyn_lazy` + `falcon_verify` = 0/200 verify-pass on x86 AND on the
M4 (sign returns success, verify always fails). It was never caught because all
tests and benchmarks (this review's C4 and the paper's `bench_lazy_falcon.cpp`)
use the SPLIT inner API (`sign_dyn_lazy_offline`/`_online`) + `verify_raw`, which
is correct (C4 = 100%). So the scheme is algorithmically sound but the packaged
public API is unfinished. Fix: encode `sv` (nonce + CT/compressed) in the lazy
finish, mirroring `falcon_sign_dyn_finish`. NOT yet fixed.

## Comparison vs other pqm4 schemes (STM32F407 @ 168 MHz, NIST level 1)

Cycles, standard pqm4 `speed` harness (except lazy = validated split numbers):

| scheme | keygen | sign | verify | pk / sig |
| --- | --- | --- | --- | --- |
| Falcon-OO-512 (lazy) | ~285M | offline 7.24M + online 10.52M (=17.8M) | 174K* | 897 / 809 |
| FN-DSA-512 (opt Falcon) | 89M | 24.0M | 411K | 897 / 666 |
| ML-DSA-44 (Dilithium2) | 1.81M | 5.22M | 1.80M | 1312 / 2420 |
| SDitH cat1-fast/short | build failed on F407 (`-Werror`; needs ccmstack ld) | | | 70 / 4468, 3689 |

*lazy verify = `verify_raw` (core check); others include decode+hash.

Takeaway: lazy combined (17.8M) beats optimized FN-DSA sign (24M), but ML-DSA-44
sign (5.22M) beats lazy online (10.52M) because the M4 has only a
single-precision FPU, so lazy Falcon's online FP-FFT is software-emulated while
Dilithium is integer-only. Falcon wins on key/signature size and verify cost.

## General-C pass and where the offline cost actually is (2026-08-19)

A profile-first sweep for plain-C wins that also carry to the M4/M7/rpi3 (no
AVX2). Scalar `build2` (`-O3`), invariant-TSC ticks, C4 stayed 100% (2000/2000)
and all 11 unit tests green throughout.

**Clean-ups (correctness- and perf-neutral, verified):**
- **Deduplicated the offline path.** `Zf(sign_dyn_lazy)` had a full copy-paste of
  `sign_dyn_lazy_offline` (basis FFT + `to_ntt_monty` + sampler + target). The
  combined signer now just calls the split helper (and re-calls it on the
  over-bound retry, which is ~never taken). Removed the fragile hand-kept
  `sc`/`h_monty` duplication.
- **Removed dead code:** never-called `mq_add_sign`/`mq_sub_sign`, the commented
  `align_u16`/`v_round`/`make_matrix`/`mat_mul`/`fpr_print`/`mq_poly_sub2`
  scaffolding, and the stale `mu`/`isigma`/`muinc` sampler leftovers. With the
  earlier I4 removal this is -369/+92 lines in `sign.c`.

**Measured a micro-opt that made things worse, and reverted it.** Rounding the
Babai fractional part in place (dropping the `y1_temp`/`y2_temp` round buffers in
`short_preimage`) was a consistent **+3.6% online** regression: the original
two-buffer loop keeps the two coordinate streams independent so the compiler
auto-vectorises the round + subtract. The "naive" version was the fast one; kept
it and documented why.

**Definitive offline profile (offline-only driver, keygen excluded):**

| symbol | % offline | nature |
| --- | --- | --- |
| `prng_refill` (ChaCha20) | 55.6% | sampler randomness |
| `sample_gaussian_poly_bern` | 14.4% | sampler (CDT + accept) |
| `exp_scaled` | 12.2% | sampler acceptance test |
| `mq_NTT` + `FFT` + `mq_iNTT` | ~16% | stock transforms |
| everything else | <2% | — |

So **~82% of the offline is the custom Bernoulli/CDT Gaussian sampler**, over
half of it just generating ChaCha bytes (it draws ~26 bytes per trial: two u64
for the 128-bit CDT, one u64 for the Bernoulli, two bytes). The stock FFT/NTT
(~16%) is already tuned; the 4 per-key FFTs are only ~6%, so amortising them into
a one-time key-expand step saves <6% and isn't worth the API change.

**Conclusion — the two real levers, neither a plain-C micro-opt:**
1. **Offline: the sampler design/precision.** This is the only place with real
   offline cycles, and every part of it (CDT bit-width, `exp_scaled` constants,
   bytes per trial) is distribution- and security-defining. Any change alters the
   output distribution / KAT, so it needs your (and the co-authors') sign-off, not
   a unilateral edit. Options if we go there: lower CDT precision, fewer
   randomness bytes per trial, or a cheaper base sampler, each measured against a
   Rényi/statistical-quality bound.
2. **Online: `FALCON_FPNATIVE` on a hardware double-FPU target (M7 / rpi3).** The
   online step is transform-bound and near-minimal in C; on the M4 it is software
   double-emulation. Native hardware doubles are the large, safe win, and this is
   the next step.
