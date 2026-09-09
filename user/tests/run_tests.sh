#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-dev}"

make -f user/Makefile.rahasher test TARGET_PLATFORM=mac TARGET_ARCH=arm64 VERSION="$VERSION"

echo "All tests passed"
