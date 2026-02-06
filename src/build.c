#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
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

/* =========================================================
   This builder emits SPC compatible with the simplified VM
   header you posted:
     - u8 node type
     - strings: u32 len + bytes
     - numbers: 8 bytes raw (double)
     - STATEMENTS: u32 count + nodes
     - LET/ASSIGN: str name + node
     - BINARY_OP: str op + left + right
     - PRINT: node
     - WHILE: cond + body
     - IF: cond + then + else (else can be NULL node)
     - FOR: var + start + end + body
     - FUNC_DEF: name + body
     - FUNCTION_CALL: name
     - RETURN: expr (can be NULL node)
     - BREAK/CONTINUE: no payload
   ========================================================= */

/* ===== SPC header ===== */
#define SPC_MAGIC "SPC\0"
#define SPC_VERSION 1

/* ===== AST types (MUST MATCH YOUR VM) ===== */
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
        char *string;

        struct { char *op; ASTNode *left; ASTNode *right; } binop;
        struct { char *name; ASTNode *value; } var;
        struct { ASTNode *expr; } print;
        struct { ASTNode *cond; ASTNode *body; } whilestmt;
        struct { ASTNode *cond; ASTNode *then_b; ASTNode *else_b; } ifstmt;
        struct { ASTNode **stmts; int count; } statements;
        struct { char *name; ASTNode *body; } funcdef;
        struct { char *name; } funccall;
        struct { ASTNode *expr; } retstmt;
        struct { char *var; ASTNode *start; ASTNode *end; ASTNode *body; } forstmt;
    };
};

