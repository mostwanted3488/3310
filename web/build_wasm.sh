#!/bin/sh
# Compiles the real DSP engine (Source/Dsp) to WebAssembly for the browser preview.
# Needs clang + lld (wasm-ld). Output: engine.wasm, then embedded as base64 into preview/index.html (WASM_B64).
set -e
cd "$(dirname "$0")"
FAKE=$(mktemp -d); mkdir -p "$FAKE/gnu"; : > "$FAKE/gnu/stubs-32.h"
SYS=$(ls -d /usr/include/c++/* | tail -1)
clang++ --target=wasm32 -std=c++17 -O3 -fno-exceptions -fno-rtti -nostdlib -nostdinc++ -fno-threadsafe-statics \
  -I../Source/Dsp -I"$SYS" -I/usr/include/x86_64-linux-gnu/c++/$(basename "$SYS") \
  -isystem "$FAKE" -isystem /usr/include -isystem /usr/include/x86_64-linux-gnu \
  -Wl,--no-entry -Wl,--allow-undefined -Wl,--export-memory -Wl,--initial-memory=4194304 -Wl,-z,stack-size=262144 \
  -fuse-ld=lld -o engine.wasm engine_wasm.cpp
echo "engine.wasm: $(wc -c < engine.wasm) bytes"
