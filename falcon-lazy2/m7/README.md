# Cortex-M7 (pqm4) benchmarking of the online/offline Falcon

Real embedded cycles for the online/offline Falcon on an STM32F767ZI
(NUCLEO-F767ZI, Cortex-M7, chipid 0x451). The M7 has a **double-precision FPU**
(`fpv5-d16`), so unlike the M4 it can run Falcon's `double` maths in hardware
(`FALCON_FPNATIVE`). That is the paper's "FPU" path, and the M7 reproduces the
paper's Cortex-A53 FPU numbers closely. Measured via pqm4 at `CLOCK_FAST`
(216 MHz off HSI), cycles via SysTick, L1 I/D caches enabled.

Two independent levers are exposed here:

1. **FP backend.** `config.h` selects `FALCON_FPNATIVE` (hardware double) vs
   `FALCON_FPEMU` (software emulation). On the M7 Falcon would otherwise
   autodetect the Cortex-M4 assembly path and emulate, so the backend is set
   explicitly. This is the before/after in the results below.
2. **L1 caches.** The F767 branch of the HAL turns on the core I and D caches
   (the F407 M4 has none). This alone was worth roughly 2.4x to 4x on the
   FP-heavy phases during bring-up.

## Files here

- `nucleo-f767zi.mk` (drop into `pqm4/mk/`): the platform definition. It forces
  the double-precision FPU because libopencm3's `genlink` reports the
  single-precision `fpv5-sp-d16` for every F7 (see gotchas).
- `config.h` (the scheme's `crypto_sign/falcon-oo-512/ref/config.h`): the
  `FPNATIVE` vs `FPEMU` selector at the top.
- `speedoo.c` (drop into `pqm4/mupq/crypto_sign/`): the split harness, now
  `OO_ITER=100` repetitions with fresh randomness per iteration, and a
  `STACK_MEAS` guard (per-op stack measurement hangs on the F767 split RAM).
- `hal-opencm3.f767.patch`: the STM32F767ZI branch for `pqm4/common/hal-opencm3.c`
  (device select on USART3, HSI 216 MHz clock, direct `USART_BRR`, RNG, and the
  L1 I/D cache enable).
- `randombytes.f767.patch`: one line adding `STM32F7` to the hardware-RNG guard
  in `pqm4/common/randombytes.c`, so each repetition draws real entropy.

## Reproduce (with a pqm4 checkout)

```
cd pqm4
git apply .../falcon-lazy2/m7/hal-opencm3.f767.patch
git apply .../falcon-lazy2/m7/randombytes.f767.patch
cp .../falcon-lazy2/m7/nucleo-f767zi.mk mk/
cp .../falcon-lazy2/m7/speedoo.c        mupq/crypto_sign/

# scheme = falcon-lazy sources + the m4 pqm4 glue + this config.h
mkdir -p crypto_sign/falcon-oo-512/ref
cp .../falcon-lazy/{codec,common,falcon,fft,fpr,keygen,rng,shake,sign,vrfy}.c \
   .../falcon-lazy/{falcon,inner,fpr}.h \
   .../falcon-lazy2/m4/{api.h,pqm4_sign.c} \
   crypto_sign/falcon-oo-512/ref/
cp .../falcon-lazy2/m7/config.h crypto_sign/falcon-oo-512/ref/

make PLATFORM=nucleo-f767zi bin/crypto_sign_falcon-oo-512_ref_speedoo.bin
st-flash --reset write bin/crypto_sign_falcon-oo-512_ref_speedoo.bin 0x8000000
# read the ST-Link VCP (USART3) at 115200
```

Make does not track `config.h`, so after flipping `FPNATIVE`/`FPEMU` delete the
scheme objects (`find obj -path '*falcon-oo-512*' -delete`) before rebuilding.

Comparison schemes use the stock pqm4 speed harness, e.g.
`make MUPQ_ITERATIONS=100 PLATFORM=nucleo-f767zi bin/crypto_sign_ml-dsa-44_m4f_speed.bin`.

## Results (STM32F767ZI @ 216 MHz, Falcon-512, median of 100, caches on)

| op | EMU (before) | FPNATIVE (after) | speedup |
| --- | --- | --- | --- |
| keygen | 103M | 66M | 1.6x |
| sign_offline | 5.00M | 1.35M | 3.7x |
| sign_online | 7.93M | 1.14M | 7.0x |
| sign_dyn (reference full signer) | 28.7M | 3.91M | 7.4x |
| verify | 0.157M | 0.157M | 1.0x (integer, FP-independent) |

Signatures verify (`verify_ok=100/100`) in both backends.

Key points. With the hardware double FPU the **online phase is 1.14M cycles**,
which matches the paper's Cortex-A53 FPU online (about 1.12M) to within 2%, and
verify (157K) matches too. Online is 3.4x cheaper than the reference full signer
(the paper's ~3.6x claim), and 9x cheaper than the M4's emulated online (10.5M).
See the repo-root `REVIEW.md` "Cross-platform performance" section for the full
comparison against ML-DSA-44 and FN-DSA-512, and against the x86 box and the Mac.

## Gotchas

- libopencm3 `genlink` reports `fpv5-sp-d16` (single precision) for all F7. The
  `.mk` rewrites it to `fpv5-d16`. Verify with `arm-none-eabi-objdump`: the
  FPNATIVE ELF has 200+ hardware `.f64` instructions, the EMU ELF has none.
- `usart_set_baudrate` mis-derives the F7 USART3 clock. The HAL sets
  `USART_BRR = rcc_apb1_frequency / baud` directly.
- HSE routing on the Nucleo is solder-bridge dependent, so the clock comes up on
  HSI (`rcc_clock_setup_hsi`, 216 MHz), not HSE.
- `hal_checkstack` hangs on the F767 split RAM map, hence `STACK_MEAS=0`.
- The cache-enable register is `SCB_CCSELR` (not `CSSELR`) and needs
  `#include <libopencm3/cm3/scb.h>`.
