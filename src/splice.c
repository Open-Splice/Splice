#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "splice.h"
/* splice.c
Copyright (c) The Sinha Group and Open-Splice */
// helper to read file fully
static char *read_file_or_null(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb"); // binary, since .spbc is not text
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0'; // not strictly needed for binary
    fclose(f);
    if (out_len) *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.spbc | file.Splice>\n", argv[0]);
        return 1;
    }

    const char *arg = argv[1];
    if (arg && strcmp(arg, "-v") == 0) {
        printf("Splice Version 1.0.0\n");
        return 0;
    }

    long file_len = 0;
    char *file_data = read_file_or_null(arg, &file_len);
    if (!file_data) {
        fprintf(stderr, "Could not read file: %s\n", arg);
        return 1;
    }

    // reset globals
    for (int t = 0; t < i; ++t) { free(arr[t]); arr[t] = NULL; }
    i = 0; current = 0; line = 1;

    // detect file type by extension
    const char *ext = strrchr(arg, '.');
    if (ext && strcmp(ext, ".spbc") == 0) {
        // bytecode file → reverse lexer
        lex_from_bytecode(arg);
        info(0, "Loaded bytecode from %s (%ld bytes)\n", arg, file_len);
    } else {
        fprintf(stderr, "[ERROR] No tokens generated\n");
        free(file_data);
        return 1;
    }

    ASTNode *root = parse_statements();
    info(0, "Parsing Code");
    if (!root) {
        fprintf(stderr, "[ERROR] Failed to parse source\n");
        free(file_data);
        return 1;
    }

    interpret(root);
    free_ast(root);

    success(0, "Code ran successfully");

    free(file_data);
    return 0;
}
