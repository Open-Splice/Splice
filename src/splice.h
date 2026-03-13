#ifndef SPLICE_H
#define SPLICE_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(SPLICE_PLATFORM_ARDUINO)
#include <Arduino.h>
#define SPLICE_PRINTLN(s) Serial.println(s)
#define SPLICE_FAIL(msg) do { Serial.println(msg); while (1) delay(1000); } while (0)
#else
#define SPLICE_PRINTLN(s) puts(s)
#define SPLICE_FAIL(msg) do { fprintf(stderr, "%s\n", msg); exit(1); } while (0)
#endif

/* ================= VALUES ================= */

typedef enum { VAL_NUMBER, VAL_STRING, VAL_OBJECT } ValueType;

typedef struct Value {
    ValueType type;
    double number;
    const char *string;
    void *object;
} Value;

typedef enum {
    OBJ_ARRAY,
    OBJ_TUPLE
} ObjectType;

typedef struct {
    ObjectType type;
    int count;
    int capacity;
    Value *items;
} ObjArray;

/* SDK depends on Value being defined */
#include "sdk.h"
#include "opcode.h"

/* ================= AST (kept for parser compatibility) ================= */

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
        const char *string;

        struct { const char *op; ASTNode *left; ASTNode *right; } binop;
        struct { const char *name; ASTNode *value; } var;
        struct { ASTNode *expr; } print;
        struct { ASTNode *cond; ASTNode *body; } whilestmt;
        struct { ASTNode *cond; ASTNode *then_b; ASTNode *else_b; } ifstmt;
        struct { ASTNode **stmts; int count; } statements;
        struct { const char *name; const char **params; int param_count; ASTNode *body; } funcdef;
        struct { const char *name; ASTNode **args; int arg_count; } funccall;
        struct { ASTNode *expr; } retstmt;
        struct { const char *var; ASTNode *start; ASTNode *end; ASTNode *body; } forstmt;
        struct { ASTNode **items; int count; } arraylit;
        struct { ASTNode *array; ASTNode *index; } index;
        struct { ASTNode *array; ASTNode *index; ASTNode *value; } indexassign;
    };
};

/* ================= BYTECODE VM ================= */

#define SPC_MAGIC "SPC\0"
#define SPC_VERSION 2

#define VM_STACK_MAX 1024
#define VAR_STACK_MAX 32
#define CALLSTACK_MAX 64
#define VM_ARG_MAX 64

typedef enum {
    CONST_NUMBER = 0,
    CONST_STRING = 1
} ConstType;

typedef struct {
    uint8_t type;
    double number;
    const char *string;
} Constant;

typedef struct {
    uint16_t symbol;
    uint16_t param_count;
    uint32_t addr;
    uint16_t *params;
} FunctionEntry;

typedef struct {
    const unsigned char *code;
    uint32_t code_size;
    int owns_code;

    Constant *consts;
    uint16_t const_count;

    const char **symbols;
    uint16_t symbol_count;

    FunctionEntry *funcs;
    uint16_t func_count;
    FunctionEntry **func_by_symbol;
    Value *global_values;
    uint8_t *global_used;
    Value *frame_values;
    uint8_t *frame_stamp;
} BytecodeProgram;

typedef struct {
    uint32_t return_ip;
} CallFrame;

static Value vm_stack[VM_STACK_MAX];
static int vm_sp = 0;
static uint32_t vm_ip = 0;

static int var_stack_depth = 0;
static uint8_t vm_frame_epoch[VAR_STACK_MAX];

static CallFrame vm_callstack[CALLSTACK_MAX];
static int vm_callsp = 0;

static Value value_number(double n) {
    Value v = { VAL_NUMBER, n, NULL, NULL };
    return v;
}

static Value value_string(const char *s) {
    Value v = { VAL_STRING, 0.0, s, NULL };
    return v;
}

static int value_truthy(Value v) {
    if (v.type == VAL_STRING) return v.string && v.string[0] != '\0';
    if (v.type == VAL_OBJECT) return v.object != NULL;
    return v.number != 0.0;
}

static int value_eq(Value a, Value b) {
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char *as = a.string ? a.string : "";
        const char *bs = b.string ? b.string : "";
        return strcmp(as, bs) == 0;
    }
    return a.number == b.number;
}

