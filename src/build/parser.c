#include "builder.h"

static TokVec *G;
static int P;

static Tok *peek(void) { return &G->data[P]; }
static int at(TokType t) { return peek()->t == t; }
static Tok *advance(void) { return &G->data[P++]; }
static int match(TokType t) { if (at(t)) { P++; return 1; } return 0; }

static void expect(TokType t, const char *msg) {
    if (!at(t)) {
        fprintf(stderr, "parse error line %d: %s\n", peek()->line, msg);
        exit(1);
    }
    P++;
}

static ASTNode *parse_expr(void);
static ASTNode *parse_stmt(void);
static ASTNode *parse_block(void);
static ASTNode *parse_if_stmt(void);

static int has_spl_extension(const char *path) {
    size_t n = strlen(path);
    return n >= 4 && strcmp(path + n - 4, ".spl") == 0;
}

static int has_c_extension(const char *path) {
    size_t n = strlen(path);
    return n >= 2 && strcmp(path + n - 2, ".c") == 0;
}

static void dirname_of(const char *path, char *out, size_t out_sz) {
    char *slash1;
    char *slash2;
    char *slash;

    if (snprintf(out, out_sz, "%s", path) >= (int)out_sz) die("spbuild: path too long");
    slash1 = strrchr(out, '/');
    slash2 = strrchr(out, '\\');
    slash = slash1;
    if (slash2 && (!slash1 || slash2 > slash1)) slash = slash2;
    if (!slash) {
        if (snprintf(out, out_sz, ".") >= (int)out_sz) die("spbuild: path too long");
        return;
    }
    if (slash == out) slash[1] = '\0';
    else *slash = '\0';
}

static int resolve_import_path(const char *raw, char *out, size_t out_sz) {
    char base_dir[PATH_MAX];
    char combined[PATH_MAX];
    char resolved[PATH_MAX];

    if (!is_safe_relative_path(raw)) return 0;
    if (!has_spl_extension(raw) && !has_c_extension(raw)) return 0;
    if (g_current_source_file[0] == '\0') return 0;

    dirname_of(g_current_source_file, base_dir, sizeof(base_dir));
    if (snprintf(combined, sizeof(combined), "%s/%s", base_dir, raw) >= (int)sizeof(combined)) {
        return 0;
    }
    if (!fullpath_buf(combined, resolved, sizeof(resolved))) return 0;
    if (!path_within_base(resolved, g_project_root)) return 0;
    if (snprintf(out, out_sz, "%s", resolved) >= (int)out_sz) return 0;
    return 1;
}

static ASTNode *parse_file_with_imports(const char *path) {
    char *src = read_file(path);
    TokVec tv = {0};
    TokVec *saved_G;
    int saved_P;
    char saved_file[PATH_MAX];
    ASTNode *root;

    if (!src) {
        fprintf(stderr, "spbuild: cannot read import %s\n", path);
        exit(1);
    }

    lex(src, &tv);

    saved_G = G;
    saved_P = P;
    if (snprintf(saved_file, sizeof(saved_file), "%s", g_current_source_file) >= (int)sizeof(saved_file)) {
        die("spbuild: path too long");
    }
    if (snprintf(g_current_source_file, sizeof(g_current_source_file), "%s", path) >= (int)sizeof(g_current_source_file)) {
        die("spbuild: path too long");
    }

    root = parse_program(&tv);

    if (snprintf(g_current_source_file, sizeof(g_current_source_file), "%s", saved_file) >= (int)sizeof(g_current_source_file)) {
        die("spbuild: path too long");
    }
    G = saved_G;
    P = saved_P;

    tv_free(&tv);
    free(src);
    return root;
}

static int is_index_assign_start(void) {
    int depth = 0;
    int i;

    if (!at(TK_IDENT)) return 0;
    if (G->data[P + 1].t != TK_LBRACKET) return 0;

    for (i = P + 1; i < G->count; i++) {
        TokType t = G->data[i].t;
        if (t == TK_LBRACKET) depth++;
        else if (t == TK_RBRACKET) {
            depth--;
            if (depth == 0) {
                if (i + 1 >= G->count) return 0;
                return G->data[i + 1].t == TK_ASSIGN;
            }
        }
        if (t == TK_EOF) break;
    }
    return 0;
}

static ASTNode *parse_primary(void) {
    if (match(TK_NUMBER)) return ast_number(G->data[P - 1].num);
    if (match(TK_STRING)) return ast_string(G->data[P - 1].lex ? G->data[P - 1].lex : "");
    if (match(TK_IDENT)) return ast_ident(G->data[P - 1].lex);
    if (match(TK_LPAREN)) {
        ASTNode *e = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        return e;
    }
    if (match(TK_LBRACKET)) {
        ASTNode **items = NULL;
        int count = 0;
        int cap = 0;

        if (!at(TK_RBRACKET)) {
            for (;;) {
                ASTNode *e = parse_expr();
                if (count >= cap) {
                    cap = cap ? cap * 2 : 8;
                    items = (ASTNode **)xrealloc(items, sizeof(ASTNode *) * (size_t)cap);
                }
                items[count++] = e;
                if (!match(TK_COMMA)) break;
            }
        }
        expect(TK_RBRACKET, "Expected ']'");
        return ast_array(items, count);
    }
    fprintf(stderr, "parse error line %d: expected primary\n", peek()->line);
    exit(1);
}

