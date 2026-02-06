#ifndef SPLICE_H
#define SPLICE_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#if defined(SPLICE_PLATFORM_ARDUINO)
  #include <Arduino.h>
  #define SPLICE_PRINTLN(s) Serial.println(s)
  #define SPLICE_FAIL(msg) do { Serial.println(msg); while (1) delay(1000); } while (0)
#else
  #define SPLICE_PRINTLN(s) puts(s)
  #define SPLICE_FAIL(msg) do { fprintf(stderr, "%s\n", msg); exit(1); } while (0)
#endif

/* ================= ARENA (RUNTIME ALLOCATED) ================= */

static unsigned char *splice_arena = NULL;
static size_t splice_arena_size = 0;
static size_t splice_arena_pos  = 0;

static void arena_init(size_t size) {
    splice_arena = (unsigned char *)malloc(size);
    if (!splice_arena) SPLICE_FAIL("ARENA_ALLOC_FAIL");
    splice_arena_size = size;
    splice_arena_pos = 0;
    memset(splice_arena, 0, size);
}

static void arena_free(void) {
    free(splice_arena);
    splice_arena = NULL;
    splice_arena_size = splice_arena_pos = 0;
}

static void *arena_alloc(size_t n) {
    n = (n + 7) & ~7;
    if (splice_arena_pos + n > splice_arena_size)
        SPLICE_FAIL("ARENA_OOM");
    void *p = splice_arena + splice_arena_pos;
    splice_arena_pos += n;
    return p;
}

/* ================= VALUES ================= */

typedef enum { VAL_NUMBER, VAL_STRING, VAL_OBJECT } ValueType;

typedef struct {
    ValueType type;
    double number;
    const char *string;
    void *object;
} Value;

/* ================= EXEC ================= */

typedef enum {
    EXEC_OK,
    EXEC_BREAK,
    EXEC_CONTINUE,
    EXEC_RETURN,
    EXEC_ERROR
} ExecResult;

static Value splice_return_value;

/* ================= AST ================= */

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
    AST_FOR
} ASTNodeType;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeType type;
    union {
        double number;
        const char *string;

        struct { const char *op; ASTNode *left; ASTNode *right; } binop;
        struct { const char *name; ASTNode *value; } var;
        struct { ASTNode *expr; } print;
        struct { ASTNode *cond; ASTNode *body; } whilestmt;
        struct { ASTNode *cond; ASTNode *then_b; ASTNode *else_b; } ifstmt;
        struct { ASTNode **stmts; int count; } statements;
        struct { const char *name; ASTNode *body; } funcdef;
        struct { const char *name; } funccall;
        struct { ASTNode *expr; } retstmt;
        struct { const char *var; ASTNode *start; ASTNode *end; ASTNode *body; } forstmt;
    };
};

/* ================= VARIABLES ================= */

#define VAR_TABLE_SIZE 64

typedef struct {
    const char *name;
    Value value;
    int used;
} VarSlot;

static VarSlot var_table[VAR_TABLE_SIZE];

static unsigned hash_str(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static VarSlot *get_var(const char *name) {
    unsigned i = hash_str(name) & (VAR_TABLE_SIZE - 1);
    for (;;) {
        VarSlot *v = &var_table[i];
        if (!v->used) return NULL;
        if (strcmp(v->name, name) == 0) return v;
        i = (i + 1) & (VAR_TABLE_SIZE - 1);
    }
}

static void set_var(const char *name, Value v) {
    unsigned i = hash_str(name) & (VAR_TABLE_SIZE - 1);
    for (;;) {
        VarSlot *s = &var_table[i];
        if (!s->used || strcmp(s->name, name) == 0) {
            s->used = 1;
            s->name = name;
            s->value = v;
            return;
        }
        i = (i + 1) & (VAR_TABLE_SIZE - 1);
    }
}

/* ================= FUNCTIONS ================= */

#define FUNC_TABLE_SIZE 32

typedef struct {
    const char *name;
    ASTNode *body;
    int used;
} FuncSlot;

static FuncSlot func_table[FUNC_TABLE_SIZE];

static void add_func(const char *name, ASTNode *body) {
    unsigned i = hash_str(name) & (FUNC_TABLE_SIZE - 1);
    for (;;) {
        FuncSlot *f = &func_table[i];
        if (!f->used || strcmp(f->name, name) == 0) {
            f->used = 1;
            f->name = name;
            f->body = body;
            return;
        }
        i = (i + 1) & (FUNC_TABLE_SIZE - 1);
    }
}

static ASTNode *get_func(const char *name) {
    unsigned i = hash_str(name) & (FUNC_TABLE_SIZE - 1);
    for (;;) {
        FuncSlot *f = &func_table[i];
        if (!f->used) return NULL;
        if (strcmp(f->name, name) == 0) return f->body;
        i = (i + 1) & (FUNC_TABLE_SIZE - 1);
    }
}

/* ================= SPC LOADER ================= */

#define SPC_MAGIC "SPC\0"
#define SPC_VERSION 1

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} Reader;