static void splice_print_value(Value v) {
    char buf[64];
    switch (v.type) {
        case VAL_STRING:
            SPLICE_PRINTLN(v.string ? v.string : "(null)");
            break;
        case VAL_NUMBER:
            snprintf(buf, sizeof(buf), "%g", v.number);
            SPLICE_PRINTLN(buf);
            break;
        case VAL_OBJECT:
            SPLICE_PRINTLN("<object>");
            break;
        default:
            SPLICE_PRINTLN("<unknown>");
            break;
    }
}

static uint8_t rd_u8(const unsigned char *data, size_t size, size_t *pos) {
    if (*pos >= size) SPLICE_FAIL("SPC_EOF");
    return data[(*pos)++];
}

static uint16_t rd_u16(const unsigned char *data, size_t size, size_t *pos) {
    uint16_t v = 0;
    v |= (uint16_t)rd_u8(data, size, pos);
    v |= (uint16_t)rd_u8(data, size, pos) << 8;
    return v;
}

static uint32_t rd_u32(const unsigned char *data, size_t size, size_t *pos) {
    uint32_t v = 0;
    v |= (uint32_t)rd_u8(data, size, pos);
    v |= (uint32_t)rd_u8(data, size, pos) << 8;
    v |= (uint32_t)rd_u8(data, size, pos) << 16;
    v |= (uint32_t)rd_u8(data, size, pos) << 24;
    return v;
}

static double rd_double(const unsigned char *data, size_t size, size_t *pos) {
    if (*pos + 8 > size) SPLICE_FAIL("SPC_EOF");
    double out;
    memcpy(&out, data + *pos, 8);
    *pos += 8;
    return out;
}

static char *rd_str(const unsigned char *data, size_t size, size_t *pos) {
    uint32_t len = rd_u32(data, size, pos);
    if (*pos + len > size) SPLICE_FAIL("SPC_STR");
    char *s = (char *)malloc((size_t)len + 1);
    if (!s) SPLICE_FAIL("OOM");
    memcpy(s, data + *pos, len);
    s[len] = 0;
    *pos += len;
    return s;
}

static void free_program(BytecodeProgram *p) {
    if (!p) return;

    if (p->consts) {
        for (uint16_t i = 0; i < p->const_count; i++) {
            if (p->consts[i].type == CONST_STRING) free((void *)p->consts[i].string);
        }
    }
    if (p->symbols) {
        for (uint16_t i = 0; i < p->symbol_count; i++) free((void *)p->symbols[i]);
    }
    if (p->funcs) {
        for (uint16_t i = 0; i < p->func_count; i++) free(p->funcs[i].params);
    }

    free(p->consts);
    free((void *)p->symbols);
    free(p->funcs);
    free(p->func_by_symbol);
    free(p->global_values);
    free(p->global_used);
    free(p->frame_values);
    free(p->frame_stamp);
    if (p->owns_code) free((void *)p->code);
    memset(p, 0, sizeof(*p));
}

