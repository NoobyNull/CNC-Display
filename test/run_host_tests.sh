#!/usr/bin/env bash
# Host unit tests without PlatformIO: plain g++, Unity and the GrblParser header
# cloned once into test/.deps. Same sources and flags as [env:native] in
# platformio.ini, so `pio test -e native` and this script test the same code.
#
#   test/run_host_tests.sh             every test_* folder
#   test/run_host_tests.sh sendq caps  only those
set -euo pipefail
cd "$(dirname "$0")/.."

DEPS=test/.deps
UNITY_REF=v2.6.0
GRBL_REF=9108f54a3e30526b66194898676af34e96a87da9   # keep in step with platformio.ini

mkdir -p "$DEPS"
if [ ! -d "$DEPS/Unity" ]; then
    git clone -q --depth 1 --branch "$UNITY_REF" https://github.com/ThrowTheSwitch/Unity "$DEPS/Unity"
fi
if [ ! -d "$DEPS/GrblParser" ]; then
    git -c advice.detachedHead=false clone -q https://github.com/MitchBradley/GrblParser "$DEPS/GrblParser"
    git -c advice.detachedHead=false -C "$DEPS/GrblParser" checkout -q "$GRBL_REF"
fi

# The model sources [env:native] builds into every test binary.
SRCS=$(sed -n '/^\[env:native\]/,/^\[/{s/^build_src_filter *= *//p}' platformio.ini |
       grep -o '+<[^>]*>' | sed 's/+<\(.*\)>/src\/\1/')

CXXFLAGS="-std=gnu++17 -DUNIT_TEST -DE4_POS_T -Wall -Wno-unused-function -I src -I test
          -I $DEPS/Unity/src -I $DEPS/GrblParser/src"

OUT=test/.build
mkdir -p "$OUT"
fail=0
tests=("$@")
[ ${#tests[@]} -eq 0 ] && tests=($(ls -d test/test_* | xargs -n1 basename | sed 's/^test_//'))

for t in "${tests[@]}"; do
    dir=test/test_$t
    bin=$OUT/test_$t
    # A test with its own `sources` file links exactly those (paths from the
    # repo root, plus the parser) and brings its own stand-ins, instead of the
    # shared model set and host_stubs.cpp, which stub out the very code it tests.
    if [ -f "$dir/sources" ]; then
        srcs="$(grep -v '^#' "$dir/sources" | xargs)"
        extra="-I $dir -I sim/shim -x c $DEPS/GrblParser/src/GrblParserC.c $DEPS/GrblParser/src/e4math.c -x none"
    else
        srcs="test/host_stubs.cpp $SRCS"
        extra=""
    fi
    # shellcheck disable=SC2086
    if ! g++ $CXXFLAGS -o "$bin" "$dir"/*.cpp $srcs $extra \
            -x c "$DEPS/Unity/src/unity.c" 2> "$bin.log"; then
        echo "BUILD FAIL  $t"; sed -n 1,40p "$bin.log"; fail=1; continue
    fi
    if "$bin" > "$bin.out" 2>&1; then
        echo "PASS        $t  ($(tail -n 3 "$bin.out" | grep -o '^[0-9]* Tests' || true))"
    else
        echo "FAIL        $t"; grep -E 'FAIL|Tests' "$bin.out"; fail=1
    fi
done
exit $fail
