#!/usr/bin/env bash
#
# mayhem/build.sh — libmdbx (amalgamated distribution: mdbx.c + mdbx.c++ + CLI tools).
# Builds, under /mayhem/mayhem/out (gitignored):
#   1) mdbx.c / mdbx.c++ with $SANITIZER_FLAGS + SanCov + DWARF-3, and the libFuzzer harnesses
#      in mayhem/harnesses/ (+ a -standalone reproducer for each);
#   2) the mdbx_load CLI with afl-clang-fast (mdbx_load exit()s on malformed dumps, so it is a
#      file-input AFL target driven by mayhem/harnesses/mdbx_load_driver.c);
#   3) the CLI tools (mdbx_load/dump/chk/stat/copy) with NORMAL flags for mayhem/test.sh.
# Binaries land in /mayhem (= $SRC). No network, no configure state: re-runnable offline.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OUT="$SRC/mayhem/out"
H="$SRC/mayhem/harnesses"
mkdir -p "$OUT/fuzz" "$OUT/afl" "$OUT/tests"

# Run commands in parallel; `waitall` fails if any of them failed (plain `wait` would not).
PIDS=()
bg() { "$@" & PIDS+=($!); }
waitall() { local p rc=0; for p in "${PIDS[@]}"; do wait "$p" || rc=1; done; PIDS=(); return $rc; }

DEFS=(-DMDBX_BUILD_FLAGS='"mayhem"' -I"$SRC")
# Upstream's own UBSan build switch (CMake ENABLE_UBSAN): makes mdbx-internals.h pick
# MDBX_UNALIGNED_OK=0, i.e. no deliberate unaligned word access that UBSan would flag on x86.
FUZZ_DEFS=("${DEFS[@]}" -DENABLE_UBSAN=1)
FUZZ_CFLAGS="-O1 $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS"

# 1) sanitized + SanCov library objects, then the libFuzzer harnesses.
bg $CC  $FUZZ_CFLAGS "${FUZZ_DEFS[@]}" -c mdbx.c -o "$OUT/fuzz/mdbx.o"
bg $CXX $FUZZ_CFLAGS "${FUZZ_DEFS[@]}" -DMDBX_BUILD_CXX=1 -std=c++20 -c mdbx.c++ -o "$OUT/fuzz/mdbxxx.o"
bg $CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OUT/fuzz/lsan_off.o"
bg $CC  $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/fuzz/standalone_main.o"
waitall

for h in fuzz_env_chk fuzz_env_read fuzz_env_write fuzz_env_copy; do
  $CC $FUZZ_CFLAGS "${FUZZ_DEFS[@]}" -c "$H/$h.c" -o "$OUT/fuzz/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz/$h.o" "$OUT/fuzz/mdbx.o" \
    "$OUT/fuzz/lsan_off.o" -lpthread -lm -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/fuzz/standalone_main.o" "$OUT/fuzz/$h.o" "$OUT/fuzz/mdbx.o" \
    "$OUT/fuzz/lsan_off.o" -lpthread -lm -o "$SRC/$h-standalone"
done

h=fuzz_cxx_codecs
$CXX $FUZZ_CFLAGS "${FUZZ_DEFS[@]}" -DMDBX_BUILD_CXX=1 -std=c++20 -c "$H/$h.cpp" -o "$OUT/fuzz/$h.o"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz/$h.o" "$OUT/fuzz/mdbxxx.o" "$OUT/fuzz/mdbx.o" \
  "$OUT/fuzz/lsan_off.o" -lpthread -lm -o "$SRC/$h"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/fuzz/standalone_main.o" "$OUT/fuzz/$h.o" "$OUT/fuzz/mdbxxx.o" \
  "$OUT/fuzz/mdbx.o" "$OUT/fuzz/lsan_off.o" -lpthread -lm -o "$SRC/$h-standalone"

# 2) mdbx_load under AFL instrumentation (its own reproducer, no -standalone).
export AFL_QUIET=1
AFL_CFLAGS="-O1 $SANITIZER_FLAGS $DEBUG_FLAGS"
bg afl-clang-fast $AFL_CFLAGS "${FUZZ_DEFS[@]}" -c mdbx.c -o "$OUT/afl/mdbx.o"
bg afl-clang-fast $AFL_CFLAGS "${FUZZ_DEFS[@]}" -Dmain=mdbx_load_main -c mdbx_load.c -o "$OUT/afl/mdbx_load.o"
bg afl-clang-fast $AFL_CFLAGS -c "$H/mdbx_load_driver.c" -o "$OUT/afl/driver.o"
bg afl-clang-fast++ $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OUT/afl/lsan_off.o"
waitall
afl-clang-fast++ $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/afl/driver.o" "$OUT/afl/mdbx_load.o" \
  "$OUT/afl/mdbx.o" "$OUT/afl/lsan_off.o" -lpthread -lm -o "$SRC/mdbx_load_afl"

# 3) CLI tools with the project's normal flags (functional oracle for test.sh).
$CC -O2 $COVERAGE_FLAGS "${DEFS[@]}" -c mdbx.c -o "$OUT/tests/mdbx.o"
for t in mdbx_load mdbx_dump mdbx_chk mdbx_stat mdbx_copy; do
  bg $CC -O2 $COVERAGE_FLAGS "${DEFS[@]}" "$t.c" "$OUT/tests/mdbx.o" -lpthread -lm -o "$OUT/tests/$t"
done
waitall
echo "build.sh: done"