static int load_program(const unsigned char *data, size_t size, BytecodeProgram *out) {
    memset(out, 0, sizeof(*out));

    if (size < 5) return 0;
    if (memcmp(data, SPC_MAGIC, 4) != 0) return 0;
    if (data[4] != SPC_VERSION) return 0;

    size_t pos = 5;

    out->const_count = rd_u16(data, size, &pos);
    out->consts = (Constant *)calloc(out->const_count ? out->const_count : 1, sizeof(Constant));
    if (!out->consts) return 0;
    for (uint16_t i = 0; i < out->const_count; i++) {
        out->consts[i].type = rd_u8(data, size, &pos);
        if (out->consts[i].type == CONST_NUMBER) {
            out->consts[i].number = rd_double(data, size, &pos);
        } else if (out->consts[i].type == CONST_STRING) {
            out->consts[i].string = rd_str(data, size, &pos);
        } else {
            return 0;
        }
    }

    out->symbol_count = rd_u16(data, size, &pos);
    out->symbols = (const char **)calloc(out->symbol_count ? out->symbol_count : 1, sizeof(char *));
    if (!out->symbols) return 0;
    for (uint16_t i = 0; i < out->symbol_count; i++) {
        out->symbols[i] = rd_str(data, size, &pos);
    }

    out->func_count = rd_u16(data, size, &pos);
    out->funcs = (FunctionEntry *)calloc(out->func_count ? out->func_count : 1, sizeof(FunctionEntry));
    if (!out->funcs) return 0;
    for (uint16_t i = 0; i < out->func_count; i++) {
        out->funcs[i].symbol = rd_u16(data, size, &pos);
        out->funcs[i].param_count = rd_u16(data, size, &pos);
        out->funcs[i].addr = rd_u32(data, size, &pos);
        if (out->funcs[i].param_count > 0) {
            out->funcs[i].params = (uint16_t *)calloc(out->funcs[i].param_count, sizeof(uint16_t));
            if (!out->funcs[i].params) return 0;
            for (uint16_t j = 0; j < out->funcs[i].param_count; j++) {
                out->funcs[i].params[j] = rd_u16(data, size, &pos);
            }
        }
    }

    out->func_by_symbol = (FunctionEntry **)calloc(out->symbol_count ? out->symbol_count : 1, sizeof(FunctionEntry *));
    if (!out->func_by_symbol) return 0;
    for (uint16_t i = 0; i < out->func_count; i++) {
        if (out->funcs[i].symbol < out->symbol_count) out->func_by_symbol[out->funcs[i].symbol] = &out->funcs[i];
    }

    out->global_values = (Value *)calloc(out->symbol_count ? out->symbol_count : 1, sizeof(Value));
    out->global_used = (uint8_t *)calloc(out->symbol_count ? out->symbol_count : 1, sizeof(uint8_t));
    out->frame_values = (Value *)calloc((size_t)(out->symbol_count ? out->symbol_count : 1) * VAR_STACK_MAX, sizeof(Value));
    out->frame_stamp = (uint8_t *)calloc((size_t)(out->symbol_count ? out->symbol_count : 1) * VAR_STACK_MAX, sizeof(uint8_t));
    if (!out->global_values || !out->global_used || !out->frame_values || !out->frame_stamp) return 0;

    out->code_size = rd_u32(data, size, &pos);
    if (pos + out->code_size > size) return 0;
    out->code = data + pos;
    out->owns_code = 0;

    return 1;
}

static inline FunctionEntry *find_function(const BytecodeProgram *p, uint16_t symbol_idx) {
    if (symbol_idx >= p->symbol_count) return NULL;
    return p->func_by_symbol[symbol_idx];
}

