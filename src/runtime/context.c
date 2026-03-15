static Value vm_stack[VM_STACK_MAX];
static int vm_sp = 0;
static uint32_t vm_ip = 0;

static int var_stack_depth = 0;
static uint8_t vm_frame_epoch[VAR_STACK_MAX];

static CallFrame vm_callstack[CALLSTACK_MAX];
static int vm_callsp = 0;

static Value value_number(double n) {
    Value v = { VAL_NUMBER, n, NULL, NULL };
    return v;
}

static int value_truthy(Value v) {
    if (v.type == VAL_STRING) return v.string && v.string[0] != '\0';
    if (v.type == VAL_OBJECT) return v.object != NULL;
    return v.number != 0.0;
}

static int splice_array_reserve(ObjArray *oa, size_t min_capacity) {
    size_t cap;
    size_t newcap;
    Value *ni;

    if (!oa) return 0;
    if (!splice_array_capacity_valid(min_capacity)) return 0;

    cap = oa->capacity > 0 ? (size_t)oa->capacity : 0u;
    if (cap >= min_capacity) return 1;

    newcap = cap ? cap : 4u;
    while (newcap < min_capacity) {
        if (newcap >= SPLICE_MAX_ARRAY_CAPACITY) {
            newcap = SPLICE_MAX_ARRAY_CAPACITY;
            break;
        }
        if (newcap > SPLICE_MAX_ARRAY_CAPACITY / 2u) newcap = SPLICE_MAX_ARRAY_CAPACITY;
        else newcap *= 2u;
    }
    if (newcap < min_capacity || !splice_allocation_fits(newcap, sizeof(Value))) return 0;

    ni = (Value *)realloc(oa->items, sizeof(Value) * newcap);
    if (!ni) return 0;
    oa->items = ni;
    oa->capacity = (int)newcap;
    return 1;
}

static void splice_reset_vm(void) {
    vm_sp = 0;
    vm_ip = 0;
    var_stack_depth = 0;
    memset(vm_frame_epoch, 0, sizeof(vm_frame_epoch));
    vm_callsp = 0;
}
