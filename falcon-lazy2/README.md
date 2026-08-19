# falcon-lazy2: host build, benchmarks, tests, fuzzing

Host (x86-64/Linux) harness for the online/offline Falcon in `../falcon-lazy`.
The `*.c`/`*.h` here are symlinks into `../falcon-lazy`; `resync.sh` recreates
them plus the `dilithium` and `ed25519` symlinks the build needs.

## Build

Needs cmake, gtest, google-benchmark. The `dilithium`/`ed25519` submodules must
be checked out at the repo root.

```
sh resync.sh                 # ensure symlinks (incl. dilithium)
mkdir build && cd build
cmake .. -DCMAKE_C_FLAGS="-O3" -DCMAKE_CXX_FLAGS="-O3"
make -j
```

For the AVX2 FFT/sampler (x86 only) add `-mavx2 -mfma -DFALCON_AVX2=1
-DFALCON_FMA=1` to both flag variables. `FALCON_AVX2` is not auto-enabled by
`-mavx2` alone.

## Targets

- `unittest` — googletest correctness (keygen algebra, sampler, `short_preimage`,
  and `lazy_verify_realbound`: lazy signatures must pass Falcon's real
  `verify_raw` bound).
- `bench_cycles` — standalone rdtscp cycle + correctness harness (no PMU / no
  google-benchmark needed). `taskset -c 1 ./bench_cycles [logn] [--trials=N]`.
- `falcon_bench` — google-benchmark comparison (offline/online/orig, ed25519,
  Dilithium ref/avx2).

## Fuzzing

`sh fuzz/build.sh` (clang) builds libFuzzer harnesses for the attacker-facing
decoders: `fuzz_verify`, `fuzz_pubkey`, `fuzz_comp_decode` (ASan+UBSan). Run e.g.
`taskset -c 1 ./fuzz/fuzz_verify fuzz/corpus_verify -max_total_time=60`.

## Constant-time diagnosis

`ct/ct_check.c` is a Valgrind/ctgrind harness that marks the secret key and
offline samples as secret and reports secret-dependent branches. See the repo
root `REVIEW.md` for results and the full review (fixes, before/after cycles and
area, fuzzing, CT).
