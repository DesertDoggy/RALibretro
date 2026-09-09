#!/usr/bin/env bash
set -euo pipefail

TARGET="auto"
TYPE="dynamic"
VERSION="dev"

usage() {
  cat <<EOF
Usage: user/build_rahasher.sh [--target auto|mac|windows|linux|iphone|android] [--type dynamic|static|bin|all] [--version <name>]

Defaults:
  --target auto
  --type dynamic
  --version dev
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      TARGET="${2:-}"
      shift 2
      ;;
    --type)
      TYPE="${2:-}"
      shift 2
      ;;
    --version)
      VERSION="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$TARGET" == "auto" ]]; then
  UNAME_S="$(uname -s)"
  case "$UNAME_S" in
    Darwin) TARGET="mac" ;;
    Linux) TARGET="linux" ;;
    MINGW*|MSYS*|CYGWIN*) TARGET="windows" ;;
    *)
      echo "Cannot auto-detect target for uname=$UNAME_S" >&2
      exit 1
      ;;
  esac
fi

case "$TARGET" in
  mac|iphone|android) ARCH="arm64" ;;
  windows|linux) ARCH="x64" ;;
  *)
    echo "Unsupported target: $TARGET" >&2
    exit 1
    ;;
esac

case "$TYPE" in
  dynamic|static|bin)
    MAKE_TARGETS=("$TYPE")
    ;;
  all)
    MAKE_TARGETS=(dynamic static bin)
    ;;
  *)
    echo "Unsupported build type: $TYPE" >&2
    exit 1
    ;;
esac

if [[ "$TARGET" == "iphone" || "$TARGET" == "android" || "$TARGET" == "windows" ]]; then
  echo "Note: cross-compilation requires external toolchain setup (CC/CXX/SDK) in your environment." >&2
fi

for mt in "${MAKE_TARGETS[@]}"; do
  echo "Building $mt for target=$TARGET arch=$ARCH version=$VERSION"
  make -f user/Makefile.rahasher "$mt" TARGET_PLATFORM="$TARGET" TARGET_ARCH="$ARCH" VERSION="$VERSION"
done

echo "Build done. Output root: user/release/$TARGET/$ARCH"
echo "Dynamic lib: user/release/$TARGET/$ARCH/dynamic"
echo "Header: user/release/$TARGET/$ARCH/iclude"
