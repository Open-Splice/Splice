#ifndef OPCODE_H
#define OPCODE_H

// Keywords
#define OP_LET       0x01
#define OP_PRINT     0x02
#define OP_RAISE     0x03
#define OP_WARN      0x04
#define OP_INFO      0x05
#define OP_WHILE     0x06
#define OP_IF        0x07
#define OP_ELSE      0x08
#define OP_FUNC      0x09
#define OP_RETURN    0x0A
#define OP_IMPORT    0x0B
#define OP_FOR       0x0C
#define OP_IN        0x0D
#define OP_TRUE      0x0E
#define OP_FALSE     0x0F
#define OP_AND       0x10
#define OP_OR        0x11
#define OP_NOT       0x12

// Operators / comparisons
#define OP_ASSIGN    0x20
#define OP_PLUS      0x21
#define OP_MINUS     0x22
#define OP_MULTIPLY  0x23
#define OP_DIVIDE    0x24
#define OP_LT        0x25
#define OP_GT        0x26
#define OP_LE        0x27
#define OP_GE        0x28
#define OP_EQ        0x29
#define OP_NEQ       0x2A

// Punctuation
#define OP_SEMICOLON 0x30
#define OP_COMMA     0x31
#define OP_DOT       0x32

// Grouping
#define OP_LPAREN    0x40
#define OP_RPAREN    0x41
#define OP_LBRACE    0x42
#define OP_RBRACE    0x43
#define OP_LBRACKET  0x44
#define OP_RBRACKET  0x45

// Literals
#define OP_NUMBER    0x50
#define OP_STRING    0x51
#define OP_IDENTIFIER 0x52
#define OP_IMSTRING  0x53   // import string

#endif
