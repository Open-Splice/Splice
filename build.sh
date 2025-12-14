#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Splice build script
# - Builds splice runtime + spbuild compiler
# - Cross-platform: macOS, Linux, Windows (GitHub Actions / MSYS)
# - Outputs binaries into ./bin
# ============================================================

FORCE=0
if [[ "${1-}" == "--force" ]]; then
    FORCE=1
fi

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
ROOT_DIR="$(pwd)"
BIN_DIR="$ROOT_DIR/bin"

mkdir -p "$BIN_DIR"

echo "========================================"
echo " Splice Build Script"
echo " OS   : $OS"
echo " ARCH : $ARCH"
echo " BIN  : $BIN_DIR"
echo "========================================"

# ------------------------------------------------------------
# Detect executable names
# ------------------------------------------------------------
SPLICE_BIN="splice"
SPBUILD_BIN="spbuild"

if [[ "$OS" == mingw* || "$OS" == cygwin* ]]; then
    SPLICE_BIN="splice.exe"
    SPBUILD_BIN="spbuild.exe"
fi

# ------------------------------------------------------------
# Skip build if binaries exist and not forced
# ------------------------------------------------------------
if [[ $FORCE -eq 0 && -x "$BIN_DIR/$SPLICE_BIN" && -x "$BIN_DIR/$SPBUILD_BIN" ]]; then
    echo "Binaries already exist. Use --force to rebuild."
    exit 0
fi

# ------------------------------------------------------------
# Compile Splice runtime
# ------------------------------------------------------------
echo "[1/3] Compiling Splice runtime..."

gcc -DSDK_IMPLEMENTATION \
    -Isrc \
    -Wall -Wextra \
    -c src/splice.c \
    -o "$BIN_DIR/splice.o"

gcc -Isrc \
    -Wall -Wextra \
    -c src/module_stubs.c \
    -o "$BIN_DIR/module_stubs.o"

gcc \
    "$BIN_DIR/splice.o" \
    "$BIN_DIR/module_stubs.o" \
    -o "$BIN_DIR/$SPLICE_BIN"

# ------------------------------------------------------------
# Compile spbuild (bytecode compiler)
# ------------------------------------------------------------
echo "[2/3] Compiling spbuild..."

gcc -Isrc \
    -Wall -Wextra \
    src/build.c \
    -o "$BIN_DIR/$SPBUILD_BIN"

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------
echo "[3/3] Cleaning up..."

rm -f \
    "$BIN_DIR/splice.o" \
    "$BIN_DIR/module_stubs.o"

# ------------------------------------------------------------
# Done
# ------------------------------------------------------------
echo "========================================"
echo " Build complete"
echo "----------------------------------------"
ls -lh "$BIN_DIR"
echo "========================================"

echo "Usage:"
echo "  ./bin/$SPBUILD_BIN input.sp output.spbc"
echo "  ./bin/$SPLICE_BIN  output.spbc"
