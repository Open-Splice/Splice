#include "builder.h"

static int is_number_lit(ASTNode *n, double *out) {
    if (n && n->type == AST_NUMBER) {
        if (out) *out = n->number;
        return 1;
    }
    return 0;
}

static int is_numeric_literal(ASTNode *n, double *out) {
    if (!n) return 0;
    if (n->type == AST_NUMBER) {
        if (out) *out = n->number;
        return 1;
    }
    if (n->type == AST_STRING) {
        if (out) *out = 0.0;
        return 1;
    }
    return 0;
}

static int is_string_lit(ASTNode *n, const char **out) {
    if (n && n->type == AST_STRING) {
        if (out) *out = n->string;
        return 1;
    }
    return 0;
}

static int literal_truthy(ASTNode *n, int *out) {
    if (!n || !out) return 0;
    if (n->type == AST_NUMBER) {
        *out = (n->number != 0.0);
        return 1;
    }
    if (n->type == AST_STRING) {
        *out = n->string && n->string[0] != '\0';
        return 1;
    }
    return 0;
}

static int is_pure_expr(ASTNode *n) {
    int i;

    if (!n) return 1;
    switch (n->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_IDENTIFIER:
            return 1;
        case AST_BINARY_OP:
            return is_pure_expr(n->binop.left) && is_pure_expr(n->binop.right);
        case AST_ARRAY:
            for (i = 0; i < n->arraylit.count; i++) {
                if (!is_pure_expr(n->arraylit.items[i])) return 0;
            }
            return 1;
        case AST_INDEX:
            return is_pure_expr(n->index.array) && is_pure_expr(n->index.index);
        default:
            return 0;
    }
}

static int is_noop_stmt(ASTNode *n) {
    if (!n) return 1;
    switch (n->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_IDENTIFIER:
            return 1;
        case AST_BINARY_OP:
        case AST_ARRAY:
        case AST_INDEX:
            return is_pure_expr(n);
        case AST_STATEMENTS:
            return n->statements.count == 0;
        default:
            return 0;
    }
}

static ASTNode *replace_with_number(ASTNode *n, double out) {
    ASTNode *r = ast_number(out);
    free_ast(n);
    return r;
}

static ASTNode *replace_with_string(ASTNode *n, const char *s) {
    ASTNode *r = ast_string(s);
    free_ast(n);
    return r;
}

static ASTNode *take_left_binary(ASTNode *n) {
    ASTNode *l = n->binop.left;
    ASTNode *r = n->binop.right;
    n->binop.left = NULL;
    n->binop.right = NULL;
    free_ast(r);
    free(n->binop.op);
    free(n);
    return l;
}

static ASTNode *take_right_binary(ASTNode *n) {
    ASTNode *l = n->binop.left;
    ASTNode *r = n->binop.right;
    n->binop.left = NULL;
    n->binop.right = NULL;
    free_ast(l);
    free(n->binop.op);
    free(n);
    return r;
}

static ASTNode *optimize_statements(ASTNode *n) {
    int out_count = 0;
    int out_cap = n->statements.count > 0 ? n->statements.count : 4;
    int i;
    ASTNode **out = (ASTNode **)xmalloc(sizeof(ASTNode *) * (size_t)out_cap);

    for (i = 0; i < n->statements.count; i++) {
        int j;
        ASTNode *s = optimize_node(n->statements.stmts[i]);

        if (s && s->type == AST_STATEMENTS) {
            if (s->statements.count > 0) {
                for (j = 0; j < s->statements.count; j++) {
                    if (out_count >= out_cap) {
                        out_cap *= 2;
                        out = (ASTNode **)xrealloc(out, sizeof(ASTNode *) * (size_t)out_cap);
                    }
                    out[out_count++] = s->statements.stmts[j];
                }
            }
            free(s->statements.stmts);
            free(s);
            s = NULL;
        }

        if (s && is_noop_stmt(s)) {
            free_ast(s);
            s = NULL;
        }

        if (s) {
            if (out_count >= out_cap) {
                out_cap *= 2;
                out = (ASTNode **)xrealloc(out, sizeof(ASTNode *) * (size_t)out_cap);
            }
            out[out_count++] = s;

            if (s->type == AST_RETURN || s->type == AST_BREAK || s->type == AST_CONTINUE) {
                for (j = i + 1; j < n->statements.count; j++) {
                    free_ast(n->statements.stmts[j]);
                }
                break;
            }
        }
    }

    free(n->statements.stmts);
    free(n);

    if (out_count == 0) {
        free(out);
        return ast_statements(NULL, 0);
    }
    if (out_count == 1) {
        ASTNode *single = out[0];
        free(out);
        return single;
    }
    return ast_statements(out, out_count);
}

