#include "builder.h"

static void tv_push(TokVec *v, Tok t) {
    if (v->count >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 256;
        v->data = (Tok *)xrealloc(v->data, sizeof(Tok) * (size_t)v->cap);
    }
    v->data[v->count++] = t;
}

static TokType kw_type(const char *id) {
    if (!strcmp(id, "let")) return TK_LET;
    if (!strcmp(id, "print")) return TK_PRINT;
    if (!strcmp(id, "if")) return TK_IF;
    if (!strcmp(id, "else")) return TK_ELSE;
    if (!strcmp(id, "while")) return TK_WHILE;
    if (!strcmp(id, "for")) return TK_FOR;
    if (!strcmp(id, "in")) return TK_IN;
    if (!strcmp(id, "func")) return TK_FUNC;
    if (!strcmp(id, "return")) return TK_RETURN;
    if (!strcmp(id, "break")) return TK_BREAK;
    if (!strcmp(id, "continue")) return TK_CONTINUE;
    if (!strcmp(id, "import")) return TK_IMPORT;
    if (!strcmp(id, "not")) return TK_NOT;
    if (!strcmp(id, "and")) return TK_AND;
    if (!strcmp(id, "or")) return TK_OR;
    return TK_IDENT;
}

void lex(const char *src, TokVec *out) {
    int line = 1;
    const char *p = src;

    while (*p) {
        if (*p == '\n') {
            line++;
            p++;
            tv_push(out, (Tok){ .t = TK_SEMI, .line = line });
            continue;
        }
        if (isspace((unsigned char)*p)) {
            p++;
            continue;
        }

        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') p++;
            continue;
        }

        if (*p == '"') {
            const char *s;
            size_t n;
            char *buf;
            size_t i;
            size_t j = 0;

            p++;
            s = p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            n = (size_t)(p - s);
            buf = (char *)xmalloc(n + 1);
            for (i = 0; i < n; i++) {
                char c = s[i];
                if (c == '\\' && i + 1 < n) {
                    char d = s[++i];
                    if (d == 'n') buf[j++] = '\n';
                    else buf[j++] = d;
                } else {
                    buf[j++] = c;
                }
            }
            buf[j] = 0;
            tv_push(out, (Tok){ .t = TK_STRING, .lex = buf, .line = line });
            if (*p == '"') p++;
            continue;
        }

        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            const char *s = p;
            char tmp[128];
            size_t n;

            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.' && p[1] != '.') {
                p++;
                while (isdigit((unsigned char)*p)) p++;
            }
            n = (size_t)(p - s);
            if (n >= sizeof(tmp)) die("number too long");
            memcpy(tmp, s, n);
            tmp[n] = 0;
            tv_push(out, (Tok){ .t = TK_NUMBER, .num = strtod(tmp, NULL), .line = line });
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *s = p;
            size_t n;
            char *id;
            TokType t;

            while (isalnum((unsigned char)*p) || *p == '_') p++;
            n = (size_t)(p - s);
            id = (char *)xmalloc(n + 1);
            memcpy(id, s, n);
            id[n] = 0;
            t = kw_type(id);
            if (t == TK_IDENT) tv_push(out, (Tok){ .t = t, .lex = id, .line = line });
            else {
                free(id);
                tv_push(out, (Tok){ .t = t, .line = line });
            }
            continue;
        }

        if (p[0] == '=' && p[1] == '=') { tv_push(out, (Tok){ .t = TK_EQ, .line = line }); p += 2; continue; }
        if (p[0] == '!' && p[1] == '=') { tv_push(out, (Tok){ .t = TK_NEQ, .line = line }); p += 2; continue; }
        if (p[0] == '<' && p[1] == '=') { tv_push(out, (Tok){ .t = TK_LE, .line = line }); p += 2; continue; }
        if (p[0] == '>' && p[1] == '=') { tv_push(out, (Tok){ .t = TK_GE, .line = line }); p += 2; continue; }
        if (p[0] == '&' && p[1] == '&') { tv_push(out, (Tok){ .t = TK_AND, .line = line }); p += 2; continue; }
        if (p[0] == '|' && p[1] == '|') { tv_push(out, (Tok){ .t = TK_OR, .line = line }); p += 2; continue; }
        if (p[0] == '.' && p[1] == '.') { tv_push(out, (Tok){ .t = TK_DOTDOT, .line = line }); p += 2; continue; }

        switch (*p) {
            case '(':
                tv_push(out, (Tok){ .t = TK_LPAREN, .line = line });
                p++;
                break;
            case ')':
                tv_push(out, (Tok){ .t = TK_RPAREN, .line = line });
                p++;
                break;
            case '{':
                tv_push(out, (Tok){ .t = TK_LBRACE, .line = line });
                p++;
                break;
            case '}':
                tv_push(out, (Tok){ .t = TK_RBRACE, .line = line });
                p++;
                break;
            case '[':
                tv_push(out, (Tok){ .t = TK_LBRACKET, .line = line });
                p++;
                break;
            case ']':
                tv_push(out, (Tok){ .t = TK_RBRACKET, .line = line });
                p++;
                break;
            case ',':
                tv_push(out, (Tok){ .t = TK_COMMA, .line = line });
                p++;
                break;
            case ';':
                tv_push(out, (Tok){ .t = TK_SEMI, .line = line });
                p++;
                break;
            case '=':
                tv_push(out, (Tok){ .t = TK_ASSIGN, .line = line });
                p++;
                break;
            case '+':
                tv_push(out, (Tok){ .t = TK_PLUS, .line = line });
                p++;
                break;
            case '-':
                tv_push(out, (Tok){ .t = TK_MINUS, .line = line });
                p++;
                break;
            case '*':
                tv_push(out, (Tok){ .t = TK_STAR, .line = line });
                p++;
                break;
            case '/':
                tv_push(out, (Tok){ .t = TK_SLASH, .line = line });
                p++;
                break;
            case '%':
                tv_push(out, (Tok){ .t = TK_MOD, .line = line });
                p++;
                break;
            case '<':
                tv_push(out, (Tok){ .t = TK_LT, .line = line });
                p++;
                break;
            case '>':
                tv_push(out, (Tok){ .t = TK_GT, .line = line });
                p++;
                break;
            case '!':
                tv_push(out, (Tok){ .t = TK_NOT, .line = line });
                p++;
                break;
            default:
                fprintf(stderr, "lexer: unknown char '%c' at line %d\n", *p, line);
                exit(1);
        }
    }

    tv_push(out, (Tok){ .t = TK_EOF, .line = line });
}

void tv_free(TokVec *v) {
    int i;
    for (i = 0; i < v->count; i++) free(v->data[i].lex);
    free(v->data);
}