static Value call_builtin_or_native(const char *name, int argc, Value *argv) {
#ifdef _WIN32
#define splice_sleep_ms(ms) Sleep((DWORD)(ms))
#else
#define splice_sleep_ms(ms) usleep((useconds_t)((ms) * 1000))
#endif

    if (strcmp(name, "print") == 0) {
        if (argc > 0) splice_print_value(argv[0]);
        return value_number(0.0);
    }

    if (strcmp(name, "input") == 0) {
        if (argc > 0) {
            if (argv[0].type == VAL_STRING) {
                fputs(argv[0].string ? argv[0].string : "", stdout);
            } else {
                char pbuf[64];
                snprintf(pbuf, sizeof(pbuf), "%g", argv[0].number);
                fputs(pbuf, stdout);
            }
            fflush(stdout);
        }

        char in[512];
        if (!fgets(in, sizeof(in), stdin)) return value_string("");

        size_t n = strlen(in);
        if (n > 0 && in[n - 1] == '\n') in[n - 1] = '\0';

        char *copy = (char *)malloc(strlen(in) + 1);
        if (!copy) SPLICE_FAIL("OOM");
        strcpy(copy, in);
        return value_string(copy);
    }

    if (strcmp(name, "sleep") == 0) {
        double secs = (argc > 0) ? argv[0].number : 0.0;
        if (secs < 0.0) secs = 0.0;
        splice_sleep_ms((unsigned int)(secs * 1000.0));
        return value_number(0.0);
    }

    if (strcmp(name, "noop") == 0) {
        return value_number(0.0);
    }

    if (strcmp(name, "len") == 0) {
        if (argc < 1) return value_number(0.0);
        Value v = argv[0];
        if (v.type == VAL_STRING) return value_number((double)strlen(v.string ? v.string : ""));
        if (v.type == VAL_OBJECT && v.object) {
            ObjArray *oa = (ObjArray *)v.object;
            if (oa->type == OBJ_ARRAY || oa->type == OBJ_TUPLE) return value_number((double)oa->count);
        }
        return value_number(0.0);
    }

    if (strcmp(name, "append") == 0) {
        if (argc < 2) return value_number(0.0);
        Value target = argv[0];
        Value val = argv[1];
        if (target.type != VAL_OBJECT || !target.object) SPLICE_FAIL("APPEND_TARGET");
        ObjArray *oa = (ObjArray *)target.object;
        if (oa->type != OBJ_ARRAY) SPLICE_FAIL("APPEND_TARGET");
        if (oa->count >= oa->capacity) {
            int newcap = oa->capacity ? oa->capacity * 2 : 4;
            Value *ni = (Value *)realloc(oa->items, sizeof(Value) * (size_t)newcap);
            if (!ni) SPLICE_FAIL("ARRAY_OOM");
            oa->items = ni;
            oa->capacity = newcap;
        }
        if (val.type == VAL_STRING) {
            Value copy = val;
            copy.string = strdup(val.string ? val.string : "");
            oa->items[oa->count++] = copy;
        } else {
            oa->items[oa->count++] = val;
        }
        return value_number((double)oa->count);
    }

    if (strcmp(name, "sin") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(sin(argv[0].number));
    }

    if (strcmp(name, "cos") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(cos(argv[0].number));
    }

    if (strcmp(name, "tan") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(tan(argv[0].number));
    }

    if (strcmp(name, "sqrt") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER || argv[0].number < 0.0) return value_number(0.0);
        return value_number(sqrt(argv[0].number));
    }

    if (strcmp(name, "pow") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER) return value_number(0.0);
        return value_number(pow(argv[0].number, argv[1].number));
    }

    if (strcmp(name, "mod") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER || argv[1].number == 0.0) {
            return value_number(0.0);
        }
        return value_number(fmod(argv[0].number, argv[1].number));
    }

    if (strcmp(name, "abs") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(fabs(argv[0].number));
    }

    if (strcmp(name, "floor") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(floor(argv[0].number));
    }

    if (strcmp(name, "ceil") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(ceil(argv[0].number));
    }

    if (strcmp(name, "round") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(round(argv[0].number));
    }

    if (strcmp(name, "min") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER) return value_number(0.0);
        return value_number(argv[0].number < argv[1].number ? argv[0].number : argv[1].number);
    }

    if (strcmp(name, "max") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER) return value_number(0.0);
        return value_number(argv[0].number > argv[1].number ? argv[0].number : argv[1].number);
    }

    if (strcmp(name, "clamp") == 0) {
        if (argc < 3 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER || argv[2].type != VAL_NUMBER) {
            return value_number(0.0);
        }
        double x = argv[0].number;
        double lo = argv[1].number;
        double hi = argv[2].number;
        if (x < lo) x = lo;
        if (x > hi) x = hi;
        return value_number(x);
    }
    if (strcmp(name, "to_number") == 0) {
        if (argc < 1) return value_number(0.0);

        if (argv[0].type == VAL_NUMBER)
            return argv[0];

        if (argv[0].type == VAL_STRING) {
            const char *s = argv[0].string ? argv[0].string : "";
            return value_number(strtod(s, NULL));
        }

        return value_number(0.0);
    }
    if (strcmp(name, "lerp") == 0) {
        if (argc < 3 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER || argv[2].type != VAL_NUMBER) {
            return value_number(0.0);
        }
        return value_number(argv[0].number + (argv[1].number - argv[0].number) * argv[2].number);
    }
    if (strcmp(name, "slice") == 0) {
        if (argc < 3) return value_number(0.0);

        Value arrv = argv[0];
        int start = (int)argv[1].number;
        int end   = (int)argv[2].number;

        if (arrv.type != VAL_OBJECT || !arrv.object)
            return value_number(0.0);

        ObjArray *src = (ObjArray*)arrv.object;

        if (start < 0) start = 0;
        if (end > src->count) end = src->count;
        if (end < start) end = start;

        int count = end - start;

        ObjArray *oa = malloc(sizeof(ObjArray));
        oa->type = OBJ_ARRAY;
        oa->count = count;
        oa->capacity = count;
        oa->items = malloc(sizeof(Value) * count);

        for (int i=0;i<count;i++)
            oa->items[i] = src->items[start+i];

        Value out = { VAL_OBJECT, 0, NULL, oa };
        return out;
    }
    if (strcmp(name, "split") == 0) {
        if (argc < 2) return value_number(0.0);

        const char *str = argv[0].string ? argv[0].string : "";
        const char *sep = argv[1].string ? argv[1].string : "";

        ObjArray *oa = malloc(sizeof(ObjArray));
        oa->type = OBJ_ARRAY;
        oa->count = 0;
        oa->capacity = 8;
        oa->items = malloc(sizeof(Value) * oa->capacity);

        char *copy = strdup(str);
        char *tok = strtok(copy, sep);

        while (tok) {
            if (oa->count >= oa->capacity) {
                oa->capacity *= 2;
                oa->items = realloc(oa->items,sizeof(Value)*oa->capacity);
            }

            oa->items[oa->count++] = value_string(strdup(tok));
            tok = strtok(NULL, sep);
        }

        free(copy);

        Value out = { VAL_OBJECT, 0, NULL, oa };
        return out;
    }
    SpliceCFunc native = Splice_get_native(name);
    if (!native) SPLICE_FAIL("UNDEF_FUNC");
    return native(argc, argv);
}

