#include "splice.h"
#include "sdk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
/* ===== VM OBJECT MODEL (LOCAL MIRROR) ===== */
/* MUST match VM layout EXACTLY */
static void error(int ln, const char *fmt, ...) {
    (void)ln;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
typedef enum {
    OBJ_ARRAY,
    OBJ_TUPLE
} ObjectType;

typedef struct {
    ObjectType type;
    int count;
    int capacity;
    Value *items;
} ObjArray;

/* ===== natives ===== */

static Value native_noop(int argc, Value *argv) {
    (void)argc; (void)argv;
    Value v = { .type = VAL_NUMBER, .number = 0 };
    return v;
}

static Value native_len(int argc, Value *argv) {
    Value out = { .type = VAL_NUMBER, .number = 0 };
    if (argc < 1) return out;

    Value v = argv[0];

    if (v.type == VAL_STRING) {
        out.number = (double)strlen(v.string ? v.string : "");
        return out;
    }

    if (v.type == VAL_OBJECT && v.object) {
        ObjArray *oa = (ObjArray*)v.object;
        if (oa->type == OBJ_ARRAY || oa->type == OBJ_TUPLE) {
            out.number = (double)oa->count;
        }
    }
    return out;
}

static Value native_append(int argc, Value *argv) {
    Value out = { .type = VAL_NUMBER, .number = 0 };
    if (argc < 2) return out;

    Value target = argv[0];
    Value val    = argv[1];

    if (target.type != VAL_OBJECT)
        error(0, "Splice/TypeError append: target must be array");

    ObjArray *oa = (ObjArray*)target.object;
    if (!oa || oa->type != OBJ_ARRAY)
        error(0, "Splice/TypeError append: target not array");

    if (oa->count >= oa->capacity) {
        int newcap = oa->capacity ? oa->capacity * 2 : 4;
        Value *ni = (Value*)realloc(oa->items, sizeof(Value) * newcap);
        if (!ni) error(0, "Splice/SystemError OOM append");
        oa->items = ni;
        oa->capacity = newcap;
    }

    if (val.type == VAL_STRING) {
        Value copy = val;
        copy.string = strdup(val.string ? val.string : "");
        oa->items[oa->count++] = copy;
    } else {
        oa->items[oa->count++] = val;
    }

    out.number = (double)oa->count;
    return out;
}

/* ===== registration ===== */

__attribute__((constructor))
static void Splice_module_stubs_init(void) {
    Splice_register_native("noop",   native_noop);
    Splice_register_native("len",    native_len);
    Splice_register_native("append", native_append);
    Splice_init_all_modules();
}
