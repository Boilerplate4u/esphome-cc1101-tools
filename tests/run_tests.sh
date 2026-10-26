#!/usr/bin/env bash
# Builds and runs the host tests with AddressSanitizer and UBSan.
# Environment: Debian/WSL, Linux or macOS with g++ or clang++.
set -euo pipefail

cd "$(dirname "$0")"
CXX="${CXX:-g++}"
SRC=../components/rf_sniffer
OUT=build
mkdir -p "$OUT"

FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer
       -fsanitize=address,undefined -fno-sanitize-recover=all -I"$SRC")

"$CXX" "${FLAGS[@]}" test_core.cpp "$SRC/sniffer_core.cpp" "$SRC/manchester_decoder.cpp" "$SRC/oregon_protocol.cpp" \
  -o "$OUT/test_core"

echo "== core tests"
"$OUT/test_core" data/cc1101_frames.txt
