#define SPLICE_NO_INLINE_MEMREADER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "splice.h"
#include "sdk.h"

/* =========================
   Logging helpers
   ========================= */

static void success(int ln, const char *fmt, ...) {
    (void)ln;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "[OK] ");
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    va_end(ap);
}

/* =========================
   CLI VM entry
   ========================= */

/* Basic validation to reduce path traversal risk.
   Allows only non-empty relative paths and rejects any ".." component. */
static int is_safe_relative_path(const char *path) {
    if (path == NULL || *path == '\0') {
        return 0;
    }

    /* Reject absolute POSIX-style paths. */
    if (path[0] == '/') {
        return 0;
    }

    /* Rudimentary check against Windows drive letters like "C:" or "C:\". */
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':') {
        return 0;
    }

    /* Scan components separated by '/' or '\\' and reject any that are exactly "..". */
    const char *p = path;
    while (*p) {
        while (*p == '/' || *p == '\\') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            return 0;
        }
    }

    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.spc>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-v") == 0) {
        printf("Splice Version 1.0.0\n");
        return 0;
    }

    const char *path = argv[1];
    if (!is_safe_relative_path(path)) {
        fprintf(stderr, "[ERROR] invalid SPC path\n");
        return 1;
    }
    const char *ext = strrchr(path, '.');
    if (!ext || strcmp(ext, ".spc") != 0) {
        fprintf(stderr, "[ERROR] only .spc supported by VM now\n");
        return 1;
    }

    /* =========================
       Read SPC into memory
       ========================= */

    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fprintf(stderr, "[ERROR] empty SPC file\n");
        fclose(f);
        return 1;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)size);
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

    /* =========================
       Reset VM state (important!)
       ========================= */

    splice_reset_vm();

    /* =========================
       Load AST from memory
       ========================= */
    arena_init(64 * 1024);
    ASTNode *root = read_ast_from_spc_mem(buf, (size_t)size);
    if (!root) {
        fprintf(stderr, "[ERROR] failed to parse SPC\n");
        free(buf);
        return 1;
    }

    /* =========================
       Execute program
       ========================= */

    interpret(root);

    /* =========================
       Cleanup
       ========================= */

    /*
      DO NOT free AST here unless you have a free_ast().
      VM owns AST lifetime.
    */
    free(buf);


    return 0;
}
