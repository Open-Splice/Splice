#ifndef SPLICE_BUILD_BUILDER_H
#define SPLICE_BUILD_BUILDER_H

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

typedef enum {
    TK_EOF = 0,
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

extern char g_project_root[PATH_MAX];
extern char g_current_source_file[PATH_MAX];
extern char **g_imported_files;
extern int g_imported_count;
extern int g_imported_cap;
extern char **g_import_stack;
extern int g_import_stack_count;
extern int g_import_stack_cap;

void die(const char *msg);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

ASTNode *ast_new(ASTNodeType t);
ASTNode *ast_number(double v);
ASTNode *ast_string(const char *s);
ASTNode *ast_ident(const char *s);
ASTNode *ast_binop(const char *op, ASTNode *l, ASTNode *r);
ASTNode *ast_print(ASTNode *e);
ASTNode *ast_var(ASTNodeType t, const char *name, ASTNode *val);
ASTNode *ast_statements(ASTNode **stmts, int count);
ASTNode *ast_while(ASTNode *c, ASTNode *b);
ASTNode *ast_if(ASTNode *c, ASTNode *t, ASTNode *e);
ASTNode *ast_for(const char *v, ASTNode *s, ASTNode *e, ASTNode *b);
ASTNode *ast_funcdef_params(const char *name, char **params, int param_count, ASTNode *body);
ASTNode *ast_call(const char *name, ASTNode **args, int arg_count);
ASTNode *ast_return(ASTNode *e);
ASTNode *ast_array(ASTNode **items, int count);
ASTNode *ast_index(ASTNode *arr, ASTNode *idx);
ASTNode *ast_index_assign(ASTNode *arr, ASTNode *idx, ASTNode *val);
ASTNode *ast_import_c(const char *path);
void free_ast(ASTNode *n);

void lex(const char *src, TokVec *out);
void tv_free(TokVec *v);

ASTNode *parse_program(TokVec *v);
ASTNode *optimize_node(ASTNode *n);
int write_spc(const char *out_path, ASTNode *root);

char *read_file(const char *path);
int is_safe_relative_path(const char *arg);
int path_within_base(const char *path, const char *base);
int fullpath_buf(const char *path, char *out, size_t out_sz);
int resolve_input_path(const char *arg, char *dst, size_t dst_len);
int is_safe_filename(const char *name);
int path_in_list(char **list, int count, const char *path);
void path_list_push(char ***list, int *count, int *cap, const char *path);
void path_list_pop(char **list, int *count);
void path_list_free(char **list, int count);

#endif
