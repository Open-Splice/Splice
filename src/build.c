#include "build/builder.h"

int main(int argc, char **argv) {
    const char *in_arg;
    const char *out_arg;
    char in_path[PATH_MAX];
    char out_path[PATH_MAX];
    char *src;
    TokVec tv = {0};
    ASTNode *root;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.spl> <output.spc>\n", argv[0]);
        return 1;
    }

    in_arg = argv[1];
    out_arg = argv[2];

    if (!splice_getcwd(g_project_root, sizeof(g_project_root))) {
        fprintf(stderr, "spbuild: failed to resolve working directory\n");
        return 1;
    }

    if (!resolve_input_path(in_arg, in_path, sizeof(in_path))) {
        fprintf(stderr, "spbuild: unsafe input path '%s'\n", in_arg);
        return 1;
    }

    if (!is_safe_filename(out_arg)) {
        fprintf(stderr, "spbuild: unsafe output filename '%s'\n", out_arg);
        return 1;
    }

    if (snprintf(out_path, sizeof(out_path), "./%s", out_arg) >= (int)sizeof(out_path)) {
        fprintf(stderr, "spbuild: output path too long\n");
        return 1;
    }

    src = read_file(in_path);
    if (!src) {
        fprintf(stderr, "spbuild: cannot read %s\n", in_arg);
        return 1;
    }

    lex(src, &tv);

    if (snprintf(g_current_source_file, sizeof(g_current_source_file), "%s", in_path) >= (int)sizeof(g_current_source_file)) {
        fprintf(stderr, "spbuild: input path too long\n");
        tv_free(&tv);
        free(src);
        return 1;
    }

    path_list_push(&g_imported_files, &g_imported_count, &g_imported_cap, in_path);
    path_list_push(&g_import_stack, &g_import_stack_count, &g_import_stack_cap, in_path);

    root = parse_program(&tv);
    path_list_pop(g_import_stack, &g_import_stack_count);
    root = optimize_node(root);

    if (!write_spc(out_path, root)) {
        fprintf(stderr, "spbuild: failed to write %s\n", out_arg);
        free_ast(root);
        tv_free(&tv);
        free(src);
        path_list_free(g_imported_files, g_imported_count);
        path_list_free(g_import_stack, g_import_stack_count);
        return 1;
    }

    free_ast(root);
    tv_free(&tv);
    free(src);
    path_list_free(g_imported_files, g_imported_count);
    path_list_free(g_import_stack, g_import_stack_count);
    return 0;
}
