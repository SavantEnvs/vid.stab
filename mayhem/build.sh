#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# vid.stab is a small dependency-free C99 library. Its CMake build adds the SIMD kernels
# (AVX2/AVX-512) that src/cpudetect.c picks at RUNTIME by host CPU, which would make a fuzz
# run depend on the machine it lands on. So the fuzz build compiles the library's own source
# list (CMakeLists.txt SOURCES + the built-in LP solver) in its portable scalar configuration,
# with the sanitizers and DWARF-3, into build/fuzz (gitignored by upstream's `build` rule).
FZ="$SRC/build/fuzz"
mkdir -p "$FZ/obj" "$FZ/inc" "$FZ/bin"
sed 's/@MAJOR_VERSION@/1/g;s/@MINOR_VERSION@/4/g;s/@PATCH_VERSION@/0/g' \
  src/libvidstab_version.h.in > "$FZ/inc/libvidstab_version.h"
FUZZ_CFLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -O1 -std=gnu99 -Wno-pointer-sign -DUSE_IPM -DVS_HAVE_LPSOLVER -DVIDSTAB_STATIC_DEFINE -I$SRC/src -I$FZ/inc"
LIB_SRCS="frameinfo transformtype libvidstab transform transformfixedpoint motiondetect serialize
  localmotion2transform boxblur vsvector lensdistortion lensmap l1campathoptimization lpsolver_ipm
  cpudetect motiondetect_dispatch motiondetect_opt"
OBJS=()
for s in $LIB_SRCS; do
  $CC $FUZZ_CFLAGS -c "src/$s.c" -o "$FZ/obj/$s.o"
  OBJS+=("$FZ/obj/$s.o")
done
rm -f "$FZ/libvidstab_fuzz.a"
ar rcs "$FZ/libvidstab_fuzz.a" "${OBJS[@]}"

# LeakSanitizer off at build time (leaks aren't the bug class we fuzz for).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$FZ/obj/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FZ/obj/standalone_main.o"

# 2) Each harness twice: libFuzzer binary + run-once standalone reproducer.
for h in fuzz_read_localmotions fuzz_trf_pipeline fuzz_motiondetect fuzz_transform_frame \
         fuzz_lens_estimate fuzz_read_old_transforms; do
  $CC $FUZZ_CFLAGS -c "mayhem/harnesses/$h.c" -o "$FZ/obj/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FZ/obj/$h.o" "$FZ/libvidstab_fuzz.a" \
    "$FZ/obj/lsan_off.o" -lm -o "$FZ/bin/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$FZ/obj/$h.o" "$FZ/obj/standalone_main.o" "$FZ/libvidstab_fuzz.a" \
    "$FZ/obj/lsan_off.o" -lm -o "$FZ/bin/$h-standalone"
done

# 3) Upstream's own unit-test suite (tests/CMakeLists.txt), with its NORMAL flags (Debug,
#    asserts on), OpenMP off for deterministic single-threaded runs. build/tests is gitignored.
cmake -S tests -B build/tests -DUSE_OMP=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS"
cmake --build build/tests -j"$MAYHEM_JOBS" --target tests
