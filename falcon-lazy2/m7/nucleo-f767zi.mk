# SPDX-License-Identifier: Apache-2.0 or CC0-1.0
#
# STM32 NUCLEO-F767ZI (Cortex-M7, STM32F767ZI). Added to benchmark the
# online/offline Falcon on a core with a real DOUBLE-precision FPU, i.e. the
# "FPU (native double)" path of ePrint 2025/117. libopencm3's genlink reports
# the conservative single-precision fpv5-sp-d16 for the whole F7 family; the
# F767 actually has fpv5-d16, so we override the FPU flags below.
DEVICE=stm32f767zi
OPENCM3_TARGET=lib/stm32/f7

EXCLUDED_SCHEMES = \
	mupq/pqclean/crypto_kem/mceliece% \
	mupq/crypto_sign/tuov%

include mk/opencm3.mk

# Force the double-precision FPU. CFLAGS/LDFLAGS hold $(ARCH_FLAGS) by reference
# (recursively expanded), so reassigning it here propagates to every compile and
# link. Without this, doubles would be software-emulated even on the M7.
ARCH_FLAGS := $(patsubst -mfpu=fpv5-sp-d16,-mfpu=fpv5-d16,$(ARCH_FLAGS))
