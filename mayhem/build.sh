#!/usr/bin/env bash
# Mining build (BASE 12917af): the one libFuzzer harness that reaches Verifier::VerifyTableStart with alignment
# checking off (mayhem/harness/), as a libFuzzer binary and a run-once -standalone reproducer, linked against the
# library list of upstream's tests/fuzzer CMake project, plus the upstream test suite (flattests) with normal flags.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OBJ="$SRC/mayhem/build-obj"
# A re-run may resolve the compiler through a different path (ccache masquerade vs. /usr/bin), which makes
# CMake discard its cache and reconfigure without the -D options; always configure from scratch.
rm -rf "$SRC"/mayhem/build-fuzz "$SRC"/mayhem/build-standalone "$SRC"/mayhem/build-tests "$SRC"/mayhem/build-src
mkdir -p "$OBJ"

TARGETS="verifier_unaligned_fuzzer"

# LeakSanitizer off for every ASan target (link-time hook); the standalone driver is compiled as C so
# its LLVMFuzzerTestOneInput reference keeps C linkage.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

# Upstream's tests/fuzzer/CMakeLists.txt, with its source-relative paths made absolute so the
# library/compiler source lists keep following upstream (the mined harness is appended below).
mkdir -p "$SRC/mayhem/build-src"
sed -e "s#\${CMAKE_CURRENT_SOURCE_DIR}#$SRC/tests/fuzzer#g" \
    -e "s#\${CMAKE_SOURCE_DIR}/\.\.#$SRC/tests#g" \
    -e "s#\(add_executable([a-z0-9_]* \)\(f[a-z]*_[a-z0-9]*_fuzzer\.cc\)#\1$SRC/tests/fuzzer/\2#" \
    -e "s#^include_directories(\${FLATBUFFERS_DIR}/tests)#&\ninclude_directories($SRC/tests/fuzzer)#" \
    "$SRC/tests/fuzzer/CMakeLists.txt" > "$SRC/mayhem/build-src/CMakeLists.txt"
cat >> "$SRC/mayhem/build-src/CMakeLists.txt" <<CMAKE
add_executable(verifier_unaligned_fuzzer $SRC/mayhem/harness/flatbuffers_unaligned_verifier_fuzzer.cc)
target_link_libraries(verifier_unaligned_fuzzer PRIVATE flatbuffers_fuzzed)
CMAKE

# 1) Fuzz binaries via upstream's tests/fuzzer CMake project in OSS-Fuzz mode (what OSS-Fuzz runs).
# OSS_FUZZ mode adds no SanCov flags itself (OSS-Fuzz passes them via CXXFLAGS), so the library and harness
# TUs need -fsanitize=fuzzer-no-link here or the fuzz binaries record 0 edges in Mayhem. The run-once
# standalone build stays uninstrumented.
fuzz_build() { # <build dir> <engine link item> [extra compile flags]
  LIB_FUZZING_ENGINE="$2" cmake -S "$SRC/mayhem/build-src" -B "$1" -G "Unix Makefiles" \
    -DOSS_FUZZ=ON -DCMAKE_BUILD_TYPE= \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS ${3:-}" \
    -DCMAKE_EXE_LINKER_FLAGS="$SANITIZER_FLAGS $OBJ/lsan_off.o"
  cmake --build "$1" -j"$MAYHEM_JOBS" --target $TARGETS
}
fuzz_build "$SRC/mayhem/build-fuzz" "$LIB_FUZZING_ENGINE" -fsanitize=fuzzer-no-link
fuzz_build "$SRC/mayhem/build-standalone" "$OBJ/standalone_main.o"

for t in $TARGETS; do
  cp "$SRC/mayhem/build-fuzz/$t" "$SRC/$t"
  cp "$SRC/mayhem/build-standalone/$t" "$SRC/$t-standalone"
done
# 2) Upstream's own test suite, normal flags (no sanitizers), for mayhem/test.sh to run.
env -u CFLAGS -u CXXFLAGS -u LDFLAGS cmake -S . -B "$SRC/mayhem/build-tests" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS" \
  -DFLATBUFFERS_BUILD_TESTS=ON -DFLATBUFFERS_BUILD_FLATC=ON -DFLATBUFFERS_BUILD_CPP17=ON \
  -DFLATBUFFERS_LIBCXX_WITH_CLANG=OFF -DFLATBUFFERS_INSTALL=OFF
cmake --build "$SRC/mayhem/build-tests" -j"$MAYHEM_JOBS" --target flattests flattests_cpp17
