# Post-Quantum Online/Offline Signatures

This repository contains the implementation made for the [Post-Quantum Online/Offline Signatures](https://eprint.iacr.org/2025/) paper by Martin R. Albrecht, Nicolas Gama, James Howe, and Anand Kumar
Narayanan. This is a proof-of-concept implementation and is not optimised.

The host build, benchmarks, tests, fuzzing and constant-time tooling live under
`falcon-lazy2/` (see `falcon-lazy2/README.md`). A review with fixes, before/after
cycle and area numbers, fuzzing and a constant-time diagnosis is in `REVIEW.md`.

## Benchmarks

Cross-platform, every operation repeated with fresh randomness per repetition
(1000 reps on the hosts, 100 on the M7); every signature verifies. Units differ
per platform, so read within a table.

### Apple Silicon (arm64, hardware double FPU), 1000 reps, median us

| scheme / op | keygen | sign | verify |
| --- | --- | --- | --- |
| Falcon online/offline: offline | 3064 | 79.1 | 3.5 |
| Falcon online/offline: online  |      | **7.4** | |
| Falcon online/offline: full (sign_dyn) | | 118 | |
| ed25519 | 12.2 | 12.5 | 38.2 |
| Dilithium2 / ML-DSA-44 | 35.2 | 126 | 39.1 |

### x86-64 (Xeon 8259CL), 1000 reps, median cycles, scalar -> AVX2

| scheme / op | keygen | sign | verify |
| --- | --- | --- | --- |
| Falcon offline | 20.4M | 447,688 -> 251,268 | 33,918 |
| Falcon online  |       | 74,754 -> **70,602** | |
| Falcon full (sign_dyn) | | 676,122 -> 496,824 | |
| ed25519 | 97,792 | 100,726 | 330,172 |
| Dilithium2 (ref) | 237,384 | 862,164 | 265,128 |
| Dilithium2 (AVX2) | 84,720 | 192,250 | 87,862 |

### Cortex-M7 (STM32F767 @ 216 MHz), 100 reps, median cycles, FPEMU -> FPNATIVE

| scheme / op | keygen | sign | verify |
| --- | --- | --- | --- |
| Falcon offline | 103M -> 66M | 5.00M -> 1.35M | 0.157M |
| Falcon online  |             | 7.93M -> **1.14M** | |
| Falcon full (sign_dyn) |     | 28.7M -> 3.91M | |
| ML-DSA-44 (m4f) | 1.05M | 2.32M | 1.05M |
| FN-DSA-512 (m4f) | 47.7M | 19.3M | 0.313M |

The online step is the cheapest signing operation on every platform measured:
7.4 us on Apple Silicon, ~71K cycles on x86, and 1.14M cycles on the M7, where it
reproduces the paper's Cortex-A53 FPU result.

# Disclaimer
The software and documentation are provided "as is" and SandboxAQ hereby disclaims all warranties, whether express, implied, statutory, or otherwise. SandboxAQ specifically disclaims, without limitation, all implied warranties of merchantability, fitness for a particular purpose, title, and non-infringement, and all warranties arising from course of dealing, usage, or trade practice. SandboxAQ makes no warranty of any kind that the software and documentation, or any products or results of the use thereof, will meet any person's requirements, operate without interruption, achieve any intended result, be compatible or work with any software, system or other services, or be secure, accurate, complete, free of harmful code, or error free.