static unsigned char rd_u8(Reader *r) {
    if (r->pos >= r->size) SPLICE_FAIL("SPC_EOF");
    return r->data[r->pos++];
}

static uint32_t rd_u32(Reader *r) {
    uint32_t v = 0;
    v |= rd_u8(r);
    v |= rd_u8(r) << 8;
    v |= rd_u8(r) << 16;
    v |= rd_u8(r) << 24;
    return v;
}

static const char *rd_str(Reader *r) {
    uint32_t len = rd_u32(r);
    if (r->pos + len > r->size) SPLICE_FAIL("SPC_STR");
    char *s = (char *)arena_alloc(len + 1);
    memcpy(s, r->data + r->pos, len);
    s[len] = 0;
    r->pos += len;
    return s;
}

static ASTNode *read_node(Reader *r);

static ASTNode *read_node(Reader *r) {
    ASTNode *n = (ASTNode *)arena_alloc(sizeof(ASTNode));
    n->type = (ASTNodeType)rd_u8(r);

    switch (n->type) {
        case AST_NUMBER: {
            double tmp;
            memcpy(&tmp, r->data + r->pos, 8);
            r->pos += 8;
            n->number = tmp;
            break;
        }
        case AST_STRING:
        case AST_IDENTIFIER:
            n->string = rd_str(r);
            break;
        case AST_PRINT:
            n->print.expr = read_node(r);
            break;
        case AST_LET:
        case AST_ASSIGN:
            n->var.name = rd_str(r);
            n->var.value = read_node(r);
            break;
        case AST_BINARY_OP:
            n->binop.op = rd_str(r);
            n->binop.left = read_node(r);
            n->binop.right = read_node(r);
            break;
        case AST_STATEMENTS: {
            int c = (int)rd_u32(r);
            n->statements.count = c;
            n->statements.stmts = (ASTNode **)arena_alloc(sizeof(ASTNode *) * c);
            for (int i = 0; i < c; i++) n->statements.stmts[i] = read_node(r);
            break;
        }
        case AST_FUNC_DEF:
            n->funcdef.name = rd_str(r);
            n->funcdef.body = read_node(r);
            break;
        case AST_FUNCTION_CALL:
            n->funccall.name = rd_str(r);
            break;
        case AST_RETURN:
            n->retstmt.expr = read_node(r);
            break;
        case AST_WHILE:
            n->whilestmt.cond = read_node(r);
            n->whilestmt.body = read_node(r);
            break;
        case AST_IF:
            n->ifstmt.cond = read_node(r);
            n->ifstmt.then_b = read_node(r);
            n->ifstmt.else_b = read_node(r);
            break;
        case AST_FOR:
            n->forstmt.var = rd_str(r);
            n->forstmt.start = read_node(r);
            n->forstmt.end = read_node(r);
            n->forstmt.body = read_node(r);
            break;
        default:
            break;
    }
    return n;
}

static ASTNode *read_ast_from_spc_mem(const unsigned char *data, size_t size) {
    if (size < 5) SPLICE_FAIL("SPC_SHORT");
    if (memcmp(data, SPC_MAGIC, 4) != 0) SPLICE_FAIL("SPC_MAGIC");
    if (data[4] != SPC_VERSION) SPLICE_FAIL("SPC_VERSION");
    Reader r = { data, size, 5 };
    return read_node(&r);
}

/* ================= EVAL ================= */

