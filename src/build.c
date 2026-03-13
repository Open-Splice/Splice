#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#ifdef _WIN32
#include <direct.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#define splice_getcwd _getcwd
#else
#define splice_getcwd getcwd
#endif

#include "opcode.h"

/* =========================================================
   This builder parses to AST, then emits linear RPN bytecode
   for the stack VM in splice.h.
   ========================================================= */

/* ===== SPC header ===== */
#define SPC_MAGIC "SPC\0"
#define SPC_VERSION 2

/* ===== AST types (MUST MATCH YOUR VM) ===== */
typedef enum {
    AST_NUMBER = 0,
    AST_STRING,
    AST_IDENTIFIER,
    AST_BINARY_OP,
    AST_LET,
    AST_ASSIGN,
    AST_BREAK,
    AST_PRINT,
    AST_CONTINUE,
    AST_WHILE,
    AST_IF,
    AST_STATEMENTS,
    AST_FUNC_DEF,
    AST_FUNCTION_CALL,
    AST_RETURN,
    AST_FOR,
    AST_ARRAY,
    AST_INDEX,
    AST_INDEX_ASSIGN,
    AST_IMPORT_C
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    union {
        double number;
        char *string;

        struct { char *op; ASTNode *left; ASTNode *right; } binop;
        struct { char *name; ASTNode *value; } var;
        struct { ASTNode *expr; } print;
        struct { ASTNode *cond; ASTNode *body; } whilestmt;
        struct { ASTNode *cond; ASTNode *then_b; ASTNode *else_b; } ifstmt;
        struct { ASTNode **stmts; int count; } statements;
        struct { char *name; char **params; int param_count; ASTNode *body; } funcdef;
        struct { char *name; ASTNode **args; int arg_count; } funccall;
        struct { ASTNode *expr; } retstmt;
        struct { char *var; ASTNode *start; ASTNode *end; ASTNode *body; } forstmt;
        struct { ASTNode **items; int count; } arraylit;
        struct { ASTNode *array; ASTNode *index; } index;
        struct { ASTNode *array; ASTNode *index; ASTNode *value; } indexassign;
    };
};

/* ===== utils ===== */
static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(1); }

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("spbuild: OOM");
    return p;
}
static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) die("spbuild: OOM");
    return q;
}
static char *xstrdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *p = (char*)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

