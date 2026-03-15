#!/usr/bin/env bash
set -euo pipefail

FORCE=0
if [[ "${1-}" == "--force" ]]; then
    FORCE=1
fi

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BIN_DIR="bin"
CC="${CC:-cc}"

mkdir -p "$BIN_DIR"

echo "Installing Splice"
echo "Proceeding with build on $OS/$ARCH..."

COMMON_WARN=(
    -Wall
    -Wextra
)

COMMON_DEFS=(
    -DNDEBUG
)

COMMON_OPT=(
    -O3
    -march=native
    -mtune=native
    -flto
    -fomit-frame-pointer
    -funroll-loops
    -fstrict-aliasing
    -fno-math-errno
    -fno-trapping-math
    -ffast-math
    -fmerge-all-constants
    -fdata-sections
    -ffunction-sections
    -fvisibility=hidden
    -fno-plt
    -pipe
)

COMMON_INCLUDES=(
    -Isrc
)

LINK_FLAGS=(
    -flto
    -Wl,--gc-sections
)

LINK_EXPORT_FLAGS=()
MATH_LIBS=()

if "$CC" --version 2>/dev/null | grep -qi clang; then
    COMMON_OPT+=(
        -mllvm -inline-threshold=1000
    )
    LINK_FLAGS=(
        -flto
    )
elif "$CC" --version 2>/dev/null | grep -qi gcc; then
    COMMON_OPT+=(
        -fdevirtualize-at-ltrans
        -fipa-pta
        -fgraphite-identity
        -floop-nest-optimize
    )
    LINK_FLAGS+=(
        -Wl,-O3
    )
fi

if [[ "$OS" == "linux" ]]; then
    LINK_EXPORT_FLAGS+=(-rdynamic)
    MATH_LIBS+=(-lm)
elif [[ "$OS" == "darwin" ]]; then
    LINK_EXPORT_FLAGS+=(-Wl,-export_dynamic)
    LINK_FLAGS=(
        -flto
    )
elif [[ "$OS" == mingw* || "$OS" == msys* || "$OS" == cygwin* ]]; then
    :
else
    echo "Unsupported OS: $OS"
    exit 1
fi

RUNTIME_WRAPPER="$BIN_DIR/splice_desktop_runtime.c"

cat > "$RUNTIME_WRAPPER" <<'EOF'
#define SPLICE_NO_INLINE_MEMREADER
#include "../src/runtime/splice.h"
#include "../src/sdk.h"

#if SPLICE_EMBED
int splice_run_embedded_program(const unsigned char *data, size_t size) {
    if (!data || size == 0) {
        return 0;
    }
    return splice_execute_bytecode(data, size);
}
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_safe_relative_path(const char *arg) {
    if (arg == NULL || *arg == '\0') return 0;
    if (arg[0] == '/') return 0;
    if (((arg[0] >= 'A' && arg[0] <= 'Z') || (arg[0] >= 'a' && arg[0] <= 'z')) && arg[1] == ':') {
        return 0;
    }

    for (const char *p = arg; *p;) {
        const char *start;
        size_t len;
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        start = p;
        while (*p && *p != '/' && *p != '\\') p++;
        len = (size_t)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') return 0;
    }

    return 1;
}

int main(int argc, char **argv) {
    FILE *f;
    const char *path;
    const char *ext;
    long size;
    unsigned char *buf;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.spc>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-v") == 0) {
        printf("Splice Version 1.0.0\n");
        printf("Splice(TM) VM Enviorment (build 1.0.0)\n");
        printf("Splice(TM) Compiler Crosschain (spbuild) build 1.0.0\n");
        return 0;
    }

    if (!is_safe_relative_path(argv[1])) {
        fprintf(stderr, "[ERROR] invalid SPC path\n");
        return 1;
    }

    path = argv[1];
    ext = strrchr(path, '.');
    if (!ext || strcmp(ext, ".spc") != 0) {
        fprintf(stderr, "[ERROR] only .spc supported by VM now\n");
        return 1;
    }

    f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fprintf(stderr, "[ERROR] empty SPC file\n");
        fclose(f);
        return 1;
    }

    buf = (unsigned char *)malloc((size_t)size);
    if (!buf) {
        fprintf(stderr, "[ERROR] OOM reading SPC\n");
        fclose(f);
        return 1;
    }

    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "[ERROR] failed to read SPC\n");
        free(buf);
        fclose(f);
        return 1;
    }

    fclose(f);

    if (!splice_execute_bytecode(buf, (size_t)size)) {
        fprintf(stderr, "[ERROR] failed to execute SPC\n");
        free(buf);
        return 1;
    }

    free(buf);
    return 0;
}
#endif
EOF

echo "Building Splice runtime and native module..."
"$CC" "${COMMON_WARN[@]}" "${COMMON_DEFS[@]}" "${COMMON_OPT[@]}" "${COMMON_INCLUDES[@]}" -DSDK_IMPLEMENTATION -c "$RUNTIME_WRAPPER" -o "$BIN_DIR/Splice.o"
"$CC" "${COMMON_WARN[@]}" "${COMMON_DEFS[@]}" "${COMMON_OPT[@]}" "${COMMON_INCLUDES[@]}" -c src/module_stubs.c -o "$BIN_DIR/module_stubs.o"
"$CC" "${LINK_EXPORT_FLAGS[@]}" "${LINK_FLAGS[@]}" "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o" "${MATH_LIBS[@]}" -o "$BIN_DIR/Splice"

echo "Building spbuild (bytecode compiler)..."
"$CC" "${COMMON_WARN[@]}" "${COMMON_DEFS[@]}" "${COMMON_OPT[@]}" "${COMMON_INCLUDES[@]}" \
    src/build.c \
    src/build/common.c \
    src/build/lexer.c \
    src/build/parser.c \
    src/build/optimizer.c \
    src/build/codegen.c \
    "${MATH_LIBS[@]}" \
    "${LINK_FLAGS[@]}" \
    -o "$BIN_DIR/spbuild"

INSTALL_DIR=""
if [[ "$OS" == "linux" || "$OS" == "darwin" ]]; then
    INSTALL_DIR="/usr/local/bin"
elif [[ "$OS" == mingw* || "$OS" == msys* || "$OS" == cygwin* ]]; then
    INSTALL_DIR="$HOME/bin"
fi

echo "Installing binaries into $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"

if [[ $FORCE -eq 0 && -x "$INSTALL_DIR/spbuild" && -x "$INSTALL_DIR/splice" ]]; then
    echo "Binaries already exist in $INSTALL_DIR. Use --force to overwrite."
else
    if [[ "$OS" == "linux" || "$OS" == "darwin" ]]; then
        sudo cp "$BIN_DIR/Splice" "$INSTALL_DIR/splice"
        sudo cp "$BIN_DIR/spbuild" "$INSTALL_DIR/spbuild"
        sudo cp -r "splib" "$INSTALL_DIR/splib"
    else
        cp "$BIN_DIR/Splice" "$INSTALL_DIR/splice"
        cp "$BIN_DIR/spbuild" "$INSTALL_DIR/spbuild"
    fi
    echo "Installation complete."
fi

echo "Cleaning up object files..."
rm -f "$BIN_DIR/Splice.o" "$BIN_DIR/module_stubs.o" "$RUNTIME_WRAPPER"

echo "Splice build script finished."
