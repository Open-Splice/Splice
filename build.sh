#!/usr/bin/env bash
set -euo pipefail

# Cross-platform build script for Splice
# Builds local binaries into ./bin, then installs them to a system path
# Works on macOS, Linux, and Windows (GitHub Actions or local)

FORCE=0
if [[ "${1-}" == "--force" ]]; then
    FORCE=1
fi

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BIN_DIR="bin"

mkdir -p "$BIN_DIR"
echo "Installing Splice"
echo "Proceeding with build on $OS/$ARCH..."
echo "Building Splice runtime and native module..."

# Compile Splice runtime (with SDK globals)
gcc -DSDK_IMPLEMENTATION -Isrc -Wall -Wextra -c src/splice.c -o "$BIN_DIR/Splice.o"

# Compile native module without SDK_IMPLEMENTATION
gcc -Isrc -Wall -Wextra -c src/module_stubs.c -o "$BIN_DIR/module_stubs.o"

# Link executable (local binary: Splice)
gcc "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o" -o "$BIN_DIR/Splice"

echo "Building spbuild (bytecode compiler)..."
gcc -Isrc -Wall -Wextra src/build.c -o "$BIN_DIR/spbuild"

# --- Install section ---
INSTALL_DIR=""
if [[ "$OS" == "linux" || "$OS" == "darwin" ]]; then
    INSTALL_DIR="/usr/local/bin"
elif [[ "$OS" == mingw* || "$OS" == cygwin* ]]; then
    # On Windows GitHub Actions (MinGW/Cygwin), use a writable path
    INSTALL_DIR="$HOME/bin"
else
    echo "Unsupported OS: $OS"
    exit 1
fi

echo "Installing binaries into $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"


if [[ $FORCE -eq 0 && -x "$INSTALL_DIR/spbuild" && -x "$INSTALL_DIR/Splice" ]]; then
    echo "Binaries already exist in $INSTALL_DIR. Use --force to overwrite."
else
    if [[ "$OS" == "linux" || "$OS" == "darwin" ]]; then
        sudo cp "$BIN_DIR/Splice" "$INSTALL_DIR/splice"
        sudo cp "$BIN_DIR/spbuild" "$INSTALL_DIR/spbuild"
    elif [[ "$OS" == mingw* || "$OS" == cygwin* ]]; then
        if [[ "$OS" == mingw* || "$OS" == msys* || "$OS" == cygwin* ]]; then
            SPLICE_OUT="$BIN_DIR/Splice.exe"
            SPBUILD_OUT="$BIN_DIR/spbuild.exe"
        else
            SPLICE_OUT="$BIN_DIR/splice"
            SPBUILD_OUT="$BIN_DIR/spbuild"
        fi

    fi
    echo "Installation complete."
fi

echo "Cleaning up object files..."
rm -f "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o"

echo "Splice build script finished."
