#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-release"

BOTH_ITERATIONS="${BOTH_ITERATIONS:-20000000}"
MIXED_ITERATIONS="${MIXED_ITERATIONS:-10000000}"
REALISH_ITERATIONS="${REALISH_ITERATIONS:-200000}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --target j6510_benchmark

echo
echo "== both ${BOTH_ITERATIONS} =="
"${BUILD_DIR}/j6510_benchmark" both "${BOTH_ITERATIONS}"

echo
echo "== mixed ${MIXED_ITERATIONS} =="
"${BUILD_DIR}/j6510_benchmark" mixed "${MIXED_ITERATIONS}"

echo
echo "== realish ${REALISH_ITERATIONS} =="
"${BUILD_DIR}/j6510_benchmark" realish "${REALISH_ITERATIONS}"