/* ===== utils ===== */
static void die(const char *msg) { fprintf(stderr, "%s\n", msg); exit(1); }

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("spbuild: OOM");
    return p;
}
static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) die("spbuild: OOM");
    return q;
}
static char *xstrdup(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *p = (char*)xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

/* ===== AST constructors ===== */
static ASTNode *ast_new(ASTNodeType t) {
    ASTNode *n = (ASTNode*)xmalloc(sizeof(ASTNode));
    memset(n, 0, sizeof(*n));
    n->type = t;
    return n;
}

static ASTNode *ast_number(double v) { ASTNode *n=ast_new(AST_NUMBER); n->number=v; return n; }
static ASTNode *ast_string(const char *s) { ASTNode *n=ast_new(AST_STRING); n->string=xstrdup(s); return n; }
static ASTNode *ast_ident(const char *s) { ASTNode *n=ast_new(AST_IDENTIFIER); n->string=xstrdup(s); return n; }

static ASTNode *ast_binop(const char *op, ASTNode *l, ASTNode *r) {
    ASTNode *n = ast_new(AST_BINARY_OP);
    n->binop.op = xstrdup(op);
    n->binop.left = l;
    n->binop.right = r;
    return n;
}

static ASTNode *ast_print(ASTNode *e) { ASTNode *n=ast_new(AST_PRINT); n->print.expr=e; return n; }

static ASTNode *ast_var(ASTNodeType t, const char *name, ASTNode *val) {
    ASTNode *n = ast_new(t);
    n->var.name = xstrdup(name);
    n->var.value = val;
    return n;
}

static ASTNode *ast_statements(ASTNode **stmts, int count) {
    ASTNode *n=ast_new(AST_STATEMENTS);
    n->statements.stmts=stmts;
    n->statements.count=count;
    return n;
}

static ASTNode *ast_while(ASTNode *c, ASTNode *b) {
    ASTNode *n=ast_new(AST_WHILE);
    n->whilestmt.cond=c; n->whilestmt.body=b;
    return n;
}

static ASTNode *ast_if(ASTNode *c, ASTNode *t, ASTNode *e) {
    ASTNode *n=ast_new(AST_IF);
    n->ifstmt.cond=c; n->ifstmt.then_b=t; n->ifstmt.else_b=e;
    return n;
}

static ASTNode *ast_for(const char *v, ASTNode *s, ASTNode *e, ASTNode *b) {
    ASTNode *n=ast_new(AST_FOR);
    n->forstmt.var=xstrdup(v);
    n->forstmt.start=s; n->forstmt.end=e; n->forstmt.body=b;
    return n;
}

static ASTNode *ast_funcdef(const char *name, ASTNode *body) {
    ASTNode *n=ast_new(AST_FUNC_DEF);
    n->funcdef.name=xstrdup(name);
    n->funcdef.body=body;
    return n;
}

static ASTNode *ast_call0(const char *name) {
    ASTNode *n=ast_new(AST_FUNCTION_CALL);
    n->funccall.name=xstrdup(name);
    return n;
}

static ASTNode *ast_return(ASTNode *e) { ASTNode *n=ast_new(AST_RETURN); n->retstmt.expr=e; return n; }

/* ===== free AST (builder side) ===== */
static void free_ast(ASTNode *n) {
    if (!n) return;
    switch (n->type) {
        case AST_STRING:
        case AST_IDENTIFIER:
            free(n->string);
            break;
        case AST_BINARY_OP:
            free(n->binop.op);
            free_ast(n->binop.left);
            free_ast(n->binop.right);
            break;
        case AST_PRINT:
            free_ast(n->print.expr);
            break;
        case AST_LET:
        case AST_ASSIGN:
            free(n->var.name);
            free_ast(n->var.value);
            break;
        case AST_STATEMENTS:
            for (int i=0;i<n->statements.count;i++) free_ast(n->statements.stmts[i]);
            free(n->statements.stmts);
            break;
        case AST_WHILE:
            free_ast(n->whilestmt.cond);
            free_ast(n->whilestmt.body);
            break;
        case AST_IF:
            free_ast(n->ifstmt.cond);
            free_ast(n->ifstmt.then_b);
            free_ast(n->ifstmt.else_b);
            break;
        case AST_FOR:
            free(n->forstmt.var);
            free_ast(n->forstmt.start);
            free_ast(n->forstmt.end);
            free_ast(n->forstmt.body);
            break;
        case AST_FUNC_DEF:
            free(n->funcdef.name);
            free_ast(n->funcdef.body);
            break;
        case AST_FUNCTION_CALL:
            free(n->funccall.name);
            break;
        case AST_RETURN:
            free_ast(n->retstmt.expr);
            break;
        default:
            break;
    }
    free(n);
}

/* =========================================================
   LEXER
   ========================================================= */

typedef enum {
    TK_EOF=0,
    TK_IDENT, TK_NUMBER, TK_STRING,

    TK_LET, TK_PRINT, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_IN,
    TK_FUNC, TK_RETURN, TK_BREAK, TK_CONTINUE,

    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
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

static void tv_push(TokVec *v, Tok t) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap*2 : 256;
        v->data = (Tok*)xrealloc(v->data, sizeof(Tok)*(size_t)v->cap);
    }
    v->data[v->count++] = t;
}

static TokType kw_type(const char *id) {
    if (!strcmp(id,"let")) return TK_LET;
    if (!strcmp(id,"print")) return TK_PRINT;
    if (!strcmp(id,"if")) return TK_IF;
    if (!strcmp(id,"else")) return TK_ELSE;
    if (!strcmp(id,"while")) return TK_WHILE;
    if (!strcmp(id,"for")) return TK_FOR;
    if (!strcmp(id,"in")) return TK_IN;
    if (!strcmp(id,"func")) return TK_FUNC;
    if (!strcmp(id,"return")) return TK_RETURN;
    if (!strcmp(id,"break")) return TK_BREAK;
    if (!strcmp(id,"continue")) return TK_CONTINUE;
    return TK_IDENT;
}

