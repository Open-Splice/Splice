#ifndef SDK_H
#define SDK_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>   // tolower, isspace

/* ============================================================
   Value forward declaration (from Splice.h)
   ============================================================ */
struct Value;
typedef struct Value Value;

/* ============================================================
   Native function pointer type
   ============================================================ */
typedef Value (*SpliceCFunc)(int argc, Value *argv);

typedef struct {
    char *name;       // normalized name
    SpliceCFunc func;
} SpliceCFuncEntry;

/* ============================================================
   Globals (ONE copy across all .c files)
   ============================================================ */
#define MAX_NATIVE_FUNCS 16
#define MAX_MODULES      8

// Declare as extern by default
extern SpliceCFuncEntry Splice_native_funcs[MAX_NATIVE_FUNCS];
extern int Splice_native_func_count;

typedef void (*SpliceModuleInit)(void);
extern SpliceModuleInit Splice_modules[MAX_MODULES];
extern int Splice_module_count;

/* ============================================================
   Helper: normalize names
   ============================================================ */
static inline char *Splice_normalize_name(const char *raw) {
    while (isspace((unsigned char)*raw)) raw++;
    size_t len = strlen(raw);
    while (len > 0 && isspace((unsigned char)raw[len-1])) len--;

#ifdef __cplusplus
    char *clean = new char[len + 1];
#else
    char *clean = (char*)malloc(len + 1);
#endif
    for (size_t j = 0; j < len; j++)
        clean[j] = (char)tolower((unsigned char)raw[j]);
    clean[len] = '\0';
    return clean;
}

/* ============================================================
   Register / Lookup natives
   ============================================================ */
static inline void Splice_register_native(const char *name, SpliceCFunc func) {
    if (Splice_native_func_count < MAX_NATIVE_FUNCS) {
        char *clean = Splice_normalize_name(name);
        Splice_native_funcs[Splice_native_func_count].name = clean;
        Splice_native_funcs[Splice_native_func_count].func = func;
        Splice_native_func_count++;
    }
}

static inline SpliceCFunc Splice_get_native(const char *name) {
    char *clean = Splice_normalize_name(name);

    for (int i = 0; i < Splice_native_func_count; i++) {


        if (strcmp(Splice_native_funcs[i].name, clean) == 0) {
            SpliceCFunc fn = Splice_native_funcs[i].func;
#ifdef __cplusplus
            delete[] clean;
#else
            free(clean);
#endif
            return fn;
        }
    }

#ifdef __cplusplus
    delete[] clean;
#else
    free(clean);
#endif
    return NULL;
}

/* ============================================================
   Module registration
   ============================================================ */
static inline void Splice_register_module(SpliceModuleInit init) {
    if (Splice_module_count < MAX_MODULES) {
        Splice_modules[Splice_module_count++] = init;
    }
}

static inline void Splice_init_all_modules(void) {
    for (int i = 0; i < Splice_module_count; i++) {
        Splice_modules[i]();
    }
}

#ifdef SDK_IMPLEMENTATION
SpliceCFuncEntry Splice_native_funcs[MAX_NATIVE_FUNCS];
int Splice_native_func_count = 0;
SpliceModuleInit Splice_modules[MAX_MODULES];
int Splice_module_count = 0;
#endif

#endif /* SDK_H */