static inline void vm_push_fast(int *sp, Value v) {
#ifndef NDEBUG
    if (*sp >= VM_STACK_MAX) SPLICE_FAIL("STACK_OVERFLOW");
#endif
    vm_stack[(*sp)++] = v;
}

static inline Value vm_pop_fast(int *sp) {
#ifndef NDEBUG
    if (*sp <= 0) SPLICE_FAIL("STACK_UNDERFLOW");
#endif
    return vm_stack[--(*sp)];
}

static inline uint16_t fetch_u16_fast(const BytecodeProgram *p, uint32_t *ip) {
#ifndef NDEBUG
    if (*ip + 2 > p->code_size) SPLICE_FAIL("IP_OOB");
#endif
    uint16_t v = (uint16_t)p->code[*ip] | ((uint16_t)p->code[*ip + 1] << 8);
    *ip += 2;
    return v;
}

static inline uint32_t fetch_u32_fast(const BytecodeProgram *p, uint32_t *ip) {
#ifndef NDEBUG
    if (*ip + 4 > p->code_size) SPLICE_FAIL("IP_OOB");
#endif
    uint32_t v = (uint32_t)p->code[*ip] |
                 ((uint32_t)p->code[*ip + 1] << 8) |
                 ((uint32_t)p->code[*ip + 2] << 16) |
                 ((uint32_t)p->code[*ip + 3] << 24);
    *ip += 4;
    return v;
}

static void splice_reset_vm(void) {
    vm_sp = 0;
    vm_ip = 0;
    var_stack_depth = 0;
    memset(vm_frame_epoch, 0, sizeof(vm_frame_epoch));
    vm_callsp = 0;
}

