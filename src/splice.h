#ifndef Splice_H
#define Splice_H
/* Header-only Splice runtime + AST serialization.
   - VM loads .spc => AST => interpret
   - Builder writes AST directly into .spc
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <setjmp.h>

#if defined(_WIN32)
  #include <windows.h>
#elif !defined(ARDUINO)
  #include <dlfcn.h>
#endif



/* =========================
   Diagnostics
   ========================= */
static inline void error(int ln, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ERROR] line %d: ", ln);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
static inline void warn(int ln, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[WARN]  line %d: ", ln);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
static inline void info(int ln, const char *fmt, ...) {
    (void)ln;
    va_list ap; va_start(ap, fmt);
    fprintf(stdout, "[INFO] ");
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    va_end(ap);
}
static inline void success(int ln, const char *fmt, ...) {
    (void)ln;
    va_list ap; va_start(ap, fmt);
    fprintf(stdout, "[SUCCESS] ");
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    va_end(ap);
}

/* =========================
   Values / Objects
   ========================= */
typedef enum { VAL_NUMBER, VAL_STRING, VAL_OBJECT } ValueType;

typedef struct Value {
    ValueType type;
    double number;
    char *string;
    void *object;
} Value;

typedef enum { OBJ_ARRAY } ObjectType;
typedef struct {
    ObjectType type;
    int count;
    int capacity;
    Value *items;
} ObjArray;

/* If you have sdk.h natives, keep it. If not, stub it. */
#include "sdk.h"

/* =========================
   AST
   ========================= */
typedef enum {
    AST_NUMBER = 0,
    AST_STRING,
    AST_IDENTIFIER,
    AST_BINARY_OP,
    AST_LET,
    AST_ASSIGN,
    AST_PRINT,
    AST_READ,
    AST_WRITE,
    AST_RAISE,
    AST_WARN,
    AST_INFO,
    AST_WHILE,
    AST_IF,
    AST_STATEMENTS,
    AST_FUNC_DEF,
    AST_FUNCTION_CALL,
    AST_RETURN,
    AST_IMPORT,
    AST_FOR,
    AST_ARRAY_LITERAL,
    AST_INDEX_EXPR,
    AST_INDEX_ASSIGN
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    union {
        double number;
        char *string;

        struct {
            char *op;       /* "+", "-", "*", "/", "==", "&&", "!" ... */
            ASTNode *left;
            ASTNode *right; /* may be NULL for unary */
        } binop;

        struct {
            char *varname;
            ASTNode *value;
        } var;

        struct { ASTNode *expr; } print;
        struct { ASTNode *expr; } raise;
        struct { ASTNode *expr; } warn;
        struct { ASTNode *expr; } info;
        struct { ASTNode *expr; } read;
        struct { ASTNode *path; ASTNode *value; } write;


        struct { ASTNode *cond; ASTNode *body; } whilestmt;

        struct {
            ASTNode *condition;
            ASTNode *then_branch;
            ASTNode *else_branch; /* may be NULL */
        } ifstmt;

        struct {
            ASTNode **stmts;
            int count;
        } statements;

        struct {
            char *funcname;
            char **params;
            int param_count;
            ASTNode *body;
        } funcdef;

        struct {
            char *funcname;
            ASTNode **args;
            int arg_count;
        } funccall;

        struct { ASTNode *expr; } retstmt;

        struct { char *filename; } importstmt;

        struct {
            char *for_var;
            ASTNode *for_start;
            ASTNode *for_end;
            ASTNode *for_body;
        } forstmt;

        struct {
            ASTNode **elements;
            int count;
        } arraylit;

        struct {
            ASTNode *target;
            ASTNode *index;
        } indexexpr;

        struct {
            ASTNode *target;
            ASTNode *index;
            ASTNode *value;
        } indexassign;
    };
};

/* =========================
   Env (vars + funcs)
   ========================= */
static jmp_buf return_buf;
static Value   return_value;

typedef enum { VAR_NUMBER, VAR_STRING, VAR_OBJECT } VarType;
typedef struct {
    char  *name;
    VarType type;
    double value;
    char  *str;
    void  *obj;
} Var;

static Var vars[32];
static int var_count = 0;

static inline Var *get_var(const char *name) {
    for (int j = 0; j < var_count; ++j)
        if (strcmp(vars[j].name, name) == 0) return &vars[j];
    return NULL;
}

static inline void free_object(void *obj) {
    if (!obj) return;
    ObjArray *oa = (ObjArray*)obj;
    if (oa->type == OBJ_ARRAY) {
        for (int j = 0; j < oa->count; ++j) {
            if (oa->items[j].type == VAL_STRING) free(oa->items[j].string);
        }
        free(oa->items);
    }
    free(oa);
}

static inline void set_var_object(const char *name, void *obj) {
    for (int j = 0; j < var_count; ++j) {
        if (strcmp(vars[j].name, name) == 0) {
            if (vars[j].type == VAR_OBJECT) free_object(vars[j].obj);
            vars[j].type = VAR_OBJECT;
            vars[j].obj = obj;
            free(vars[j].str); vars[j].str = NULL;
            vars[j].value = 0;
            return;
        }
    }
    vars[var_count].name = strdup(name);
    vars[var_count].type = VAR_OBJECT;
    vars[var_count].obj  = obj;
    vars[var_count].value = 0;
    vars[var_count].str = NULL;
    var_count++;
}

static inline void set_var(const char *name, VarType type, double value, const char *str) {
    for (int j = 0; j < var_count; ++j) {
        if (strcmp(vars[j].name, name) == 0) {
            vars[j].type = type;
            if (type == VAR_STRING) {
                free(vars[j].str);
                vars[j].str = strdup(str ? str : "");
            } else {
                vars[j].value = value;
                free(vars[j].str);
                vars[j].str = NULL;
            }
            return;
        }
    }
    vars[var_count].name = strdup(name);
    vars[var_count].type = type;
    vars[var_count].value = (type == VAR_NUMBER) ? value : 0;
    vars[var_count].str   = (type == VAR_STRING) ? strdup(str ? str : "") : NULL;
    var_count++;
}

#define MAX_FUNCS 16
typedef struct { char *name; ASTNode *def; } Func;
static Func funcs[MAX_FUNCS];
static int  func_count = 0;

static inline void add_func(const char *name, ASTNode *def) {
    for (int j = 0; j < func_count; ++j)
        if (strcmp(funcs[j].name, name) == 0) { funcs[j].def = def; return; }
    funcs[func_count].name = strdup(name);
    funcs[func_count].def  = def;
    func_count++;
}

static inline ASTNode *get_func(const char *name) {
    for (int j = 0; j < func_count; ++j)
        if (strcmp(funcs[j].name, name) == 0) return funcs[j].def;
    return NULL;
}

/* =========================
   AST alloc/free
   ========================= */
static inline ASTNode *ast_new(ASTNodeType t) {
    ASTNode *n = (ASTNode*)calloc(1, sizeof(ASTNode));
    if (!n) error(0, "OOM allocating ASTNode");
    n->type = t;
    return n;
}

static inline void free_ast(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_STRING:
        case AST_IDENTIFIER:
            free(node->string);
            break;

        case AST_BINARY_OP:
            free(node->binop.op);
            free_ast(node->binop.left);
            free_ast(node->binop.right);
            break;

        case AST_LET:
        case AST_ASSIGN:
            free(node->var.varname);
            free_ast(node->var.value);
            break;

        case AST_PRINT: free_ast(node->print.expr); break;
        case AST_RAISE: free_ast(node->raise.expr); break;
        case AST_WARN:  free_ast(node->warn.expr);  break;
        case AST_INFO:  free_ast(node->info.expr);  break;

        case AST_WHILE:
            free_ast(node->whilestmt.cond);
            free_ast(node->whilestmt.body);
            break;

        case AST_IF:
            free_ast(node->ifstmt.condition);
            free_ast(node->ifstmt.then_branch);
            free_ast(node->ifstmt.else_branch);
            break;

        case AST_STATEMENTS:
            for (int j = 0; j < node->statements.count; ++j)
                free_ast(node->statements.stmts[j]);
            free(node->statements.stmts);
            break;

        case AST_FUNC_DEF:
            free(node->funcdef.funcname);
            for (int j = 0; j < node->funcdef.param_count; ++j) free(node->funcdef.params[j]);
            free(node->funcdef.params);
            free_ast(node->funcdef.body);
            break;

        case AST_FUNCTION_CALL:
            free(node->funccall.funcname);
            for (int j = 0; j < node->funccall.arg_count; ++j) free_ast(node->funccall.args[j]);
            free(node->funccall.args);
            break;

        case AST_RETURN:
            free_ast(node->retstmt.expr);
            break;

        case AST_IMPORT:
            free(node->importstmt.filename);
            break;

        case AST_FOR:
            free(node->forstmt.for_var);
            free_ast(node->forstmt.for_start);
            free_ast(node->forstmt.for_end);
            free_ast(node->forstmt.for_body);
            break;

        case AST_ARRAY_LITERAL:
            for (int j = 0; j < node->arraylit.count; ++j) free_ast(node->arraylit.elements[j]);
            free(node->arraylit.elements);
            break;

        case AST_INDEX_EXPR:
            free_ast(node->indexexpr.target);
            free_ast(node->indexexpr.index);
            break;

        case AST_INDEX_ASSIGN:
            free_ast(node->indexassign.target);
            free_ast(node->indexassign.index);
            free_ast(node->indexassign.value);
            break;

        default:
            break;
    }
    free(node);
}

