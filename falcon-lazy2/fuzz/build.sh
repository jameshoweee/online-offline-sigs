#!/bin/sh
# Build the libFuzzer harnesses for the online/offline Falcon.
# Requires clang. Harnesses: fuzz_verify, fuzz_pubkey, fuzz_comp_decode.
# Sanitizers: fuzzer + address + undefined.
set -e
cd "$(dirname "$0")"

CC=${CC:-clang}
CXX=${CXX:-clang++}
SAN="address,undefined"
CFLAGS="-g -O1 -fsanitize=fuzzer-no-link,$SAN -I.."
LDFLAGS="-g -O1 -fsanitize=fuzzer,$SAN -lm"
SRCS="codec common falcon fft fpr keygen rng shake sign vrfy"

rm -f ./*.o
OBJ=""
for c in $SRCS; do
    $CC $CFLAGS -c "../$c.c" -o "$c.o"
    OBJ="$OBJ $c.o"
done

for h in fuzz_verify fuzz_pubkey fuzz_comp_decode; do
    $CXX -std=c++17 $CFLAGS -c "$h.cc" -o "$h.o"
    # shellcheck disable=SC2086
    $CXX "$h.o" $OBJ $LDFLAGS -o "$h"
    echo "built $h"
done

# seed generator (plain C, no sanitizer needed) + corpora
mkdir -p corpus_verify corpus_pubkey corpus_comp
# shellcheck disable=SC2086
$CC -O2 -I.. gen_seeds.c $(for c in $SRCS; do echo ../$c.c; done) -lm -o gen_seeds
./gen_seeds
echo "done"
