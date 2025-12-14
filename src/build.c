#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
   Bytecode writer
   ============================================================ */
static FILE *bc_out;

static inline void bc_write_byte(unsigned char b) { fputc(b, bc_out); }

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
   Debug dump
   ============================================================ */

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

        #define KW(x,op) if(!strncmp(p,x,strlen(x)) && boundary(p[strlen(x)])){ tv_push(out,op,NULL); p+=strlen(x); continue; }

        KW("let",OP_LET)
        KW("func",OP_FUNC)
        KW("return",OP_RETURN)
        KW("print",OP_PRINT)

        if (*p=='='){ tv_push(out,OP_ASSIGN,NULL); p++; continue; }
        if (*p=='+'){ tv_push(out,OP_PLUS,NULL); p++; continue; }
        if (*p=='-'){ tv_push(out,OP_MINUS,NULL); p++; continue; }
        if (*p=='*'){ tv_push(out,OP_MULTIPLY,NULL); p++; continue; }
        if (*p=='/'){ tv_push(out,OP_DIVIDE,NULL); p++; continue; }
        if (*p=='('){ tv_push(out,OP_LPAREN,NULL); p++; continue; }
        if (*p==')'){ tv_push(out,OP_RPAREN,NULL); p++; continue; }
        if (*p=='{'){ tv_push(out,OP_LBRACE,NULL); p++; continue; }
        if (*p=='}'){ tv_push(out,OP_RBRACE,NULL); p++; continue; }
        if (*p==';'){ tv_push(out,OP_SEMICOLON,NULL); p++; continue; }
        if (*p==','){ tv_push(out,OP_COMMA,NULL); p++; continue; }

        if (*p=='"') {
            p++;
            const char *s = p;
            while (*p && *p!='"') p++;
            char buf[256];
            int n = p - s;
            strncpy(buf,s,n); buf[n]=0;
            tv_push(out,OP_STRING,buf);
            if (*p=='"') p++;
            continue;
        }

        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            while (isdigit((unsigned char)*p)) p++;
            char buf[64];
            int n = p - s;
            strncpy(buf,s,n); buf[n]=0;
            tv_push(out,OP_NUMBER,buf);
            continue;
        }

        if (isalpha((unsigned char)*p) || *p=='_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p=='_') p++;
            char buf[64];
            int n = p - s;
            strncpy(buf,s,n); buf[n]=0;
            tv_push(out,OP_IDENTIFIER,buf);
            continue;
        }

        p++;
    }
}

/* ============================================================
   DCE
   ============================================================ */
typedef struct { char name[32]; int read; } Var;
static Var vars[128];
static int var_count;

static int var_index(const char *n) {
    for (int i=0;i<var_count;i++)
        if (!strcmp(vars[i].name,n)) return i;
    strcpy(vars[var_count].name,n);
    vars[var_count].read = 0;
    return var_count++;
}

static void dce(TokenVec *v) {
    var_count = 0;

    for (int i=0;i<v->count;i++)
        if (v->data[i].op==OP_IDENTIFIER)
            vars[var_index(v->data[i].lex)].read++;

    TokenVec out; tv_init(&out);
    int dead = 0;

    for (int i=0;i<v->count;i++) {
        if (v->data[i].op==OP_RETURN) dead = 1;

        if (v->data[i].op==OP_LET &&
            i+1<v->count &&
            v->data[i+1].op==OP_IDENTIFIER &&
            vars[var_index(v->data[i+1].lex)].read==1) {

            while (i<v->count && v->data[i].op!=OP_SEMICOLON) i++;
            continue;
        }

        if (dead && v->data[i].op!=OP_RBRACE) continue;

        tv_push(&out,v->data[i].op,v->data[i].lex);
        if (v->data[i].op==OP_RBRACE) dead = 0;
    }

    tv_free(v);
    *v = out;
}

/* ============================================================
   Tiny function inlining
   ============================================================ */
