#ifndef SPLICE_OPCODE_H
#define SPLICE_OPCODE_H

typedef enum {
    OP_PUSH_CONST = 0,
    OP_LOAD,
    OP_STORE,
    OP_POP,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,

    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_GT,
    OP_LTE,
    OP_GTE,

    OP_JMP,
    OP_JMP_IF_FALSE,

    OP_CALL,
    OP_CALL1,
    OP_RET,

    OP_PRINT,

    OP_NOT,
    OP_AND,
    OP_OR,

    OP_ARRAY_NEW,
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_IMPORT,
    OP_INC,
    OP_DEC,
    OP_IADD_VAR,

    OP_HALT
} OpCode;

#endif
