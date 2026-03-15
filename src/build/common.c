#include "builder.h"

char g_project_root[PATH_MAX];
char g_current_source_file[PATH_MAX];
char **g_imported_files = NULL;
int g_imported_count = 0;
int g_imported_cap = 0;
char **g_import_stack = NULL;
int g_import_stack_count = 0;
int g_import_stack_cap = 0;

void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("spbuild: OOM");
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) die("spbuild: OOM");
    return q;
}

char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (!s) s = "";
    n = strlen(s);
    p = (char *)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

ASTNode *ast_new(ASTNodeType t) {
    ASTNode *n = (ASTNode *)xmalloc(sizeof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->type = t;
    return n;
}

ASTNode *ast_number(double v) {
    ASTNode *n = ast_new(AST_NUMBER);
    n->number = v;
    return n;
}

ASTNode *ast_string(const char *s) {
    ASTNode *n = ast_new(AST_STRING);
    n->string = xstrdup(s);
    return n;
}

ASTNode *ast_ident(const char *s) {
    ASTNode *n = ast_new(AST_IDENTIFIER);
    n->string = xstrdup(s);
    return n;
}

ASTNode *ast_binop(const char *op, ASTNode *l, ASTNode *r) {
    ASTNode *n = ast_new(AST_BINARY_OP);
    n->binop.op = xstrdup(op);
    n->binop.left = l;
    n->binop.right = r;
    return n;
}

ASTNode *ast_print(ASTNode *e) {
    ASTNode *n = ast_new(AST_PRINT);
    n->print.expr = e;
    return n;
}

ASTNode *ast_var(ASTNodeType t, const char *name, ASTNode *val) {
    ASTNode *n = ast_new(t);
    n->var.name = xstrdup(name);
    n->var.value = val;
    return n;
}

ASTNode *ast_statements(ASTNode **stmts, int count) {
    ASTNode *n = ast_new(AST_STATEMENTS);
    n->statements.stmts = stmts;
    n->statements.count = count;
    return n;
}

ASTNode *ast_while(ASTNode *c, ASTNode *b) {
    ASTNode *n = ast_new(AST_WHILE);
    n->whilestmt.cond = c;
    n->whilestmt.body = b;
    return n;
}

ASTNode *ast_if(ASTNode *c, ASTNode *t, ASTNode *e) {
    ASTNode *n = ast_new(AST_IF);
    n->ifstmt.cond = c;
    n->ifstmt.then_b = t;
    n->ifstmt.else_b = e;
    return n;
}

ASTNode *ast_for(const char *v, ASTNode *s, ASTNode *e, ASTNode *b) {
    ASTNode *n = ast_new(AST_FOR);
    n->forstmt.var = xstrdup(v);
    n->forstmt.start = s;
    n->forstmt.end = e;
    n->forstmt.body = b;
    return n;
}

ASTNode *ast_funcdef_params(const char *name, char **params, int param_count, ASTNode *body) {
    ASTNode *n = ast_new(AST_FUNC_DEF);
    n->funcdef.name = xstrdup(name);
    n->funcdef.params = params;
    n->funcdef.param_count = param_count;
    n->funcdef.body = body;
    return n;
}

ASTNode *ast_call(const char *name, ASTNode **args, int arg_count) {
    ASTNode *n = ast_new(AST_FUNCTION_CALL);
    n->funccall.name = xstrdup(name);
    n->funccall.args = args;
    n->funccall.arg_count = arg_count;
    return n;
}

ASTNode *ast_return(ASTNode *e) {
    ASTNode *n = ast_new(AST_RETURN);
    n->retstmt.expr = e;
    return n;
}

ASTNode *ast_array(ASTNode **items, int count) {
    ASTNode *n = ast_new(AST_ARRAY);
    n->arraylit.items = items;
    n->arraylit.count = count;
    return n;
}

ASTNode *ast_index(ASTNode *arr, ASTNode *idx) {
    ASTNode *n = ast_new(AST_INDEX);
    n->index.array = arr;
    n->index.index = idx;
    return n;
}

ASTNode *ast_index_assign(ASTNode *arr, ASTNode *idx, ASTNode *val) {
    ASTNode *n = ast_new(AST_INDEX_ASSIGN);
    n->indexassign.array = arr;
    n->indexassign.index = idx;
    n->indexassign.value = val;
    return n;
}

ASTNode *ast_import_c(const char *path) {
    ASTNode *n = ast_new(AST_IMPORT_C);
    n->string = xstrdup(path);
    return n;
}

void free_ast(ASTNode *n) {
    int i;

    if (!n) return;
    switch (n->type) {
        case AST_STRING:
        case AST_IDENTIFIER:
        case AST_IMPORT_C:
            free(n->string);
            break;
        case AST_BINARY_OP:
            free(n->binop.op);
            free_ast(n->binop.left);
            free_ast(n->binop.right);
            break;
        case AST_PRINT:
            free_ast(n->print.expr);
            break;
        case AST_LET:
        case AST_ASSIGN:
            free(n->var.name);
            free_ast(n->var.value);
            break;
        case AST_STATEMENTS:
            for (i = 0; i < n->statements.count; i++) free_ast(n->statements.stmts[i]);
            free(n->statements.stmts);
            break;
        case AST_WHILE:
            free_ast(n->whilestmt.cond);
            free_ast(n->whilestmt.body);
            break;
        case AST_IF:
            free_ast(n->ifstmt.cond);
            free_ast(n->ifstmt.then_b);
            free_ast(n->ifstmt.else_b);
            break;
        case AST_FOR:
            free(n->forstmt.var);
            free_ast(n->forstmt.start);
            free_ast(n->forstmt.end);
            free_ast(n->forstmt.body);
            break;
        case AST_FUNC_DEF:
            free(n->funcdef.name);
            for (i = 0; i < n->funcdef.param_count; i++) free(n->funcdef.params[i]);
            free(n->funcdef.params);
            free_ast(n->funcdef.body);
            break;
        case AST_FUNCTION_CALL:
            free(n->funccall.name);
            for (i = 0; i < n->funccall.arg_count; i++) free_ast(n->funccall.args[i]);
            free(n->funccall.args);
            break;
        case AST_RETURN:
            free_ast(n->retstmt.expr);
            break;
        case AST_ARRAY:
            for (i = 0; i < n->arraylit.count; i++) free_ast(n->arraylit.items[i]);
            free(n->arraylit.items);
            break;
        case AST_INDEX:
            free_ast(n->index.array);
            free_ast(n->index.index);
            break;
        case AST_INDEX_ASSIGN:
            free_ast(n->indexassign.array);
            free_ast(n->indexassign.index);
            free_ast(n->indexassign.value);
            break;
        default:
            break;
    }
    free(n);
}

char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long sz;
    size_t rd;
    char *buf;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)xmalloc((size_t)sz + 1);
    rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);
    return buf;
}