typedef struct {
    char name[32];
    char param[8][32];
    int param_count;
    Token *body;
    int body_len;
} TinyFunc;

static TinyFunc tfs[32];
static int tf_count;

static TinyFunc *find_tf(const char *n) {
    for (int i=0;i<tf_count;i++)
        if (!strcmp(tfs[i].name,n)) return &tfs[i];
    return NULL;
}

static void inline_funcs(TokenVec *v) {
    tf_count = 0;

    for (int i=0;i<v->count;i++) {
        if (v->data[i].op!=OP_FUNC) continue;

        TinyFunc tf = {0};
        strcpy(tf.name,v->data[i+1].lex);
        int j=i+3;

        while (v->data[j].op!=OP_RPAREN)
            if (v->data[j].op==OP_IDENTIFIER)
                strcpy(tf.param[tf.param_count++],v->data[j].lex), j++;
            else j++;

        j++;
        if (v->data[j].op!=OP_RETURN) continue;
        j++;

        int s=j;
        while (v->data[j].op!=OP_SEMICOLON) j++;

        tf.body_len=j-s;
        tf.body=malloc(sizeof(Token)*tf.body_len);
        for(int k=0;k<tf.body_len;k++){
            tf.body[k]=v->data[s+k];
            tf.body[k].lex=tf.body[k].lex?strdup(tf.body[k].lex):NULL;
        }

        tfs[tf_count++]=tf;
    }

    TokenVec out; tv_init(&out);

    for (int i=0;i<v->count;i++) {
        if (v->data[i].op==OP_IDENTIFIER &&
            i+1<v->count &&
            v->data[i+1].op==OP_LPAREN) {

            TinyFunc *tf=find_tf(v->data[i].lex);
            if (!tf) goto normal;

            Token args[8][8]; int alen[8]={0}; int ac=0;
            int j=i+2;

            while (v->data[j].op!=OP_RPAREN) {
                if (v->data[j].op==OP_COMMA) ac++;
                else args[ac][alen[ac]++]=v->data[j];
                j++;
            }
            ac++;

            tv_push(&out,OP_LPAREN,NULL);
            for (int b=0;b<tf->body_len;b++) {
                Token *t=&tf->body[b];
                int rep=0;
                for (int p=0;p<tf->param_count;p++)
                    if (t->op==OP_IDENTIFIER && !strcmp(t->lex,tf->param[p])) {
                        for(int a=0;a<alen[p];a++)
                            tv_push(&out,args[p][a].op,args[p][a].lex);
                        rep=1;
                    }
                if (!rep) tv_push(&out,t->op,t->lex);
            }
            tv_push(&out,OP_RPAREN,NULL);
            i=j;
            continue;
        }
normal:
        if (v->data[i].op!=OP_FUNC)
            tv_push(&out,v->data[i].op,v->data[i].lex);
    }

    tv_free(v);
    *v=out;
}

/* ============================================================
   Emit SPBC
   ============================================================ */
static void emit(const TokenVec *v,const char *outf){
    bc_out=fopen(outf,"wb");
    for(int i=0;i<v->count;i++){
        bc_write_byte(v->data[i].op);
        if (v->data[i].lex)
            bc_write_string(v->data[i].lex);
    }
    fclose(bc_out);
}

/* ============================================================
   Main
   ============================================================ */
int main(int c,char**v){
    if(c<3){printf("usage: %s in.sp out.spbc\n",v[0]);return 1;}
    FILE*f=fopen(v[1],"rb");
    fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    char*src=malloc(n+1);fread(src,1,n,f);src[n]=0;fclose(f);

    strip_comments(src);
    TokenVec t; tv_init(&t);
    tokenize(src,&t);

    dce(&t);

    inline_funcs(&t);
    printf("Optimized bytecode tokens\n");
    printf("Compiled %s to %s\n",v[1],v[2]);
    emit(&t,v[2]);
    tv_free(&t);
    free(src);
}