static Value eval(ASTNode *n) {
    switch (n->type) {
        case AST_NUMBER: return (Value){ VAL_NUMBER, n->number, NULL, NULL };
        case AST_STRING: return (Value){ VAL_STRING, 0, n->string, NULL };
        case AST_IDENTIFIER: {
            VarSlot *v = get_var(n->string);
            return v ? v->value : (Value){ VAL_NUMBER, 0, NULL, NULL };
        }
        case AST_BINARY_OP: {
            Value a = eval(n->binop.left);
            Value b = eval(n->binop.right);
            if (!strcmp(n->binop.op, "+")) {
                if (a.type == VAL_STRING && b.type == VAL_STRING) {
                    size_t la = strlen(a.string);
                    size_t lb = strlen(b.string);
                    char *s = arena_alloc(la + lb + 1);
                    memcpy(s, a.string, la);
                    memcpy(s + la, b.string, lb);
                    s[la + lb] = 0;
                    return (Value){ VAL_STRING, 0, s, NULL };
                }
                // fallback numeric +
                return (Value){ VAL_NUMBER, a.number + b.number, NULL, NULL };
            }
            
            if (!strcmp(n->binop.op, "+")) return (Value){ VAL_NUMBER, a.number + b.number, NULL, NULL };
            if (!strcmp(n->binop.op, "-")) return (Value){ VAL_NUMBER, a.number - b.number, NULL, NULL };
            if (!strcmp(n->binop.op, "*")) return (Value){ VAL_NUMBER, a.number * b.number, NULL, NULL };
            if (!strcmp(n->binop.op, "/")) return (Value){ VAL_NUMBER, a.number / b.number, NULL, NULL };
            return (Value){ VAL_NUMBER, 0, NULL, NULL };
        }
        default:
            return (Value){ VAL_NUMBER, 0, NULL, NULL };
    }
}
static void splice_print_value(Value v) {
    char buf[32];

    switch (v.type) {

    case VAL_STRING:
        if (v.string) {
            SPLICE_PRINTLN(v.string);
        } else {
            SPLICE_PRINTLN("(null)");
        }
        break;

    case VAL_NUMBER:
        // portable, works everywhere
        snprintf(buf, sizeof(buf), "%g", v.number);
        SPLICE_PRINTLN(buf);
        break;

    case VAL_OBJECT:
        // placeholder until objects exist
        SPLICE_PRINTLN("<object>");
        break;

    default:
        SPLICE_PRINTLN("<unknown>");
        break;
    }
}


/* ================= INTERPRET ================= */

static ExecResult interpret(ASTNode *n) {
    switch (n->type) {

        case AST_STATEMENTS:
            for (int i = 0; i < n->statements.count; i++) {
                ExecResult r = interpret(n->statements.stmts[i]);
                if (r != EXEC_OK) return r;
            }
            return EXEC_OK;

        case AST_PRINT: {
            Value v = eval(n->print.expr);
            splice_print_value(v);
            return EXEC_OK;
        }
            

        case AST_LET:
        case AST_ASSIGN:
            set_var(n->var.name, eval(n->var.value));
            return EXEC_OK;

        case AST_IF:
            return eval(n->ifstmt.cond).number
                ? interpret(n->ifstmt.then_b)
                : interpret(n->ifstmt.else_b);

        case AST_WHILE:
            while (eval(n->whilestmt.cond).number) {
                ExecResult r = interpret(n->whilestmt.body);
                if (r == EXEC_BREAK) break;
                if (r == EXEC_CONTINUE) continue;
                if (r != EXEC_OK) return r;
            }
            return EXEC_OK;

        case AST_FOR: {
            int s = (int)eval(n->forstmt.start).number;
            int e = (int)eval(n->forstmt.end).number;
            for (int i = s; i <= e; i++) {
                set_var(n->forstmt.var, (Value){ VAL_NUMBER, i, NULL, NULL });
                ExecResult r = interpret(n->forstmt.body);
                if (r == EXEC_BREAK) break;
                if (r == EXEC_CONTINUE) continue;
                if (r != EXEC_OK) return r;
            }
            return EXEC_OK;
        }

        case AST_BREAK: return EXEC_BREAK;
        case AST_CONTINUE: return EXEC_CONTINUE;

        case AST_RETURN:
            splice_return_value = eval(n->retstmt.expr);
            return EXEC_RETURN;

        case AST_FUNC_DEF:
            add_func(n->funcdef.name, n->funcdef.body);
            return EXEC_OK;

        case AST_FUNCTION_CALL: {
            ASTNode *body = get_func(n->funccall.name);
            if (!body) SPLICE_FAIL("UNDEF_FUNC");
            ExecResult r = interpret(body);
            if (r == EXEC_RETURN) return EXEC_OK;
            return r;
        }

        default:
            eval(n);
            return EXEC_OK;
    }
}

/* ================= RESET ================= */

static void splice_reset_vm(void) {
    memset(var_table, 0, sizeof(var_table));
    memset(func_table, 0, sizeof(func_table));
    splice_return_value = (Value){ VAL_NUMBER, 0, NULL, NULL };
}

#endif