ASTNode *optimize_node(ASTNode *n) {
    if (!n) return NULL;

    switch (n->type) {
        case AST_NUMBER:
        case AST_STRING:
        case AST_IDENTIFIER:
        case AST_BREAK:
        case AST_CONTINUE:
        case AST_IMPORT_C:
            return n;

        case AST_BINARY_OP: {
            double a;
            double b;
            const char *sa = NULL;
            const char *sb = NULL;
            int lnum;
            int rnum;
            int lstr;
            int rstr;
            int ltrue = 0;
            int rtrue = 0;
            int lbool;
            int rbool;
            const char *op;

            n->binop.left = optimize_node(n->binop.left);
            n->binop.right = optimize_node(n->binop.right);

            lnum = is_number_lit(n->binop.left, &a);
            rnum = is_number_lit(n->binop.right, &b);
            lstr = is_string_lit(n->binop.left, &sa);
            rstr = is_string_lit(n->binop.right, &sb);
            lbool = literal_truthy(n->binop.left, &ltrue);
            rbool = literal_truthy(n->binop.right, &rtrue);
            op = n->binop.op ? n->binop.op : "";

            if (!strcmp(op, "!") && n->binop.right == NULL && lbool) {
                return replace_with_number(n, ltrue ? 0.0 : 1.0);
            }

            if (!strcmp(op, "+") && lstr && rstr) {
                size_t la = strlen(sa);
                size_t lb = strlen(sb);
                char *buf = (char *)xmalloc(la + lb + 1);
                memcpy(buf, sa, la);
                memcpy(buf + la, sb, lb);
                buf[la + lb] = 0;
                n = replace_with_string(n, buf);
                free(buf);
                return n;
            }

            if ((lnum || lstr) && (rnum || rstr)) {
                double la = lnum ? a : 0.0;
                double rb = rnum ? b : 0.0;
                double out;

                if (!strcmp(op, "+")) out = la + rb;
                else if (!strcmp(op, "-")) out = la - rb;
                else if (!strcmp(op, "*")) out = la * rb;
                else if (!strcmp(op, "/")) out = la / rb;
                else if (!strcmp(op, "%")) {
                    if ((int)rb == 0) return n;
                    out = (double)((int)la % (int)rb);
                } else if (!strcmp(op, "<")) out = la < rb ? 1.0 : 0.0;
                else if (!strcmp(op, ">")) out = la > rb ? 1.0 : 0.0;
                else if (!strcmp(op, "<=")) out = la <= rb ? 1.0 : 0.0;
                else if (!strcmp(op, ">=")) out = la >= rb ? 1.0 : 0.0;
                else if (!strcmp(op, "==")) out = la == rb ? 1.0 : 0.0;
                else if (!strcmp(op, "!=")) out = la != rb ? 1.0 : 0.0;
                else if (!strcmp(op, "&&")) out = (la != 0.0 && rb != 0.0) ? 1.0 : 0.0;
                else if (!strcmp(op, "||")) out = (la != 0.0 || rb != 0.0) ? 1.0 : 0.0;
                else out = 0.0;

                return replace_with_number(n, out);
            }

            if ((lstr && rstr) && (!strcmp(op, "==") || !strcmp(op, "!="))) {
                int eq = strcmp(sa, sb) == 0;
                return replace_with_number(n, (!strcmp(op, "==") ? eq : !eq) ? 1.0 : 0.0);
            }

            if (lnum && !strcmp(op, "+") && a == 0.0 && is_pure_expr(n->binop.right)) return take_right_binary(n);
            if (rnum && !strcmp(op, "+") && b == 0.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);
            if (rnum && !strcmp(op, "-") && b == 0.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);
            if (lnum && !strcmp(op, "*") && a == 1.0 && is_pure_expr(n->binop.right)) return take_right_binary(n);
            if (rnum && !strcmp(op, "*") && b == 1.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);
            if (lnum && !strcmp(op, "*") && a == 0.0 && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);
            if (rnum && !strcmp(op, "*") && b == 0.0 && is_pure_expr(n->binop.left)) return replace_with_number(n, 0.0);
            if (rnum && !strcmp(op, "/") && b == 1.0 && is_pure_expr(n->binop.left)) return take_left_binary(n);
            if (lnum && !strcmp(op, "/") && a == 0.0 && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);
            if (rnum && !strcmp(op, "%") && b == 1.0 && is_pure_expr(n->binop.left)) return replace_with_number(n, 0.0);
            if (lnum && !strcmp(op, "%") && a == 0.0 && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);
            if (lbool && !strcmp(op, "&&") && !ltrue && is_pure_expr(n->binop.right)) return replace_with_number(n, 0.0);
            if (rbool && !strcmp(op, "&&") && !rtrue && is_pure_expr(n->binop.left)) return replace_with_number(n, 0.0);
            if (lbool && !strcmp(op, "||") && ltrue && is_pure_expr(n->binop.right)) return replace_with_number(n, 1.0);
            if (rbool && !strcmp(op, "||") && rtrue && is_pure_expr(n->binop.left)) return replace_with_number(n, 1.0);

            if (!strcmp(op, "==") && n->binop.left && n->binop.right &&
                n->binop.left->type == AST_STRING && n->binop.right->type == AST_STRING &&
                strcmp(n->binop.left->string, n->binop.right->string) == 0) {
                return replace_with_number(n, 1.0);
            }
            if (!strcmp(op, "!=") && n->binop.left && n->binop.right &&
                n->binop.left->type == AST_STRING && n->binop.right->type == AST_STRING &&
                strcmp(n->binop.left->string, n->binop.right->string) == 0) {
                return replace_with_number(n, 0.0);
            }
            return n;
        }

        case AST_PRINT:
            n->print.expr = optimize_node(n->print.expr);
            return n;

        case AST_LET:
        case AST_ASSIGN:
            n->var.value = optimize_node(n->var.value);
            return n;

        case AST_STATEMENTS:
            return optimize_statements(n);

        case AST_WHILE:
            n->whilestmt.cond = optimize_node(n->whilestmt.cond);
            n->whilestmt.body = optimize_node(n->whilestmt.body);
            {
                double cval;
                if (is_numeric_literal(n->whilestmt.cond, &cval) && cval == 0.0) {
                    free_ast(n);
                    return ast_statements(NULL, 0);
                }
            }
            return n;

        case AST_IF: {
            double cval;
            ASTNode *chosen;

            n->ifstmt.cond = optimize_node(n->ifstmt.cond);
            n->ifstmt.then_b = optimize_node(n->ifstmt.then_b);
            n->ifstmt.else_b = optimize_node(n->ifstmt.else_b);

            if (!is_numeric_literal(n->ifstmt.cond, &cval)) return n;

            chosen = cval != 0.0
                ? n->ifstmt.then_b
                : (n->ifstmt.else_b ? n->ifstmt.else_b : ast_statements(NULL, 0));
            if (n->ifstmt.cond) free_ast(n->ifstmt.cond);
            if (chosen != n->ifstmt.then_b) free_ast(n->ifstmt.then_b);
            if (chosen != n->ifstmt.else_b) free_ast(n->ifstmt.else_b);
            free(n);
            return chosen;
        }

        case AST_FOR: {
            double s;
            double e;

            n->forstmt.start = optimize_node(n->forstmt.start);
            n->forstmt.end = optimize_node(n->forstmt.end);
            n->forstmt.body = optimize_node(n->forstmt.body);
            if (is_numeric_literal(n->forstmt.start, &s) &&
                is_numeric_literal(n->forstmt.end, &e) &&
                (int)s > (int)e) {
                free_ast(n);
                return ast_statements(NULL, 0);
            }
            return n;
        }

        case AST_FUNC_DEF:
            n->funcdef.body = optimize_node(n->funcdef.body);
            return n;

        case AST_FUNCTION_CALL: {
            int i;
            for (i = 0; i < n->funccall.arg_count; i++) {
                n->funccall.args[i] = optimize_node(n->funccall.args[i]);
            }
            return n;
        }

        case AST_RETURN:
            n->retstmt.expr = optimize_node(n->retstmt.expr);
            return n;

        case AST_ARRAY: {
            int i;
            for (i = 0; i < n->arraylit.count; i++) {
                n->arraylit.items[i] = optimize_node(n->arraylit.items[i]);
            }
            return n;
        }

        case AST_INDEX:
            n->index.array = optimize_node(n->index.array);
            n->index.index = optimize_node(n->index.index);
            return n;

        case AST_INDEX_ASSIGN:
            n->indexassign.array = optimize_node(n->indexassign.array);
            n->indexassign.index = optimize_node(n->indexassign.index);
            n->indexassign.value = optimize_node(n->indexassign.value);
            return n;

        default:
            return n;
    }
}
