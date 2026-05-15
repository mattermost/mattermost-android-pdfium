#!/bin/bash
# Compiles the UNMODIFIED src/main/cpp/native-lib.cpp against a mock PDFium +
# mock JNIEnv under AddressSanitizer, then runs each abuse scenario in its own
# process (ASan abort = the finding is a real memory-corruption, proven).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/../.."
JH="$(/usr/libexec/java_home)"
BIN="$HERE/poc_harness"

echo "== build =="
SDK="$(xcrun --sdk macosx --show-sdk-path)"
xcrun clang++ -isysroot "$SDK" \
  -std=c++17 -g -O1 -fno-omit-frame-pointer -fsanitize=address \
  -fexceptions \
  -isystem "$SDK/usr/include/c++/v1" \
  -include vector \
  -I"$JH/include" -I"$JH/include/darwin" \
  -I"$ROOT/src/main/cpp/include" \
  -I"$HERE" \
  "$ROOT/src/main/cpp/native-lib.cpp" \
  "$HERE/mock_pdfium.cpp" "$HERE/mock_jni.cpp" \
  "$HERE/android_stub.cpp" "$HERE/harness.cpp" \
  -o "$BIN" || { echo "BUILD FAILED"; exit 1; }
echo "build OK -> $BIN"

export ASAN_OPTIONS="abort_on_error=0:exitcode=99:detect_leaks=0:print_summary=1"
run() {
  echo; echo "############### scenario: $1 ###############"
  "$BIN" "$1"; rc=$?
  echo "--- scenario '$1' exit code: $rc ---"
}
run f1
run f1render
run df
POC_NLINKS=250  run f4
POC_NLINKS=1500 run f4
run f1b
echo
echo "NOTE: exit code 99 (or ASan 'ERROR: AddressSanitizer:' above) = the"
echo "memory-corruption finding was triggered in the real native-lib.cpp."
