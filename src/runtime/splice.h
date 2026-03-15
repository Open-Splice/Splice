#ifndef SPLICE_RUNTIME_H
#define SPLICE_RUNTIME_H

#if defined(ARDUINO) || defined(SPLICE_PLATFORM_ARDUINO)
#define SPLICE_EMBED 1
#else
#define SPLICE_EMBED 0
#endif

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#if defined(ARDUINO)
#include <Arduino.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(SPLICE_PLATFORM_WASM)
extern void splice_wasm_print(const char *);
#define SPLICE_PRINTLN(s) splice_wasm_print(s)
#endif

#if SPLICE_EMBED
void splice_embed_print(const char *s);
void splice_embed_println(const char *s);
void splice_embed_delay_ms(unsigned long ms);
int splice_embed_input_available(void);
int splice_embed_input_read(void);

#ifndef SPLICE_EMBED_PRINT
#define SPLICE_EMBED_PRINT(s) splice_embed_print(s)
#endif
#ifndef SPLICE_EMBED_PRINTLN
#define SPLICE_EMBED_PRINTLN(s) splice_embed_println(s)
#endif
#ifndef SPLICE_EMBED_DELAY_MS
#define SPLICE_EMBED_DELAY_MS(ms) splice_embed_delay_ms((unsigned long)(ms))
#endif
#ifndef SPLICE_EMBED_INPUT_AVAILABLE
#define SPLICE_EMBED_INPUT_AVAILABLE() splice_embed_input_available()
#endif
#ifndef SPLICE_EMBED_INPUT_READ
#define SPLICE_EMBED_INPUT_READ() splice_embed_input_read()
#endif
#ifndef SPLICE_EMBED_HAS_INPUT
#define SPLICE_EMBED_HAS_INPUT 1
#endif

#define SPLICE_PRINTLN(s) SPLICE_EMBED_PRINTLN(s)
#define SPLICE_FAIL(msg) do { SPLICE_EMBED_PRINTLN(msg); while (1) SPLICE_EMBED_DELAY_MS(1000); } while (0)
#else
#define SPLICE_PRINTLN(s) puts(s)
#define SPLICE_FAIL(msg) do { fprintf(stderr, "%s\n", msg); exit(1); } while (0)
#endif

#ifndef SPLICE_MAX_CONST_COUNT
#define SPLICE_MAX_CONST_COUNT 16384u
#endif
#ifndef SPLICE_MAX_SYMBOL_COUNT
#define SPLICE_MAX_SYMBOL_COUNT 16384u
#endif
#ifndef SPLICE_MAX_FUNC_COUNT
#define SPLICE_MAX_FUNC_COUNT 16384u
#endif
#ifndef SPLICE_MAX_PARAM_COUNT
#define SPLICE_MAX_PARAM_COUNT 1024u
#endif
#ifndef SPLICE_MAX_ARRAY_CAPACITY
#define SPLICE_MAX_ARRAY_CAPACITY 1048576u
#endif
#ifndef SPLICE_MAX_ALLOC_SIZE
#define SPLICE_MAX_ALLOC_SIZE (16u * 1024u * 1024u)
#endif

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

#include "../sdk.h"
#include "../opcode.h"

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

int splice_run_embedded_program(const unsigned char *data, size_t size);

static char *splice_strdup_owned(const char *s);
static Value value_number(double n);
static Value value_string(const char *s);
static int value_truthy(Value v);
static int value_eq(Value a, Value b);
static void splice_print_value(Value v);

static uint8_t rd_u8(const unsigned char *data, size_t size, size_t *pos);
static uint16_t rd_u16(const unsigned char *data, size_t size, size_t *pos);
static uint32_t rd_u32(const unsigned char *data, size_t size, size_t *pos);
static double rd_double(const unsigned char *data, size_t size, size_t *pos);
static char *rd_str(const unsigned char *data, size_t size, size_t *pos);

static int splice_array_reserve(ObjArray *oa, size_t min_capacity);
static void splice_reset_vm(void);
static void free_program(BytecodeProgram *p);
static int load_program(const unsigned char *data, size_t size, BytecodeProgram *out);
static inline FunctionEntry *find_function(const BytecodeProgram *p, uint16_t symbol_idx);
static Value call_builtin_or_native(const char *name, int argc, Value *argv);
static Value splice_load_variable(BytecodeProgram *prog, uint16_t idx);
static void splice_store_variable(BytecodeProgram *prog, uint16_t idx, Value v);
static void splice_incdec_variable(BytecodeProgram *prog, uint16_t idx, double delta);
static void splice_iadd_variable(BytecodeProgram *prog, uint16_t dst, uint16_t src);
static inline void vm_push_fast(int *sp, Value v);
static inline Value vm_pop_fast(int *sp);
static inline uint16_t fetch_u16_fast(const BytecodeProgram *p, uint32_t *ip);
static inline uint32_t fetch_u32_fast(const BytecodeProgram *p, uint32_t *ip);
static int splice_execute_bytecode(const unsigned char *data, size_t size);

#include "errors.c"
#include "string.c"
#include "context.c"
#include "functions.c"
#include "varibles.c"
#include "program.c"
#include "execute.c"

#endif
