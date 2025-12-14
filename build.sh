#!/usr/bin/env bash
set -eo pipefail

# Cross-platform build script for Splice
# Builds binaries into ./bin
# Installs:
#   macOS/Linux -> /usr/local/bin
#   Windows     -> $HOME/bin

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
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
# Output binary names (THE SOURCE OF TRUTH)
# ------------------------------------------------------------
if [[ $IS_WINDOWS -eq 1 ]]; then
    SPLICE_OUT="$BIN_DIR/Splice.exe"
    SPBUILD_OUT="$BIN_DIR/spbuild.exe"
else
    SPLICE_OUT="$BIN_DIR/splice"
    SPBUILD_OUT="$BIN_DIR/spbuild"
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
# Link splice
# ------------------------------------------------------------
echo "[2/3] Linking splice..."

gcc "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o" -o "$SPLICE_OUT"

# ------------------------------------------------------------
# Build spbuild
# ------------------------------------------------------------
echo "[3/3] Building spbuild..."

gcc -Isrc -Wall -Wextra src/build.c -o "$SPBUILD_OUT"

# ------------------------------------------------------------
# Cleanup objects
# ------------------------------------------------------------
rm -f "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o"

echo "Build outputs:"
ls -lh "$BIN_DIR"

# ------------------------------------------------------------
# CI guard (ABSOLUTE RULE)
# ------------------------------------------------------------
if [[ "${CI:-}" == "true" ]]; then
    echo "CI detected — skipping install"
    exit 0
fi

# ------------------------------------------------------------
# Install path
# ------------------------------------------------------------
if [[ $IS_WINDOWS -eq 1 ]]; then
    INSTALL_DIR="$HOME/bin"
else
    INSTALL_DIR="/usr/local/bin"
fi

mkdir -p "$INSTALL_DIR"
echo "Installing to $INSTALL_DIR"

# ------------------------------------------------------------
# Skip install if already present
# ------------------------------------------------------------
if [[ $FORCE -eq 0 ]]; then
    if [[ -x "$INSTALL_DIR/$(basename "$SPBUILD_OUT")" &&
          -x "$INSTALL_DIR/$(basename "$SPLICE_OUT")" ]]; then
        echo "Binaries already installed. Use --force to overwrite."
        exit 0
    fi
fi

# ------------------------------------------------------------
# Install (NO RENAMING, EVER)
# ------------------------------------------------------------
if [[ $IS_WINDOWS -eq 1 ]]; then
    cp "$SPBUILD_OUT" "$INSTALL_DIR/$(basename "$SPBUILD_OUT")"
    cp "$SPLICE_OUT"  "$INSTALL_DIR/$(basename "$SPLICE_OUT")"
else
    sudo cp "$SPBUILD_OUT" "$INSTALL_DIR/$(basename "$SPBUILD_OUT")"
    sudo cp "$SPLICE_OUT"  "$INSTALL_DIR/$(basename "$SPLICE_OUT")"
fi

echo "Installation complete."

if [[ $IS_WINDOWS -eq 1 ]]; then
    echo "NOTE: Ensure $HOME/bin is in your PATH (Git Bash usually is)."
fi

echo "========================================"
echo "Done."