int is_safe_relative_path(const char *arg) {
    const char *p;

    if (arg == NULL || *arg == '\0') return 0;
    if (arg[0] == '/') return 0;
    if (((arg[0] >= 'A' && arg[0] <= 'Z') || (arg[0] >= 'a' && arg[0] <= 'z')) && arg[1] == ':') {
        return 0;
    }

    p = arg;
    while (*p) {
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

int path_within_base(const char *path, const char *base) {
    size_t base_len = strlen(base);
    if (strncmp(path, base, base_len) != 0) return 0;
    return path[base_len] == '\0' || path[base_len] == '/';
}

int fullpath_buf(const char *path, char *out, size_t out_sz) {
#ifdef _WIN32
    return _fullpath(out, path, out_sz) != NULL;
#else
    (void)out_sz;
    return realpath(path, out) != NULL;
#endif
}

int resolve_input_path(const char *arg, char *dst, size_t dst_len) {
    char cwd[PATH_MAX];
    char resolved[PATH_MAX];

    if (!is_safe_relative_path(arg)) return 0;
    if (!splice_getcwd(cwd, sizeof(cwd))) return 0;
    if (!fullpath_buf(arg, resolved, sizeof(resolved))) return 0;
    if (!path_within_base(resolved, cwd)) return 0;
    if (snprintf(dst, dst_len, "%s", resolved) >= (int)dst_len) return 0;
    return 1;
}

int is_safe_filename(const char *name) {
    const char *p;

    if (name == NULL || *name == '\0') return 0;
    for (p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(c == '.' || c == '_' || c == '-' ||
              (c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) {
            return 0;
        }
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    return 1;
}

int path_in_list(char **list, int count, const char *path) {
    int i;
    for (i = 0; i < count; i++) {
#ifdef _WIN32
        if (_stricmp(list[i], path) == 0) return 1;
#else
        if (strcmp(list[i], path) == 0) return 1;
#endif
    }
    return 0;
}

void path_list_push(char ***list, int *count, int *cap, const char *path) {
    if (*count >= *cap) {
        *cap = *cap ? (*cap * 2) : 8;
        *list = (char **)xrealloc(*list, sizeof(char *) * (size_t)(*cap));
    }
    (*list)[*count] = xstrdup(path);
    (*count)++;
}

void path_list_pop(char **list, int *count) {
    if (*count <= 0) return;
    free(list[*count - 1]);
    (*count)--;
}

void path_list_free(char **list, int count) {
    int i;
    for (i = 0; i < count; i++) free(list[i]);
    free(list);
}
