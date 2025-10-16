#!/usr/bin/env bash
set -euo pipefail

# Cross-platform build script for Splice
# Builds local binaries into ./bin (no system install required)
# Works on macOS, Linux, and Windows (GitHub Actions)

FORCE=0
if [[ "${1-}" == "--force" ]]; then
    FORCE=1
fi

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BIN_DIR="bin"

mkdir -p "$BIN_DIR"

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

# --- Compute checksums ---
NEWBIN="$BIN_DIR/Splice"
OLDBIN="/usr/local/bin/Splice"

NEWSUM=""
OLDSUM=""

if command -v shasum >/dev/null 2>&1; then
    NEWSUM=$(shasum -a 256 "$NEWBIN" | awk '{print $1}')
    [[ -f "$OLDBIN" ]] && OLDSUM=$(shasum -a 256 "$OLDBIN" | awk '{print $1}') || true
elif command -v sha256sum >/dev/null 2>&1; then
    NEWSUM=$(sha256sum "$NEWBIN" | awk '{print $1}')
    [[ -f "$OLDBIN" ]] && OLDSUM=$(sha256sum "$OLDBIN" | awk '{print $1}') || true
elif [[ "$OS" == "mingw"* || "$OS" == "cygwin"* ]]; then
    # Windows GitHub runners: use CertUtil
    NEWSUM=$(certutil -hashfile "$NEWBIN" SHA256 | findstr /v "hash" | tr -d '\r\n')
    [[ -f "$OLDBIN" ]] && OLDSUM=$(certutil -hashfile "$OLDBIN" SHA256 | findstr /v "hash" | tr -d '\r\n') || true
fi

# --- Install (only on local dev, not CI) ---
if [[ "${CI-}" == "true" ]]; then
    echo "CI build: skipping install into /usr/local/bin"
else
    if [[ $FORCE -eq 0 && -n "$OLDSUM" && "$NEWSUM" == "$OLDSUM" ]]; then
        echo "$OLDBIN is already the current build (checksums match). Skipping install."
    else
        echo "Installing Splice to $OLDBIN (requires sudo)..."
        sudo cp "$BIN_DIR/spbuild" /usr/local/bin/spbuild
        sudo cp "$BIN_DIR/Splice"  /usr/local/bin/Splice
        echo "Build and install complete."
    fi
fi

echo "Cleaning up object files..."
rm -f "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o"

echo "Splice build script finished."
