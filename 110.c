#include"step.h"
#include"programmer.h"
S(id_) { }
S(comma_) {}
D(parameters,   2, id_, 1, A)D(A,
                3, parameters, 2, comma_, 2, id_, 1, parameters)
S(op_) {}
S(cp_) {}
S(block);
S(sa_opt) {} // syntax action: on(backtrack) -> skipt to next text member
S(sa_pbt) {} // syntax action: pop backtrack stack
D(function,     2, id_, 2, op_, 1, A)D(A, 
                2, sa_opt, 3, parameters, 2, sa_pbt, 1, A)D(A,
                2, cp_, 3, block, 1, function)

S(func_) {}
D(funDecl,      2, func_, 3, function, 1, funDecl)

S(expression);
S(number_) {}
S(string_) {}
S(true_) {}
S(false_) {}
D(primary,      2, true_,   1, A)D(A,
                2, false_,  1, A)D(A,
                2, number_, 1, A)D(A,
                2, string_, 1, A)D(A,
                2, id_,     1, A)D(A,
                2, op_, 3, expression, 2, cp_, 1, primary)

D(arguments,    3, expression, 1, A)D(A,
                3, arguments, 2, comma_, 3, expression, 1, arguments)
S(excl_) {}
S(minus_) {}
D(unary,        2, excl_,  3, unary, 1, A)D(A,
                2, minus_, 3, unary, 1, A)D(A,
                2, id_, 2, op_, 2, sa_opt, 3, arguments, 2, sa_pbt, 2, cp_, 1, unary)
S(star_) {}
S(slash_) {}
D(factor,       3, unary, 1, A)D(A,
                3, factor, 2, star_,  3, unary, 1, A)D(A,
                3, factor, 2, slash_, 3, unary, 1, factor)
S(plus_) {}
D(term,         3, factor, 1, A)D(A,
                3, term, 2, plus_,  3, factor, 1, A)D(A,
                3, term, 2, minus_, 3, factor, 1, term)
S(lt_) {}
S(gt_) {}
S(le_) {}
S(ge_) {}
D(comparison,   3, term, 1, A)D(A,
                3, comparison, 2, lt_, 3, term, 1, A)D(A,
                3, comparison, 2, gt_, 3, term, 1, A)D(A,
                3, comparison, 2, le_, 3, term, 1, A)D(A,
                3, comparison, 2, ge_, 3, term, 1, comparison)
S(eqq_) {}
S(ne_) {}
D(equality,     3, comparison, 1, A)D(A,
                3, equality, 2, eqq_, 3, comparison, 1, A)D(A,
                3, equality, 2, ne_,  3, comparison, 1, equality)
S(and_) {}
D(logic_and,    3, equality, 1, A)D(A,
                3, logic_and, 2, and_, 3, equality, 1, logic_and);
S(or_) {}
D(logic_or,     3, logic_and, 1, A)D(A,
                3, logic_or, 2, or_, 3, logic_and, 1, logic_or)
S(eq_) {}
D(assignment,   2, id_, 2, eq_, 3, assignment, 1, A)D(A,
                3, logic_or, 1, assignment)
D(expression,   3, assignment, 1, expression)

S(let_) {}
S(semicol_) {}
D(varDecl,      2, let_, 2, id_, 2, eq_, 3, expression, 2, semicol_, 1, varDecl)
D(exprStmt,     3, expression, 2, semicol_, 1, exprStmt);
S(for_) {}
S(in_) {}
S(dot_) {}
S(statement);
D(forStmt,      2, for_, 2, id_, 2, in_, 2, number_, 2, dot_, 2, number_, 3, statement, 1, forStmt)
S(if_) {}
S(else_) {}
S(sa_not) {  } /*syntactic action to tell cmachine this is the end of left recursive growth*/
D(ifStmt,       2, if_, 2, op_, 3, expression, 2, cp_, 3, statement, 1, A)D(A,
                3, ifStmt, 2, else_, 3, statement, 2, sa_not, 1, ifStmt)
S(print_) {}
D(printStmt,    2, print_, 3, expression, 2, semicol_, 1, printStmt)
S(return_) {}
D(returnStmt,   2, return_, 3, expression, 2, semicol_, 1, returnStmt)
S(while_) {}
D(whileStmt,    2, while_, 2, op_, 3, expression, 2, cp_, 3, statement, 1, whileStmt)
S(declarations);
S(ocb_) {}
S(ccb_) {}
D(block,        2, ocb_, 3, declarations, 2, ccb_, 1, block)
D(statement,    3, exprStmt,  1, A)D(A,
                3, forStmt,   1, A)D(A,
                3, ifStmt,    1, A)D(A,
                3, printStmt, 1, A)D(A,
                3, returnStmt,1, A)D(A,
                3, whileStmt, 1, A)D(A,
                3, block, 1, statement);
D(declaration,  3, funDecl, 1, A)D(A,
                3, varDecl, 1, A)D(A,
                3, statement, 1, declarations)
D(declarations, 3, declaration, 1, A)D(A,
                3, declarations, 3, declaration, 1, declarations)
S(zero_) {}
D(splice,       3, declarations, 2, zero_, 1, splice)