static ASTNode *parse_postfix(void) {
    ASTNode *e = parse_primary();

    for (;;) {
        if (match(TK_LPAREN)) {
            ASTNode **args = NULL;
            int count = 0;
            int cap = 0;
            const char *name;
            ASTNode *call;

            if (e->type != AST_IDENTIFIER) {
                fprintf(stderr, "parse error line %d: call target must be identifier\n", peek()->line);
                exit(1);
            }

            name = e->string;
            if (!at(TK_RPAREN)) {
                for (;;) {
                    ASTNode *a = parse_expr();
                    if (count >= cap) {
                        cap = cap ? cap * 2 : 4;
                        args = (ASTNode **)xrealloc(args, sizeof(ASTNode *) * (size_t)cap);
                    }
                    args[count++] = a;
                    if (!match(TK_COMMA)) break;
                }
            }
            expect(TK_RPAREN, "Expected ')' after call");
            call = ast_call(name, args, count);
            free_ast(e);
            e = call;
            continue;
        }

        if (match(TK_LBRACKET)) {
            ASTNode *idx = parse_expr();
            expect(TK_RBRACKET, "Expected ']'");
            e = ast_index(e, idx);
            continue;
        }

        break;
    }
    return e;
}

static ASTNode *parse_unary(void) {
    if (match(TK_MINUS)) return ast_binop("*", ast_number(-1.0), parse_unary());
    if (match(TK_NOT)) return ast_binop("!", parse_unary(), NULL);
    return parse_postfix();
}

static ASTNode *parse_mul(void) {
    ASTNode *l = parse_unary();
    while (at(TK_STAR) || at(TK_SLASH) || at(TK_MOD)) {
        TokType op = advance()->t;
        const char *s = (op == TK_STAR) ? "*" : (op == TK_SLASH) ? "/" : "%";
        ASTNode *r = parse_unary();
        l = ast_binop(s, l, r);
    }
    return l;
}

static ASTNode *parse_add(void) {
    ASTNode *l = parse_mul();
    while (at(TK_PLUS) || at(TK_MINUS)) {
        TokType op = advance()->t;
        const char *s = (op == TK_PLUS) ? "+" : "-";
        ASTNode *r = parse_mul();
        l = ast_binop(s, l, r);
    }
    return l;
}

static ASTNode *parse_cmp(void) {
    ASTNode *l = parse_add();
    while (at(TK_LT) || at(TK_GT) || at(TK_LE) || at(TK_GE) || at(TK_EQ) || at(TK_NEQ)) {
        TokType op = advance()->t;
        const char *s =
            (op == TK_LT) ? "<" :
            (op == TK_GT) ? ">" :
            (op == TK_LE) ? "<=" :
            (op == TK_GE) ? ">=" :
            (op == TK_EQ) ? "==" : "!=";
        ASTNode *r = parse_add();
        l = ast_binop(s, l, r);
    }
    return l;
}

static ASTNode *parse_logic(void) {
    ASTNode *l = parse_cmp();
    while (at(TK_AND) || at(TK_OR)) {
        TokType op = advance()->t;
        const char *s = (op == TK_AND) ? "&&" : "||";
        ASTNode *r = parse_cmp();
        l = ast_binop(s, l, r);
    }
    return l;
}

static ASTNode *parse_expr(void) {
    return parse_logic();
}

static void eat_separators(void) {
    while (match(TK_SEMI)) {}
}

