#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "splice.h"  /* uses AST types + write_ast_to_spc */

/* =========================
   Read whole file
   ========================= */
static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = 0;
    fclose(f);
    return buf;
}

/* =========================
   Tokenizer
   ========================= */
typedef enum {
    TK_EOF=0,
    TK_IDENT,
    TK_NUMBER,
    TK_STRING,

    TK_LET, TK_FUNC, TK_RETURN, TK_PRINT,
    TK_READ, TK_WRITE,
    TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_IN,
    TK_RAISE,
    TK_TRUE, TK_FALSE,
    TK_AND, TK_OR, TK_NOT,

    TK_LPAREN, TK_RPAREN,
    TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_COMMA, TK_SEMI,
    TK_DOT,

    TK_ASSIGN,      /* = */
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,

    TK_LT, TK_GT, TK_LE, TK_GE, TK_EQ, TK_NEQ
} TokType;

typedef struct {
    TokType t;
    char *lex;      /* for IDENT/STRING */
    double num;     /* for NUMBER */
    int line;
} Tok;

typedef struct {
    Tok *data;
    int count;
    int cap;
} TokVec;

static void tv_init(TokVec *v) { v->data=NULL; v->count=0; v->cap=0; }
static void tv_push(TokVec *v, Tok x) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap*2 : 256;
        Tok *nd = (Tok*)realloc(v->data, sizeof(Tok) * (size_t)v->cap);
        if (!nd) error(0, "oom realloc tokens");
        v->data = nd;
    }
    v->data[v->count++] = x;
}
static void tv_free(TokVec *v) {
    for (int i=0;i<v->count;i++) free(v->data[i].lex);
    free(v->data);
}

static int is_boundary(char c) { return !(isalnum((unsigned char)c) || c=='_'); }

