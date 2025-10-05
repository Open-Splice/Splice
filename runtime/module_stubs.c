/* Minimal module stubs for Splice runtime
 * This file is intentionally small: it registers a tiny example native
 * function and ensures module initializers are invoked at program
 * startup. Real native modules can be added by providing additional
 * init functions that call Splice_register_native() or by providing
 * Splice_register_module_<name> symbols which are looked up by the
 * import mechanism in `splice.h`.
 */

#include "splice.h"
#include <stdlib.h>
#include <string.h>

/* Example native: noop() -> 0 */
static Value native_noop(int argc, Value *argv) {
    (void)argc; (void)argv;
    return (Value){ .type = VAL_NUMBER, .number = 0 };
}

/* Constructor: register builtins and initialize any registered modules. */
__attribute__((constructor)) static void Splice_module_stubs_init(void) {
    /* register a tiny example native so tests/examples can call it */
    Splice_register_native("noop", native_noop);

    /* initialize any modules that have been registered */
    Splice_init_all_modules();
}