/* =========================
   AST serialization helpers
   ========================= */
#define SPC_MAGIC "SPC"
#define SPC_VERSION 1
#define AST_NULL_SENTINEL 0xFF

static inline void w_u8(FILE *f, unsigned char v) {
    if (fputc(v, f) == EOF) error(0, "write u8 failed");
}
static inline void w_u32(FILE *f, unsigned int v) {
    if (fwrite(&v, 4, 1, f) != 1) error(0, "write u32 failed");
}
static inline void w_u16(FILE *f, unsigned short v) {
    if (fwrite(&v, 2, 1, f) != 1) error(0, "write u16 failed");
}
static inline void w_double(FILE *f, double d) {
    if (fwrite(&d, sizeof(double), 1, f) != 1) error(0, "write double failed");
}
static inline void w_str(FILE *f, const char *s) {
    if (!s) s = "";
    unsigned short len = (unsigned short)strlen(s);
    w_u16(f, len);
    if (len && fwrite(s, 1, len, f) != len) error(0, "write string failed");
}

static inline unsigned char r_u8(FILE *f) {
    int c = fgetc(f);
    if (c == EOF) error(0, "Unexpected EOF (u8)");
    return (unsigned char)c;
}
static inline unsigned int r_u32(FILE *f) {
    unsigned int v;
    if (fread(&v, 4, 1, f) != 1) error(0, "Unexpected EOF (u32)");
    return v;
}
static inline unsigned short r_u16(FILE *f) {
    unsigned short v;
    if (fread(&v, 2, 1, f) != 1) error(0, "Unexpected EOF (u16)");
    return v;
}
static inline double r_double(FILE *f) {
    double d;
    if (fread(&d, sizeof(double), 1, f) != 1) error(0, "Unexpected EOF (double)");
    return d;
}
static inline char *r_str(FILE *f) {
    unsigned short len = r_u16(f);
    char *s = (char*)malloc((size_t)len + 1);
    if (!s) error(0, "OOM reading string");
    if (len && fread(s, 1, len, f) != len) error(0, "Unexpected EOF (string)");
    s[len] = 0;
    return s;
}

