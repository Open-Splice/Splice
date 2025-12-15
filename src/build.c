#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
   Bytecode writer
   ============================================================ */
static FILE *bc_out;
static int expect_import_string = 0;

static inline void bc_write_byte(unsigned char b) {
    fputc(b, bc_out);
}

static inline void bc_write_string(const char *s) {
    unsigned short len = (unsigned short)strlen(s);
    fwrite(&len, 2, 1, bc_out);
    fwrite(s, 1, len, bc_out);
}

/* ============================================================
   Token structures
   ============================================================ */
typedef struct {
    unsigned char op;
    char *lex;
} Token;

typedef struct {
    Token *data;
    int count;
    int cap;
} TokenVec;

static void tv_init(TokenVec *v) {
    v->data = NULL;
    v->count = 0;
    v->cap = 0;
}

static void tv_push(TokenVec *v, unsigned char op, const char *lex) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 256;
        v->data = realloc(v->data, sizeof(Token) * v->cap);
        if (!v->data) { perror("oom"); exit(1); }
    }
    v->data[v->count].op = op;
    v->data[v->count].lex = lex ? strdup(lex) : NULL;
    v->count++;
}

static void tv_free(TokenVec *v) {
    for (int i = 0; i < v->count; i++)
        free(v->data[i].lex);
    free(v->data);
}

/* ============================================================
   Comment stripping
   ============================================================ */
static void strip_comments(char *src) {
    char *w = src;
    for (int i = 0; src[i]; i++) {
        if (src[i] == '/' && src[i+1] == '/') {
            while (src[i] && src[i] != '\n') i++;
        }
        *w++ = src[i];
        if (!src[i]) break;
    }
    *w = 0;
}

/* ============================================================
   Lexer
   ============================================================ */
static int boundary(char c) {
    return !(isalnum((unsigned char)c) || c == '_');
}

static void tokenize(const char *p, TokenVec *out) {
    while (*p) {
        if (isspace((unsigned char)*p)) { p++; continue; }

#define KW(x,op) \
    if (!strncmp(p, x, strlen(x)) && boundary(p[strlen(x)])) { \
        tv_push(out, op, NULL); \
        p += strlen(x); \
        continue; \
    }

        /* --- keywords --- */
        KW("let", OP_LET)
        KW("func", OP_FUNC)
        KW("return", OP_RETURN)
        KW("print", OP_PRINT)
        KW("if", OP_IF)
        KW("else", OP_ELSE)
        KW("while", OP_WHILE)
        KW("true", OP_TRUE)
        KW("false", OP_FALSE)
        KW("and", OP_AND)
        KW("or", OP_OR)
        KW("not", OP_NOT)
        KW("raise", OP_RAISE)
        KW("warn", OP_WARN)
        KW("info", OP_INFO)
        KW("for", OP_FOR)
        KW("in", OP_IN)

        /* --- import (special) --- */
        if (!strncmp(p, "import", 6) && boundary(p[6])) {
            tv_push(out, OP_IMPORT, NULL);
            p += 6;
            expect_import_string = 1;
            continue;
        }

        /* --- symbols --- */
        if (*p=='='){ tv_push(out,OP_ASSIGN,NULL); p++; continue; }
        if (*p=='+'){ tv_push(out,OP_PLUS,NULL); p++; continue; }
        if (*p=='-'){ tv_push(out,OP_MINUS,NULL); p++; continue; }
        if (*p=='*'){ tv_push(out,OP_MULTIPLY,NULL); p++; continue; }
        if (*p=='/'){ tv_push(out,OP_DIVIDE,NULL); p++; continue; }
        if (*p=='('){ tv_push(out,OP_LPAREN,NULL); p++; continue; }
        if (*p==')'){ tv_push(out,OP_RPAREN,NULL); p++; continue; }
        if (*p=='{'){ tv_push(out,OP_LBRACE,NULL); p++; continue; }
        if (*p=='}'){ tv_push(out,OP_RBRACE,NULL); p++; continue; }
        if (*p==';'){ tv_push(out,OP_SEMICOLON,NULL); p++; expect_import_string = 0; continue; }
        if (*p==','){ tv_push(out,OP_COMMA,NULL); p++; continue; }

        /* --- string --- */
        if (*p=='"') {
            p++;
            const char *s = p;
            while (*p && *p!='"') p++;
            char buf[256];
            int n = p - s;
            strncpy(buf, s, n);
            buf[n] = 0;

            if (expect_import_string) {
                tv_push(out, OP_IMSTRING, buf);
                expect_import_string = 0;
            } else {
                tv_push(out, OP_STRING, buf);
            }

            if (*p=='"') p++;
            continue;
        }

        /* --- number --- */
        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            while (isdigit((unsigned char)*p)) p++;
            char buf[64];
            int n = p - s;
            strncpy(buf, s, n);
            buf[n] = 0;
            tv_push(out, OP_NUMBER, buf);
            continue;
        }

        /* --- identifier --- */
        if (isalpha((unsigned char)*p) || *p=='_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p=='_') p++;
            char buf[64];
            int n = p - s;
            strncpy(buf, s, n);
            buf[n] = 0;
            tv_push(out, OP_IDENTIFIER, buf);
            continue;
        }

        p++;
    }
}

/* ============================================================
   Dead Code Elimination (SAFE)
   ============================================================ */
static void dce(TokenVec *v) {
    TokenVec out;
    tv_init(&out);

    for (int i = 0; i < v->count; i++) {

        /* 🔒 NEVER eliminate functions */
        if (v->data[i].op == OP_FUNC) {
            while (i < v->count) {
                tv_push(&out, v->data[i].op, v->data[i].lex);
                if (v->data[i].op == OP_RBRACE) break;
                i++;
            }
            continue;
        }

        tv_push(&out, v->data[i].op, v->data[i].lex);
    }

    tv_free(v);
    *v = out;
}

/* ============================================================
   Emit SPBC
   ============================================================ */
static void emit(const TokenVec *v, const char *outf) {
    bc_out = fopen(outf, "wb");
    if (!bc_out) { perror("open"); exit(1); }

    for (int i = 0; i < v->count; i++) {
        bc_write_byte(v->data[i].op);
        if (v->data[i].lex)
            bc_write_string(v->data[i].lex);
    }
    fclose(bc_out);
}

/* ============================================================
   Main
   ============================================================ */
int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s input.sp output.spbc\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);

    char *src = malloc(n + 1);
    fread(src, 1, n, f);
    src[n] = 0;
    fclose(f);

    strip_comments(src);

    TokenVec t;
    tv_init(&t);
    tokenize(src, &t);

    dce(&t);

    emit(&t, argv[2]);

    tv_free(&t);
    free(src);

    printf("Compiled %s -> %s\n", argv[1], argv[2]);
    return 0;
}
