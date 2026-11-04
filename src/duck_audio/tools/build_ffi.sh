#!/usr/bin/env bash
# 构建 microduck_sounds C ABI，并跑 C++ 冒烟测试。
#
# 用法:
#   tools/build_ffi.sh            # release 构建 + smoke test
#   tools/build_ffi.sh --debug    # debug 构建，便于调试
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUST_DIR="${ROOT}/rust"
HEADER_DIR="${ROOT}/include/duck_audio/ffi"
PROFILE="release"
CARGO_FLAGS=(--release)

if [[ "${1:-}" == "--debug" ]]; then
    PROFILE="debug"
    CARGO_FLAGS=()
fi

echo "building libmicroduck_sounds (${PROFILE})"
cargo build --locked "${CARGO_FLAGS[@]}" --manifest-path "${RUST_DIR}/Cargo.toml" -p microduck_sounds_ffi

LIB_DIR="${RUST_DIR}/target/${PROFILE}"
CXX="${CXX:-g++}"
OUT="${LIB_DIR}/ffi_smoke"
echo "compiling smoke test with ${CXX}"
"${CXX}" -std=c++23 -Wall -Wextra -Werror \
    -I "${HEADER_DIR}" \
    "${RUST_DIR}/test/ffi_smoke.cpp" \
    -L "${LIB_DIR}" -lmicroduck_sounds \
    -Wl,-rpath,"${LIB_DIR}" \
    -o "${OUT}"
echo "running smoke test"
"${OUT}"