static void write_ast_node(FILE *f, const ASTNode *n);

static void write_ast_node(FILE *f, const ASTNode *n) {
    if (!n) { w_u8(f, AST_NULL_SENTINEL); return; }
    w_u8(f, (unsigned char)n->type);

    switch (n->type) {
        case AST_NUMBER: w_double(f, n->number); break;

        case AST_STRING:
        case AST_IDENTIFIER:
            w_str(f, n->string);
            break;

        case AST_BINARY_OP:
            w_str(f, n->binop.op);
            write_ast_node(f, n->binop.left);
            write_ast_node(f, n->binop.right);
            break;
        case AST_READ:
            write_ast_node(f, n->read.expr);
            break;

        case AST_WRITE:
            write_ast_node(f, n->write.path);
            write_ast_node(f, n->write.value);
            break;

        case AST_PRINT: write_ast_node(f, n->print.expr); break;
        case AST_RAISE: write_ast_node(f, n->raise.expr); break;
        case AST_WARN:  write_ast_node(f, n->warn.expr);  break;
        case AST_INFO:  write_ast_node(f, n->info.expr);  break;

        case AST_LET:
        case AST_ASSIGN:
            w_str(f, n->var.varname);
            write_ast_node(f, n->var.value);
            break;

        case AST_RETURN:
            write_ast_node(f, n->retstmt.expr);
            break;

        case AST_WHILE:
            write_ast_node(f, n->whilestmt.cond);
            write_ast_node(f, n->whilestmt.body);
            break;

        case AST_IF:
            write_ast_node(f, n->ifstmt.condition);
            write_ast_node(f, n->ifstmt.then_branch);
            write_ast_node(f, n->ifstmt.else_branch);
            break;

        case AST_FOR:
            w_str(f, n->forstmt.for_var);
            write_ast_node(f, n->forstmt.for_start);
            write_ast_node(f, n->forstmt.for_end);
            write_ast_node(f, n->forstmt.for_body);
            break;

        case AST_ARRAY_LITERAL:
            w_u32(f, (unsigned int)n->arraylit.count);
            for (int i = 0; i < n->arraylit.count; ++i) write_ast_node(f, n->arraylit.elements[i]);
            break;

        case AST_INDEX_EXPR:
            write_ast_node(f, n->indexexpr.target);
            write_ast_node(f, n->indexexpr.index);
            break;

        case AST_INDEX_ASSIGN:
            write_ast_node(f, n->indexassign.target);
            write_ast_node(f, n->indexassign.index);
            write_ast_node(f, n->indexassign.value);
            break;

        case AST_FUNC_DEF:
            w_str(f, n->funcdef.funcname);
            w_u32(f, (unsigned int)n->funcdef.param_count);
            for (int i = 0; i < n->funcdef.param_count; ++i) w_str(f, n->funcdef.params[i]);
            write_ast_node(f, n->funcdef.body);
            break;

        case AST_FUNCTION_CALL:
            w_str(f, n->funccall.funcname);
            w_u32(f, (unsigned int)n->funccall.arg_count);
            for (int i = 0; i < n->funccall.arg_count; ++i) write_ast_node(f, n->funccall.args[i]);
            break;

        case AST_STATEMENTS:
            w_u32(f, (unsigned int)n->statements.count);
            for (int i = 0; i < n->statements.count; ++i) write_ast_node(f, n->statements.stmts[i]);
            break;

        case AST_IMPORT:
            w_str(f, n->importstmt.filename);
            break;

        default:
            error(0, "write_ast_node: unsupported type %d", (int)n->type);
    }
}

