cmake_minimum_required(VERSION 3.10)
project(falcon-lazy2)

enable_language(C ASM)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_C_FLAGS_DEBUG "-Wall -Wextra -Wshadow -Wundef -Werror -g3 -O0")
set (CMAKE_CXX_FLAGS_DEBUG "-Wall -Wextra -Wshadow -Wundef -Werror -g3 -O0")
set (CMAKE_C_FLAGS_RELEASE "-Wall -Wextra -Wshadow -Wundef -g3 -O3")
set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Wextra -Wshadow -Wundef -g3 -O3")
# The Apple (ld64) linker does not understand GNU ld's --no-undefined; keep the
# strict-link check only on GNU/Linux (the box).
if (APPLE)
    set (CMAKE_EXE_LINKER_FLAGS_DEBUG "")
    set (CMAKE_EXE_LINKER_FLAGS_RELEASE "")
else ()
    set (CMAKE_EXE_LINKER_FLAGS_DEBUG "-Wl,--no-undefined")
    set (CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,--no-undefined")
endif ()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    set(X86 ON)
else ()
    set(X86 OFF)
endif ()
message(STATUS "X86 architecture: ${X86}")


set(TEST_INCS)
set(BENCHMARK_LIBS)
set(UTESTS_LIBS)

find_library(gtest NAMES gtest)
find_library(gtest_main NAMES gtest_main)
find_path(gtest_incs gtest/gtest.h)
find_library(benchmark NAMES benchmark)
find_library(benchmark_main NAMES benchmark_main)
find_path(benchmark_incs benchmark/benchmark.h)
if (gtest AND gtest_main AND gtest_incs)
    message(STATUS "Found gtest: L=${gtest},${gtest_main} I=${gtest_incs}")
    set(UTESTS_LIBS ${UTESTS_LIBS} ${gtest} ${gtest_main})
    set(TEST_INCS ${TEST_INCS} ${gtest_incs})
else()
    message(FATAL_ERROR "ERROR: Cannot Find gtest: L=${gtest},${gtest_main} I=${gtest_incs}")
endif()
if (benchmark AND benchmark_main AND benchmark_incs)
    message(STATUS "Found benchmark: L=${benchmark},${benchmark_main} I=${benchmark_incs}")
    set(BENCHMARK_LIBS ${BENCHMARK_LIBS} ${benchmark} ${benchmark_main})
    set(TEST_INCS ${TEST_INCS} ${benchmark_incs})
else()
    message(FATAL_ERROR "ERROR: Cannot Find benchmark: L=${benchmark},${benchmark_main} I=${benchmark_incs}")
endif()


set (SRCS
        codec.c
        common.c
        config.h
        falcon.c
        falcon.h
        fft.c
        fpr.c
        fpr.h
        inner.h
        keygen.c
        rng.c
        sign.c
        #speed.c
        vrfy.c
        shake.c
)

set (TESTLIB_SRCS
        tests/testlib.cpp
        tests/testlib.h
)

set(ED25519_SRCS
        ed25519/src/key_exchange.c
        ed25519/src/sc.h
        ed25519/src/sc.c
        ed25519/src/sign.c
        ed25519/src/sha512.h
        ed25519/src/ge.c
        ed25519/src/verify.c
        ed25519/src/fe.c
        ed25519/src/ed25519.h
        ed25519/src/fixedint.h
        ed25519/src/precomp_data.h
        ed25519/src/keypair.c
        ed25519/src/seed.c
        ed25519/src/add_scalar.c
        ed25519/src/sha512.c
        ed25519/src/ge.h
        ed25519/src/fe.h
)

set(DILITHIUM_FIPS202_REF_SRCS
        dilithium/ref/sign.c
        dilithium/ref/packing.c
        dilithium/ref/polyvec.c
        dilithium/ref/poly.c
        dilithium/ref/ntt.c
        dilithium/ref/reduce.c
        dilithium/ref/rounding.c
        dilithium/ref/symmetric-shake.c
        dilithium/ref/fips202.c)

set(DILITHIUM_FIPS202_AVX_SRCS
        dilithium/avx2/consts.c
        dilithium/avx2/rounding.c
        dilithium/avx2/align.h
        dilithium/avx2/randombytes.h
        dilithium/avx2/fips202.c
        dilithium/avx2/rejsample.h
        dilithium/avx2/packing.c
        dilithium/avx2/sign.c
        dilithium/avx2/polyvec.h
        dilithium/avx2/shuffle.S
        dilithium/avx2/ntt.h
        dilithium/avx2/fips202.h
        dilithium/avx2/sign.h
        dilithium/avx2/shuffle.inc
        dilithium/avx2/aes256ctr.c
        dilithium/avx2/invntt.S
        dilithium/avx2/rejsample.c
        dilithium/avx2/consts.h
        dilithium/avx2/rounding.h
        dilithium/avx2/poly.h
        dilithium/avx2/pointwise.S
        dilithium/avx2/randombytes.c
        dilithium/avx2/api.h
        dilithium/avx2/params.h
        dilithium/avx2/symmetric.h
        dilithium/avx2/fips202x4.h
        dilithium/avx2/aes256ctr.h
        dilithium/avx2/config.h
        dilithium/avx2/f1600x4.S
        dilithium/avx2/fips202x4.c
        dilithium/avx2/symmetric-shake.c
        dilithium/avx2/packing.h
        dilithium/avx2/poly.c
        dilithium/avx2/ntt.S
        dilithium/avx2/polyvec.c
)

add_library(falcon STATIC ${SRCS})
target_link_libraries(falcon m)

add_library(ed25519 STATIC ${ED25519_SRCS})
target_include_directories(ed25519 INTERFACE ed25519/src)
target_compile_options(ed25519 PRIVATE -Wno-unused-result)

# On x86 the AVX2 lib supplies the randombytes() symbol; on other targets
# (e.g. Apple arm64) the AVX2 lib is not built, so pull it from the ref tree.
if (NOT X86)
    list(APPEND DILITHIUM_FIPS202_REF_SRCS dilithium/ref/randombytes.c)
endif ()

add_library(dilithium-ref STATIC ${DILITHIUM_FIPS202_REF_SRCS})
target_include_directories(dilithium-ref INTERFACE .)
target_include_directories(dilithium-ref PRIVATE dilithium/ref)
#target_compile_options(dilithium PRIVATE -Wno-unused-result)
set(DILITHIUM_LIBS dilithium-ref)

if (X86)
add_library(dilithium-avx STATIC ${DILITHIUM_FIPS202_AVX_SRCS})
target_include_directories(dilithium-avx INTERFACE .)
target_include_directories(dilithium-avx PRIVATE dilithium/avx2)
target_compile_options(dilithium-avx PRIVATE -mavx;-mavx2;-maes)
set(DILITHIUM_LIBS dilithium-ref dilithium-avx)
endif ()

add_library(falcon_testlib STATIC ${TESTLIB_SRCS})
target_link_libraries(falcon_testlib falcon)

add_executable(speed speed.c)
target_link_libraries(speed falcon m)

add_executable(test_falcon test_falcon.c)
target_link_libraries(test_falcon falcon m)
target_compile_options(test_falcon PRIVATE -Wno-unused)

add_executable(unittest tests/unittest.cpp)
target_link_libraries(unittest falcon falcon_testlib ${UTESTS_LIBS})
target_include_directories(unittest PRIVATE ${TEST_INCS})

add_executable(falcon_bench tests/bench_lazy_falcon.cpp)
target_link_libraries(falcon_bench falcon falcon_testlib  ed25519 ${DILITHIUM_LIBS} ${BENCHMARK_LIBS})
target_include_directories(falcon_bench PRIVATE ${TEST_INCS})

# Standalone rdtscp cycle + correctness harness (no google-benchmark / PMU needed).
add_executable(bench_cycles tests/bench_cycles.cpp)
target_link_libraries(bench_cycles falcon falcon_testlib m)

# Repetition harness: N independent reps (default 1000) with fresh randomness,
# times keygen/sign/verify for the online/offline Falcon + ed25519 + Dilithium2.
# Portable time everywhere; invariant-TSC cycles additionally on x86. No
# google-benchmark dependency, so it builds and runs on the Mac and the box.
add_executable(falcon_bench_reps tests/bench_reps.cpp)
target_link_libraries(falcon_bench_reps falcon falcon_testlib ed25519 ${DILITHIUM_LIBS} m)
target_include_directories(falcon_bench_reps PRIVATE ${TEST_INCS})

