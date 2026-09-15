#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
ctest --preset release
