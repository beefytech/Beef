#!/bin/bash
# Builds the Beef wasm runtime libraries, the Linux/macOS counterpart of build_wasm.bat.
#
# Emits into IDE/dist, which is where BuildContext looks before it will link a wasm32
# target, the runtime each project links:
#   Beef<ver>RT32_wasm.a            optimized (-O2)
#   Beef<ver>RT32_wasm_pthread.a    optimized, threads enabled (Wasm options)
#   Beef<ver>RT32_wasm_d.a          debug (-O0 -g), for Beef Lib Type "DynamicDebug", as
#   Beef<ver>RT32_wasm_pthread_d.a  libBeefRT_d.a is on Linux and macOS
# Needs emcc on PATH: source the emsdk's emsdk_env.sh first.
#
#   ./build_wasm.sh          build all four
#   ./build_wasm.sh release  build the optimized two only
#   ./build_wasm.sh debug    build the debug two only
#   ./build_wasm.sh setup    stage the sources only
set -e
cd "$(dirname "$0")"

RTVER=042
LIBPATH=../bin
if [ -f ../BeefRT/rt/Chars.cpp ]; then
    LIBPATH=../IDE/dist
    # Staged into src/ rather than compiled in place, because the sources include each other
    # as "rt/..." and "BeefySysLib/..." relative to one root.
    mkdir -p src/rt src/BeefySysLib/platform/posix src/BeefySysLib/platform/wasm \
             src/BeefySysLib/util src/BeefySysLib/third_party/utf8proc \
             src/BeefySysLib/third_party/stb src/BeefySysLib/third_party/putty
    cp ../BeefRT/rt/* src/rt/ 2>/dev/null || true
    cp ../BeefySysLib/*.h src/BeefySysLib/ 2>/dev/null || true
    cp ../BeefySysLib/Common.cpp src/BeefySysLib/
    cp ../BeefySysLib/platform/* src/BeefySysLib/platform/ 2>/dev/null || true
    cp ../BeefySysLib/platform/posix/* src/BeefySysLib/platform/posix/ 2>/dev/null || true
    cp ../BeefySysLib/platform/wasm/* src/BeefySysLib/platform/wasm/ 2>/dev/null || true
    cp ../BeefySysLib/util/* src/BeefySysLib/util/ 2>/dev/null || true
    cp ../BeefySysLib/third_party/utf8proc/* src/BeefySysLib/third_party/utf8proc/ 2>/dev/null || true
    cp ../BeefySysLib/third_party/stb/* src/BeefySysLib/third_party/stb/ 2>/dev/null || true
    cp ../BeefySysLib/third_party/putty/* src/BeefySysLib/third_party/putty/ 2>/dev/null || true
fi

[ "$1" = "setup" ] && { echo "SUCCESS (setup only)"; exit 0; }

case "$1" in
    "") KINDS="release debug" ;;
    release | debug) KINDS="$1" ;;
    *) echo "usage: $0 [release | debug | setup]" >&2; exit 1 ;;
esac

SOURCES="src/rt/Chars.cpp src/rt/Math.cpp src/rt/Object.cpp src/rt/Thread.cpp \
src/rt/Internal.cpp src/rt/zmij.c src/BeefySysLib/platform/wasm/WasmCommon.cpp \
src/BeefySysLib/Common.cpp src/BeefySysLib/util/String.cpp src/BeefySysLib/util/Hash.cpp \
src/BeefySysLib/util/UTF8.cpp src/BeefySysLib/third_party/utf8proc/utf8proc.c \
src/BeefySysLib/third_party/putty/wildcard.c"
INCLUDES="-Isrc/ -Isrc/BeefySysLib -Isrc/BeefySysLib/platform/wasm"
OBJECTS="Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o Hash.o UTF8.o \
utf8proc.o wildcard.o WasmCommon.o zmij.o"

for KIND in $KINDS; do
    if [ "$KIND" = "debug" ]; then FLAGS="-O0 -g"; SUFFIX="_d"; else FLAGS="-O2"; SUFFIX=""; fi

    emcc $SOURCES $INCLUDES $FLAGS -DBF_DISABLE_FFI -c
    emar r "$LIBPATH/Beef${RTVER}RT32_wasm${SUFFIX}.a" $OBJECTS

    emcc $SOURCES $INCLUDES $FLAGS -DBF_DISABLE_FFI -c -pthread
    emar r "$LIBPATH/Beef${RTVER}RT32_wasm_pthread${SUFFIX}.a" $OBJECTS
done

echo "SUCCESS!"
