#!/usr/bin/env bash
set -eo pipefail

# ============================================================
# Cross-platform build script for Splice
# - Builds into ./bin
# - Installs only for local dev (never in CI)
# ============================================================

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
    FORCE=1
fi

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BIN_DIR="bin"

mkdir -p "$BIN_DIR"

echo "========================================"
echo " Splice Build"
echo " OS   : $OS"
echo " ARCH : $ARCH"
echo " BIN  : $BIN_DIR"
echo "========================================"

# ------------------------------------------------------------
# Detect Windows
# ------------------------------------------------------------
IS_WINDOWS=0
case "$OS" in
  mingw*|msys*|cygwin*)
    IS_WINDOWS=1
    ;;
esac

# ------------------------------------------------------------
# Binary names
# ------------------------------------------------------------
SPLICE_BIN="splice"
SPBUILD_BIN="spbuild"

if [[ $IS_WINDOWS -eq 1 ]]; then
    SPLICE_BIN="splice.exe"
    SPBUILD_BIN="spbuild.exe"
fi

# ------------------------------------------------------------
# Build Splice runtime
# ------------------------------------------------------------
echo "[1/2] Building Splice runtime..."

gcc -DSDK_IMPLEMENTATION -Isrc -Wall -Wextra \
    -c src/splice.c \
    -o "$BIN_DIR/splice.o"

gcc -Isrc -Wall -Wextra \
    -c src/module_stubs.c \
    -o "$BIN_DIR/module_stubs.o"

gcc \
    "$BIN_DIR/splice.o" \
    "$BIN_DIR/module_stubs.o" \
    -o "$BIN_DIR/$SPLICE_BIN"

# ------------------------------------------------------------
# Build spbuild
# ------------------------------------------------------------
echo "[2/2] Building spbuild..."

gcc -Isrc -Wall -Wextra \
    src/build.c \
    -o "$BIN_DIR/$SPBUILD_BIN"

# ------------------------------------------------------------
# Cleanup
# ------------------------------------------------------------
rm -f "$BIN_DIR/splice.o" "$BIN_DIR/module_stubs.o"

echo "Build outputs:"
ls -lh "$BIN_DIR"

# ------------------------------------------------------------
# CI guard — DO NOT INSTALL IN CI
# ------------------------------------------------------------
if [[ "${CI:-}" == "true" ]]; then
    echo "CI detected — skipping install step"
    exit 0
fi

# ------------------------------------------------------------
# Install paths
# ------------------------------------------------------------
if [[ $IS_WINDOWS -eq 1 ]]; then
    INSTALL_DIR="$HOME/bin"
else
    INSTALL_DIR="/usr/local/bin"
fi

mkdir -p "$INSTALL_DIR"

echo "Installing to $INSTALL_DIR"

# ------------------------------------------------------------
# Install binaries
# ------------------------------------------------------------
if [[ $FORCE -eq 0 && -x "$INSTALL_DIR/$SPBUILD_BIN" && -x "$INSTALL_DIR/$SPLICE_BIN" ]]; then
    echo "Binaries already installed. Use --force to overwrite."
    exit 0
fi

if [[ $IS_WINDOWS -eq 1 ]]; then
    cp "$BIN_DIR/$SPBUILD_BIN" "$INSTALL_DIR/$SPBUILD_BIN"
    cp "$BIN_DIR/$SPLICE_BIN"  "$INSTALL_DIR/$SPLICE_BIN"
else
    sudo cp "$BIN_DIR/$SPBUILD_BIN" "$INSTALL_DIR/$SPBUILD_BIN"
    sudo cp "$BIN_DIR/$SPLICE_BIN"  "$INSTALL_DIR/$SPLICE_BIN"
fi

echo "Installation complete."

if [[ $IS_WINDOWS -eq 1 ]]; then
    echo "NOTE: Ensure $HOME/bin is in your PATH (Git Bash usually does this)."
fi

echo "========================================"
echo "Done."
