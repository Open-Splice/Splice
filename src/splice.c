#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "splice.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.spc>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-v") == 0) {
        printf("Splice Version 1.0.0\n");
        return 0;
    }

    const char *arg = argv[1];
    const char *ext = strrchr(arg, '.');
    if (!ext || strcmp(ext, ".spc") != 0) {
        fprintf(stderr, "[ERROR] only .spc supported by VM now\n");
        return 1;
    }

    ASTNode *root = read_ast_from_spc(arg);
    interpret(root);
    free_ast(root);
    return 0;
}
