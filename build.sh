#!/usr/bin/env bash
set -euo pipefail

# Cross-platform build script for Splice
# Builds local binaries into ./bin
# Runs them explicitly from ./bin (NO PATH reliance)

FORCE=0
if [[ "${1-}" == "--force" ]]; then
    FORCE=1
fi

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BIN_DIR="bin"

mkdir -p "$BIN_DIR"

echo "========================================"
echo " Building Splice"
echo " OS   : $OS"
echo " ARCH : $ARCH"
echo "========================================"

# ------------------------------------------------------------
# Detect Windows
# ------------------------------------------------------------
IS_WINDOWS=0
case "$OS" in
  mingw*|msys*|cygwin*) IS_WINDOWS=1 ;;
esac

# ------------------------------------------------------------
# Output binary names (SOURCE OF TRUTH)
# ------------------------------------------------------------
if [[ $IS_WINDOWS -eq 1 ]]; then
    SPLICE_BIN="$BIN_DIR/Splice.exe"
    SPBUILD_BIN="$BIN_DIR/spbuild.exe"
else
    SPLICE_BIN="$BIN_DIR/Splice"
    SPBUILD_BIN="$BIN_DIR/spbuild"
fi

# ------------------------------------------------------------
# Compile objects
# ------------------------------------------------------------
echo "[1/3] Compiling objects..."

gcc -DSDK_IMPLEMENTATION -Isrc -Wall -Wextra \
    -c src/splice.c \
    -o "$BIN_DIR/Splice.o"

gcc -Isrc -Wall -Wextra \
    -c src/module_stubs.c \
    -o "$BIN_DIR/module_stubs.o"

# ------------------------------------------------------------
# Link Splice runtime
# ------------------------------------------------------------
echo "[2/3] Linking Splice..."

gcc "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o" -o "$SPLICE_BIN"

# ------------------------------------------------------------
# Build spbuild
# ------------------------------------------------------------
echo "[3/3] Building spbuild..."

gcc -Isrc -Wall -Wextra src/build.c -o "$SPBUILD_BIN"

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------
rm -f "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o"

echo "Build outputs:"
ls -lh "$BIN_DIR"

# ------------------------------------------------------------
# OPTIONAL: Run using explicit ./bin paths (OPTION 1)
# ------------------------------------------------------------
if [[ "${RUN_SPLICE_TEST:-}" == "true" ]]; then
    echo "Running Splice using explicit ./bin paths..."

    "$SPBUILD_BIN" test/main.sp main.spbc
    "$SPLICE_BIN" main.spbc

    echo "Splice test run complete."
fi

echo "========================================"
echo "Done."