static ASTNode *parse_stmt(void) {
    eat_separators();

    if (match(TK_LET)) {
        const char *name;
        expect(TK_IDENT, "Expected identifier after let");
        name = G->data[P - 1].lex;
        expect(TK_ASSIGN, "Expected '=' after let name");
        return ast_var(AST_LET, name, parse_expr());
    }

    if (match(TK_PRINT)) {
        ASTNode *e;
        expect(TK_LPAREN, "Expected '(' after print");
        e = parse_expr();
        expect(TK_RPAREN, "Expected ')' after print");
        return ast_print(e);
    }

    if (match(TK_BREAK)) return ast_new(AST_BREAK);
    if (match(TK_CONTINUE)) return ast_new(AST_CONTINUE);

    if (match(TK_RETURN)) {
        if (at(TK_SEMI) || at(TK_RBRACE) || at(TK_EOF)) return ast_return(NULL);
        return ast_return(parse_expr());
    }

    if (match(TK_IMPORT)) {
        const char *raw;
        char resolved[PATH_MAX];

        expect(TK_STRING, "Expected import path string");
        raw = G->data[P - 1].lex ? G->data[P - 1].lex : "";
        if (!resolve_import_path(raw, resolved, sizeof(resolved))) {
            fprintf(stderr, "parse error line %d: invalid import path '%s'\n", peek()->line, raw);
            exit(1);
        }

        if (has_c_extension(resolved)) {
            if (path_in_list(g_imported_files, g_imported_count, resolved)) {
                return ast_statements(NULL, 0);
            }
            path_list_push(&g_imported_files, &g_imported_count, &g_imported_cap, resolved);
            return ast_import_c(resolved);
        }

        if (path_in_list(g_import_stack, g_import_stack_count, resolved)) {
            fprintf(stderr, "parse error line %d: cyclic import '%s'\n", peek()->line, raw);
            exit(1);
        }
        if (path_in_list(g_imported_files, g_imported_count, resolved)) {
            return ast_statements(NULL, 0);
        }

        path_list_push(&g_imported_files, &g_imported_count, &g_imported_cap, resolved);
        path_list_push(&g_import_stack, &g_import_stack_count, &g_import_stack_cap, resolved);
        {
            ASTNode *imported = parse_file_with_imports(resolved);
            path_list_pop(g_import_stack, &g_import_stack_count);
            return imported;
        }
    }

    if (match(TK_FUNC)) {
        char **params = NULL;
        int count = 0;
        int cap = 0;
        ASTNode *body;
        const char *name;

        expect(TK_IDENT, "Expected function name");
        name = G->data[P - 1].lex;
        expect(TK_LPAREN, "Expected '(' after func name");
        if (!at(TK_RPAREN)) {
            for (;;) {
                expect(TK_IDENT, "Expected parameter name");
                if (count >= cap) {
                    cap = cap ? cap * 2 : 4;
                    params = (char **)xrealloc(params, sizeof(char *) * (size_t)cap);
                }
                params[count++] = xstrdup(G->data[P - 1].lex);
                if (!match(TK_COMMA)) break;
            }
        }
        expect(TK_RPAREN, "Expected ')' after parameters");
        body = parse_block();
        return ast_funcdef_params(name, params, count, body);
    }

    if (match(TK_IF)) return parse_if_stmt();

    if (match(TK_WHILE)) {
        ASTNode *cond;
        ASTNode *body;
        expect(TK_LPAREN, "Expected '(' after while");
        cond = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        body = parse_block();
        return ast_while(cond, body);
    }

    if (match(TK_FOR)) {
        int has_parens = match(TK_LPAREN);
        const char *var;
        ASTNode *start;
        ASTNode *end;
        ASTNode *body;

        expect(TK_IDENT, "Expected for variable name");
        var = G->data[P - 1].lex;
        expect(TK_IN, "Expected 'in' after for var");
        start = parse_expr();
        expect(TK_DOTDOT, "Expected '..' in for range");
        end = parse_expr();
        if (has_parens) expect(TK_RPAREN, "Expected ')' after for header");
        body = parse_block();
        return ast_for(var, start, end, body);
    }

    if (is_index_assign_start()) {
        const char *name = advance()->lex;
        ASTNode *idx;
        ASTNode *val;

        expect(TK_LBRACKET, "Expected '['");
        idx = parse_expr();
        expect(TK_RBRACKET, "Expected ']'");
        expect(TK_ASSIGN, "Expected '=' after index");
        val = parse_expr();
        return ast_index_assign(ast_ident(name), idx, val);
    }

    if (at(TK_IDENT) && G->data[P + 1].t == TK_ASSIGN) {
        const char *name = advance()->lex;
        advance();
        return ast_var(AST_ASSIGN, name, parse_expr());
    }

    return parse_expr();
}

static ASTNode *parse_if_stmt(void) {
    ASTNode *cond;
    ASTNode *thenb;
    ASTNode *elseb = NULL;

    expect(TK_LPAREN, "Expected '(' after if");
    cond = parse_expr();
    expect(TK_RPAREN, "Expected ')'");
    thenb = parse_block();

    if (match(TK_ELSE)) {
        eat_separators();
        if (match(TK_IF)) elseb = parse_if_stmt();
        else elseb = parse_block();
    }

    return ast_if(cond, thenb, elseb);
}

static ASTNode *parse_block(void) {
    ASTNode **stmts = NULL;
    int count = 0;
    int cap = 0;

    expect(TK_LBRACE, "Expected '{'");
    while (!at(TK_RBRACE) && !at(TK_EOF)) {
        ASTNode *s = parse_stmt();
        if (s) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 16;
                stmts = (ASTNode **)xrealloc(stmts, sizeof(ASTNode *) * (size_t)cap);
            }
            stmts[count++] = s;
        }
        eat_separators();
    }
    expect(TK_RBRACE, "Expected '}'");
    return ast_statements(stmts, count);
}

ASTNode *parse_program(TokVec *v) {
    ASTNode **stmts = NULL;
    int count = 0;
    int cap = 0;

    G = v;
    P = 0;

    while (!at(TK_EOF)) {
        ASTNode *s = parse_stmt();
        if (s) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 16;
                stmts = (ASTNode **)xrealloc(stmts, sizeof(ASTNode *) * (size_t)cap);
            }
            stmts[count++] = s;
        }
        eat_separators();
    }

    return ast_statements(stmts, count);
}
