#!/usr/bin/env bash
set -euo pipefail

# Resolve paths from this script's own location (submodule_root/user), not from the
# caller's working directory. This lets the script be invoked the same way whether
# run from the RALibretro submodule root, from the main project root, or via an
# absolute/relative path from anywhere else.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUBMODULE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SUBMODULE_ROOT"

TARGET="auto"
TYPE="dynamic"
ARCH=""
# No upstream version to track, so each build is named after its build date.
VERSION="$(date +%Y%m%d)"

usage() {
  cat <<EOF
Usage: user/build_rahasher.sh [--target auto|mac|windows|linux|iphone|android] [--arch x64|arm64] [--type dynamic|static|bin|all] [--version <name>]

Defaults:
  --target auto
  --arch   the host's for windows/linux; arm64 for mac/iphone/android
  --type dynamic
  --version <build date, YYYYMMdd>
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
    --arch)
      ARCH="${2:-}"
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

UNAME_S="$(uname -s)"
case "$UNAME_S" in
  Darwin) HOST_PLATFORM="mac" ;;
  Linux) HOST_PLATFORM="linux" ;;
  MINGW*|MSYS*|CYGWIN*) HOST_PLATFORM="windows" ;;
  *) HOST_PLATFORM="unknown" ;;
esac

if [[ "$TARGET" == "auto" ]]; then
  if [[ "$HOST_PLATFORM" == "unknown" ]]; then
    echo "Cannot auto-detect target for uname=$UNAME_S" >&2
    exit 1
  fi
  TARGET="$HOST_PLATFORM"
fi

case "$(uname -m)" in
  arm64|aarch64) HOST_ARCH="arm64" ;;
  *) HOST_ARCH="x64" ;;
esac
case "$ARCH" in
  aarch64) ARCH="arm64" ;;
  x86_64|amd64) ARCH="x64" ;;
esac
case "$TARGET" in
  mac|iphone|android) ARCH="${ARCH:-arm64}" ;;
  windows|linux) ARCH="${ARCH:-$HOST_ARCH}" ;;
  *)
    echo "Unsupported target: $TARGET" >&2
    exit 1
    ;;
esac
case "$TARGET/$ARCH" in
  mac/arm64|iphone/arm64|android/arm64|windows/x64|windows/arm64|linux/x64|linux/arm64) ;;
  *)
    echo "Unsupported target/arch: $TARGET/$ARCH" >&2
    exit 1
    ;;
esac

# Cross compilers for the arm64 targets that are not native here. Passed on make's command
# line: Makefile.rahasher's `CC ?= gcc` never applies (make predefines CC), so an
# environment CC is the only other way in, and that leaks into every sub-make.
MAKE_TOOLS=()
if [[ "$TARGET/$ARCH" == "windows/arm64" ]]; then
  # MSYS2's MinGW64 gcc has no aarch64 target; llvm-mingw's clang does.
  CC="${CC_WINDOWS_ARM64:-aarch64-w64-mingw32-clang}"
  CXX="${CXX_WINDOWS_ARM64:-aarch64-w64-mingw32-clang++}"
  command -v "$CXX" >/dev/null 2>&1 || { echo "Error: $CXX not found. Put llvm-mingw's bin/ on PATH (https://github.com/mstorsjo/llvm-mingw)." >&2; exit 1; }
  MAKE_TOOLS=(CC="$CC" CXX="$CXX" AR="${AR_WINDOWS_ARM64:-llvm-ar}")
elif [[ "$TARGET/$ARCH" == "linux/arm64" && "$HOST_ARCH" != "arm64" ]]; then
  prefix="${LINUX_ARM64_CROSS_PREFIX:-aarch64-linux-gnu-}"
  command -v "${prefix}g++" >/dev/null 2>&1 || { echo "Error: ${prefix}g++ not found. Install g++-aarch64-linux-gnu." >&2; exit 1; }
  MAKE_TOOLS=(CC="${prefix}gcc" CXX="${prefix}g++" AR="${prefix}ar")
fi

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

if [[ "$TARGET" == "iphone" || "$TARGET" == "android" ]]; then
  echo "Note: cross-compilation requires external toolchain setup (CC/CXX/SDK) in your environment." >&2
elif [[ "$TARGET" == "windows" && "$HOST_PLATFORM" != "windows" ]]; then
  echo "Note: cross-compiling for windows from $HOST_PLATFORM requires a mingw-w64 CC/CXX toolchain in your environment." >&2
fi

if [[ "$TARGET" == "windows" && "$HOST_PLATFORM" == "windows" && "$ARCH" == "x64" ]]; then
  CXX_BIN="${CXX:-g++}"
  if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    echo "Error: no C++ compiler found ('$CXX_BIN')." >&2
    echo "Install the MSYS2 mingw-w64-x86_64-toolchain package and run this script from an MSYS2/MinGW (or Git Bash with mingw64 on PATH) shell." >&2
    exit 1
  fi
fi

for mt in "${MAKE_TARGETS[@]}"; do
  echo "Building $mt for target=$TARGET arch=$ARCH version=$VERSION"
  make -f user/Makefile.rahasher "$mt" TARGET_PLATFORM="$TARGET" TARGET_ARCH="$ARCH" VERSION="$VERSION" ${MAKE_TOOLS[@]+"${MAKE_TOOLS[@]}"}
done

OUT_ROOT="$SUBMODULE_ROOT/user/release/$TARGET/$ARCH/$VERSION"
echo "Build done. Output root: $OUT_ROOT"
echo "Dynamic lib: $OUT_ROOT/dynamic"
echo "Header: $OUT_ROOT/include"