static ASTNode *read_ast_node(FILE *f);

static ASTNode *read_ast_node(FILE *f) {
    unsigned char t = r_u8(f);
    if (t == AST_NULL_SENTINEL) return NULL;

    ASTNodeType type = (ASTNodeType)t;
    ASTNode *n = ast_new(type);

    switch (type) {
        case AST_READ:
            n->read.expr = read_ast_node(f);
            break;

        case AST_WRITE:
            n->write.path  = read_ast_node(f);
            n->write.value = read_ast_node(f);
            break;

        case AST_NUMBER:
            n->number = r_double(f);
            break;

        case AST_STRING:
        case AST_IDENTIFIER:
            n->string = r_str(f);
            break;

        case AST_BINARY_OP:
            n->binop.op = r_str(f);
            n->binop.left  = read_ast_node(f);
            n->binop.right = read_ast_node(f);
            break;

        case AST_PRINT: n->print.expr = read_ast_node(f); break;
        case AST_RAISE: n->raise.expr = read_ast_node(f); break;
        case AST_WARN:  n->warn.expr  = read_ast_node(f); break;
        case AST_INFO:  n->info.expr  = read_ast_node(f); break;

        case AST_LET:
        case AST_ASSIGN:
            n->var.varname = r_str(f);
            n->var.value   = read_ast_node(f);
            break;

        case AST_RETURN:
            n->retstmt.expr = read_ast_node(f);
            break;

        case AST_WHILE:
            n->whilestmt.cond = read_ast_node(f);
            n->whilestmt.body = read_ast_node(f);
            break;

        case AST_IF:
            n->ifstmt.condition   = read_ast_node(f);
            n->ifstmt.then_branch = read_ast_node(f);
            n->ifstmt.else_branch = read_ast_node(f);
            break;

        case AST_FOR:
            n->forstmt.for_var   = r_str(f);
            n->forstmt.for_start = read_ast_node(f);
            n->forstmt.for_end   = read_ast_node(f);
            n->forstmt.for_body  = read_ast_node(f);
            break;

        case AST_ARRAY_LITERAL: {
            unsigned int count = r_u32(f);
            n->arraylit.count = (int)count;
            n->arraylit.elements = (ASTNode**)calloc(count ? count : 1, sizeof(ASTNode*));
            if (!n->arraylit.elements) error(0, "OOM arraylit elements");
            for (unsigned int i = 0; i < count; ++i) n->arraylit.elements[i] = read_ast_node(f);
            break;
        }

        case AST_INDEX_EXPR:
            n->indexexpr.target = read_ast_node(f);
            n->indexexpr.index  = read_ast_node(f);
            break;

        case AST_INDEX_ASSIGN:
            n->indexassign.target = read_ast_node(f);
            n->indexassign.index  = read_ast_node(f);
            n->indexassign.value  = read_ast_node(f);
            break;

        case AST_FUNC_DEF: {
            n->funcdef.funcname = r_str(f);
            unsigned int pc = r_u32(f);
            n->funcdef.param_count = (int)pc;
            n->funcdef.params = (char**)calloc(pc ? pc : 1, sizeof(char*));
            if (!n->funcdef.params) error(0, "OOM func params");
            for (unsigned int i = 0; i < pc; ++i) n->funcdef.params[i] = r_str(f);
            n->funcdef.body = read_ast_node(f);
            break;
        }

        case AST_FUNCTION_CALL: {
            n->funccall.funcname = r_str(f);
            unsigned int ac = r_u32(f);
            n->funccall.arg_count = (int)ac;
            n->funccall.args = (ASTNode**)calloc(ac ? ac : 1, sizeof(ASTNode*));
            if (!n->funccall.args) error(0, "OOM funccall args");
            for (unsigned int i = 0; i < ac; ++i) n->funccall.args[i] = read_ast_node(f);
            break;
        }

        case AST_STATEMENTS: {
            unsigned int c = r_u32(f);
            n->statements.count = (int)c;
            n->statements.stmts = (ASTNode**)calloc(c ? c : 1, sizeof(ASTNode*));
            if (!n->statements.stmts) error(0, "OOM statements");
            for (unsigned int i = 0; i < c; ++i) n->statements.stmts[i] = read_ast_node(f);
            break;
        }

        case AST_IMPORT:
            n->importstmt.filename = r_str(f);
            break;

        default:
            error(0, "read_ast_node: unknown type %d", (int)type);
    }
    return n;
}

