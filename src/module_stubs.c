#include "sdk.h"

__attribute__((constructor))
static void Splice_module_stubs_init(void) {
    Splice_init_all_modules();
}