static int splice_execute_bytecode(const unsigned char *data, size_t size) {
    BytecodeProgram prog;
    if (!load_program(data, size, &prog)) return 0;

    splice_reset_vm();

    int sp = vm_sp;
    uint32_t ip = vm_ip;
    int callsp = vm_callsp;
    int depth = var_stack_depth;

#define vm_push(v) vm_push_fast(&sp, (v))
#define vm_pop() vm_pop_fast(&sp)
#define fetch_u16(p) fetch_u16_fast((p), &ip)
#define fetch_u32(p) fetch_u32_fast((p), &ip)
#define vm_ip ip
#define vm_callsp callsp
#define var_stack_depth depth
#define SYNC_VM_STATE() do { vm_sp = sp; vm_ip = ip; vm_callsp = callsp; var_stack_depth = depth; } while (0)

    while (vm_ip < prog.code_size) {
        OpCode op = (OpCode)prog.code[vm_ip++];

        switch (op) {
            case OP_PUSH_CONST: {
                uint16_t idx = fetch_u16(&prog);
                if (idx >= prog.const_count) SPLICE_FAIL("CONST_OOB");
                Constant c = prog.consts[idx];
                if (c.type == CONST_NUMBER) vm_push(value_number(c.number));
                else vm_push(value_string(c.string ? c.string : ""));
                break;
            }

            case OP_LOAD: {
                uint16_t idx = fetch_u16(&prog);
                if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                if (var_stack_depth > 0) {
                    size_t frame = (size_t)var_stack_depth - 1u;
                    size_t off = frame * prog.symbol_count + idx;
                    if (prog.frame_stamp[off] == vm_frame_epoch[frame]) {
                        vm_push(prog.frame_values[off]);
                        break;
                    }
                }
                vm_push(prog.global_used[idx] ? prog.global_values[idx] : value_number(0.0));
                break;
            }

            case OP_STORE: {
                uint16_t idx = fetch_u16(&prog);
                if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                Value v = vm_pop();
                if (var_stack_depth > 0) {
                    size_t frame = (size_t)var_stack_depth - 1u;
                    size_t off = frame * prog.symbol_count + idx;
                    if (prog.frame_stamp[off] == vm_frame_epoch[frame]) {
                        prog.frame_values[off] = v;
                    } else if (prog.global_used[idx]) {
                        prog.global_values[idx] = v;
                    } else {
                        prog.frame_stamp[off] = vm_frame_epoch[frame];
                        prog.frame_values[off] = v;
                    }
                } else {
                    prog.global_used[idx] = 1;
                    prog.global_values[idx] = v;
                }
                break;
            }

            case OP_POP:
                (void)vm_pop();
                break;

            case OP_ADD: {
                Value b = vm_pop();
                Value a = vm_pop();
                if (a.type == VAL_STRING && b.type == VAL_STRING) {
                    size_t la = strlen(a.string ? a.string : "");
                    size_t lb = strlen(b.string ? b.string : "");
                    char *s = (char *)malloc(la + lb + 1);
                    if (!s) SPLICE_FAIL("OOM");
                    memcpy(s, a.string ? a.string : "", la);
                    memcpy(s + la, b.string ? b.string : "", lb);
                    s[la + lb] = 0;
                    vm_push(value_string(s));
                } else {
                    vm_push(value_number(a.number + b.number));
                }
                break;
            }

            case OP_SUB: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number - b.number));
                break;
            }

            case OP_MUL: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number * b.number));
                break;
            }

            case OP_DIV: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number / b.number));
                break;
            }

            case OP_MOD: {
                Value b = vm_pop();
                Value a = vm_pop();
                int bi = (int)b.number;
                if (bi == 0) SPLICE_FAIL("MOD_ZERO");
                vm_push(value_number((double)((int)a.number % bi)));
                break;
            }

            case OP_NEG: {
                Value a = vm_pop();
                vm_push(value_number(-a.number));
                break;
            }

            case OP_EQ: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(value_eq(a, b) ? 1.0 : 0.0));
                break;
            }

            case OP_NEQ: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(value_eq(a, b) ? 0.0 : 1.0));
                break;
            }

            case OP_LT: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number < b.number ? 1.0 : 0.0));
                break;
            }

            case OP_GT: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number > b.number ? 1.0 : 0.0));
                break;
            }

            case OP_LTE: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number <= b.number ? 1.0 : 0.0));
                break;
            }

            case OP_GTE: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number(a.number >= b.number ? 1.0 : 0.0));
                break;
            }

            case OP_JMP: {
                uint32_t addr = fetch_u32(&prog);
                if (addr > prog.code_size) SPLICE_FAIL("JMP_OOB");
                vm_ip = addr;
                break;
            }

            case OP_JMP_IF_FALSE: {
                uint32_t addr = fetch_u32(&prog);
                Value cond = vm_pop();
                if (!value_truthy(cond)) {
                    if (addr > prog.code_size) SPLICE_FAIL("JMP_OOB");
                    vm_ip = addr;
                }
                break;
            }

            case OP_CALL: {
                uint16_t symbol = fetch_u16(&prog);
                uint16_t argc = fetch_u16(&prog);

                if (argc > VM_ARG_MAX) SPLICE_FAIL("ARGC_OOB");
                if (symbol >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");

                FunctionEntry *fn = find_function(&prog, symbol);
                if (!fn) {
                    Value argv[VM_ARG_MAX];
                    for (int i = (int)argc - 1; i >= 0; i--) argv[i] = vm_pop();
                    Value ret = call_builtin_or_native(prog.symbols[symbol], (int)argc, argv);
                    vm_push(ret);
                    break;
                }

                if (vm_callsp >= CALLSTACK_MAX) SPLICE_FAIL("CALLSTACK_OOM");
                if (var_stack_depth >= VAR_STACK_MAX) SPLICE_FAIL("VARSTACK_OOM");

                vm_callstack[vm_callsp].return_ip = vm_ip;
                vm_callsp++;

                size_t frame = (size_t)var_stack_depth++;
                uint8_t epoch = (uint8_t)(vm_frame_epoch[frame] + 1u);
                if (epoch == 0) {
                    memset(prog.frame_stamp + frame * prog.symbol_count, 0, prog.symbol_count);
                    epoch = 1;
                }
                vm_frame_epoch[frame] = epoch;

                int limit = fn->param_count < argc ? fn->param_count : argc;
                for (int i = (int)argc - 1; i >= limit; i--) (void)vm_pop();
                for (int i = limit - 1; i >= 0; i--) {
                    if (fn->params[i] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    size_t off = frame * prog.symbol_count + fn->params[i];
                    prog.frame_stamp[off] = epoch;
                    prog.frame_values[off] = vm_pop();
                }
                for (uint16_t i = (uint16_t)limit; i < fn->param_count; i++) {
                    if (fn->params[i] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    size_t off = frame * prog.symbol_count + fn->params[i];
                    prog.frame_stamp[off] = epoch;
                    prog.frame_values[off] = value_number(0.0);
                }

                vm_ip = fn->addr;
                break;
            }

            case OP_CALL1: {
                uint16_t symbol = fetch_u16(&prog);
                if (symbol >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");

                FunctionEntry *fn = find_function(&prog, symbol);
                if (!fn) {
                    Value argv[1];
                    argv[0] = vm_pop();
                    Value ret = call_builtin_or_native(prog.symbols[symbol], 1, argv);
                    vm_push(ret);
                    break;
                }

                if (vm_callsp >= CALLSTACK_MAX) SPLICE_FAIL("CALLSTACK_OOM");
                if (var_stack_depth >= VAR_STACK_MAX) SPLICE_FAIL("VARSTACK_OOM");

                vm_callstack[vm_callsp].return_ip = vm_ip;
                vm_callsp++;

                size_t frame = (size_t)var_stack_depth++;
                uint8_t epoch = (uint8_t)(vm_frame_epoch[frame] + 1u);
                if (epoch == 0) {
                    memset(prog.frame_stamp + frame * prog.symbol_count, 0, prog.symbol_count);
                    epoch = 1;
                }
                vm_frame_epoch[frame] = epoch;

                if (fn->param_count > 0) {
                    if (fn->params[0] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    size_t off0 = frame * prog.symbol_count + fn->params[0];
                    prog.frame_stamp[off0] = epoch;
                    prog.frame_values[off0] = vm_pop();
                    for (uint16_t i = 1; i < fn->param_count; i++) {
                        if (fn->params[i] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                        size_t off = frame * prog.symbol_count + fn->params[i];
                        prog.frame_stamp[off] = epoch;
                        prog.frame_values[off] = value_number(0.0);
                    }
                } else {
                    (void)vm_pop();
                }

                vm_ip = fn->addr;
                break;
            }

            case OP_RET: {
                Value ret = vm_pop();

                if (vm_callsp <= 0) {
                    vm_push(ret);
                    SYNC_VM_STATE();
                    free_program(&prog);
                    return 1;
                }

                vm_callsp--;
                vm_ip = vm_callstack[vm_callsp].return_ip;
                if (var_stack_depth > 0) var_stack_depth--;
                vm_push(ret);
                break;
            }

            case OP_PRINT: {
                Value v = vm_pop();
                splice_print_value(v);
                break;
            }

            case OP_NOT: {
                Value v = vm_pop();
                vm_push(value_number(value_truthy(v) ? 0.0 : 1.0));
                break;
            }

            case OP_AND: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number((value_truthy(a) && value_truthy(b)) ? 1.0 : 0.0));
                break;
            }

            case OP_OR: {
                Value b = vm_pop();
                Value a = vm_pop();
                vm_push(value_number((value_truthy(a) || value_truthy(b)) ? 1.0 : 0.0));
                break;
            }

            case OP_ARRAY_NEW: {
                uint16_t count = fetch_u16(&prog);
                ObjArray *oa = (ObjArray *)malloc(sizeof(ObjArray));
                if (!oa) SPLICE_FAIL("ARRAY_OOM");
                oa->type = OBJ_ARRAY;
                oa->count = (int)count;
                oa->capacity = count > 0 ? (int)count : 4;
                oa->items = (Value *)malloc(sizeof(Value) * (size_t)oa->capacity);
                if (!oa->items) SPLICE_FAIL("ARRAY_OOM");
                for (int i = (int)count - 1; i >= 0; i--) oa->items[i] = vm_pop();
                Value arr = { VAL_OBJECT, 0.0, NULL, oa };
                vm_push(arr);
                break;
            }

            case OP_INDEX_GET: {
                Value idxv = vm_pop();
                Value arrv = vm_pop();
                if (arrv.type != VAL_OBJECT || !arrv.object) {
                    vm_push(value_number(0.0));
                    break;
                }
                ObjArray *oa = (ObjArray *)arrv.object;
                int idx = (int)idxv.number;
                if (idx < 0 || idx >= oa->count) {
                    vm_push(value_number(0.0));
                    break;
                }
                vm_push(oa->items[idx]);
                break;
            }

            case OP_INDEX_SET: {
                Value val = vm_pop();
                Value idxv = vm_pop();
                Value arrv = vm_pop();
                if (arrv.type != VAL_OBJECT || !arrv.object) SPLICE_FAIL("INDEX_TARGET");
                ObjArray *oa = (ObjArray *)arrv.object;
                int idx = (int)idxv.number;
                if (idx < 0) SPLICE_FAIL("INDEX_OOB");
                if (idx >= oa->capacity) {
                    int newcap = oa->capacity ? oa->capacity : 4;
                    while (idx >= newcap) newcap *= 2;
                    Value *ni = (Value *)realloc(oa->items, sizeof(Value) * (size_t)newcap);
                    if (!ni) SPLICE_FAIL("ARRAY_OOM");
                    oa->items = ni;
                    oa->capacity = newcap;
                }
                if (idx >= oa->count) {
                    for (int i = oa->count; i <= idx; i++) oa->items[i] = value_number(0.0);
                    oa->count = idx + 1;
                }
                oa->items[idx] = val;
                vm_push(val);
                break;
            }

            case OP_IMPORT: {
                uint16_t idx = fetch_u16(&prog);
                if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                if (!Splice_load_c_module_source(prog.symbols[idx])) SPLICE_FAIL("NATIVE_IMPORT_FAIL");
                break;
            }

            case OP_INC:
            case OP_DEC: {
                uint16_t idx = fetch_u16(&prog);
                if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                double delta = (op == OP_INC) ? 1.0 : -1.0;
                if (var_stack_depth > 0) {
                    size_t frame = (size_t)var_stack_depth - 1u;
                    size_t off = frame * prog.symbol_count + idx;
                    if (prog.frame_stamp[off] == vm_frame_epoch[frame]) {
                        prog.frame_values[off].number += delta;
                    } else if (prog.global_used[idx]) {
                        prog.global_values[idx].number += delta;
                    } else {
                        prog.frame_stamp[off] = vm_frame_epoch[frame];
                        prog.frame_values[off] = value_number(delta);
                    }
                } else {
                    if (!prog.global_used[idx]) {
                        prog.global_used[idx] = 1;
                        prog.global_values[idx] = value_number(delta);
                    } else {
                        prog.global_values[idx].number += delta;
                    }
                }
                break;
            }

            case OP_IADD_VAR: {
                uint16_t dst = fetch_u16(&prog);
                uint16_t src = fetch_u16(&prog);
                if (dst >= prog.symbol_count || src >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");

                Value rhs = value_number(0.0);
                if (var_stack_depth > 0) {
                    size_t frame = (size_t)var_stack_depth - 1u;
                    size_t src_off = frame * prog.symbol_count + src;
                    if (prog.frame_stamp[src_off] == vm_frame_epoch[frame]) rhs = prog.frame_values[src_off];
                    else if (prog.global_used[src]) rhs = prog.global_values[src];
                } else if (prog.global_used[src]) {
                    rhs = prog.global_values[src];
                }

                if (var_stack_depth > 0) {
                    size_t frame = (size_t)var_stack_depth - 1u;
                    size_t dst_off = frame * prog.symbol_count + dst;
                    if (prog.frame_stamp[dst_off] == vm_frame_epoch[frame]) {
                        prog.frame_values[dst_off].number += rhs.number;
                    } else if (prog.global_used[dst]) {
                        prog.global_values[dst].number += rhs.number;
                    } else {
                        prog.frame_stamp[dst_off] = vm_frame_epoch[frame];
                        prog.frame_values[dst_off] = value_number(rhs.number);
                    }
                } else {
                    if (!prog.global_used[dst]) {
                        prog.global_used[dst] = 1;
                        prog.global_values[dst] = value_number(rhs.number);
                    } else {
                        prog.global_values[dst].number += rhs.number;
                    }
                }
                break;
            }

            case OP_HALT:
                SYNC_VM_STATE();
                free_program(&prog);
                return 1;

            default:
                SPLICE_FAIL("BAD_OPCODE");
        }
    }

    SYNC_VM_STATE();
#undef vm_push
#undef vm_pop
#undef fetch_u16
#undef fetch_u32
#undef vm_ip
#undef vm_callsp
#undef var_stack_depth
#undef SYNC_VM_STATE
    free_program(&prog);
    return 1;
}

#endif