static void lex(const char *src, TokVec *out) {
    int line = 1;
    const char *p = src;

    while (*p) {
        if (*p == '\n') { line++; p++; tv_push(out,(Tok){.t=TK_SEMI,.line=line}); continue; }
        if (isspace((unsigned char)*p)) { p++; continue; }

        if (p[0]=='/' && p[1]=='/') { while (*p && *p!='\n') p++; continue; }

        if (*p=='"') {
            p++;
            const char *s = p;
            while (*p && *p!='"') {
                if (*p=='\\' && p[1]) p++; /* skip escaped char */
                p++;
            }
            size_t n = (size_t)(p - s);
            char *buf = (char*)xmalloc(n+1);
            /* simple unescape for \" and \\ and \n */
            size_t j=0;
            for (size_t i=0;i<n;i++) {
                char c = s[i];
                if (c=='\\' && i+1<n) {
                    char d = s[++i];
                    if (d=='n') buf[j++]='\n';
                    else buf[j++]=d;
                } else buf[j++]=c;
            }
            buf[j]=0;
            tv_push(out,(Tok){.t=TK_STRING,.lex=buf,.line=line});
            if (*p=='"') p++;
            continue;
        }

        if (isdigit((unsigned char)*p) || (*p=='.' && isdigit((unsigned char)p[1]))) {
            const char *s = p;
            while (isdigit((unsigned char)*p)) p++;
            if (*p=='.') { p++; while (isdigit((unsigned char)*p)) p++; }
            char tmp[128];
            size_t n = (size_t)(p - s);
            if (n >= sizeof(tmp)) die("number too long");
            memcpy(tmp,s,n); tmp[n]=0;
            tv_push(out,(Tok){.t=TK_NUMBER,.num=strtod(tmp,NULL),.line=line});
            continue;
        }

        if (isalpha((unsigned char)*p) || *p=='_') {
            const char *s=p;
            while (isalnum((unsigned char)*p) || *p=='_') p++;
            size_t n=(size_t)(p-s);
            char *id=(char*)xmalloc(n+1);
            memcpy(id,s,n); id[n]=0;
            TokType t = kw_type(id);
            if (t==TK_IDENT) tv_push(out,(Tok){.t=t,.lex=id,.line=line});
            else { free(id); tv_push(out,(Tok){.t=t,.line=line}); }
            continue;
        }

        /* two-char ops */
        if (p[0]=='=' && p[1]=='=') { tv_push(out,(Tok){.t=TK_EQ,.line=line}); p+=2; continue; }
        if (p[0]=='!' && p[1]=='=') { tv_push(out,(Tok){.t=TK_NEQ,.line=line}); p+=2; continue; }
        if (p[0]=='<' && p[1]=='=') { tv_push(out,(Tok){.t=TK_LE,.line=line}); p+=2; continue; }
        if (p[0]=='>' && p[1]=='=') { tv_push(out,(Tok){.t=TK_GE,.line=line}); p+=2; continue; }
        if (p[0]=='&' && p[1]=='&') { tv_push(out,(Tok){.t=TK_AND,.line=line}); p+=2; continue; }
        if (p[0]=='|' && p[1]=='|') { tv_push(out,(Tok){.t=TK_OR,.line=line}); p+=2; continue; }
        if (p[0]=='.' && p[1]=='.') { tv_push(out,(Tok){.t=TK_DOTDOT,.line=line}); p+=2; continue; }

        /* single char */
        switch (*p) {
            case '(': tv_push(out,(Tok){.t=TK_LPAREN,.line=line}); p++; break;
            case ')': tv_push(out,(Tok){.t=TK_RPAREN,.line=line}); p++; break;
            case '{': tv_push(out,(Tok){.t=TK_LBRACE,.line=line}); p++; break;
            case '}': tv_push(out,(Tok){.t=TK_RBRACE,.line=line}); p++; break;
            case ',': tv_push(out,(Tok){.t=TK_COMMA,.line=line}); p++; break;
            case ';': tv_push(out,(Tok){.t=TK_SEMI,.line=line}); p++; break;
            case '=': tv_push(out,(Tok){.t=TK_ASSIGN,.line=line}); p++; break;
            case '+': tv_push(out,(Tok){.t=TK_PLUS,.line=line}); p++; break;
            case '-': tv_push(out,(Tok){.t=TK_MINUS,.line=line}); p++; break;
            case '*': tv_push(out,(Tok){.t=TK_STAR,.line=line}); p++; break;
            case '/': tv_push(out,(Tok){.t=TK_SLASH,.line=line}); p++; break;
            case '%': tv_push(out,(Tok){.t=TK_MOD,.line=line}); p++; break;
            case '<': tv_push(out,(Tok){.t=TK_LT,.line=line}); p++; break;
            case '>': tv_push(out,(Tok){.t=TK_GT,.line=line}); p++; break;
            case '!': tv_push(out,(Tok){.t=TK_NOT,.line=line}); p++; break;
            default:
                fprintf(stderr,"lexer: unknown char '%c' at line %d\n", *p, line);
                exit(1);
        }
    }

    tv_push(out,(Tok){.t=TK_EOF,.line=line});
}