/* Public: builder helper */
static inline int write_ast_to_spc(const char *out_file, const ASTNode *root) {
    FILE *f = fopen(out_file, "wb");
    if (!f) return 0;

    fwrite(SPC_MAGIC, 1, 4, f);
    w_u8(f, (unsigned char)SPC_VERSION);

    write_ast_node(f, root);
    fclose(f);
    return 1;
}

/* Public: VM helper */
static inline ASTNode *read_ast_from_spc(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) { error(0, "Could not open bytecode file: %s", filename); return NULL; }

    char magic[5] = {0};
    if (fread(magic, 1, 4, f) != 4) error(0, "Invalid SPC (short)");
    if (memcmp(magic, SPC_MAGIC, 4) != 0) error(0, "Invalid SPC magic");

    unsigned char ver = r_u8(f);
    if (ver != SPC_VERSION) error(0, "Unsupported SPC version: %u", ver);

    ASTNode *root = read_ast_node(f);
    fclose(f);
    return root;
}

/* =========================
   Runtime eval/interpret
   ========================= */
static inline char *eval_to_string(ASTNode *node);

static inline Value eval(ASTNode *node) {
    if (!node) {
        Value tmp;
        tmp.type = VAL_NUMBER;
        tmp.number = 0;
        return tmp;
    }

    switch (node->type) {
        case AST_READ: {
            char *path = eval_to_string(node->read.expr);

            FILE *f = fopen(path, "rb");
            if (!f) {
                free(path);
                Value tmp;
                tmp.type = VAL_STRING;
                tmp.string = strdup("");
                return tmp;
            }

            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            rewind(f);

            char *buf = (char*)malloc(size + 1);
            if (!buf) error(0, "OOM in read()");
            fread(buf, 1, size, f);
            buf[size] = 0;

            fclose(f);
            free(path);

            Value tmp;
            tmp.type = VAL_STRING;
            tmp.string = buf;
            return tmp;
        }

        case AST_NUMBER: {
            Value tmp;
            tmp.type = VAL_NUMBER;
            tmp.number = node->number;
            return tmp;
        }

        case AST_STRING: {
            Value tmp;
            tmp.type = VAL_STRING;
            tmp.string = strdup(node->string ? node->string : "");
            return tmp;
        }

        case AST_IDENTIFIER: {
            Var *v = get_var(node->string);
            if (!v) {
                Value tmp;
                tmp.type = VAL_NUMBER;
                tmp.number = 0;
                return tmp;
            }

            if (v->type == VAR_STRING) {
                Value tmp;
                tmp.type = VAL_STRING;
                tmp.string = strdup(v->str ? v->str : "");
                return tmp;
            }
            if (v->type == VAR_OBJECT) {
                Value tmp;
                tmp.type = VAL_OBJECT;
                tmp.object = v->obj;
                return tmp;
            }
            Value tmp;
            tmp.type = VAL_NUMBER;
            tmp.number = v->value;
            return tmp;
        }

        case AST_ARRAY_LITERAL: {
            ObjArray *oa = (ObjArray*)calloc(1, sizeof(ObjArray));
            if (!oa) error(0, "OOM array");
            oa->type = OBJ_ARRAY;
            oa->count = node->arraylit.count;
            oa->capacity = node->arraylit.count;
            oa->items = (Value*)calloc((size_t)(oa->capacity ? oa->capacity : 1), sizeof(Value));
            if (!oa->items) error(0, "OOM array items");
            for (int j = 0; j < node->arraylit.count; ++j) oa->items[j] = eval(node->arraylit.elements[j]);
            Value tmp;
            tmp.type = VAL_OBJECT;
            tmp.object = oa;
            return tmp;
        }

        case AST_INDEX_EXPR: {
            Value target = eval(node->indexexpr.target);
            Value idxv   = eval(node->indexexpr.index);
            int idx = (int)idxv.number;

            if (target.type != VAL_OBJECT) error(0, "index: target is not array");
            ObjArray *oa = (ObjArray*)target.object;
            if (!oa || oa->type != OBJ_ARRAY) error(0, "index: not an array");

            if (idx < 0 || idx >= oa->count) {
                Value tmp;
                tmp.type = VAL_NUMBER;
                tmp.number = 0;
                return tmp;
            }
            return oa->items[idx];
        }

        case AST_INDEX_ASSIGN: {
            if (!node->indexassign.target || node->indexassign.target->type != AST_IDENTIFIER)
                error(0, "index assign: target must be identifier");

            Var *v = get_var(node->indexassign.target->string);
            if (!v || v->type != VAR_OBJECT) error(0, "index assign: variable is not array");

            ObjArray *oa = (ObjArray*)v->obj;
            int idx = (int)eval(node->indexassign.index).number;
            Value val = eval(node->indexassign.value);

            if (idx < 0) error(0, "index assign: negative index");
            if (idx >= oa->count) {
                while (idx >= oa->capacity) {
                    int newcap = oa->capacity ? oa->capacity * 2 : 4;
                    Value *ni = (Value*)realloc(oa->items, sizeof(Value) * (size_t)newcap);
                    if (!ni) error(0, "OOM realloc array");
                    oa->items = ni;
                    oa->capacity = newcap;
                }
                for (int k = oa->count; k <= idx; ++k) {
                    oa->items[k].type = VAL_NUMBER;
                    oa->items[k].number = 0;
                }
                oa->count = idx + 1;
            }
            if (oa->items[idx].type == VAL_STRING) free(oa->items[idx].string);
            oa->items[idx] = val;
            Value tmp;
            tmp.type = VAL_NUMBER;
            tmp.number = 1;
            return tmp;
        }

        case AST_BINARY_OP: {
            Value left  = eval(node->binop.left);
            Value right;
            if (node->binop.right) {
                right = eval(node->binop.right);
            } else {
                right.type = VAL_NUMBER;
                right.number = 0;
                right.string = NULL;
                right.object = NULL;
            }

            /* string concat on + */
            if (node->binop.op && strcmp(node->binop.op, "+") == 0 &&
                (left.type == VAL_STRING || right.type == VAL_STRING)) {

                char lb[64], rb[64];
                const char *ls = (left.type == VAL_STRING) ? left.string : (snprintf(lb, sizeof(lb), "%g", left.number), lb);
                const char *rs = (right.type == VAL_STRING) ? right.string : (snprintf(rb, sizeof(rb), "%g", right.number), rb);

                char *out = (char*)malloc(strlen(ls) + strlen(rs) + 1);
                if (!out) error(0, "OOM concat");
                strcpy(out, ls);
                strcat(out, rs);

                if (left.type == VAL_STRING) free(left.string);
                if (right.type == VAL_STRING) free(right.string);

                Value tmp;
                tmp.type = VAL_STRING;
                tmp.string = out;
                return tmp;
            }

            double lnum = (left.type == VAL_NUMBER) ? left.number : strtod(left.string ? left.string : "0", NULL);
            double rnum = (right.type == VAL_NUMBER) ? right.number : strtod(right.string ? right.string : "0", NULL);

            double result = 0;
            const char *op = node->binop.op ? node->binop.op : "";

            if (strcmp(op, "==") == 0) result = (lnum == rnum);
            else if (strcmp(op, "!=") == 0) result = (lnum != rnum);
            else if (strcmp(op, "+") == 0) result = lnum + rnum;
            else if (strcmp(op, "-") == 0) result = lnum - rnum;
            else if (strcmp(op, "*") == 0) result = lnum * rnum;
            else if (strcmp(op, "/") == 0) result = lnum / rnum;
            else if (strcmp(op, "<") == 0) result = (lnum < rnum);
            else if (strcmp(op, ">") == 0) result = (lnum > rnum);
            else if (strcmp(op, "<=") == 0) result = (lnum <= rnum);
            else if (strcmp(op, ">=") == 0) result = (lnum >= rnum);
            else if (strcmp(op, "&&") == 0) result = ((lnum != 0) && (rnum != 0));
            else if (strcmp(op, "||") == 0) result = ((lnum != 0) || (rnum != 0));
            else if (strcmp(op, "!") == 0) result = (lnum == 0);

            if (left.type == VAL_STRING) free(left.string);
            if (right.type == VAL_STRING) free(right.string);

            Value tmp;
            tmp.type = VAL_NUMBER;
            tmp.number = result;
            return tmp;
        }

        case AST_FUNCTION_CALL: {
            /* builtins */
            if (strcmp(node->funccall.funcname, "len") == 0 && node->funccall.arg_count == 1) {
                Value a = eval(node->funccall.args[0]);
                if (a.type != VAL_OBJECT) {
                    Value tmp;
                    tmp.type = VAL_NUMBER;
                    tmp.number = 0;
                    return tmp;
                }
                ObjArray *oa = (ObjArray*)a.object;
                if (!oa || oa->type != OBJ_ARRAY) {
                    Value tmp;
                    tmp.type = VAL_NUMBER;
                    tmp.number = 0;
                    return tmp;
                }
                Value tmp;
                tmp.type = VAL_NUMBER;
                tmp.number = (double)oa->count;
                return tmp;
            }

            if (strcmp(node->funccall.funcname, "append") == 0 && node->funccall.arg_count == 2) {
                Value a = eval(node->funccall.args[0]);
                Value v = eval(node->funccall.args[1]);
                if (a.type != VAL_OBJECT) error(0, "append: first arg must be array");
                ObjArray *oa = (ObjArray*)a.object;
                if (!oa || oa->type != OBJ_ARRAY) error(0, "append: not an array");

                if (oa->count >= oa->capacity) {
                    int newcap = oa->capacity ? oa->capacity * 2 : 4;
                    Value *ni = (Value*)realloc(oa->items, sizeof(Value) * (size_t)newcap);
                    if (!ni) error(0, "OOM append realloc");
                    oa->items = ni;
                    oa->capacity = newcap;
                }
                oa->items[oa->count++] = v;
                Value tmp;
                tmp.type = VAL_NUMBER;
                tmp.number = 1;
                return tmp;
            }

            /* native */
            SpliceCFunc native = Splice_get_native(node->funccall.funcname);
            if (native) {
                Value *args = (Value*)malloc(sizeof(Value) * (size_t)node->funccall.arg_count);
                if (!args) error(0, "OOM native args");
                for (int j = 0; j < node->funccall.arg_count; ++j) args[j] = eval(node->funccall.args[j]);
                Value r = native(node->funccall.arg_count, args);
                for (int j = 0; j < node->funccall.arg_count; ++j)
                    if (args[j].type == VAL_STRING) free(args[j].string);
                free(args);
                return r;
            }

            /* user-defined */
            ASTNode *func = get_func(node->funccall.funcname);
            if (!func) error(0, "Undefined function: %s", node->funccall.funcname);

            int saved = var_count;
            for (int j = 0; j < func->funcdef.param_count; ++j) {
                Value av;
                if (j < node->funccall.arg_count) {
                    av = eval(node->funccall.args[j]);
                } else {
                    av.type = VAL_NUMBER;
                    av.number = 0;
                    av.string = NULL;
                    av.object = NULL;
                }

                if (av.type == VAL_STRING) {
                    set_var(func->funcdef.params[j], VAR_STRING, 0, av.string);
                    free(av.string);
                } else if (av.type == VAL_OBJECT) {
                    set_var_object(func->funcdef.params[j], av.object);
                } else {
                    set_var(func->funcdef.params[j], VAR_NUMBER, av.number, NULL);
                }
            }

            Value result;
            result.type = VAL_NUMBER;
            result.number = 0;
            if (setjmp(return_buf) == 0) {
                /* interpret body in-place */
                /* interpret declared later */
            } else {
                result = return_value;
            }
            var_count = saved;
            return result;
        }

        default: {
            Value tmp;
            tmp.type = VAL_NUMBER;
            tmp.number = 0;
            return tmp;
        }
    }
}

