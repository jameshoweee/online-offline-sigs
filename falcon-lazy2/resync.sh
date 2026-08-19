#!/bin/sh
# Recreate the convenience symlinks used by falcon-lazy2's CMake build.
#
# The falcon source symlinks (*.c/*.h -> ../falcon-lazy/) and ed25519 are
# committed, so a fresh clone already has them. The dilithium symlink is not
# committed and must be created here, otherwise the falcon_bench target fails
# to find dilithium/ref/*.c and dilithium/avx2/*.c. Idempotent (ln -sfn).
set -e
cd "$(dirname "$0")"
for f in ../falcon-lazy/*.c ../falcon-lazy/*.h; do
    ln -sfn "$f" "$(basename "$f")"
done
ln -sfn ../dilithium dilithium
ln -sfn ../ed25519 ed25519