static void tv_free(TokVec *v) {
    for (int i=0;i<v->count;i++) free(v->data[i].lex);
    free(v->data);
}

/* =========================================================
   PARSER (recursive descent)
   ========================================================= */

static TokVec *G;
static int P;

static Tok *peek(void) { return &G->data[P]; }
static int at(TokType t) { return peek()->t == t; }
static Tok *advance(void) { return &G->data[P++]; }
static int match(TokType t) { if (at(t)) { P++; return 1; } return 0; }

static void expect(TokType t, const char *msg) {
    if (!at(t)) {
        fprintf(stderr,"parse error line %d: %s\n", peek()->line, msg);
        exit(1);
    }
    P++;
}

static ASTNode *parse_expr(void);
static ASTNode *parse_stmt(void);
static ASTNode *parse_block(void);

/* primary: number|string|ident|call| (expr) */
static ASTNode *parse_primary(void) {
    if (match(TK_NUMBER)) {
        return ast_number(G->data[P-1].num);
    }
    if (match(TK_STRING)) {
        return ast_string(G->data[P-1].lex ? G->data[P-1].lex : "");
    }
    if (match(TK_IDENT)) {
        const char *name = G->data[P-1].lex;
        /* call: ident() only (no args in your VM struct) */
        if (match(TK_LPAREN)) {
            expect(TK_RPAREN, "Expected ')' after call");
            return ast_call0(name);
        }
        return ast_ident(name);
    }
    if (match(TK_LPAREN)) {
        ASTNode *e = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        return e;
    }
    fprintf(stderr,"parse error line %d: expected primary\n", peek()->line);
    exit(1);
}

/* unary: -x or !x */
static ASTNode *parse_unary(void) {
    if (match(TK_MINUS)) {
        /* -(x) -> (-1 * x) */
        return ast_binop("*", ast_number(-1.0), parse_unary());
    }
    if (match(TK_NOT)) {
        /* !x -> (0 == x) is not correct; but your VM has no boolean type.
           We'll implement !x as (x == 0) using binary op "!"? No.
           We'll encode op "!" and expect VM to support it if you added it.
           If VM doesn't support "!", remove this feature.
        */
        return ast_binop("!", parse_unary(), NULL);
    }
    return parse_primary();
}

/* mul: * / % */
static ASTNode *parse_mul(void) {
    ASTNode *l = parse_unary();
    while (at(TK_STAR) || at(TK_SLASH) || at(TK_MOD)) {
        TokType op = advance()->t;
        const char *s = (op==TK_STAR)?"*":(op==TK_SLASH)?"/":"%";
        ASTNode *r = parse_unary();
        l = ast_binop(s, l, r);
    }
    return l;
}

/* add: + - */
static ASTNode *parse_add(void) {
    ASTNode *l = parse_mul();
    while (at(TK_PLUS) || at(TK_MINUS)) {
        TokType op = advance()->t;
        const char *s = (op==TK_PLUS)?"+":"-";
        ASTNode *r = parse_mul();
        l = ast_binop(s, l, r);
    }
    return l;
}

/* cmp: < > <= >= == != */
static ASTNode *parse_cmp(void) {
    ASTNode *l = parse_add();
    while (at(TK_LT)||at(TK_GT)||at(TK_LE)||at(TK_GE)||at(TK_EQ)||at(TK_NEQ)) {
        TokType op = advance()->t;
        const char *s =
            (op==TK_LT)?"<":(op==TK_GT)?">":(op==TK_LE)?"<=":(op==TK_GE)?">=":(op==TK_EQ)?"==":"!=";
        ASTNode *r = parse_add();
        l = ast_binop(s, l, r);
    }
    return l;
}

