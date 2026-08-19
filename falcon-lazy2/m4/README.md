# Cortex-M4 (pqm4) benchmarking of the online/offline Falcon

Real embedded cycles + flash + RAM for the online/offline Falcon on an
STM32F407 (Cortex-M4, single-precision FPU, so Falcon's doubles use `FPEMU`
plus the auto-enabled `FALCON_ASM_CORTEXM4` assembly). Measured via pqm4 at
`CLOCK_FAST` (168 MHz, cycle counts include flash wait states, same convention
as the SDitH M4 tracker).

## Files here

- `api.h`, `pqm4_sign.c` — a pqm4 `crypto_sign` scheme wrapping the public
  online/offline API (keypair = Falcon keygen; sign = `falcon_sign_dyn_lazy`
  offline+online; open = `falcon_verify`). Enables the standard pqm4
  `speed`/`stack`/`test` harnesses.
- `speedoo.c` — a custom pqm4 harness that times keygen, sign_offline,
  sign_online, sign_dyn (reference full signer) and verify SEPARATELY (the
  offline/online split), and reports per-op stack via `hal_spraystack` /
  `hal_checkstack`.

## Reproduce (with a pqm4 checkout)

```
# scheme = falcon-lazy sources + the pqm4 glue
mkdir -p pqm4/crypto_sign/falcon-oo-512/ref
cp falcon-lazy/{codec,common,falcon,fft,fpr,keygen,rng,shake,sign,vrfy}.c \
   falcon-lazy/{falcon,inner,fpr,config}.h \
   falcon-lazy2/m4/{api.h,pqm4_sign.c} \
   pqm4/crypto_sign/falcon-oo-512/ref/
cp falcon-lazy2/m4/speedoo.c pqm4/mupq/crypto_sign/

cd pqm4
make PLATFORM=stm32f4discovery bin/crypto_sign_falcon-oo-512_ref_speedoo.bin
st-flash --reset write bin/crypto_sign_falcon-oo-512_ref_speedoo.bin 0x8000000
# read USART2 (PA2/PA3) at 115200, e.g. on a USB-serial adapter
```

## Results (STM32F407 @ 168 MHz, Falcon-512)

| op | cycles | stack (RAM) |
| --- | --- | --- |
| keygen | ~285M (variable 150M-311M) | ~1.1 KB (+15.9 KB tmp) |
| sign_offline | 7.24M | 2.2 KB |
| sign_online | 10.52M | 20.8 KB |
| sign_dyn (reference full signer) | 39.6M | (40 KB tmp) |
| verify | 174K | small |

Flash: ~107 KB (.bin). Signatures verify (`verify_ok=1`).

Key point: on M4 the **online phase (10.5M) is more expensive than the offline
(7.2M)** — the reverse of x86 — because the online `short_preimage` is
FP-FFT-bound and the M4 emulates doubles. The split still helps (online is 3.8x
cheaper than the full signer, 39.6M) but the online is not cheap in absolute
terms (~63 ms). See the repo-root `REVIEW.md` for the full write-up, and the
C5 (VLA stack) and C9 (no-retry) findings surfaced by this board bring-up.
