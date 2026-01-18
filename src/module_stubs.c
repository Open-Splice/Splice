
#include "splice.h"
#include <stdlib.h>
#include <string.h>

/* Example native: noop() -> 0 */
static Value native_noop(int argc, Value *argv) {
    (void)argc; (void)argv;
    Value tmp;
    tmp.type = VAL_NUMBER;
    tmp.number = 0;
    return tmp;
}

/* Constructor: register builtins and initialize any registered modules. */
__attribute__((constructor)) static void Splice_module_stubs_init(void) {
    /* register a tiny example native so tests/examples can call it */
    Splice_register_native("noop", native_noop);

    /* initialize any modules that have been registered */
    Splice_init_all_modules();
}