/* logic: && || (encoded as "&&" "||") */
static ASTNode *parse_logic(void) {
    ASTNode *l = parse_cmp();
    while (at(TK_AND) || at(TK_OR)) {
        TokType op = advance()->t;
        const char *s = (op==TK_AND)?"&&":"||";
        ASTNode *r = parse_cmp();
        l = ast_binop(s, l, r);
    }
    return l;
}

static ASTNode *parse_expr(void) { return parse_logic(); }

/* statement separators: many semis/newlines are ok */
static void eat_separators(void) {
    while (match(TK_SEMI)) {}
}

static ASTNode *parse_stmt(void) {
    eat_separators();

    if (match(TK_LET)) {
        expect(TK_IDENT, "Expected identifier after let");
        const char *name = G->data[P-1].lex;
        expect(TK_ASSIGN, "Expected '=' after let name");
        ASTNode *rhs = parse_expr();
        return ast_var(AST_LET, name, rhs);
    }

    if (match(TK_PRINT)) {
        expect(TK_LPAREN, "Expected '(' after print");
        ASTNode *e = parse_expr();
        expect(TK_RPAREN, "Expected ')' after print");
        return ast_print(e);
    }

    if (match(TK_BREAK)) return ast_new(AST_BREAK);
    if (match(TK_CONTINUE)) return ast_new(AST_CONTINUE);

    if (match(TK_RETURN)) {
        /* return expr; (expr optional) */
        if (at(TK_SEMI) || at(TK_RBRACE) || at(TK_EOF)) return ast_return(NULL);
        return ast_return(parse_expr());
    }

    if (match(TK_FUNC)) {
        expect(TK_IDENT, "Expected function name");
        const char *name = G->data[P-1].lex;
        expect(TK_LPAREN, "Expected '(' after func name");
        expect(TK_RPAREN, "Expected ')' (no args supported)");
        ASTNode *body = parse_block();
        return ast_funcdef(name, body);
    }

    if (match(TK_IF)) {
        expect(TK_LPAREN, "Expected '(' after if");
        ASTNode *cond = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        ASTNode *thenb = parse_block();
        ASTNode *elseb = NULL;
        if (match(TK_ELSE)) {
            /* else { ... } */
            elseb = parse_block();
        }
        return ast_if(cond, thenb, elseb);
    }

    if (match(TK_WHILE)) {
        expect(TK_LPAREN, "Expected '(' after while");
        ASTNode *cond = parse_expr();
        expect(TK_RPAREN, "Expected ')'");
        ASTNode *body = parse_block();
        return ast_while(cond, body);
    }

    if (match(TK_FOR)) {
        expect(TK_IDENT, "Expected for variable name");
        const char *var = G->data[P-1].lex;
        expect(TK_IN, "Expected 'in' after for var");
        ASTNode *start = parse_expr();
        expect(TK_DOTDOT, "Expected '..' in for range");
        ASTNode *end = parse_expr();
        ASTNode *body = parse_block();
        return ast_for(var, start, end, body);
    }

    /* assignment or expression statement */
    if (at(TK_IDENT) && G->data[P+1].t == TK_ASSIGN) {
        const char *name = advance()->lex;
        advance(); /* '=' */
        ASTNode *rhs = parse_expr();
        return ast_var(AST_ASSIGN, name, rhs);
    }

    /* expression stmt (incl call) */
    return parse_expr();
}

static ASTNode *parse_block(void) {
    expect(TK_LBRACE, "Expected '{'");

    ASTNode **stmts = NULL;
    int count = 0, cap = 0;

    while (!at(TK_RBRACE) && !at(TK_EOF)) {
        ASTNode *s = parse_stmt();
        if (s) {
            if (count >= cap) {
                cap = cap ? cap*2 : 16;
                stmts = (ASTNode**)xrealloc(stmts, sizeof(ASTNode*)*(size_t)cap);
            }
            stmts[count++] = s;
        }
        eat_separators();
    }

    expect(TK_RBRACE, "Expected '}'");
    return ast_statements(stmts, count);
}

