#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TARGET="all"
BUILD_TYPE="Release"
RUN_TESTS=0
PACKAGE=0
CLEAN=0

usage() {
    cat <<USAGE
Usage: ./scripts/build.sh [options]

Targets:
  --linux         Build Linux binary only (build-linux/)
  --windows       Build Windows x86_64 binary only via MinGW-w64 (build-win/)
  --all           Build both Linux and Windows (default)

Options:
  --debug         Configure with Debug instead of Release
  --test          Build and run Linux Catch2 unit tests (ctest)
  --package       Bundle standalone playtest archives in dist/
  --clean         Wipe target build directories before configuring
  -h, --help      Show this help message

Examples:
  ./scripts/build.sh --linux --test
  ./scripts/build.sh --windows --package
  ./scripts/build.sh --all --package
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --linux)   TARGET="linux"; shift ;;
        --windows) TARGET="windows"; shift ;;
        --all)     TARGET="all"; shift ;;
        --debug)   BUILD_TYPE="Debug"; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --test)    RUN_TESTS=1; shift ;;
        --package) PACKAGE=1; shift ;;
        --clean)   CLEAN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1"; usage; exit 1 ;;
    esac
done

ensure_mingw_toolchain() {
    if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
        echo "[ERR] x86_64-w64-mingw32-g++ not found. Install it on Fedora with:"
        echo "      sudo dnf install -y mingw64-gcc-c++ mingw64-winpthreads-static"
        exit 1
    fi
    if [[ ! -f "$ROOT_DIR/cmake/mingw-w64-x86_64.cmake" ]]; then
        mkdir -p "$ROOT_DIR/cmake"
        cat <<'TOOLCHAIN' > "$ROOT_DIR/cmake/mingw-w64-x86_64.cmake"
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32/sys-root/mingw)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
TOOLCHAIN
        echo "[INF] Created cmake/mingw-w64-x86_64.cmake"
    fi
}

build_linux() {
    echo "=========================================="
    echo "[BUILD] Linux ($BUILD_TYPE)"
    echo "=========================================="
    local BDIR="$ROOT_DIR/build-linux"
    [[ "$CLEAN" -eq 1 ]] && rm -rf "$BDIR"

    cmake -B "$BDIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    if [[ "$RUN_TESTS" -eq 1 ]]; then
        cmake --build "$BDIR"
        ctest --test-dir "$BDIR" --output-on-failure
    else
        cmake --build "$BDIR" --target Carnage
    fi

    if [[ "$PACKAGE" -eq 1 ]]; then
        local PDIR="$ROOT_DIR/dist/Carnage-Linux-$BUILD_TYPE"
        rm -rf "$PDIR"
        mkdir -p "$PDIR"
        cp "$BDIR/Carnage" "$PDIR/"
        cp -r "$ROOT_DIR/assets" "$PDIR/"
        (cd "$ROOT_DIR/dist" && tar -czf "Carnage-Linux-$BUILD_TYPE.tar.gz" "Carnage-Linux-$BUILD_TYPE")
        echo "[PKG] dist/Carnage-Linux-$BUILD_TYPE.tar.gz"
    fi
}

build_windows() {
    echo "=========================================="
    echo "[BUILD] Windows x86_64 ($BUILD_TYPE)"
    echo "=========================================="
    ensure_mingw_toolchain
    local BDIR="$ROOT_DIR/build-win"
    [[ "$CLEAN" -eq 1 ]] && rm -rf "$BDIR"

    cmake -B "$BDIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/mingw-w64-x86_64.cmake" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    cmake --build "$BDIR" --target Carnage

    if [[ "$PACKAGE" -eq 1 ]]; then
        local PDIR="$ROOT_DIR/dist/Carnage-Win64-$BUILD_TYPE"
        rm -rf "$PDIR"
        mkdir -p "$PDIR"
        cp "$BDIR/Carnage.exe" "$PDIR/"
        cp -r "$ROOT_DIR/assets" "$PDIR/"
        (cd "$ROOT_DIR/dist" && rm -f "Carnage-Win64-$BUILD_TYPE.zip" && zip -rq "Carnage-Win64-$BUILD_TYPE.zip" "Carnage-Win64-$BUILD_TYPE")
        echo "[PKG] dist/Carnage-Win64-$BUILD_TYPE.zip"
    fi
}

case "$TARGET" in
    linux)   build_linux ;;
    windows) build_windows ;;
    all)     build_linux; build_windows ;;
esac

echo "[DONE] Build completed successfully."