static void tokenize(const char *src, TokVec *out) {
    int line = 1;
    const char *p = src;

    while (*p) {
        if (*p=='\n') { line++; p++; continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }

        /* comments // ... */
        if (p[0]=='/' && p[1]=='/') { while (*p && *p!='\n') p++; continue; }

        /* string "..." */
        if (*p=='"') {
            p++;
            const char *s = p;
            while (*p && *p!='"') p++;
            size_t n = (size_t)(p - s);
            char *str = (char*)malloc(n+1);
            if (!str) error(line, "oom string");
            memcpy(str, s, n);
            str[n]=0;
            Tok t = { .t=TK_STRING, .lex=str, .line=line };
            tv_push(out, t);
            if (*p=='"') p++;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            while (isdigit((unsigned char)*p) || *p=='.') p++;
            char tmp[128];
            size_t n = (size_t)(p - s);
            if (n >= sizeof(tmp)) error(line, "number too long");
            memcpy(tmp, s, n);
            tmp[n]=0;
            Tok t = { .t=TK_NUMBER, .num=strtod(tmp,NULL), .line=line };
            tv_push(out, t);
            continue;
        }

        /* identifiers/keywords */
        if (isalpha((unsigned char)*p) || *p=='_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p=='_') p++;
            size_t n = (size_t)(p - s);
            char *id = (char*)malloc(n+1);
            if (!id) error(line, "oom ident");
            memcpy(id, s, n);
            id[n]=0;

            TokType kw = TK_IDENT;
            if      (!strcmp(id,"let"))    kw=TK_LET;
            else if (!strcmp(id,"func"))   kw=TK_FUNC;
            else if (!strcmp(id,"return")) kw=TK_RETURN;
            else if (!strcmp(id,"print"))  kw=TK_PRINT;
            else if (!strcmp(id,"raise"))   kw=TK_RAISE;
            else if (!strcmp(id,"if"))     kw=TK_IF;
            else if (!strcmp(id,"else"))   kw=TK_ELSE;
            else if (!strcmp(id,"while"))  kw=TK_WHILE;
            else if (!strcmp(id,"for"))    kw=TK_FOR;
            else if (!strcmp(id,"in"))     kw=TK_IN;
            else if (!strcmp(id,"read"))   kw = TK_READ;
            else if (!strcmp(id,"write"))  kw = TK_WRITE;
            else if (!strcmp(id,"true"))   kw=TK_TRUE;
            else if (!strcmp(id,"false"))  kw=TK_FALSE;
            else if (!strcmp(id,"and"))    kw=TK_AND;
            else if (!strcmp(id,"or"))     kw=TK_OR;
            else if (!strcmp(id,"not"))    kw=TK_NOT;

            Tok t = { .t=kw, .lex=(kw==TK_IDENT? id : NULL), .line=line };
            if (kw!=TK_IDENT) free(id);
            tv_push(out, t);
            continue;
        }

        /* two-char ops */
        if (p[0]=='=' && p[1]=='=') { tv_push(out,(Tok){.t=TK_EQ,.line=line}); p+=2; continue; }
        if (p[0]=='!' && p[1]=='=') { tv_push(out,(Tok){.t=TK_NEQ,.line=line}); p+=2; continue; }
        if (p[0]=='<' && p[1]=='=') { tv_push(out,(Tok){.t=TK_LE,.line=line}); p+=2; continue; }
        if (p[0]=='>' && p[1]=='=') { tv_push(out,(Tok){.t=TK_GE,.line=line}); p+=2; continue; }

        /* single-char */
        switch (*p) {
            case '(': tv_push(out,(Tok){.t=TK_LPAREN,.line=line}); p++; break;
            case ')': tv_push(out,(Tok){.t=TK_RPAREN,.line=line}); p++; break;
            case '{': tv_push(out,(Tok){.t=TK_LBRACE,.line=line}); p++; break;
            case '}': tv_push(out,(Tok){.t=TK_RBRACE,.line=line}); p++; break;
            case '[': tv_push(out,(Tok){.t=TK_LBRACKET,.line=line}); p++; break;
            case ']': tv_push(out,(Tok){.t=TK_RBRACKET,.line=line}); p++; break;
            case ',': tv_push(out,(Tok){.t=TK_COMMA,.line=line}); p++; break;
            case ';': tv_push(out,(Tok){.t=TK_SEMI,.line=line}); p++; break;
            case '.': tv_push(out,(Tok){.t=TK_DOT,.line=line}); p++; break;

            case '=': tv_push(out,(Tok){.t=TK_ASSIGN,.line=line}); p++; break;
            case '+': tv_push(out,(Tok){.t=TK_PLUS,.line=line}); p++; break;
            case '-': tv_push(out,(Tok){.t=TK_MINUS,.line=line}); p++; break;
            case '*': tv_push(out,(Tok){.t=TK_STAR,.line=line}); p++; break;
            case '/': tv_push(out,(Tok){.t=TK_SLASH,.line=line}); p++; break;

            case '<': tv_push(out,(Tok){.t=TK_LT,.line=line}); p++; break;
            case '>': tv_push(out,(Tok){.t=TK_GT,.line=line}); p++; break;

            default:
                error(line, "Unknown char: '%c'", *p);
        }
    }

    tv_push(out, (Tok){ .t=TK_EOF, .line=line });
}

/* =========================
   Parser (recursive descent)
   ========================= */
static TokVec *G;
static int P;

static Tok *peek(void) { return &G->data[P]; }
static Tok *prev(void) { return &G->data[P-1]; }
static int at(TokType t) { return peek()->t == t; }
static Tok *consume(TokType t, const char *msg) {
    if (!at(t)) error(peek()->line, "%s", msg);
    return &G->data[P++];
}
static int match(TokType t) { if (at(t)) { P++; return 1; } return 0; }

static ASTNode *parse_expression(void);
static ASTNode *parse_statement(void);
static ASTNode *parse_statements_until(TokType end);