static ASTNode *parse_program(TokVec *v) {
    G = v; P = 0;

    ASTNode **stmts = NULL;
    int count=0, cap=0;

    while (!at(TK_EOF)) {
        ASTNode *s = parse_stmt();
        if (s) {
            if (count >= cap) {
                cap = cap ? cap*2 : 16;
                stmts = (ASTNode**)xrealloc(stmts, sizeof(ASTNode*)*(size_t)cap);
            }
            stmts[count++] = s;
        }
        eat_separators();
    }

    return ast_statements(stmts, count);
}

/* =========================================================
   SPC WRITER (matches your VM)
   ========================================================= */

static void wr_u8(FILE *f, uint8_t v) { fwrite(&v,1,1,f); }

static void wr_u32(FILE *f, uint32_t v) {
    /* little-endian */
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b,1,4,f);
}

static void wr_double(FILE *f, double v) {
    fwrite(&v, 1, 8, f);
}

static void wr_str(FILE *f, const char *s) {
    if (!s) s = "";
    uint32_t len = (uint32_t)strlen(s);
    wr_u32(f, len);
    if (len) fwrite(s,1,len,f);
}

static void write_node(FILE *f, ASTNode *n);

static void write_node(FILE *f, ASTNode *n) {
    if (!n) {
        /* NULL: write a dummy node of type AST_NUMBER with value 0
           because your VM read_node() doesn't have a NULL sentinel.
           This keeps structure valid when else/return expr is missing.
        */
        wr_u8(f, (uint8_t)AST_NUMBER);
        wr_double(f, 0.0);
        return;
    }

    wr_u8(f, (uint8_t)n->type);

    switch (n->type) {
        case AST_NUMBER:      wr_double(f, n->number); break;
        case AST_STRING:
        case AST_IDENTIFIER:  wr_str(f, n->string); break;

        case AST_BINARY_OP:
            wr_str(f, n->binop.op);
            write_node(f, n->binop.left);
            /* unary '!' encoded with right=NULL: emit 0 on right */
            write_node(f, n->binop.right);
            break;

        case AST_PRINT:
            write_node(f, n->print.expr);
            break;

        case AST_LET:
        case AST_ASSIGN:
            wr_str(f, n->var.name);
            write_node(f, n->var.value);
            break;

        case AST_STATEMENTS:
            wr_u32(f, (uint32_t)n->statements.count);
            for (int i=0;i<n->statements.count;i++) write_node(f, n->statements.stmts[i]);
            break;

        case AST_WHILE:
            write_node(f, n->whilestmt.cond);
            write_node(f, n->whilestmt.body);
            break;

        case AST_IF:
            write_node(f, n->ifstmt.cond);
            write_node(f, n->ifstmt.then_b);
            write_node(f, n->ifstmt.else_b);
            break;

        case AST_FOR:
            wr_str(f, n->forstmt.var);
            write_node(f, n->forstmt.start);
            write_node(f, n->forstmt.end);
            write_node(f, n->forstmt.body);
            break;

        case AST_FUNC_DEF:
            wr_str(f, n->funcdef.name);
            write_node(f, n->funcdef.body);
            break;

        case AST_FUNCTION_CALL:
            wr_str(f, n->funccall.name);
            break;

        case AST_RETURN:
            write_node(f, n->retstmt.expr);
            break;

        case AST_BREAK:
        case AST_CONTINUE:
            break;

        default:
            die("spbuild: unsupported node in writer");
    }
}

static int write_spc(const char *out_path, ASTNode *root) {
    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return 0;
    }
    fwrite(SPC_MAGIC, 1, 4, f);
    wr_u8(f, (uint8_t)SPC_VERSION);
    write_node(f, root);
    fclose(f);
    return 1;
}

/* =========================================================
   MAIN
   ========================================================= */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char*)xmalloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);
    return buf;
}

/* Basic validation to reduce path traversal risk.
   Allows only non-empty relative paths and rejects any ".." component. */