/* ===== AST constructors ===== */
static ASTNode *ast_new(ASTNodeType t) {
    ASTNode *n = (ASTNode*)xmalloc(sizeof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->type = t;
    return n;
}

static ASTNode *ast_number(double v) { ASTNode *n=ast_new(AST_NUMBER); n->number=v; return n; }
static ASTNode *ast_string(const char *s) { ASTNode *n=ast_new(AST_STRING); n->string=xstrdup(s); return n; }
static ASTNode *ast_ident(const char *s) { ASTNode *n=ast_new(AST_IDENTIFIER); n->string=xstrdup(s); return n; }

static ASTNode *ast_binop(const char *op, ASTNode *l, ASTNode *r) {
    ASTNode *n = ast_new(AST_BINARY_OP);
    n->binop.op = xstrdup(op);
    n->binop.left = l;
    n->binop.right = r;
    return n;
}

static ASTNode *ast_print(ASTNode *e) { ASTNode *n=ast_new(AST_PRINT); n->print.expr=e; return n; }

static ASTNode *ast_var(ASTNodeType t, const char *name, ASTNode *val) {
    ASTNode *n = ast_new(t);
    n->var.name = xstrdup(name);
    n->var.value = val;
    return n;
}

static ASTNode *ast_statements(ASTNode **stmts, int count) {
    ASTNode *n=ast_new(AST_STATEMENTS);
    n->statements.stmts=stmts;
    n->statements.count=count;
    return n;
}

static ASTNode *ast_while(ASTNode *c, ASTNode *b) {
    ASTNode *n=ast_new(AST_WHILE);
    n->whilestmt.cond=c; n->whilestmt.body=b;
    return n;
}

static ASTNode *ast_if(ASTNode *c, ASTNode *t, ASTNode *e) {
    ASTNode *n=ast_new(AST_IF);
    n->ifstmt.cond=c; n->ifstmt.then_b=t; n->ifstmt.else_b=e;
    return n;
}

static ASTNode *ast_for(const char *v, ASTNode *s, ASTNode *e, ASTNode *b) {
    ASTNode *n=ast_new(AST_FOR);
    n->forstmt.var=xstrdup(v);
    n->forstmt.start=s; n->forstmt.end=e; n->forstmt.body=b;
    return n;
}

static ASTNode *ast_funcdef_params(const char *name, char **params, int param_count, ASTNode *body) {
    ASTNode *n=ast_new(AST_FUNC_DEF);
    n->funcdef.name=xstrdup(name);
    n->funcdef.params=params;
    n->funcdef.param_count=param_count;
    n->funcdef.body=body;
    return n;
}

static ASTNode *ast_call(const char *name, ASTNode **args, int arg_count) {
    ASTNode *n=ast_new(AST_FUNCTION_CALL);
    n->funccall.name=xstrdup(name);
    n->funccall.args=args;
    n->funccall.arg_count=arg_count;
    return n;
}

static ASTNode *ast_return(ASTNode *e) { ASTNode *n=ast_new(AST_RETURN); n->retstmt.expr=e; return n; }

static ASTNode *ast_array(ASTNode **items, int count) {
    ASTNode *n=ast_new(AST_ARRAY);
    n->arraylit.items=items;
    n->arraylit.count=count;
    return n;
}

static ASTNode *ast_index(ASTNode *arr, ASTNode *idx) {
    ASTNode *n=ast_new(AST_INDEX);
    n->index.array=arr;
    n->index.index=idx;
    return n;
}

static ASTNode *ast_index_assign(ASTNode *arr, ASTNode *idx, ASTNode *val) {
    ASTNode *n=ast_new(AST_INDEX_ASSIGN);
    n->indexassign.array=arr;
    n->indexassign.index=idx;
    n->indexassign.value=val;
    return n;
}

static ASTNode *ast_import_c(const char *path) {
    ASTNode *n = ast_new(AST_IMPORT_C);
    n->string = xstrdup(path);
    return n;
}

/* ===== free AST (builder side) ===== */
static void free_ast(ASTNode *n) {
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
            for (int i=0;i<n->statements.count;i++) free_ast(n->statements.stmts[i]);
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
            for (int i=0;i<n->funcdef.param_count;i++) free(n->funcdef.params[i]);
            free(n->funcdef.params);
            free_ast(n->funcdef.body);
            break;
        case AST_FUNCTION_CALL:
            free(n->funccall.name);
            for (int i=0;i<n->funccall.arg_count;i++) free_ast(n->funccall.args[i]);
            free(n->funccall.args);
            break;
        case AST_RETURN:
            free_ast(n->retstmt.expr);
            break;
        case AST_ARRAY:
            for (int i=0;i<n->arraylit.count;i++) free_ast(n->arraylit.items[i]);
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

/* =========================================================
   OPTIMIZER (builder-side, safe constant folding + DCE)
   ========================================================= */

static int is_number_lit(ASTNode *n, double *out) {
    if (n && n->type == AST_NUMBER) {
        if (out) *out = n->number;
        return 1;
    }
    return 0;
}

static int is_numeric_literal(ASTNode *n, double *out) {
    if (!n) return 0;
    if (n->type == AST_NUMBER) {
        if (out) *out = n->number;
        return 1;
    }
    if (n->type == AST_STRING) {
        if (out) *out = 0.0;
        return 1;
    }
    return 0;
}

static int is_string_lit(ASTNode *n, const char **out) {
    if (n && n->type == AST_STRING) {
        if (out) *out = n->string;
        return 1;
    }
    return 0;
}

static int literal_truthy(ASTNode *n, int *out) {
    if (!n || !out) return 0;
    if (n->type == AST_NUMBER) {
        *out = (n->number != 0.0);
        return 1;
    }
    if (n->type == AST_STRING) {
        *out = n->string && n->string[0] != '\0';
        return 1;
    }
    return 0;
}

static int is_pure_expr(ASTNode *n) {
    if (!n) return 1;
    switch (n->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_IDENTIFIER:
            return 1;
        case AST_BINARY_OP:
            return is_pure_expr(n->binop.left) && is_pure_expr(n->binop.right);
        case AST_ARRAY:
            for (int i = 0; i < n->arraylit.count; i++) {
                if (!is_pure_expr(n->arraylit.items[i])) return 0;
            }
            return 1;
        case AST_INDEX:
            return is_pure_expr(n->index.array) && is_pure_expr(n->index.index);
        default:
            return 0;
    }
}

static int is_noop_stmt(ASTNode *n) {
    if (!n) return 1;
    switch (n->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_IDENTIFIER:
            return 1;
        case AST_BINARY_OP:
        case AST_ARRAY:
        case AST_INDEX:
            return is_pure_expr(n);
        case AST_STATEMENTS:
            return n->statements.count == 0;
        default:
            return 0;
    }
}

static ASTNode *replace_with_number(ASTNode *n, double out) {
    ASTNode *r = ast_number(out);
    free_ast(n);
    return r;
}

static ASTNode *replace_with_string(ASTNode *n, const char *s) {
    ASTNode *r = ast_string(s);
    free_ast(n);
    return r;
}

static ASTNode *take_left_binary(ASTNode *n) {
    ASTNode *l = n->binop.left;
    ASTNode *r = n->binop.right;
    n->binop.left = NULL;
    n->binop.right = NULL;
    free_ast(r);
    free(n->binop.op);
    free(n);
    return l;
}

static ASTNode *take_right_binary(ASTNode *n) {
    ASTNode *l = n->binop.left;
    ASTNode *r = n->binop.right;
    n->binop.left = NULL;
    n->binop.right = NULL;
    free_ast(l);
    free(n->binop.op);
    free(n);
    return r;
}

static ASTNode *optimize_node(ASTNode *n);

static ASTNode *optimize_statements(ASTNode *n) {
    int out_count = 0;
    int out_cap = n->statements.count > 0 ? n->statements.count : 4;
    ASTNode **out = (ASTNode **)xmalloc(sizeof(ASTNode *) * (size_t)out_cap);

    for (int i = 0; i < n->statements.count; i++) {
        ASTNode *s = optimize_node(n->statements.stmts[i]);

        if (s && s->type == AST_STATEMENTS) {
            if (s->statements.count > 0) {
                for (int j = 0; j < s->statements.count; j++) {
                    if (out_count >= out_cap) {
                        out_cap *= 2;
                        out = (ASTNode **)xrealloc(out, sizeof(ASTNode *) * (size_t)out_cap);
                    }
                    out[out_count++] = s->statements.stmts[j];
                }
            }
            free(s->statements.stmts);
            free(s);
            s = NULL;
        }

        if (s && is_noop_stmt(s)) {
            free_ast(s);
            s = NULL;
        }

        if (s) {
            if (out_count >= out_cap) {
                out_cap *= 2;
                out = (ASTNode **)xrealloc(out, sizeof(ASTNode *) * (size_t)out_cap);
            }
            out[out_count++] = s;

            if (s->type == AST_RETURN || s->type == AST_BREAK || s->type == AST_CONTINUE) {
                for (int j = i + 1; j < n->statements.count; j++) {
                    free_ast(n->statements.stmts[j]);
                }
                break;
            }
        }
    }

    free(n->statements.stmts);
    free(n);

    if (out_count == 0) {
        free(out);
        return ast_statements(NULL, 0);
    }
    if (out_count == 1) {
        ASTNode *single = out[0];
        free(out);
        return single;
    }
    return ast_statements(out, out_count);
}

static ASTNode *optimize_node(ASTNode *n) {
    if (!n) return NULL;

    switch (n->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_IDENTIFIER:
        case AST_BREAK:
        case AST_CONTINUE:
        case AST_IMPORT_C:
            return n;

        case AST_BINARY_OP: {
            n->binop.left = optimize_node(n->binop.left);
            n->binop.right = optimize_node(n->binop.right);

            double a, b;
            const char *sa = NULL;
            const char *sb = NULL;
            int lnum = is_number_lit(n->binop.left, &a);
            int rnum = is_number_lit(n->binop.right, &b);
            int lstr = is_string_lit(n->binop.left, &sa);
            int rstr = is_string_lit(n->binop.right, &sb);
            int ltrue = 0, rtrue = 0;
            int lbool = literal_truthy(n->binop.left, &ltrue);
            int rbool = literal_truthy(n->binop.right, &rtrue);
            const char *op = n->binop.op ? n->binop.op : "";

            if (!strcmp(op, "!") && n->binop.right == NULL && lbool) {
                return replace_with_number(n, ltrue ? 0.0 : 1.0);
            }

            if (!strcmp(op, "+") && lstr && rstr) {
                size_t la = strlen(sa);
                size_t lb = strlen(sb);
                char *buf = (char *)xmalloc(la + lb + 1);
                memcpy(buf, sa, la);
                memcpy(buf + la, sb, lb);
                buf[la + lb] = 0;
                ASTNode *r = replace_with_string(n, buf);
                free(buf);
                return r;
            }

            if ((lnum || lstr) && (rnum || rstr)) {
                double la = lnum ? a : 0.0;
                double rb = rnum ? b : 0.0;
                double out;

                if (!strcmp(op, "+")) out = la + rb;
                else if (!strcmp(op, "-")) out = la - rb;
                else if (!strcmp(op, "*")) out = la * rb;
                else if (!strcmp(op, "/")) out = la / rb;
                else if (!strcmp(op, "%")) {
                    if ((int)rb == 0) return n;
                    out = (double)((int)la % (int)rb);
                }
                else if (!strcmp(op, "<")) out = la < rb ? 1.0 : 0.0;
                else if (!strcmp(op, ">")) out = la > rb ? 1.0 : 0.0;
                else if (!strcmp(op, "<=")) out = la <= rb ? 1.0 : 0.0;
                else if (!strcmp(op, ">=")) out = la >= rb ? 1.0 : 0.0;
                else if (!strcmp(op, "==")) out = la == rb ? 1.0 : 0.0;
                else if (!strcmp(op, "!=")) out = la != rb ? 1.0 : 0.0;
                else if (!strcmp(op, "&&")) out = (la != 0.0 && rb != 0.0) ? 1.0 : 0.0;
                else if (!strcmp(op, "||")) out = (la != 0.0 || rb != 0.0) ? 1.0 : 0.0;
                else out = 0.0;

                return replace_with_number(n, out);
            }

            if ((lstr && rstr) && (!strcmp(op, "==") || !strcmp(op, "!="))) {
                int eq = strcmp(sa, sb) == 0;
                return replace_with_number(n, (!strcmp(op, "==") ? eq : !eq) ? 1.0 : 0.0);
            }

            if (lnum && !strcmp(op, "+") && a == 0.0 && is_pure_expr(n->binop.right)) return take_right_binary(n);
            if (rnum && !strcmp(op, "+") && b == 0.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);

            if (rnum && !strcmp(op, "-") && b == 0.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);

            if (lnum && !strcmp(op, "*") && a == 1.0 && is_pure_expr(n->binop.right)) return take_right_binary(n);
            if (rnum && !strcmp(op, "*") && b == 1.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);
            if (lnum && !strcmp(op, "*") && a == 0.0 && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);
            if (rnum && !strcmp(op, "*") && b == 0.0 && is_pure_expr(n->binop.left)) return replace_with_number(n, 0.0);

            if (rnum && !strcmp(op, "/") && b == 1.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);
            if (lnum && !strcmp(op, "/") && a == 0.0 && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);

            if (rnum && !strcmp(op, "%") && b == 1.0 && is_pure_expr(n->binop.left)) return replace_with_number(n, 0.0);
            if (lnum && !strcmp(op, "%") && a == 0.0 && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);

            if (lbool && !strcmp(op, "&&") && !ltrue && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);
            if (rbool && !strcmp(op, "&&") && !rtrue && is_pure_expr(n->binop.left)) return replace_with_number(n, 0.0);
            if (lbool && !strcmp(op, "||") && ltrue && is_pure_expr(n->binop.right)) return replace_with_number(n, 1.0);
            if (rbool && !strcmp(op, "||") && rtrue && is_pure_expr(n->binop.left)) return replace_with_number(n, 1.0);

            if (!strcmp(op, "==") && n->binop.left && n->binop.right &&
                n->binop.left->type == AST_STRING && n->binop.right->type == AST_STRING &&
                strcmp(n->binop.left->string, n->binop.right->string) == 0) {
                return replace_with_number(n, 1.0);
            }
            if (!strcmp(op, "!=") && n->binop.left && n->binop.right &&
                n->binop.left->type == AST_STRING && n->binop.right->type == AST_STRING &&
                strcmp(n->binop.left->string, n->binop.right->string) == 0) {
                return replace_with_number(n, 0.0);
            }
            return n;
        }

        case AST_PRINT:
            n->print.expr = optimize_node(n->print.expr);
            return n;

        case AST_LET:
        case AST_ASSIGN:
            n->var.value = optimize_node(n->var.value);
            return n;

        case AST_STATEMENTS:
            return optimize_statements(n);

        case AST_WHILE:
            n->whilestmt.cond = optimize_node(n->whilestmt.cond);
            n->whilestmt.body = optimize_node(n->whilestmt.body);
            {
                double cval;
                if (is_numeric_literal(n->whilestmt.cond, &cval) && cval == 0.0) {
                    free_ast(n);
                    return ast_statements(NULL, 0);
                }
            }
            return n;

        case AST_IF: {
            n->ifstmt.cond = optimize_node(n->ifstmt.cond);
            n->ifstmt.then_b = optimize_node(n->ifstmt.then_b);
            n->ifstmt.else_b = optimize_node(n->ifstmt.else_b);
            {
                double cval;
                if (is_numeric_literal(n->ifstmt.cond, &cval)) {
                    ASTNode *chosen = cval != 0.0
                    ? n->ifstmt.then_b
                    : (n->ifstmt.else_b ? n->ifstmt.else_b : ast_statements(NULL, 0));
                    if (n->ifstmt.cond) free_ast(n->ifstmt.cond);
                    if (chosen != n->ifstmt.then_b) free_ast(n->ifstmt.then_b);
                    if (chosen != n->ifstmt.else_b) free_ast(n->ifstmt.else_b);
                    free(n);
                    return chosen;
                }
            }
            return n;
        }

        case AST_FOR: {
            n->forstmt.start = optimize_node(n->forstmt.start);
            n->forstmt.end = optimize_node(n->forstmt.end);
            n->forstmt.body = optimize_node(n->forstmt.body);
            double s, e;
            if (is_numeric_literal(n->forstmt.start, &s) && is_numeric_literal(n->forstmt.end, &e)) {
                if ((int)s > (int)e) {
                    free_ast(n);
                    return ast_statements(NULL, 0);
                }
            }
            return n;
        }

        case AST_FUNC_DEF:
            n->funcdef.body = optimize_node(n->funcdef.body);
            return n;

        case AST_FUNCTION_CALL:
            for (int i = 0; i < n->funccall.arg_count; i++) {
                n->funccall.args[i] = optimize_node(n->funccall.args[i]);
            }
            return n;

        case AST_RETURN:
            n->retstmt.expr = optimize_node(n->retstmt.expr);
            return n;

        case AST_ARRAY:
            for (int i = 0; i < n->arraylit.count; i++) {
                n->arraylit.items[i] = optimize_node(n->arraylit.items[i]);
            }
            return n;

        case AST_INDEX:
            n->index.array = optimize_node(n->index.array);
            n->index.index = optimize_node(n->index.index);
            return n;

        case AST_INDEX_ASSIGN:
            n->indexassign.array = optimize_node(n->indexassign.array);
            n->indexassign.index = optimize_node(n->indexassign.index);
            n->indexassign.value = optimize_node(n->indexassign.value);
            return n;

        default:
            return n;
    }
}

/* =========================================================
   LEXER
   ========================================================= */

typedef enum {
    TK_EOF=0,
    TK_IDENT, TK_NUMBER, TK_STRING,

    TK_LET, TK_PRINT, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_IN,
    TK_FUNC, TK_RETURN, TK_BREAK, TK_CONTINUE, TK_IMPORT,

    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_COMMA, TK_SEMI,
    TK_ASSIGN,

    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_MOD,
    TK_LT, TK_GT, TK_LE, TK_GE, TK_EQ, TK_NEQ,
    TK_AND, TK_OR, TK_NOT,
    TK_DOTDOT
} TokType;

typedef struct {
    TokType t;
    char *lex;
    double num;
    int line;
} Tok;

typedef struct {
    Tok *data;
    int count;
    int cap;
} TokVec;

static void tv_push(TokVec *v, Tok t) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap*2 : 256;
        v->data = (Tok*)xrealloc(v->data, sizeof(Tok)*(size_t)v->cap);
    }
    v->data[v->count++] = t;
}