static ASTNode *parse_primary(void) {
    if (match(TK_NUMBER)) {
        ASTNode *n = ast_new(AST_NUMBER);
        n->number = prev()->num;
        return n;
    }
    if (match(TK_STRING)) {
        ASTNode *n = ast_new(AST_STRING);
        n->string = strdup(prev()->lex ? prev()->lex : "");
        return n;
    }
    if (match(TK_TRUE))  { ASTNode *n=ast_new(AST_NUMBER); n->number=1; return n; }
    if (match(TK_FALSE)) { ASTNode *n=ast_new(AST_NUMBER); n->number=0; return n; }

    if (match(TK_IDENT)) {
        char *name = strdup(prev()->lex);

        /* call: ident(...) */
        if (match(TK_LPAREN)) {
            ASTNode **args = NULL;
            int ac=0, cap=0;

            if (!at(TK_RPAREN)) {
                for (;;) {
                    ASTNode *e = parse_expression();
                    if (ac>=cap) { cap = cap?cap*2:8; args=(ASTNode**)realloc(args,sizeof(ASTNode*)*(size_t)cap); }
                    args[ac++] = e;
                    if (!match(TK_COMMA)) break;
                }
            }
            consume(TK_RPAREN, "Expected ')' after call args");

            ASTNode *c = ast_new(AST_FUNCTION_CALL);
            c->funccall.funcname = name;
            c->funccall.args = args;
            c->funccall.arg_count = ac;
            return c;
        }

        /* ident[index] */
        if (match(TK_LBRACKET)) {
            ASTNode *idx = parse_expression();
            consume(TK_RBRACKET, "Expected ']' after index");

            ASTNode *id = ast_new(AST_IDENTIFIER);
            id->string = name;

            ASTNode *ix = ast_new(AST_INDEX_EXPR);
            ix->indexexpr.target = id;
            ix->indexexpr.index  = idx;
            return ix;
        }

        ASTNode *id = ast_new(AST_IDENTIFIER);
        id->string = name;
        return id;
    }

    if (match(TK_LPAREN)) {
        ASTNode *e = parse_expression();
        consume(TK_RPAREN, "Expected ')'");
        return e;
    }
    if (match(TK_READ)) {
        consume(TK_LPAREN, "Expected '(' after read");
        ASTNode *e = parse_expression();
        consume(TK_RPAREN, "Expected ')' after read");

        ASTNode *n = ast_new(AST_READ);
        n->read.expr = e;
        return n;
    }

    if (match(TK_LBRACKET)) {
        ASTNode **els=NULL;
        int ec=0, cap=0;

        if (!at(TK_RBRACKET)) {
            for (;;) {
                ASTNode *e = parse_expression();
                if (ec>=cap) { cap = cap?cap*2:8; els=(ASTNode**)realloc(els,sizeof(ASTNode*)*(size_t)cap); }
                els[ec++] = e;
                if (!match(TK_COMMA)) break;
            }
        }
        consume(TK_RBRACKET, "Expected ']' after array literal");

        ASTNode *a = ast_new(AST_ARRAY_LITERAL);
        a->arraylit.elements = els;
        a->arraylit.count = ec;
        return a;
    }

    error(peek()->line, "Expected expression");
    return NULL;
}

static ASTNode *parse_unary(void) {
    if (match(TK_NOT)) {
        ASTNode *n = ast_new(AST_BINARY_OP);
        n->binop.op = strdup("!");
        n->binop.left = parse_unary();
        n->binop.right = NULL;
        return n;
    }
    if (match(TK_MINUS)) {
        /* -(x) => (-1 * x) */
        ASTNode *mul = ast_new(AST_BINARY_OP);
        mul->binop.op = strdup("*");
        ASTNode *m1 = ast_new(AST_NUMBER);
        m1->number = -1.0;
        mul->binop.left = m1;
        mul->binop.right = parse_unary();
        return mul;
    }
    return parse_primary();
}

static ASTNode *parse_mul(void) {
    ASTNode *left = parse_unary();
    while (at(TK_STAR) || at(TK_SLASH)) {
        TokType op = peek()->t; P++;
        ASTNode *n = ast_new(AST_BINARY_OP);
        n->binop.op = strdup(op==TK_STAR ? "*" : "/");
        n->binop.left = left;
        n->binop.right = parse_unary();
        left = n;
    }
    return left;
}

static ASTNode *parse_add(void) {
    ASTNode *left = parse_mul();
    while (at(TK_PLUS) || at(TK_MINUS)) {
        TokType op = peek()->t; P++;
        ASTNode *n = ast_new(AST_BINARY_OP);
        n->binop.op = strdup(op==TK_PLUS ? "+" : "-");
        n->binop.left = left;
        n->binop.right = parse_mul();
        left = n;
    }
    return left;
}

static ASTNode *parse_cmp(void) {
    ASTNode *left = parse_add();
    while (at(TK_LT)||at(TK_GT)||at(TK_LE)||at(TK_GE)||at(TK_EQ)||at(TK_NEQ)) {
        TokType op = peek()->t; P++;
        const char *s =
            (op==TK_LT)?"<":(op==TK_GT)?">":(op==TK_LE)?"<=":(op==TK_GE)?">=":(op==TK_EQ)?"==":"!=";
        ASTNode *n = ast_new(AST_BINARY_OP);
        n->binop.op = strdup(s);
        n->binop.left = left;
        n->binop.right = parse_add();
        left = n;
    }
    return left;
}