static int is_safe_relative_path(const char *arg) {
    if (arg == NULL || *arg == '\0') {
        return 0;
    }

    /* Reject absolute POSIX-style paths. */
    if (arg[0] == '/') {
        return 0;
    }

    /* Rudimentary check against Windows drive letters like "C:" or "C:\". */
    if (((arg[0] >= 'A' && arg[0] <= 'Z') || (arg[0] >= 'a' && arg[0] <= 'z')) &&
        arg[1] == ':') {
        return 0;
    }

    /* Scan components separated by '/' or '\\' and reject any that are exactly "..". */
    const char *p = arg;
    while (*p) {
        while (*p == '/' || *p == '\\') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != '/' && *p != '\\') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            return 0;
        }
    }

    return 1;
}

static int path_within_base(const char *path, const char *base) {
    size_t base_len = strlen(base);
    if (strncmp(path, base, base_len) != 0) {
        return 0;
    }
    return path[base_len] == '\0' || path[base_len] == '/';
}

static int fullpath_buf(const char *path, char *out, size_t out_sz) {
#ifdef _WIN32
    return _fullpath(out, path, out_sz) != NULL;
#else
    (void)out_sz;
    return realpath(path, out) != NULL;
#endif
}

static int resolve_input_path(const char *arg, char *dst, size_t dst_len) {
    if (!is_safe_relative_path(arg)) {
        return 0;
    }

    char cwd[PATH_MAX];
    char resolved[PATH_MAX];
    if (!splice_getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    if (!fullpath_buf(arg, resolved, sizeof(resolved))) {
        return 0;
    }
    if (!path_within_base(resolved, cwd)) {
        return 0;
    }
    if (snprintf(dst, dst_len, "%s", resolved) >= (int)dst_len) {
        return 0;
    }
    return 1;
}

static int resolve_output_path(const char *arg, char *dst, size_t dst_len) {
    if (!is_safe_relative_path(arg)) {
        return 0;
    }

    const char *slash = strrchr(arg, '/');
    const char *bslash = strrchr(arg, '\\');
    if (bslash && (!slash || bslash > slash)) {
        slash = bslash;
    }

    char dir[PATH_MAX];
    const char *base = arg;
    if (slash) {
        size_t dlen = (size_t)(slash - arg);
        if (dlen == 0 || dlen >= sizeof(dir)) {
            return 0;
        }
        memcpy(dir, arg, dlen);
        dir[dlen] = '\0';
        base = slash + 1;
    } else {
        strcpy(dir, ".");
    }

    if (*base == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return 0;
    }
    if (strchr(base, '/') != NULL || strchr(base, '\\') != NULL) {
        return 0;
    }

    char cwd[PATH_MAX];
    char dir_resolved[PATH_MAX];
    if (!splice_getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    if (!fullpath_buf(dir, dir_resolved, sizeof(dir_resolved))) {
        return 0;
    }
    if (!path_within_base(dir_resolved, cwd)) {
        return 0;
    }

    if (snprintf(dst, dst_len, "%s/%s", dir_resolved, base) >= (int)dst_len) {
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.sp> <output.spc>\n", argv[0]);
        return 1;
    }

    const char *in_arg = argv[1];
    const char *out_arg = argv[2];
    char in_path[PATH_MAX];
    char out_path[PATH_MAX];

    if (!resolve_input_path(in_arg, in_path, sizeof(in_path))) {
        fprintf(stderr, "spbuild: unsafe input path '%s'\n", in_arg);
        return 1;
    }

    if (!resolve_output_path(out_arg, out_path, sizeof(out_path))) {
        fprintf(stderr, "spbuild: unsafe output path '%s'\n", out_arg);
        return 1;
    }

    char *src = read_file(in_path);
    if (!src) {
        fprintf(stderr, "spbuild: cannot read %s\n", in_arg);
        return 1;
    }

    TokVec tv = {0};
    lex(src, &tv);

    ASTNode *root = parse_program(&tv);

    if (!write_spc(out_path, root)) {
        fprintf(stderr, "spbuild: failed to write %s\n", out_arg);
        free_ast(root);
        tv_free(&tv);
        free(src);
        return 1;
    }

    free_ast(root);
    tv_free(&tv);
    free(src);
    return 0;
}