static inline char *eval_to_string(ASTNode *node) {
    Value v = eval(node);
    if (v.type == VAL_STRING) return v.string;
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", v.number);
    return strdup(buf);
}

static inline void interpret(ASTNode *node);

static inline void interpret(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_STATEMENTS:
            for (int j = 0; j < node->statements.count; ++j)
                interpret(node->statements.stmts[j]);
            break;

        case AST_PRINT: {
            char *s = eval_to_string(node->print.expr);
            printf("%s\n", s);
            free(s);
            break;
        }

        case AST_FUNC_DEF:
            add_func(node->funcdef.funcname, node);
            break;

        case AST_RETURN:
            return_value = eval(node->retstmt.expr);
            longjmp(return_buf, 1);
            break;

        case AST_LET:
        case AST_ASSIGN: {
            Value val = eval(node->var.value);
            if (val.type == VAL_STRING) {
                set_var(node->var.varname, VAR_STRING, 0, val.string);
                free(val.string);
            } else if (val.type == VAL_OBJECT) {
                set_var_object(node->var.varname, val.object);
            } else {
                set_var(node->var.varname, VAR_NUMBER, val.number, NULL);
            }
            break;
        }

        case AST_IF:
            if (eval(node->ifstmt.condition).number)
                interpret(node->ifstmt.then_branch);
            else
                interpret(node->ifstmt.else_branch);
            break;
        case AST_WRITE: {
            Value path = eval(node->write.path);
            Value val  = eval(node->write.value);

            if (path.type != VAL_STRING) {
                error(0, "write(): path must be string");
            }

            char *out;
            if (val.type == VAL_STRING) {
                out = val.string;
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", val.number);
                out = strdup(buf);
            }

            FILE *f = fopen(path.string, "wb");
            if (!f) {
                free(path.string);
                if (val.type == VAL_STRING) free(val.string);
                error(0, "write(): cannot open file");
            }

            fwrite(out, 1, strlen(out), f);
            fclose(f);

            free(path.string);
            if (val.type == VAL_STRING) free(val.string);
            else free(out);

            break;
        }

        case AST_WHILE:
            while (eval(node->whilestmt.cond).number)
                interpret(node->whilestmt.body);
            break;

        case AST_FOR: {
            int start = (int)eval(node->forstmt.for_start).number;
            int end   = (int)eval(node->forstmt.for_end).number;
            for (int k = start; k <= end; ++k) {
                set_var(node->forstmt.for_var, VAR_NUMBER, k, NULL);
                interpret(node->forstmt.for_body);
            }
            break;
        }

        case AST_FUNCTION_CALL:
        case AST_ARRAY_LITERAL:
        case AST_INDEX_EXPR:
        case AST_INDEX_ASSIGN:
            (void)eval(node);
            break;

        case AST_RAISE: {
            char *msg = eval_to_string(node->raise.expr);
            error(0, "%s", msg);
            break;
        }


        case AST_WARN: {
            char *msg = eval_to_string(node->warn.expr);
            warn(0, "%s", msg);
            free(msg);
            break;
        }
        case AST_INFO: {
            char *msg = eval_to_string(node->info.expr);
            info(0, "%s", msg);
            free(msg);
            break;
        }

        default:
            break;
    }
}
typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} SpcMemReader;

