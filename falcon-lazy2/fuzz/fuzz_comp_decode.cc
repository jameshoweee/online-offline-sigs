/*
 * libFuzzer harness: the low-level decoders directly on attacker bytes.
 * comp_decode (Golomb-Rice-ish signature payload), modq_decode (public key)
 * and trim_i16_decode. These are the parsers most exposed to malformed input.
 * Build with fuzz/build.sh.
 */
#include <stddef.h>
#include <stdint.h>

extern "C" {
#define restrict
#include "inner.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    for (unsigned logn = 9; logn <= 10; ++logn) {
        size_t n = (size_t)1 << logn;
        int16_t xs[1024];
        uint16_t xu[1024];
        (void)n;
        (void)falcon_inner_comp_decode(xs, logn, data, size);
        (void)falcon_inner_modq_decode(xu, logn, data, size);
        (void)falcon_inner_trim_i16_decode(xs, logn, falcon_inner_max_sig_bits[logn], data, size);
    }
    return 0;
}
