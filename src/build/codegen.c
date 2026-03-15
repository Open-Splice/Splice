#include "builder.h"

#define SPC_MAGIC "SPC\0"
#define SPC_VERSION 2

typedef struct {
    uint8_t *data;
    int count;
    int cap;
} CodeBuf;

typedef struct {
    uint8_t type;
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

static void wr_u8(FILE *f, uint8_t v) { fwrite(&v, 1, 1, f); }

static void wr_u16(FILE *f, uint16_t v) {
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    fwrite(b, 1, 2, f);
}

static void wr_u32(FILE *f, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, f);
}

static void wr_double(FILE *f, double v) { fwrite(&v, 1, 8, f); }

static void wr_str(FILE *f, const char *s) {
    uint32_t len;
    if (!s) s = "";
    len = (uint32_t)strlen(s);
    wr_u32(f, len);
    if (len) fwrite(s, 1, len, f);
}

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

static uint32_t code_pos(void) { return (uint32_t)g_code.count; }
static void code_emit_op(OpCode op) { code_emit_u8((uint8_t)op); }

static int sym_index(const char *s) {
    int i;
    if (!s) s = "";
    for (i = 0; i < g_syms.count; i++) {
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
    int i;
    for (i = 0; i < g_consts.count; i++) {
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
    int i;
    if (!s) s = "";
    for (i = 0; i < g_consts.count; i++) {
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
    LoopCtx *ctx;
    if (g_loops.count >= g_loops.cap) {
        g_loops.cap = g_loops.cap ? g_loops.cap * 2 : 16;
        g_loops.data = (LoopCtx *)xrealloc(g_loops.data, sizeof(LoopCtx) * (size_t)g_loops.cap);
    }
    ctx = &g_loops.data[g_loops.count++];
    memset(ctx, 0, sizeof(*ctx));
    ctx->continue_target = continue_target;
}

static void loop_set_continue_target(uint32_t target) {
    LoopCtx *ctx = loop_top();
    if (!ctx) die("spbuild: internal loop state");
    ctx->continue_target = target;
}

static void loop_patch_and_pop(uint32_t break_target) {
    int i;
    LoopCtx *ctx = loop_top();
    if (!ctx) die("spbuild: internal loop pop");
    for (i = 0; i < ctx->break_count; i++) code_patch_u32(ctx->break_sites[i], break_target);
    for (i = 0; i < ctx->continue_count; i++) code_patch_u32(ctx->continue_sites[i], ctx->continue_target);
    free(ctx->break_sites);
    free(ctx->continue_sites);
    g_loops.count--;
}

static void emit_node(ASTNode *node);
static void emit_stmt(ASTNode *node);

static void emit_node(ASTNode *node) {
    int i;

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
            int si;
            for (i = 0; i < node->funccall.arg_count; i++) emit_node(node->funccall.args[i]);
            si = sym_index(node->funccall.name);
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
            for (i = 0; i < node->arraylit.count; i++) emit_node(node->arraylit.items[i]);
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
    BCFunc *f;
    uint32_t skip_site;
    int i;

    code_emit_op(OP_JMP);
    skip_site = code_emit_u32_placeholder();

    if (g_funcs.count >= g_funcs.cap) {
        g_funcs.cap = g_funcs.cap ? g_funcs.cap * 2 : 16;
        g_funcs.data = (BCFunc *)xrealloc(g_funcs.data, sizeof(BCFunc) * (size_t)g_funcs.cap);
    }

    f = &g_funcs.data[g_funcs.count++];
    memset(f, 0, sizeof(*f));
    f->name_sym = (uint16_t)sym_index(node->funcdef.name);
    f->param_count = node->funcdef.param_count;
    f->addr = code_pos();
    if (f->param_count > 0) {
        f->params = (uint16_t *)xmalloc(sizeof(uint16_t) * (size_t)f->param_count);
        for (i = 0; i < f->param_count; i++) {
            f->params[i] = (uint16_t)sym_index(node->funcdef.params[i]);
        }
    }

    emit_stmt(node->funcdef.body);
    emit_push_number(0.0);
    code_emit_op(OP_RET);

    code_patch_u32(skip_site, code_pos());
}

static void emit_stmt(ASTNode *node) {
    int i;

    if (!node) return;

    switch (node->type) {
        case AST_STATEMENTS:
            for (i = 0; i < node->statements.count; i++) emit_stmt(node->statements.stmts[i]);
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
            uint32_t jf_site;
            emit_node(node->ifstmt.cond);
            code_emit_op(OP_JMP_IF_FALSE);
            jf_site = code_emit_u32_placeholder();
            emit_stmt(node->ifstmt.then_b);
            if (node->ifstmt.else_b) {
                uint32_t jend_site;
                code_emit_op(OP_JMP);
                jend_site = code_emit_u32_placeholder();
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
            uint32_t jf_site;
            emit_node(node->whilestmt.cond);
            code_emit_op(OP_JMP_IF_FALSE);
            jf_site = code_emit_u32_placeholder();
            loop_push(loop_start);
            emit_stmt(node->whilestmt.body);
            code_emit_op(OP_JMP);
            code_emit_u32(loop_start);
            code_patch_u32(jf_site, code_pos());
            loop_patch_and_pop(code_pos());
            break;
        }
        case AST_FOR: {
            int v;
            uint32_t loop_start;
            uint32_t jf_site;
            uint32_t continue_target;
            emit_node(node->forstmt.start);
            v = sym_index(node->forstmt.var);
            code_emit_op(OP_STORE);
            code_emit_u16((uint16_t)v);

            loop_start = code_pos();
            code_emit_op(OP_LOAD);
            code_emit_u16((uint16_t)v);
            emit_node(node->forstmt.end);
            code_emit_op(OP_LTE);
            code_emit_op(OP_JMP_IF_FALSE);
            jf_site = code_emit_u32_placeholder();

            loop_push(0);
            emit_stmt(node->forstmt.body);

            continue_target = code_pos();
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
            uint32_t site;
            if (!ctx) die("spbuild: break outside loop");
            code_emit_op(OP_JMP);
            site = code_emit_u32_placeholder();
            vec_u32_push(&ctx->break_sites, &ctx->break_count, &ctx->break_cap, site);
            break;
        }
        case AST_CONTINUE: {
            LoopCtx *ctx = loop_top();
            uint32_t site;
            if (!ctx) die("spbuild: continue outside loop");
            code_emit_op(OP_JMP);
            site = code_emit_u32_placeholder();
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
    int i;

    free(g_code.data);
    g_code.data = NULL;
    g_code.count = g_code.cap = 0;

    for (i = 0; i < g_consts.count; i++) free(g_consts.data[i].string);
    free(g_consts.data);
    g_consts.data = NULL;
    g_consts.count = g_consts.cap = 0;

    for (i = 0; i < g_syms.count; i++) free(g_syms.data[i]);
    free(g_syms.data);
    g_syms.data = NULL;
    g_syms.count = g_syms.cap = 0;

    for (i = 0; i < g_funcs.count; i++) free(g_funcs.data[i].params);
    free(g_funcs.data);
    g_funcs.data = NULL;
    g_funcs.count = g_funcs.cap = 0;

    free(g_loops.data);
    g_loops.data = NULL;
    g_loops.count = g_loops.cap = 0;
}

int write_spc(const char *out_path, ASTNode *root) {
    int fd;
    FILE *f;
    int i;

    free_codegen_state();
    emit_stmt(root);
    code_emit_op(OP_HALT);

    fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return 0;
    f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return 0;
    }

    fwrite(SPC_MAGIC, 1, 4, f);
    wr_u8(f, (uint8_t)SPC_VERSION);

    wr_u16(f, (uint16_t)g_consts.count);
    for (i = 0; i < g_consts.count; i++) {
        wr_u8(f, g_consts.data[i].type);
        if (g_consts.data[i].type == 0) wr_double(f, g_consts.data[i].number);
        else wr_str(f, g_consts.data[i].string);
    }

    wr_u16(f, (uint16_t)g_syms.count);
    for (i = 0; i < g_syms.count; i++) wr_str(f, g_syms.data[i]);

    wr_u16(f, (uint16_t)g_funcs.count);
    for (i = 0; i < g_funcs.count; i++) {
        int j;
        wr_u16(f, g_funcs.data[i].name_sym);
        wr_u16(f, (uint16_t)g_funcs.data[i].param_count);
        wr_u32(f, g_funcs.data[i].addr);
        for (j = 0; j < g_funcs.data[i].param_count; j++) wr_u16(f, g_funcs.data[i].params[j]);
    }

    wr_u32(f, (uint32_t)g_code.count);
    if (g_code.count > 0) fwrite(g_code.data, 1, (size_t)g_code.count, f);

    fclose(f);
    free_codegen_state();
    return 1;
}