static inline unsigned char m_u8(SpcMemReader *r) {
    if (r->pos >= r->size) error(0, "Unexpected EOF (mem u8)");
    return r->data[r->pos++];
}

static inline unsigned short m_u16(SpcMemReader *r) {
    if (r->pos + 2 > r->size) error(0, "Unexpected EOF (mem u16)");
    unsigned short v;
    memcpy(&v, r->data + r->pos, 2);
    r->pos += 2;
    return v;
}

static inline unsigned int m_u32(SpcMemReader *r) {
    if (r->pos + 4 > r->size) error(0, "Unexpected EOF (mem u32)");
    unsigned int v;
    memcpy(&v, r->data + r->pos, 4);
    r->pos += 4;
    return v;
}

static inline double m_double(SpcMemReader *r) {
    if (r->pos + sizeof(double) > r->size)
        error(0, "Unexpected EOF (mem double)");
    double d;
    memcpy(&d, r->data + r->pos, sizeof(double));
    r->pos += sizeof(double);
    return d;
}

static inline char *m_str(SpcMemReader *r) {
    unsigned short len = m_u16(r);
    if (r->pos + len > r->size) error(0, "Unexpected EOF (mem string)");
    char *s = (char*)malloc(len + 1);
    memcpy(s, r->data + r->pos, len);
    r->pos += len;
    s[len] = 0;
    return s;
}
static ASTNode *read_ast_node_mem(SpcMemReader *r) {
    unsigned char tag = m_u8(r);

    ASTNodeType type = (ASTNodeType)tag;
    ASTNode *n = ast_new(type);


    switch (n->type) {
        case AST_NUMBER:
            n->number = m_double(r);
            break;

        case AST_STRING:
        case AST_IDENTIFIER:
            n->string = m_str(r);
            break;

        case AST_BINARY_OP:
            n->binop.op = m_str(r);
            n->binop.left  = read_ast_node_mem(r);
            n->binop.right = read_ast_node_mem(r);
            break;

        case AST_PRINT:
            n->print.expr = read_ast_node_mem(r);
            break;

        case AST_LET:
        case AST_ASSIGN:
            n->var.varname = m_str(r);
            n->var.value   = read_ast_node_mem(r);
            break;

        case AST_STATEMENTS: {
            unsigned int c = m_u32(r);
            n->statements.count = (int)c;
            n->statements.stmts = (ASTNode**)calloc(c ? c : 1, sizeof(ASTNode*));
            for (unsigned int i = 0; i < c; i++)
                n->statements.stmts[i] = read_ast_node_mem(r);
            break;
        }

        case AST_FUNCTION_CALL: {
            n->funccall.funcname = m_str(r);
            unsigned int ac = m_u32(r);
            n->funccall.arg_count = (int)ac;
            n->funccall.args = (ASTNode**)calloc(ac ? ac : 1, sizeof(ASTNode*));
            for (unsigned int i = 0; i < ac; i++)
                n->funccall.args[i] = read_ast_node_mem(r);
            break;
        }

        default:
            error(0, "Unsupported AST type in mem reader: %d", n->type);
    }

    return n;
}
static inline ASTNode *read_ast_from_spc_mem(
    const unsigned char *data,
    size_t size
) {
    if (size < 5) error(0, "Invalid SPC (too small)");
    if (memcmp(data, SPC_MAGIC, 4) != 0)
        error(0, "Invalid SPC magic");

    if (data[4] != SPC_VERSION)
        error(0, "Unsupported SPC version");

    SpcMemReader r;
    r.data = data;
    r.size = size;
    r.pos  = 5;

    return read_ast_node_mem(&r);
}


#endif /* Splice_H */
