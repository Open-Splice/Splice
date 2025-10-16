#!/bin/bash
set -euo pipefail

# Build script: always build the local `Splice]` binary first, then
# compare it with /usr/local/bin/Splice (sha256). Install only if
# different or when --force is passed.

FORCE=0
if [[ "${1-}" == "--force" ]]; then
    FORCE=1
fi

echo "Proceeding with build..."
echo "Building Splice src and native module..."

# Compile src (emit SDK globals in this TU)
gcc -DSDK_IMPLEMENTATION -Iruntime -Wall -Wextra -c src/Splice.c -o Splice.o

# Compile native module without SDK_IMPLEMENTATION
gcc -Isrc -Wall -Wextra -c src/module_stubs.c -o module_stubs.o

# Link executable (local binary: Splice)
gcc Splice.o module_stubs.o -o Splice
# Build messages
echo "Building spbuild (bytecode compiler)..."
gcc -Iruntime -Wall -Wextra src/build.c -o spbuild
sudo cp spbuild /usr/local/bin/spbuild
# Compute checksums (use shasum which exists on macOS; fallback when not present)
NEWSUM=""
OLDSUM=""
if command -v shasum >/dev/null 2>&1; then
    NEWSUM=$(shasum -a 256 Splice | awk '{print $1}')
    if [ -f /usr/local/bin/Splice ]; then
        OLDSUM=$(shasum -a 256 /usr/local/bin/Splice | awk '{print $1}') || true
    fi
elif command -v sha256sum >/dev/null 2>&1; then
        NEWSUM=$(sha256sum Splice | awk '{print $1}')
        if [ -f /usr/local/bin/Splice ]; then
            OLDSUM=$(sha256sum /usr/local/bin/Splice | awk '{print $1}') || true
    fi
fi

if [[ $FORCE -eq 0 && -n "$OLDSUM" && "$NEWSUM" == "$OLDSUM" ]]; then
    echo "/usr/local/bin/Splice is already the current build (checksums match). Skipping install."
else
    echo "Installing Splice to /usr/local/bin (requires sudo)..."
    sudo cp Splice /usr/local/bin/Splice
    echo "Build and install complete."
fi

# Clean up object files
rm -f Splice.o module_stubs.o

echo "Splice build script finished."