static ASTNode *parse_logic(void) {
    ASTNode *left = parse_cmp();
    while (at(TK_AND) || at(TK_OR)) {
        TokType op = peek()->t; P++;
        ASTNode *n = ast_new(AST_BINARY_OP);
        n->binop.op = strdup(op==TK_AND ? "&&" : "||");
        n->binop.left = left;
        n->binop.right = parse_cmp();
        left = n;
    }
    return left;
}

static ASTNode *parse_expression(void) { return parse_logic(); }

static ASTNode *parse_statements_until(TokType end) {
    ASTNode **stmts=NULL;
    int c=0, cap=0;

    while (!at(end) && !at(TK_EOF)) {
        ASTNode *s = parse_statement();
        if (!s) { /* allow empty */ }
        else {
            if (c>=cap) { cap=cap?cap*2:16; stmts=(ASTNode**)realloc(stmts,sizeof(ASTNode*)*(size_t)cap); }
            stmts[c++] = s;
        }
        match(TK_SEMI);
    }

    ASTNode *blk = ast_new(AST_STATEMENTS);
    blk->statements.stmts = stmts;
    blk->statements.count = c;
    return blk;
}

static ASTNode *parse_statement(void) {
    if (match(TK_LET)) {
        Tok *id = consume(TK_IDENT, "Expected identifier after let");
        consume(TK_ASSIGN, "Expected '=' after let name");
        ASTNode *rhs = parse_expression();

        ASTNode *n = ast_new(AST_LET);
        n->var.varname = strdup(id->lex);
        n->var.value = rhs;
        return n;
    }

    if (match(TK_PRINT)) {
        consume(TK_LPAREN, "Expected '(' after print");
        ASTNode *e = parse_expression();
        consume(TK_RPAREN, "Expected ')' after print expr");
        ASTNode *n = ast_new(AST_PRINT);
        n->print.expr = e;
        return n;
    }

    if (match(TK_WRITE)) {
        consume(TK_LPAREN, "Expected '(' after write");
        ASTNode *path = parse_expression();
        consume(TK_COMMA, "Expected ',' after write path");
        ASTNode *val = parse_expression();
        consume(TK_RPAREN, "Expected ')' after write");

        ASTNode *n = ast_new(AST_WRITE);
        n->write.path = path;
        n->write.value = val;
        return n;
    }

    if (match(TK_RAISE)) {
        consume(TK_LPAREN, "Expected '(' after raise");
        ASTNode *e = parse_expression();
        consume(TK_RPAREN, "Expected ')' after raise expr");
        ASTNode *n = ast_new(AST_RAISE);
        n->raise.expr = e;
        return n;
    }

    if (match(TK_RETURN)) {
        ASTNode *e = NULL;
        if (!at(TK_SEMI) && !at(TK_RBRACE)) e = parse_expression();
        ASTNode *n = ast_new(AST_RETURN);
        n->retstmt.expr = e;
        return n;
    }

    if (match(TK_FUNC)) {
        Tok *id = consume(TK_IDENT, "Expected function name");
        consume(TK_LPAREN, "Expected '(' after func name");

        char **params=NULL;
        int pc=0, cap=0;

        if (!at(TK_RPAREN)) {
            for (;;) {
                Tok *p = consume(TK_IDENT, "Expected param name");
                if (pc>=cap) { cap=cap?cap*2:8; params=(char**)realloc(params,sizeof(char*)*(size_t)cap); }
                params[pc++] = strdup(p->lex);
                if (!match(TK_COMMA)) break;
            }
        }
        consume(TK_RPAREN, "Expected ')' after params");

        consume(TK_LBRACE, "Expected '{' before func body");
        ASTNode *body = parse_statements_until(TK_RBRACE);
        consume(TK_RBRACE, "Expected '}' after func body");

        ASTNode *n = ast_new(AST_FUNC_DEF);
        n->funcdef.funcname = strdup(id->lex);
        n->funcdef.params = params;
        n->funcdef.param_count = pc;
        n->funcdef.body = body;
        return n;
    }

    if (match(TK_IF)) {
        consume(TK_LPAREN, "Expected '(' after if");
        ASTNode *cond = parse_expression();
        consume(TK_RPAREN, "Expected ')' after if cond");
        consume(TK_LBRACE, "Expected '{' after if");
        ASTNode *thenb = parse_statements_until(TK_RBRACE);
        consume(TK_RBRACE, "Expected '}' after if body");

        ASTNode *elseb = NULL;
        if (match(TK_ELSE)) {
            consume(TK_LBRACE, "Expected '{' after else");
            elseb = parse_statements_until(TK_RBRACE);
            consume(TK_RBRACE, "Expected '}' after else body");
        }

        ASTNode *n = ast_new(AST_IF);
        n->ifstmt.condition = cond;
        n->ifstmt.then_branch = thenb;
        n->ifstmt.else_branch = elseb;
        return n;
    }

    if (match(TK_WHILE)) {
        consume(TK_LPAREN, "Expected '(' after while");
        ASTNode *cond = parse_expression();
        consume(TK_RPAREN, "Expected ')' after while cond");
        consume(TK_LBRACE, "Expected '{' after while");
        ASTNode *body = parse_statements_until(TK_RBRACE);
        consume(TK_RBRACE, "Expected '}' after while body");

        ASTNode *n = ast_new(AST_WHILE);
        n->whilestmt.cond = cond;
        n->whilestmt.body = body;
        return n;
    }

    if (match(TK_FOR)) {
        Tok *id = consume(TK_IDENT, "Expected for variable");
        consume(TK_IN, "Expected 'in' after for var");
        ASTNode *start = parse_expression();
        consume(TK_DOT, "Expected '.' in for range");
        ASTNode *end = parse_expression();
        consume(TK_LBRACE, "Expected '{' after for range");
        ASTNode *body = parse_statements_until(TK_RBRACE);
        consume(TK_RBRACE, "Expected '}' after for body");

        ASTNode *n = ast_new(AST_FOR);
        n->forstmt.for_var = strdup(id->lex);
        n->forstmt.for_start = start;
        n->forstmt.for_end = end;
        n->forstmt.for_body = body;
        return n;
    }

    /* assignment or expr statement */
    if (at(TK_IDENT)) {
        Tok *id = peek(); P++;

        /* ident = expr */
        if (match(TK_ASSIGN)) {
            ASTNode *rhs = parse_expression();
            ASTNode *n = ast_new(AST_ASSIGN);
            n->var.varname = strdup(id->lex);
            n->var.value = rhs;
            return n;
        }

        /* ident[expr] = expr */
        if (match(TK_LBRACKET)) {
            ASTNode *idx = parse_expression();
            consume(TK_RBRACKET, "Expected ']' after index");
            consume(TK_ASSIGN, "Expected '=' after index");
            ASTNode *rhs = parse_expression();

            ASTNode *target = ast_new(AST_IDENTIFIER);
            target->string = strdup(id->lex);

            ASTNode *n = ast_new(AST_INDEX_ASSIGN);
            n->indexassign.target = target;
            n->indexassign.index = idx;
            n->indexassign.value = rhs;
            return n;
        }

        /* fall back: treat as expression starting with identifier */
        P--; /* rewind */
    }

    /* expression statement */
    return parse_expression();
}

/* Parse full program */
static ASTNode *parse_program(TokVec *v) {
    G = v; P = 0;
    ASTNode *root = parse_statements_until(TK_EOF);
    return root;
}

/* =========================
   Main (spbuild)
   ========================= */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.sp> <output.spc>\n", argv[0]);
        return 1;
    }

    char *src = read_text_file(argv[1]);
    if (!src) {
        fprintf(stderr, "Could not read: %s\n", argv[1]);
        return 1;
    }

    TokVec tv; tv_init(&tv);
    tokenize(src, &tv);

    ASTNode *root = parse_program(&tv);

    if (!write_ast_to_spc(argv[2], root)) {
        fprintf(stderr, "Could not write: %s\n", argv[2]);
        free_ast(root);
        tv_free(&tv);
        free(src);
        return 1;
    }

    success(0, "Wrote AST SPC: %s", argv[2]);

    free_ast(root);
    tv_free(&tv);
    free(src);
    return 0;
}