static TokType kw_type(const char *id) {
    if (!strcmp(id,"let")) return TK_LET;
    if (!strcmp(id,"print")) return TK_PRINT;
    if (!strcmp(id,"if")) return TK_IF;
    if (!strcmp(id,"else")) return TK_ELSE;
    if (!strcmp(id,"while")) return TK_WHILE;
    if (!strcmp(id,"for")) return TK_FOR;
    if (!strcmp(id,"in")) return TK_IN;
    if (!strcmp(id,"func")) return TK_FUNC;
    if (!strcmp(id,"return")) return TK_RETURN;
    if (!strcmp(id,"break")) return TK_BREAK;
    if (!strcmp(id,"continue")) return TK_CONTINUE;
    if (!strcmp(id,"import")) return TK_IMPORT;
    if (!strcmp(id,"not")) return TK_NOT;
    if (!strcmp(id,"and")) return TK_AND;
    if (!strcmp(id,"or")) return TK_OR;
    return TK_IDENT;
}

static void lex(const char *src, TokVec *out) {
    int line = 1;
    const char *p = src;

    while (*p) {
        if (*p == '\n') { line++; p++; tv_push(out,(Tok){.t=TK_SEMI,.line=line}); continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }

        if (p[0]=='/' && p[1]=='/') { while (*p && *p!='\n') p++; continue; }

        if (*p=='"') {
            p++;
            const char *s = p;
            while (*p && *p!='"') {
                if (*p=='\\' && p[1]) p++; /* skip escaped char */
                p++;
            }
            size_t n = (size_t)(p - s);
            char *buf = (char*)xmalloc(n+1);
            /* simple unescape for \" and \\ and \n */
            size_t j=0;
            for (size_t i=0;i<n;i++) {
                char c = s[i];
                if (c=='\\' && i+1<n) {
                    char d = s[++i];
                    if (d=='n') buf[j++]='\n';
                    else buf[j++]=d;
                } else buf[j++]=c;
            }
            buf[j]=0;
            tv_push(out,(Tok){.t=TK_STRING,.lex=buf,.line=line});
            if (*p=='"') p++;
            continue;
        }

        if (isdigit((unsigned char)*p) || (*p=='.' && isdigit((unsigned char)p[1]))) {
            const char *s = p;
            while (isdigit((unsigned char)*p)) p++;
            if (*p=='.' && p[1] != '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            char tmp[128];
            size_t n = (size_t)(p - s);
            if (n >= sizeof(tmp)) die("number too long");
            memcpy(tmp,s,n); tmp[n]=0;
            tv_push(out,(Tok){.t=TK_NUMBER,.num=strtod(tmp,NULL),.line=line});
            continue;
        }

        if (isalpha((unsigned char)*p) || *p=='_') {
            const char *s=p;
            while (isalnum((unsigned char)*p) || *p=='_') p++;
            size_t n=(size_t)(p-s);
            char *id=(char*)xmalloc(n+1);
            memcpy(id,s,n); id[n]=0;
            TokType t = kw_type(id);
            if (t==TK_IDENT) tv_push(out,(Tok){.t=t,.lex=id,.line=line});
            else { free(id); tv_push(out,(Tok){.t=t,.line=line}); }
            continue;
        }

        /* two-char ops */
        if (p[0]=='=' && p[1]=='=') { tv_push(out,(Tok){.t=TK_EQ,.line=line}); p+=2; continue; }
        if (p[0]=='!' && p[1]=='=') { tv_push(out,(Tok){.t=TK_NEQ,.line=line}); p+=2; continue; }
        if (p[0]=='<' && p[1]=='=') { tv_push(out,(Tok){.t=TK_LE,.line=line}); p+=2; continue; }
        if (p[0]=='>' && p[1]=='=') { tv_push(out,(Tok){.t=TK_GE,.line=line}); p+=2; continue; }
        if (p[0]=='&' && p[1]=='&') { tv_push(out,(Tok){.t=TK_AND,.line=line}); p+=2; continue; }
        if (p[0]=='|' && p[1]=='|') { tv_push(out,(Tok){.t=TK_OR,.line=line}); p+=2; continue; }
        if (p[0]=='.' && p[1]=='.') { tv_push(out,(Tok){.t=TK_DOTDOT,.line=line}); p+=2; continue; }

        /* single char */
        switch (*p) {
            case '(': tv_push(out,(Tok){.t=TK_LPAREN,.line=line}); p++; break;
            case ')': tv_push(out,(Tok){.t=TK_RPAREN,.line=line}); p++; break;
            case '{': tv_push(out,(Tok){.t=TK_LBRACE,.line=line}); p++; break;
            case '}': tv_push(out,(Tok){.t=TK_RBRACE,.line=line}); p++; break;
            case '[': tv_push(out,(Tok){.t=TK_LBRACKET,.line=line}); p++; break;
            case ']': tv_push(out,(Tok){.t=TK_RBRACKET,.line=line}); p++; break;
            case ',': tv_push(out,(Tok){.t=TK_COMMA,.line=line}); p++; break;
            case ';': tv_push(out,(Tok){.t=TK_SEMI,.line=line}); p++; break;
            case '=': tv_push(out,(Tok){.t=TK_ASSIGN,.line=line}); p++; break;
            case '+': tv_push(out,(Tok){.t=TK_PLUS,.line=line}); p++; break;
            case '-': tv_push(out,(Tok){.t=TK_MINUS,.line=line}); p++; break;
            case '*': tv_push(out,(Tok){.t=TK_STAR,.line=line}); p++; break;
            case '/': tv_push(out,(Tok){.t=TK_SLASH,.line=line}); p++; break;
            case '%': tv_push(out,(Tok){.t=TK_MOD,.line=line}); p++; break;
            case '<': tv_push(out,(Tok){.t=TK_LT,.line=line}); p++; break;
            case '>': tv_push(out,(Tok){.t=TK_GT,.line=line}); p++; break;
            case '!': tv_push(out,(Tok){.t=TK_NOT,.line=line}); p++; break;
            default:
                fprintf(stderr,"lexer: unknown char '%c' at line %d\n", *p, line);
                exit(1);
        }
    }

    tv_push(out,(Tok){.t=TK_EOF,.line=line});
}

static void tv_free(TokVec *v) {
    for (int i=0;i<v->count;i++) free(v->data[i].lex);
    free(v->data);
}

/* =========================================================
   PARSER (recursive descent)
   ========================================================= */

static char *read_file(const char *path);
static int is_safe_relative_path(const char *arg);
static int path_within_base(const char *path, const char *base);
static int fullpath_buf(const char *path, char *out, size_t out_sz);

static char g_project_root[PATH_MAX];
static char g_current_source_file[PATH_MAX];
static char **g_imported_files = NULL;
static int g_imported_count = 0;
static int g_imported_cap = 0;
static char **g_import_stack = NULL;
static int g_import_stack_count = 0;
static int g_import_stack_cap = 0;

static TokVec *G;
static int P;

static Tok *peek(void) { return &G->data[P]; }
static int at(TokType t) { return peek()->t == t; }
static Tok *advance(void) { return &G->data[P++]; }
static int match(TokType t) { if (at(t)) { P++; return 1; } return 0; }

static void expect(TokType t, const char *msg) {
    if (!at(t)) {
        fprintf(stderr,"parse error line %d: %s\n", peek()->line, msg);
        exit(1);
    }
    P++;
}

static ASTNode *parse_expr(void);
static ASTNode *parse_stmt(void);
static ASTNode *parse_block(void);
static ASTNode *parse_program(TokVec *v);
static ASTNode *parse_if_stmt(void);

static int has_spl_extension(const char *path) {
    size_t n = strlen(path);
    return n >= 4 && strcmp(path + n - 4, ".spl") == 0;
}

static int has_c_extension(const char *path) {
    size_t n = strlen(path);
    return n >= 2 && strcmp(path + n - 2, ".c") == 0;
}

static int path_equals(const char *a, const char *b) {
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

static int path_in_list(char **list, int count, const char *path) {
    for (int i = 0; i < count; i++) {
        if (path_equals(list[i], path)) return 1;
    }
    return 0;
}

static void path_list_push(char ***list, int *count, int *cap, const char *path) {
    if (*count >= *cap) {
        *cap = *cap ? (*cap * 2) : 8;
        *list = (char **)xrealloc(*list, sizeof(char *) * (size_t)(*cap));
    }
    (*list)[*count] = xstrdup(path);
    (*count)++;
}

static void path_list_pop(char **list, int *count) {
    if (*count <= 0) return;
    free(list[*count - 1]);
    (*count)--;
}

static void path_list_free(char **list, int count) {
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

static void dirname_of(const char *path, char *out, size_t out_sz) {
    if (snprintf(out, out_sz, "%s", path) >= (int)out_sz) {
        die("spbuild: path too long");
    }
    char *slash1 = strrchr(out, '/');
    char *slash2 = strrchr(out, '\\');
    char *slash = slash1;
    if (slash2 && (!slash1 || slash2 > slash1)) slash = slash2;
    if (!slash) {
        if (snprintf(out, out_sz, ".") >= (int)out_sz) {
            die("spbuild: path too long");
        }
        return;
    }
    if (slash == out) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
}

static int resolve_import_path(const char *raw, char *out, size_t out_sz) {
    if (!is_safe_relative_path(raw)) return 0;
    if (!has_spl_extension(raw) && !has_c_extension(raw)) return 0;
    if (g_current_source_file[0] == '\0') return 0;

    char base_dir[PATH_MAX];
    char combined[PATH_MAX];
    char resolved[PATH_MAX];

    dirname_of(g_current_source_file, base_dir, sizeof(base_dir));
    if (snprintf(combined, sizeof(combined), "%s/%s", base_dir, raw) >= (int)sizeof(combined)) {
        return 0;
    }
    if (!fullpath_buf(combined, resolved, sizeof(resolved))) {
        return 0;
    }
    if (!path_within_base(resolved, g_project_root)) {
        return 0;
    }
    if (snprintf(out, out_sz, "%s", resolved) >= (int)out_sz) {
        return 0;
    }
    return 1;
}

static ASTNode *parse_file_with_imports(const char *path) {
    char *src = read_file(path);
    if (!src) {
        fprintf(stderr, "spbuild: cannot read import %s\n", path);
        exit(1);
    }

    TokVec tv = {0};
    lex(src, &tv);

    TokVec *saved_G = G;
    int saved_P = P;
    char saved_file[PATH_MAX];
    if (snprintf(saved_file, sizeof(saved_file), "%s", g_current_source_file) >= (int)sizeof(saved_file)) {
        die("spbuild: path too long");
    }
    if (snprintf(g_current_source_file, sizeof(g_current_source_file), "%s", path) >= (int)sizeof(g_current_source_file)) {
        die("spbuild: path too long");
    }

    ASTNode *root = parse_program(&tv);

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
    if (!at(TK_IDENT)) return 0;
    if (G->data[P+1].t != TK_LBRACKET) return 0;
    int depth = 0;
    for (int i = P + 1; i < G->count; i++) {
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

/* primary: number|string|ident|call| (expr) */
static ASTNode *parse_primary(void) {
    if (match(TK_NUMBER)) {
        return ast_number(G->data[P-1].num);
    }
    if (match(TK_STRING)) {
        return ast_string(G->data[P-1].lex ? G->data[P-1].lex : "");
    }
    if (match(TK_IDENT)) {
        const char *name = G->data[P-1].lex;
        return ast_ident(name);
    }
    if (match(TK_LPAREN)) {
        ASTNode *e = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        return e;
    }
    if (match(TK_LBRACKET)) {
        ASTNode **items = NULL;
        int count = 0, cap = 0;
        if (!at(TK_RBRACKET)) {
            for (;;) {
                ASTNode *e = parse_expr();
                if (count >= cap) {
                    cap = cap ? cap*2 : 8;
                    items = (ASTNode**)xrealloc(items, sizeof(ASTNode*)*(size_t)cap);
                }
                items[count++] = e;
                if (!match(TK_COMMA)) break;
            }
        }
        expect(TK_RBRACKET, "Expected ']'");
        return ast_array(items, count);
    }
    fprintf(stderr,"parse error line %d: expected primary\n", peek()->line);
    exit(1);
}

static ASTNode *parse_postfix(void) {
    ASTNode *e = parse_primary();
    for (;;) {
        if (match(TK_LPAREN)) {
            if (e->type != AST_IDENTIFIER) {
                fprintf(stderr,"parse error line %d: call target must be identifier\n", peek()->line);
                exit(1);
            }
            const char *name = e->string;
            ASTNode **args = NULL;
            int count = 0, cap = 0;
            if (!at(TK_RPAREN)) {
                for (;;) {
                    ASTNode *a = parse_expr();
                    if (count >= cap) {
                        cap = cap ? cap*2 : 4;
                        args = (ASTNode**)xrealloc(args, sizeof(ASTNode*)*(size_t)cap);
                    }
                    args[count++] = a;
                    if (!match(TK_COMMA)) break;
                }
            }
            expect(TK_RPAREN, "Expected ')' after call");
            {
                ASTNode *call = ast_call(name, args, count);
                free_ast(e);
                e = call;
            }
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

/* unary: -x or !x */
static ASTNode *parse_unary(void) {
    if (match(TK_MINUS)) {
        /* -(x) -> (-1 * x) */
        return ast_binop("*", ast_number(-1.0), parse_unary());
    }
    if (match(TK_NOT)) {
        /* !x -> (0 == x) is not correct; but your VM has no boolean type.
           We'll implement !x as (x == 0) using binary op "!"? No.
           We'll encode op "!" and expect VM to support it if you added it.
           If VM doesn't support "!", remove this feature.
        */
        return ast_binop("!", parse_unary(), NULL);
    }
    return parse_postfix();
}

/* mul: * / % */
static ASTNode *parse_mul(void) {
    ASTNode *l = parse_unary();
    while (at(TK_STAR) || at(TK_SLASH) || at(TK_MOD)) {
        TokType op = advance()->t;
        const char *s = (op==TK_STAR)?"*":(op==TK_SLASH)?"/":"%";
        ASTNode *r = parse_unary();
        l = ast_binop(s, l, r);
    }
    return l;
}

/* add: + - */
static ASTNode *parse_add(void) {
    ASTNode *l = parse_mul();
    while (at(TK_PLUS) || at(TK_MINUS)) {
        TokType op = advance()->t;
        const char *s = (op==TK_PLUS)?"+":"-";
        ASTNode *r = parse_mul();
        l = ast_binop(s, l, r);
    }
    return l;
}

/* cmp: < > <= >= == != */
static ASTNode *parse_cmp(void) {
    ASTNode *l = parse_add();
    while (at(TK_LT)||at(TK_GT)||at(TK_LE)||at(TK_GE)||at(TK_EQ)||at(TK_NEQ)) {
        TokType op = advance()->t;
        const char *s =
            (op==TK_LT)?"<":(op==TK_GT)?">":(op==TK_LE)?"<=":(op==TK_GE)?">=":(op==TK_EQ)?"==":"!=";
        ASTNode *r = parse_add();
        l = ast_binop(s, l, r);
    }
    return l;
}

/* logic: && || (encoded as "&&" "||") */
static ASTNode *parse_logic(void) {
    ASTNode *l = parse_cmp();
    while (at(TK_AND) || at(TK_OR)) {
        TokType op = advance()->t;
        const char *s = (op==TK_AND)?"&&":"||";
        ASTNode *r = parse_cmp();
        l = ast_binop(s, l, r);
    }
    return l;
}

static ASTNode *parse_expr(void) { return parse_logic(); }

/* statement separators: many semis/newlines are ok */
static void eat_separators(void) {
    while (match(TK_SEMI)) {}
}

static ASTNode *parse_stmt(void) {
    eat_separators();

    if (match(TK_LET)) {
        expect(TK_IDENT, "Expected identifier after let");
        const char *name = G->data[P-1].lex;
        expect(TK_ASSIGN, "Expected '=' after let name");
        ASTNode *rhs = parse_expr();
        return ast_var(AST_LET, name, rhs);
    }

    if (match(TK_PRINT)) {
        expect(TK_LPAREN, "Expected '(' after print");
        ASTNode *e = parse_expr();
        expect(TK_RPAREN, "Expected ')' after print");
        return ast_print(e);
    }

    if (match(TK_BREAK)) return ast_new(AST_BREAK);
    if (match(TK_CONTINUE)) return ast_new(AST_CONTINUE);

    if (match(TK_RETURN)) {
        /* return expr; (expr optional) */
        if (at(TK_SEMI) || at(TK_RBRACE) || at(TK_EOF)) return ast_return(NULL);
        return ast_return(parse_expr());
    }

    if (match(TK_IMPORT)) {
        expect(TK_STRING, "Expected import path string");
        const char *raw = G->data[P-1].lex ? G->data[P-1].lex : "";
        char resolved[PATH_MAX];
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
        ASTNode *imported = parse_file_with_imports(resolved);
        path_list_pop(g_import_stack, &g_import_stack_count);
        return imported;
    }

    if (match(TK_FUNC)) {
        expect(TK_IDENT, "Expected function name");
        const char *name = G->data[P-1].lex;
        expect(TK_LPAREN, "Expected '(' after func name");
        char **params = NULL;
        int count = 0, cap = 0;
        if (!at(TK_RPAREN)) {
            for (;;) {
                expect(TK_IDENT, "Expected parameter name");
                if (count >= cap) {
                    cap = cap ? cap*2 : 4;
                    params = (char**)xrealloc(params, sizeof(char*)*(size_t)cap);
                }
                params[count++] = xstrdup(G->data[P-1].lex);
                if (!match(TK_COMMA)) break;
            }
        }
        expect(TK_RPAREN, "Expected ')' after parameters");
        ASTNode *body = parse_block();
        return ast_funcdef_params(name, params, count, body);
    }

    if (match(TK_IF)) {
        return parse_if_stmt();
    }

    if (match(TK_WHILE)) {
        expect(TK_LPAREN, "Expected '(' after while");
        ASTNode *cond = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        ASTNode *body = parse_block();
        return ast_while(cond, body);
    }

    if (match(TK_FOR)) {
        int has_parens = match(TK_LPAREN);
        expect(TK_IDENT, "Expected for variable name");
        const char *var = G->data[P-1].lex;
        expect(TK_IN, "Expected 'in' after for var");
        ASTNode *start = parse_expr();
        expect(TK_DOTDOT, "Expected '..' in for range");
        ASTNode *end = parse_expr();
        if (has_parens) expect(TK_RPAREN, "Expected ')' after for header");
        ASTNode *body = parse_block();
        return ast_for(var, start, end, body);
    }

    /* index assignment */
    if (is_index_assign_start()) {
        const char *name = advance()->lex;
        expect(TK_LBRACKET, "Expected '['");
        ASTNode *idx = parse_expr();
        expect(TK_RBRACKET, "Expected ']'");
        expect(TK_ASSIGN, "Expected '=' after index");
        ASTNode *val = parse_expr();
        return ast_index_assign(ast_ident(name), idx, val);
    }

    /* assignment or expression statement */
    if (at(TK_IDENT) && G->data[P+1].t == TK_ASSIGN) {
        const char *name = advance()->lex;
        advance(); /* '=' */
        ASTNode *rhs = parse_expr();
        return ast_var(AST_ASSIGN, name, rhs);
    }

    /* expression stmt (incl call) */
    return parse_expr();
}

static ASTNode *parse_if_stmt(void) {
    expect(TK_LPAREN, "Expected '(' after if");
    ASTNode *cond = parse_expr();
    expect(TK_RPAREN, "Expected ')'");
    ASTNode *thenb = parse_block();
    ASTNode *elseb = NULL;

    if (match(TK_ELSE)) {
        eat_separators();
        if (match(TK_IF)) {
            elseb = parse_if_stmt();
        } else {
            elseb = parse_block();
        }
    }

    return ast_if(cond, thenb, elseb);
}

static ASTNode *parse_block(void) {
    expect(TK_LBRACE, "Expected '{'");

    ASTNode **stmts = NULL;
    int count = 0, cap = 0;

    while (!at(TK_RBRACE) && !at(TK_EOF)) {
        ASTNode *s = parse_stmt();
        if (s) {
            if (count >= cap) {
                cap = cap ? cap*2 : 16;
                stmts = (ASTNode**)xrealloc(stmts, sizeof(ASTNode*)*(size_t)cap);
            }
            stmts[count++] = s;
        }
        eat_separators();
    }

    expect(TK_RBRACE, "Expected '}'");
    return ast_statements(stmts, count);
}

static ASTNode *parse_program(TokVec *v) {
    G = v; P = 0;

    ASTNode **stmts = NULL;
    int count=0, cap=0;

    while (!at(TK_EOF)) {
        ASTNode *s = parse_stmt();
        if (s) {
            if (count >= cap) {
                cap = cap ? cap*2 : 16;
                stmts = (ASTNode**)xrealloc(stmts, sizeof(ASTNode*)*(size_t)cap);
            }
            stmts[count++] = s;
        }
        eat_separators();
    }

    return ast_statements(stmts, count);
}

/* =========================================================
   BYTECODE COMPILER (AST -> RPN bytecode)
   ========================================================= */

static void wr_u8(FILE *f, uint8_t v) { fwrite(&v,1,1,f); }
static void wr_u16(FILE *f, uint16_t v) {
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    fwrite(b,1,2,f);
}

static void wr_u32(FILE *f, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b,1,4,f);
}

static void wr_double(FILE *f, double v) {
    fwrite(&v, 1, 8, f);
}

static void wr_str(FILE *f, const char *s) {
    if (!s) s = "";
    uint32_t len = (uint32_t)strlen(s);
    wr_u32(f, len);
    if (len) fwrite(s,1,len,f);
}

typedef struct {
    uint8_t *data;
    int count;
    int cap;
} CodeBuf;

typedef struct {
    uint8_t type; /* 0 num, 1 str */
    double number;
    char *string;
} BCConst;

typedef struct {
    BCConst *data;
    int count;
    int cap;
} ConstPool;

typedef struct {
    char **data;
    int count;
    int cap;
} SymPool;

typedef struct {
    uint16_t name_sym;
    uint16_t *params;
    int param_count;
    uint32_t addr;
} BCFunc;

typedef struct {
    BCFunc *data;
    int count;
    int cap;
} FuncPool;

typedef struct {
    uint32_t *break_sites;
    int break_count;
    int break_cap;
    uint32_t *continue_sites;
    int continue_count;
    int continue_cap;
    uint32_t continue_target;
} LoopCtx;

typedef struct {
    LoopCtx *data;
    int count;
    int cap;
} LoopStack;

static CodeBuf g_code = {0};
static ConstPool g_consts = {0};
static SymPool g_syms = {0};
static FuncPool g_funcs = {0};
static LoopStack g_loops = {0};

static void vec_u32_push(uint32_t **arr, int *count, int *cap, uint32_t v) {
    if (*count >= *cap) {
        *cap = *cap ? (*cap * 2) : 8;
        *arr = (uint32_t *)xrealloc(*arr, sizeof(uint32_t) * (size_t)(*cap));
    }
    (*arr)[(*count)++] = v;
}

static void code_emit_u8(uint8_t v) {
    if (g_code.count >= g_code.cap) {
        g_code.cap = g_code.cap ? g_code.cap * 2 : 256;
        g_code.data = (uint8_t *)xrealloc(g_code.data, (size_t)g_code.cap);
    }
    g_code.data[g_code.count++] = v;
}

static void code_emit_u16(uint16_t v) {
    code_emit_u8((uint8_t)(v & 0xFF));
    code_emit_u8((uint8_t)((v >> 8) & 0xFF));
}

static uint32_t code_emit_u32_placeholder(void) {
    uint32_t at = (uint32_t)g_code.count;
    code_emit_u8(0);
    code_emit_u8(0);
    code_emit_u8(0);
    code_emit_u8(0);
    return at;
}

static void code_emit_u32(uint32_t v) {
    code_emit_u8((uint8_t)(v & 0xFF));
    code_emit_u8((uint8_t)((v >> 8) & 0xFF));
    code_emit_u8((uint8_t)((v >> 16) & 0xFF));
    code_emit_u8((uint8_t)((v >> 24) & 0xFF));
}

static void code_patch_u32(uint32_t at, uint32_t v) {
    if ((int)at + 3 >= g_code.count) die("spbuild: bad jump patch");
    g_code.data[at] = (uint8_t)(v & 0xFF);
    g_code.data[at + 1] = (uint8_t)((v >> 8) & 0xFF);
    g_code.data[at + 2] = (uint8_t)((v >> 16) & 0xFF);
    g_code.data[at + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t code_pos(void) {
    return (uint32_t)g_code.count;
}

static void code_emit_op(OpCode op) {
    code_emit_u8((uint8_t)op);
}

static int sym_index(const char *s) {
    if (!s) s = "";
    for (int i = 0; i < g_syms.count; i++) {
        if (strcmp(g_syms.data[i], s) == 0) return i;
    }
    if (g_syms.count >= g_syms.cap) {
        g_syms.cap = g_syms.cap ? g_syms.cap * 2 : 64;
        g_syms.data = (char **)xrealloc(g_syms.data, sizeof(char *) * (size_t)g_syms.cap);
    }
    g_syms.data[g_syms.count] = xstrdup(s);
    return g_syms.count++;
}

static int const_num_index(double n) {
    for (int i = 0; i < g_consts.count; i++) {
        if (g_consts.data[i].type == 0 && g_consts.data[i].number == n) return i;
    }
    if (g_consts.count >= g_consts.cap) {
        g_consts.cap = g_consts.cap ? g_consts.cap * 2 : 64;
        g_consts.data = (BCConst *)xrealloc(g_consts.data, sizeof(BCConst) * (size_t)g_consts.cap);
    }
    g_consts.data[g_consts.count].type = 0;
    g_consts.data[g_consts.count].number = n;
    g_consts.data[g_consts.count].string = NULL;
    return g_consts.count++;
}

static int const_str_index(const char *s) {
    if (!s) s = "";
    for (int i = 0; i < g_consts.count; i++) {
        if (g_consts.data[i].type == 1 && strcmp(g_consts.data[i].string, s) == 0) return i;
    }
    if (g_consts.count >= g_consts.cap) {
        g_consts.cap = g_consts.cap ? g_consts.cap * 2 : 64;
        g_consts.data = (BCConst *)xrealloc(g_consts.data, sizeof(BCConst) * (size_t)g_consts.cap);
    }
    g_consts.data[g_consts.count].type = 1;
    g_consts.data[g_consts.count].number = 0.0;
    g_consts.data[g_consts.count].string = xstrdup(s);
    return g_consts.count++;
}

static void emit_push_number(double n) {
    int ci = const_num_index(n);
    code_emit_op(OP_PUSH_CONST);
    code_emit_u16((uint16_t)ci);
}

static void emit_push_string(const char *s) {
    int ci = const_str_index(s);
    code_emit_op(OP_PUSH_CONST);
    code_emit_u16((uint16_t)ci);
}

static LoopCtx *loop_top(void) {
    if (g_loops.count <= 0) return NULL;
    return &g_loops.data[g_loops.count - 1];
}

static void loop_push(uint32_t continue_target) {
    if (g_loops.count >= g_loops.cap) {
        g_loops.cap = g_loops.cap ? g_loops.cap * 2 : 16;
        g_loops.data = (LoopCtx *)xrealloc(g_loops.data, sizeof(LoopCtx) * (size_t)g_loops.cap);
    }
    LoopCtx *ctx = &g_loops.data[g_loops.count++];
    memset(ctx, 0, sizeof(*ctx));
    ctx->continue_target = continue_target;
}

static void loop_set_continue_target(uint32_t target) {
    LoopCtx *ctx = loop_top();
    if (!ctx) die("spbuild: internal loop state");
    ctx->continue_target = target;
}

static void loop_patch_and_pop(uint32_t break_target) {
    LoopCtx *ctx = loop_top();
    if (!ctx) die("spbuild: internal loop pop");
    for (int i = 0; i < ctx->break_count; i++) {
        code_patch_u32(ctx->break_sites[i], break_target);
    }
    for (int i = 0; i < ctx->continue_count; i++) {
        code_patch_u32(ctx->continue_sites[i], ctx->continue_target);
    }
    free(ctx->break_sites);
    free(ctx->continue_sites);
    g_loops.count--;
}

static void emit_node(ASTNode *node);
static void emit_stmt(ASTNode *node);

static void emit_node(ASTNode *node) {
    if (!node) {
        emit_push_number(0.0);
        return;
    }

    switch (node->type) {
        case AST_NUMBER:
            emit_push_number(node->number);
            break;

        case AST_STRING:
            emit_push_string(node->string);
            break;

        case AST_IDENTIFIER: {
            int si = sym_index(node->string);
            code_emit_op(OP_LOAD);
            code_emit_u16((uint16_t)si);
            break;
        }

        case AST_BINARY_OP: {
            const char *op = node->binop.op ? node->binop.op : "";
            emit_node(node->binop.left);
            if (strcmp(op, "!") == 0) {
                code_emit_op(OP_NOT);
                break;
            }
            emit_node(node->binop.right);
            if (strcmp(op, "+") == 0) code_emit_op(OP_ADD);
            else if (strcmp(op, "-") == 0) code_emit_op(OP_SUB);
            else if (strcmp(op, "*") == 0) code_emit_op(OP_MUL);
            else if (strcmp(op, "/") == 0) code_emit_op(OP_DIV);
            else if (strcmp(op, "%") == 0) code_emit_op(OP_MOD);
            else if (strcmp(op, "==") == 0) code_emit_op(OP_EQ);
            else if (strcmp(op, "!=") == 0) code_emit_op(OP_NEQ);
            else if (strcmp(op, "<") == 0) code_emit_op(OP_LT);
            else if (strcmp(op, ">") == 0) code_emit_op(OP_GT);
            else if (strcmp(op, "<=") == 0) code_emit_op(OP_LTE);
            else if (strcmp(op, ">=") == 0) code_emit_op(OP_GTE);
            else if (strcmp(op, "&&") == 0) code_emit_op(OP_AND);
            else if (strcmp(op, "||") == 0) code_emit_op(OP_OR);
            else die("spbuild: unsupported binary op");
            break;
        }

        case AST_FUNCTION_CALL: {
            for (int i = 0; i < node->funccall.arg_count; i++) {
                emit_node(node->funccall.args[i]);
            }
            int si = sym_index(node->funccall.name);
            if (node->funccall.arg_count == 1) {
                code_emit_op(OP_CALL1);
                code_emit_u16((uint16_t)si);
            } else {
                code_emit_op(OP_CALL);
                code_emit_u16((uint16_t)si);
                code_emit_u16((uint16_t)node->funccall.arg_count);
            }
            break;
        }

        case AST_ARRAY:
            for (int i = 0; i < node->arraylit.count; i++) emit_node(node->arraylit.items[i]);
            code_emit_op(OP_ARRAY_NEW);
            code_emit_u16((uint16_t)node->arraylit.count);
            break;

        case AST_INDEX:
            emit_node(node->index.array);
            emit_node(node->index.index);
            code_emit_op(OP_INDEX_GET);
            break;

        default:
            die("spbuild: unsupported expression node");
    }
}

static void emit_function(ASTNode *node) {
    code_emit_op(OP_JMP);
    uint32_t skip_site = code_emit_u32_placeholder();

    if (g_funcs.count >= g_funcs.cap) {
        g_funcs.cap = g_funcs.cap ? g_funcs.cap * 2 : 16;
        g_funcs.data = (BCFunc *)xrealloc(g_funcs.data, sizeof(BCFunc) * (size_t)g_funcs.cap);
    }

    BCFunc *f = &g_funcs.data[g_funcs.count++];
    memset(f, 0, sizeof(*f));
    f->name_sym = (uint16_t)sym_index(node->funcdef.name);
    f->param_count = node->funcdef.param_count;
    f->addr = code_pos();
    if (f->param_count > 0) {
        f->params = (uint16_t *)xmalloc(sizeof(uint16_t) * (size_t)f->param_count);
        for (int i = 0; i < f->param_count; i++) {
            f->params[i] = (uint16_t)sym_index(node->funcdef.params[i]);
        }
    }

    emit_stmt(node->funcdef.body);
    emit_push_number(0.0);
    code_emit_op(OP_RET);

    code_patch_u32(skip_site, code_pos());
}

static void emit_stmt(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_STATEMENTS:
            for (int i = 0; i < node->statements.count; i++) emit_stmt(node->statements.stmts[i]);
            break;

        case AST_FUNC_DEF:
            emit_function(node);
            break;

        case AST_PRINT:
            emit_node(node->print.expr);
            code_emit_op(OP_PRINT);
            break;

        case AST_LET:
        case AST_ASSIGN: {
            int si = sym_index(node->var.name);
            if (node->type == AST_ASSIGN &&
                node->var.value &&
                node->var.value->type == AST_BINARY_OP &&
                node->var.value->binop.left &&
                node->var.value->binop.left->type == AST_IDENTIFIER &&
                strcmp(node->var.value->binop.left->string, node->var.name) == 0 &&
                node->var.value->binop.right) {
                if (node->var.value->binop.right->type == AST_NUMBER &&
                    node->var.value->binop.right->number == 1.0) {
                    if (strcmp(node->var.value->binop.op, "+") == 0) {
                        code_emit_op(OP_INC);
                        code_emit_u16((uint16_t)si);
                        break;
                    }
                    if (strcmp(node->var.value->binop.op, "-") == 0) {
                        code_emit_op(OP_DEC);
                        code_emit_u16((uint16_t)si);
                        break;
                    }
                }
                if (strcmp(node->var.value->binop.op, "+") == 0 &&
                    node->var.value->binop.right->type == AST_IDENTIFIER) {
                    int rhs = sym_index(node->var.value->binop.right->string);
                    code_emit_op(OP_IADD_VAR);
                    code_emit_u16((uint16_t)si);
                    code_emit_u16((uint16_t)rhs);
                    break;
                }
            }
            emit_node(node->var.value);
            code_emit_op(OP_STORE);
            code_emit_u16((uint16_t)si);
            break;
        }

        case AST_IF: {
            emit_node(node->ifstmt.cond);
            code_emit_op(OP_JMP_IF_FALSE);
            uint32_t jf_site = code_emit_u32_placeholder();
            emit_stmt(node->ifstmt.then_b);
            if (node->ifstmt.else_b) {
                code_emit_op(OP_JMP);
                uint32_t jend_site = code_emit_u32_placeholder();
                code_patch_u32(jf_site, code_pos());
                emit_stmt(node->ifstmt.else_b);
                code_patch_u32(jend_site, code_pos());
            } else {
                code_patch_u32(jf_site, code_pos());
            }
            break;
        }

        case AST_WHILE: {
            uint32_t loop_start = code_pos();
            emit_node(node->whilestmt.cond);
            code_emit_op(OP_JMP_IF_FALSE);
            uint32_t jf_site = code_emit_u32_placeholder();
            loop_push(loop_start);
            emit_stmt(node->whilestmt.body);
            code_emit_op(OP_JMP);
            code_emit_u32(loop_start);
            code_patch_u32(jf_site, code_pos());
            loop_patch_and_pop(code_pos());
            break;
        }

        case AST_FOR: {
            emit_node(node->forstmt.start);
            int v = sym_index(node->forstmt.var);
            code_emit_op(OP_STORE);
            code_emit_u16((uint16_t)v);

            uint32_t loop_start = code_pos();
            code_emit_op(OP_LOAD);
            code_emit_u16((uint16_t)v);
            emit_node(node->forstmt.end);
            code_emit_op(OP_LTE);
            code_emit_op(OP_JMP_IF_FALSE);
            uint32_t jf_site = code_emit_u32_placeholder();

            loop_push(0);
            emit_stmt(node->forstmt.body);

            uint32_t continue_target = code_pos();
            loop_set_continue_target(continue_target);
            code_emit_op(OP_INC);
            code_emit_u16((uint16_t)v);
            code_emit_op(OP_JMP);
            code_emit_u32(loop_start);

            code_patch_u32(jf_site, code_pos());
            loop_patch_and_pop(code_pos());
            break;
        }

        case AST_BREAK: {
            LoopCtx *ctx = loop_top();
            if (!ctx) die("spbuild: break outside loop");
            code_emit_op(OP_JMP);
            uint32_t site = code_emit_u32_placeholder();
            vec_u32_push(&ctx->break_sites, &ctx->break_count, &ctx->break_cap, site);
            break;
        }

        case AST_CONTINUE: {
            LoopCtx *ctx = loop_top();
            if (!ctx) die("spbuild: continue outside loop");
            code_emit_op(OP_JMP);
            uint32_t site = code_emit_u32_placeholder();
            vec_u32_push(&ctx->continue_sites, &ctx->continue_count, &ctx->continue_cap, site);
            break;
        }

        case AST_RETURN:
            if (node->retstmt.expr) emit_node(node->retstmt.expr);
            else emit_push_number(0.0);
            code_emit_op(OP_RET);
            break;

        case AST_INDEX_ASSIGN:
            emit_node(node->indexassign.array);
            emit_node(node->indexassign.index);
            emit_node(node->indexassign.value);
            code_emit_op(OP_INDEX_SET);
            code_emit_op(OP_POP);
            break;

        case AST_IMPORT_C: {
            int si = sym_index(node->string);
            code_emit_op(OP_IMPORT);
            code_emit_u16((uint16_t)si);
            break;
        }

        case AST_FUNCTION_CALL:
            emit_node(node);
            code_emit_op(OP_POP);
            break;

        default:
            emit_node(node);
            code_emit_op(OP_POP);
            break;
    }
}

static void free_codegen_state(void) {
    free(g_code.data);
    g_code.data = NULL;
    g_code.count = g_code.cap = 0;

    for (int i = 0; i < g_consts.count; i++) free(g_consts.data[i].string);
    free(g_consts.data);
    g_consts.data = NULL;
    g_consts.count = g_consts.cap = 0;

    for (int i = 0; i < g_syms.count; i++) free(g_syms.data[i]);
    free(g_syms.data);
    g_syms.data = NULL;
    g_syms.count = g_syms.cap = 0;

    for (int i = 0; i < g_funcs.count; i++) free(g_funcs.data[i].params);
    free(g_funcs.data);
    g_funcs.data = NULL;
    g_funcs.count = g_funcs.cap = 0;

    free(g_loops.data);
    g_loops.data = NULL;
    g_loops.count = g_loops.cap = 0;
}

static int write_spc(const char *out_path, ASTNode *root) {
    free_codegen_state();
    emit_stmt(root);
    code_emit_op(OP_HALT);

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return 0;
    }
    fwrite(SPC_MAGIC, 1, 4, f);
    wr_u8(f, (uint8_t)SPC_VERSION);

    wr_u16(f, (uint16_t)g_consts.count);
    for (int i = 0; i < g_consts.count; i++) {
        wr_u8(f, g_consts.data[i].type);
        if (g_consts.data[i].type == 0) wr_double(f, g_consts.data[i].number);
        else wr_str(f, g_consts.data[i].string);
    }

    wr_u16(f, (uint16_t)g_syms.count);
    for (int i = 0; i < g_syms.count; i++) wr_str(f, g_syms.data[i]);

    wr_u16(f, (uint16_t)g_funcs.count);
    for (int i = 0; i < g_funcs.count; i++) {
        wr_u16(f, g_funcs.data[i].name_sym);
        wr_u16(f, (uint16_t)g_funcs.data[i].param_count);
        wr_u32(f, g_funcs.data[i].addr);
        for (int j = 0; j < g_funcs.data[i].param_count; j++) wr_u16(f, g_funcs.data[i].params[j]);
    }

    wr_u32(f, (uint32_t)g_code.count);
    if (g_code.count > 0) fwrite(g_code.data, 1, (size_t)g_code.count, f);

    fclose(f);
    free_codegen_state();
    return 1;
}

/* =========================================================
   MAIN
   ========================================================= */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char*)xmalloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);
    return buf;
}

/* Basic validation to reduce path traversal risk.
   Allows only non-empty relative paths and rejects any ".." component. */
static int is_safe_relative_path(const char *arg) {
    if (arg == NULL || *arg == '\0') {
        return 0;
    }

    /* Reject absolute POSIX-style paths. */
    if (arg[0] == '/') {
        return 0;
    }

    /* Rudimentary check against Windows drive letters like "C:" or "C:\". */
    if (((arg[0] >= 'A' && arg[0] <= 'Z') || (arg[0] >= 'a' && arg[0] <= 'z')) &&
        arg[1] == ':') {
        return 0;
    }

    /* Scan components separated by '/' or '\\' and reject any that are exactly "..". */
    const char *p = arg;
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

static int path_within_base(const char *path, const char *base) {
    size_t base_len = strlen(base);
    if (strncmp(path, base, base_len) != 0) {
        return 0;
    }
    return path[base_len] == '\0' || path[base_len] == '/';
}

static int fullpath_buf(const char *path, char *out, size_t out_sz) {
#ifdef _WIN32
    return _fullpath(out, path, out_sz) != NULL;
#else
    (void)out_sz;
    return realpath(path, out) != NULL;
#endif
}

static int resolve_input_path(const char *arg, char *dst, size_t dst_len) {
    if (!is_safe_relative_path(arg)) {
        return 0;
    }

    char cwd[PATH_MAX];
    char resolved[PATH_MAX];
    if (!splice_getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    if (!fullpath_buf(arg, resolved, sizeof(resolved))) {
        return 0;
    }
    if (!path_within_base(resolved, cwd)) {
        return 0;
    }
    if (snprintf(dst, dst_len, "%s", resolved) >= (int)dst_len) {
        return 0;
    }
    return 1;
}

static int is_safe_filename(const char *name) {
    if (name == NULL || *name == '\0') {
        return 0;
    }
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(c == '.' || c == '_' || c == '-' ||
              (c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) {
            return 0;
        }
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.spl> <output.spc>\n", argv[0]);
        return 1;
    }

    const char *in_arg = argv[1];
    const char *out_arg = argv[2];
    char in_path[PATH_MAX];
    char out_path[PATH_MAX];

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

    char *src = read_file(in_path);
    if (!src) {
        fprintf(stderr, "spbuild: cannot read %s\n", in_arg);
        return 1;
    }

    TokVec tv = {0};
    lex(src, &tv);

    if (snprintf(g_current_source_file, sizeof(g_current_source_file), "%s", in_path) >= (int)sizeof(g_current_source_file)) {
        fprintf(stderr, "spbuild: input path too long\n");
        tv_free(&tv);
        free(src);
        return 1;
    }
    path_list_push(&g_imported_files, &g_imported_count, &g_imported_cap, in_path);
    path_list_push(&g_import_stack, &g_import_stack_count, &g_import_stack_cap, in_path);
    ASTNode *root = parse_program(&tv);
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
