# Collected comments from `src/splice.h`

This file lists the comments that were present in `src/splice.h` before removal, along with their approximate line numbers (based on the header at the time of extraction). The header was stripped of comments to reduce size for embedded builds; this file preserves the removed comments for reference.

Comments in `src/splice.h` (by approximate line number):

- Line 3: `/* Splice runtime header */`
- Line 67: `/* Diagnostics */`
- Line 90: `/* Values / Objects */`
- Line 113: `/* SDK include */`
- Line 116: `/* AST */`
- Line 157: `/* "+", "-", "*", "/", "==", "&&", "!" ... */` (inline on `char *op`)
- Line 159: `/* may be NULL for unary */` (inline on `ASTNode *right`)
- Line 185: `/* may be NULL */` (inline on `else_branch`)
- Line 238: `/* Env (vars + funcs) */`
- Line 251: `/* power of 2 = fast modulo */` (inline on `#define VAR_TABLE_SIZE`)
- Line 254: `/* interned or strdup */` (inline on `char *name`)
- Line 292: `/* empty slot = not found */` (inline on `return NULL`)
- Line 297: `/* linear probe */` (inline on `idx = (idx + 1) & ...`)
- Line 370: `/* Forward declarations */`
- Line 455: `/* AST alloc/free */`
- Line 465: `/* AST serialization helpers */`
- Line 481: `/* !SPLICE_EMBED */` (in `#endif /* !SPLICE_EMBED */`)
- Line 587: `/* =========================\n   Runtime eval/interpret\n   ========================= */` (decorative)
- Line 864: `/* string concat on + */`
- Line 939: `/* ---------------- recursion safety ---------------- */`
- Line 945: `/* ============================================================\n            NATIVE C FUNCTIONS\n            ============================================================ */`
- Line 967: `/* ============================================================\n            USER-DEFINED FUNCTION\n            ============================================================ */`
- Line 975: `/* ============================================================\n            SAVE OLD PARAM VALUES (GLOBAL TABLE HACK)\n            ============================================================ */`
- Line 987: `/* shallow copy is enough */` (inline on `saved[i] = *v;`)
- Line 994: `/* ============================================================\n            BIND PARAMETERS\n            ============================================================ */`
- Line 1017: `/* ============================================================` (decorative)
- Line 1027: `/* normal execution */`
- Line 1029: `/* no return → default 0 */`
- Line 1031: `/* returned via AST_RETURN */`
- Line 1037: `/* ============================================================` (decorative)
- Line 1048: `/* param did not exist before → reset to 0 */`
- Line 1081: `/* take ownership of string returned by eval */`
- Line 1327: `/* =========================` (decorative)
- Line 1337: `/* Splice_H */` (in `#endif /* Splice_H */`)

Notes:
- Many comments were decorative section dividers or short inline hints. All have been removed from `src/splice.h` to reduce header size for embedded builds.
- If you want to retain a subset, I can reintroduce selected comments behind `#if !SPLICE_EMBED` so they remain only in desktop builds.

If you want an exact before/after diff with line numbers, I can produce that next.