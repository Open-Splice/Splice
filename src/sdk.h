#ifndef SDK_H
#define SDK_H

#include <stdlib.h>
#include <string.h>
#include <ctype.h>   // tolower, isspace
#include <stdio.h>
#include <limits.h>
#ifndef _WIN32
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef ARDUINO
  #define SPLICE_EMBED 1
#else
  #define SPLICE_EMBED 0
#endif


/* ============================================================
   Value forward declaration (from Splice.h)
   ============================================================ */
typedef struct Value Value;


/* ============================================================
   Native function pointer type
   ============================================================ */
typedef Value (*SpliceCFunc)(int argc, Value *argv);

#if !SPLICE_EMBED

/* ============================================================
   Desktop / full native support
   ============================================================ */

typedef struct {
    char *name;
    SpliceCFunc func;
} SpliceCFuncEntry;

#define MAX_NATIVE_FUNCS 128
#define MAX_MODULES      8

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

    char *clean = (char*)malloc(len + 1);
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
            free(clean);
            return fn;
        }
    }
    free(clean);
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

int Splice_load_c_module_source(const char *src_path);

#ifdef SDK_IMPLEMENTATION
SpliceCFuncEntry Splice_native_funcs[MAX_NATIVE_FUNCS];
int Splice_native_func_count = 0;
SpliceModuleInit Splice_modules[MAX_MODULES];
int Splice_module_count = 0;

#define MAX_DYNAMIC_MODULES 16
static char *Splice_dynamic_module_sources[MAX_DYNAMIC_MODULES];
static void *Splice_dynamic_module_handles[MAX_DYNAMIC_MODULES];
static int Splice_dynamic_module_count = 0;

static unsigned Splice_hash_path(const char *s) {
    unsigned h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static int Splice_try_compile_module(const char *include_dir, const char *resolved, const char *module_path) {
    if (!include_dir || !*include_dir) return 0;
    if (!resolved || !*resolved || !module_path || !*module_path) return 0;

    pid_t pid = fork();
    if (pid < 0) return 0;

    if (pid == 0) {
#ifdef __APPLE__
        execlp("cc", "cc",
               "-fPIC",
               "-shared",
               "-undefined", "dynamic_lookup",
               "-I", include_dir,
               resolved,
               "-o", module_path,
               (char *)NULL);
#else
        execlp("cc", "cc",
               "-fPIC",
               "-shared",
               "-I", include_dir,
               resolved,
               "-o", module_path,
               (char *)NULL);
#endif
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int Splice_load_c_module_source(const char *src_path) {
#ifdef _WIN32
    (void)src_path;
    return 0;
#else
    if (!src_path || !*src_path) return 0;

    char resolved[PATH_MAX];
    if (!realpath(src_path, resolved)) return 0;

    for (int i = 0; i < Splice_dynamic_module_count; i++) {
        if (strcmp(Splice_dynamic_module_sources[i], resolved) == 0) return 1;
    }
    if (Splice_dynamic_module_count >= MAX_DYNAMIC_MODULES) return 0;

    unsigned h = Splice_hash_path(resolved);
    char module_path[PATH_MAX];
#ifdef __APPLE__
    if (snprintf(module_path, sizeof(module_path), "/tmp/splice_native_%u.dylib", h) >= (int)sizeof(module_path)) return 0;
#else
    if (snprintf(module_path, sizeof(module_path), "/tmp/splice_native_%u.so", h) >= (int)sizeof(module_path)) return 0;
#endif

    const char *env_include = getenv("SPLICE_SDK_INCLUDE");
    char src_dir[PATH_MAX];
    if (snprintf(src_dir, sizeof(src_dir), "%s", resolved) >= (int)sizeof(src_dir)) return 0;
    char *slash = strrchr(src_dir, '/');
    if (!slash) slash = strrchr(src_dir, '\\');
    if (!slash) return 0;
    *slash = '\0';

    char parent_src[PATH_MAX];
    char sibling_src[PATH_MAX];
    if (snprintf(parent_src, sizeof(parent_src), "%s/../src", src_dir) >= (int)sizeof(parent_src)) return 0;
    if (snprintf(sibling_src, sizeof(sibling_src), "%s/src", src_dir) >= (int)sizeof(sibling_src)) return 0;

    int compiled = 0;
    if (env_include && *env_include) {
        compiled = Splice_try_compile_module(env_include, resolved, module_path);
    }
    if (!compiled) compiled = Splice_try_compile_module(parent_src, resolved, module_path);
    if (!compiled) compiled = Splice_try_compile_module(sibling_src, resolved, module_path);
    if (!compiled) compiled = Splice_try_compile_module("./src", resolved, module_path);
    if (!compiled) return 0;

    void *hnd = dlopen(module_path, RTLD_NOW | RTLD_GLOBAL);
    if (!hnd) return 0;

    Splice_dynamic_module_sources[Splice_dynamic_module_count] = strdup(resolved);
    if (!Splice_dynamic_module_sources[Splice_dynamic_module_count]) {
        dlclose(hnd);
        return 0;
    }
    Splice_dynamic_module_handles[Splice_dynamic_module_count] = hnd;
    Splice_dynamic_module_count++;
    return 1;
#endif
}
#endif

#else  /* ================= SPLICE_EMBED ================= */

/* ============================================================
   Embedded stubs (NO natives, NO modules)
   ============================================================ */

/* Keep API surface so VM compiles unchanged */

static inline void Splice_register_native(const char *name, SpliceCFunc func) {
    (void)name; (void)func;
}

static inline SpliceCFunc Splice_get_native(const char *name) {
    (void)name;
    return NULL;
}

static inline void Splice_register_module(void (*init)(void)) {
    (void)init;
}

static inline void Splice_init_all_modules(void) {
    /* nothing */
}

static inline int Splice_load_c_module_source(const char *src_path) {
    (void)src_path;
    return 0;
}

#endif /* SPLICE_EMBED */

#endif /* SDK_H */
