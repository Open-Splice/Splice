static inline void vm_push_fast(int *sp, Value v) {
#ifndef NDEBUG
    if (*sp >= VM_STACK_MAX) SPLICE_FAIL("STACK_OVERFLOW");
#endif
    vm_stack[(*sp)++] = v;
}

static inline Value vm_pop_fast(int *sp) {
#ifndef NDEBUG
    if (*sp <= 0) SPLICE_FAIL("STACK_UNDERFLOW");
#endif
    return vm_stack[--(*sp)];
}

static inline uint16_t fetch_u16_fast(const BytecodeProgram *p, uint32_t *ip) {
#ifndef NDEBUG
    if (*ip + 2 > p->code_size) SPLICE_FAIL("IP_OOB");
#endif
    uint16_t v = (uint16_t)p->code[*ip] | ((uint16_t)p->code[*ip + 1] << 8);
    *ip += 2;
    return v;
}

static inline uint32_t fetch_u32_fast(const BytecodeProgram *p, uint32_t *ip) {
#ifndef NDEBUG
    if (*ip + 4 > p->code_size) SPLICE_FAIL("IP_OOB");
#endif
    uint32_t v = (uint32_t)p->code[*ip] |
                 ((uint32_t)p->code[*ip + 1] << 8) |
                 ((uint32_t)p->code[*ip + 2] << 16) |
                 ((uint32_t)p->code[*ip + 3] << 24);
    *ip += 4;
    return v;
}

static int splice_execute_bytecode(const unsigned char *data, size_t size) {
    BytecodeProgram prog;
    if (!load_program(data, size, &prog)) return 0;

    splice_reset_vm();

    {
        int sp = vm_sp;
        uint32_t ip = vm_ip;
        int callsp = vm_callsp;
        int depth = var_stack_depth;

#define vm_push(v) vm_push_fast(&sp, (v))
#define vm_pop() vm_pop_fast(&sp)
#define fetch_u16(p) fetch_u16_fast((p), &ip)
#define fetch_u32(p) fetch_u32_fast((p), &ip)
#define vm_ip ip
#define vm_callsp callsp
#define var_stack_depth depth
#define SYNC_VM_STATE() do { vm_sp = sp; vm_ip = ip; vm_callsp = callsp; var_stack_depth = depth; } while (0)

        while (vm_ip < prog.code_size) {
            OpCode op = (OpCode)prog.code[vm_ip++];

            /* Keep shared VM state visible to split helper units. */
            vm_sp = sp;
            vm_ip = ip;
            vm_callsp = callsp;
            var_stack_depth = depth;

            switch (op) {
                case OP_PUSH_CONST: {
                    uint16_t idx = fetch_u16(&prog);
                    Constant c;
                    if (idx >= prog.const_count) SPLICE_FAIL("CONST_OOB");
                    c = prog.consts[idx];
                    if (c.type == CONST_NUMBER) vm_push(value_number(c.number));
                    else vm_push(value_string(c.string ? c.string : ""));
                    break;
                }
                case OP_LOAD: {
                    uint16_t idx = fetch_u16(&prog);
                    if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    if (var_stack_depth > 0) {
                        size_t frame = (size_t)var_stack_depth - 1u;
                        size_t off = frame * prog.symbol_count + idx;
                        if (prog.frame_stamp[off] == vm_frame_epoch[frame]) {
                            vm_push(prog.frame_values[off]);
                            break;
                        }
                    }
                    vm_push(prog.global_used[idx] ? prog.global_values[idx] : value_number(0.0));
                    break;
                }
                case OP_STORE: {
                    uint16_t idx = fetch_u16(&prog);
                    Value v;
                    if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    v = vm_pop();
                    if (var_stack_depth > 0) {
                        size_t frame = (size_t)var_stack_depth - 1u;
                        size_t off = frame * prog.symbol_count + idx;
                        if (prog.frame_stamp[off] == vm_frame_epoch[frame]) {
                            prog.frame_values[off] = v;
                        } else if (prog.global_used[idx]) {
                            prog.global_values[idx] = v;
                        } else {
                            prog.frame_stamp[off] = vm_frame_epoch[frame];
                            prog.frame_values[off] = v;
                        }
                    } else {
                        prog.global_used[idx] = 1;
                        prog.global_values[idx] = v;
                    }
                    break;
                }
                case OP_POP:
                    (void)vm_pop();
                    break;
                case OP_ADD: {
                    Value b = vm_pop();
                    Value a = vm_pop();
                    if (a.type == VAL_STRING && b.type == VAL_STRING) {
                        size_t la = strlen(a.string ? a.string : "");
                        size_t lb = strlen(b.string ? b.string : "");
                        char *s = splice_strdup_owned("");
                        s = (char *)realloc(s, la + lb + 1u);
                        if (!s) SPLICE_FAIL("OOM");
                        memcpy(s, a.string ? a.string : "", la);
                        memcpy(s + la, b.string ? b.string : "", lb);
                        s[la + lb] = 0;
                        vm_push(value_string(s));
                    } else {
                        vm_push(value_number(a.number + b.number));
                    }
                    break;
                }
                case OP_SUB: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number - b.number)); break; }
                case OP_MUL: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number * b.number)); break; }
                case OP_DIV: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number / b.number)); break; }
                case OP_MOD: {
                    Value b = vm_pop();
                    Value a = vm_pop();
                    int bi = (int)b.number;
                    if (bi == 0) SPLICE_FAIL("MOD_ZERO");
                    vm_push(value_number((double)((int)a.number % bi)));
                    break;
                }
                case OP_NEG: { Value a = vm_pop(); vm_push(value_number(-a.number)); break; }
                case OP_EQ: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(value_eq(a, b) ? 1.0 : 0.0)); break; }
                case OP_NEQ: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(value_eq(a, b) ? 0.0 : 1.0)); break; }
                case OP_LT: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number < b.number ? 1.0 : 0.0)); break; }
                case OP_GT: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number > b.number ? 1.0 : 0.0)); break; }
                case OP_LTE: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number <= b.number ? 1.0 : 0.0)); break; }
                case OP_GTE: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number(a.number >= b.number ? 1.0 : 0.0)); break; }
                case OP_JMP: {
                    uint32_t addr = fetch_u32(&prog);
                    if (addr > prog.code_size) SPLICE_FAIL("JMP_OOB");
                    vm_ip = addr;
                    break;
                }
                case OP_JMP_IF_FALSE: {
                    uint32_t addr = fetch_u32(&prog);
                    if (!value_truthy(vm_pop())) {
                        if (addr > prog.code_size) SPLICE_FAIL("JMP_OOB");
                        vm_ip = addr;
                    }
                    break;
                }
                case OP_CALL:
                case OP_CALL1: {
                    uint16_t symbol = fetch_u16(&prog);
                    uint16_t argc = (op == OP_CALL1) ? 1u : fetch_u16(&prog);
                    FunctionEntry *fn;

                    if (argc > VM_ARG_MAX) SPLICE_FAIL("ARGC_OOB");
                    if (symbol >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    fn = find_function(&prog, symbol);
                    if (!fn) {
                        Value argv[VM_ARG_MAX];
                        for (int i = (int)argc - 1; i >= 0; i--) argv[i] = vm_pop();
                        vm_push(call_builtin_or_native(prog.symbols[symbol], (int)argc, argv));
                        break;
                    }

                    if (vm_callsp >= CALLSTACK_MAX) SPLICE_FAIL("CALLSTACK_OOM");
                    if (var_stack_depth >= VAR_STACK_MAX) SPLICE_FAIL("VARSTACK_OOM");
                    vm_callstack[vm_callsp++].return_ip = vm_ip;

                    {
                        size_t frame = (size_t)var_stack_depth++;
                        uint8_t epoch = (uint8_t)(vm_frame_epoch[frame] + 1u);
                        if (epoch == 0) {
                            memset(prog.frame_stamp + frame * prog.symbol_count, 0, prog.symbol_count);
                            epoch = 1;
                        }
                        vm_frame_epoch[frame] = epoch;

                        if (op == OP_CALL1) {
                            if (fn->param_count > 0) {
                                if (fn->params[0] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                                prog.frame_stamp[frame * prog.symbol_count + fn->params[0]] = epoch;
                                prog.frame_values[frame * prog.symbol_count + fn->params[0]] = vm_pop();
                                for (uint16_t i = 1; i < fn->param_count; i++) {
                                    size_t off;
                                    if (fn->params[i] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                                    off = frame * prog.symbol_count + fn->params[i];
                                    prog.frame_stamp[off] = epoch;
                                    prog.frame_values[off] = value_number(0.0);
                                }
                            } else {
                                (void)vm_pop();
                            }
                        } else {
                            int limit = fn->param_count < argc ? fn->param_count : argc;
                            for (int i = (int)argc - 1; i >= limit; i--) (void)vm_pop();
                            for (int i = limit - 1; i >= 0; i--) {
                                size_t off;
                                if (fn->params[i] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                                off = frame * prog.symbol_count + fn->params[i];
                                prog.frame_stamp[off] = epoch;
                                prog.frame_values[off] = vm_pop();
                            }
                            for (uint16_t i = (uint16_t)limit; i < fn->param_count; i++) {
                                size_t off;
                                if (fn->params[i] >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                                off = frame * prog.symbol_count + fn->params[i];
                                prog.frame_stamp[off] = epoch;
                                prog.frame_values[off] = value_number(0.0);
                            }
                        }
                    }

                    vm_ip = fn->addr;
                    break;
                }
                case OP_RET: {
                    Value ret = vm_pop();
                    if (vm_callsp <= 0) {
                        vm_push(ret);
                        SYNC_VM_STATE();
                        free_program(&prog);
                        return 1;
                    }
                    vm_callsp--;
                    vm_ip = vm_callstack[vm_callsp].return_ip;
                    if (var_stack_depth > 0) var_stack_depth--;
                    vm_push(ret);
                    break;
                }
                case OP_PRINT: splice_print_value(vm_pop()); break;
                case OP_NOT: { Value v = vm_pop(); vm_push(value_number(value_truthy(v) ? 0.0 : 1.0)); break; }
                case OP_AND: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number((value_truthy(a) && value_truthy(b)) ? 1.0 : 0.0)); break; }
                case OP_OR: { Value b = vm_pop(); Value a = vm_pop(); vm_push(value_number((value_truthy(a) || value_truthy(b)) ? 1.0 : 0.0)); break; }
                case OP_ARRAY_NEW: {
                    uint16_t count = fetch_u16(&prog);
                    size_t array_capacity = count > 0 ? (size_t)count : 4u;
                    ObjArray *oa = (ObjArray *)malloc(sizeof(ObjArray));
                    if (!oa) SPLICE_FAIL("ARRAY_OOM");
                    oa->type = OBJ_ARRAY;
                    oa->count = (int)count;
                    oa->capacity = (int)array_capacity;
                    if (!splice_array_capacity_valid(array_capacity) || !splice_allocation_fits(array_capacity, sizeof(Value))) {
                        SPLICE_FAIL("ARRAY_OOM");
                    }
                    oa->items = (Value *)malloc(sizeof(Value) * array_capacity);
                    if (!oa->items) SPLICE_FAIL("ARRAY_OOM");
                    for (int i = (int)count - 1; i >= 0; i--) oa->items[i] = vm_pop();
                    vm_push(((Value){ VAL_OBJECT, 0.0, NULL, oa }));
                    break;
                }
                case OP_INDEX_GET: {
                    Value idxv = vm_pop();
                    Value arrv = vm_pop();
                    if (arrv.type != VAL_OBJECT || !arrv.object) {
                        vm_push(value_number(0.0));
                    } else {
                        ObjArray *oa = (ObjArray *)arrv.object;
                        int idx = (int)idxv.number;
                        if (idx < 0 || idx >= oa->count) vm_push(value_number(0.0));
                        else vm_push(oa->items[idx]);
                    }
                    break;
                }
                case OP_INDEX_SET: {
                    Value val = vm_pop();
                    Value idxv = vm_pop();
                    Value arrv = vm_pop();
                    ObjArray *oa;
                    int idx;
                    if (arrv.type != VAL_OBJECT || !arrv.object) SPLICE_FAIL("INDEX_TARGET");
                    oa = (ObjArray *)arrv.object;
                    idx = (int)idxv.number;
                    if (idx < 0) SPLICE_FAIL("INDEX_OOB");
                    if (idx >= oa->capacity && !splice_array_reserve(oa, (size_t)idx + 1u)) SPLICE_FAIL("ARRAY_OOM");
                    if (idx >= oa->count) {
                        for (int i = oa->count; i <= idx; i++) oa->items[i] = value_number(0.0);
                        oa->count = idx + 1;
                    }
                    oa->items[idx] = val;
                    vm_push(val);
                    break;
                }
                case OP_IMPORT: {
                    uint16_t idx = fetch_u16(&prog);
                    if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    if (!Splice_load_c_module_source(prog.symbols[idx])) SPLICE_FAIL("NATIVE_IMPORT_FAIL");
                    break;
                }
                case OP_INC:
                case OP_DEC: {
                    uint16_t idx = fetch_u16(&prog);
                    if (idx >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    {
                        double delta = (op == OP_INC) ? 1.0 : -1.0;
                        if (var_stack_depth > 0) {
                            size_t frame = (size_t)var_stack_depth - 1u;
                            size_t off = frame * prog.symbol_count + idx;
                            if (prog.frame_stamp[off] == vm_frame_epoch[frame]) {
                                prog.frame_values[off].number += delta;
                            } else if (prog.global_used[idx]) {
                                prog.global_values[idx].number += delta;
                            } else {
                                prog.frame_stamp[off] = vm_frame_epoch[frame];
                                prog.frame_values[off] = value_number(delta);
                            }
                        } else {
                            if (!prog.global_used[idx]) {
                                prog.global_used[idx] = 1;
                                prog.global_values[idx] = value_number(delta);
                            } else {
                                prog.global_values[idx].number += delta;
                            }
                        }
                    }
                    break;
                }
                case OP_IADD_VAR: {
                    uint16_t dst = fetch_u16(&prog);
                    uint16_t src = fetch_u16(&prog);
                    if (dst >= prog.symbol_count || src >= prog.symbol_count) SPLICE_FAIL("SYMBOL_OOB");
                    {
                        Value rhs = value_number(0.0);
                        if (var_stack_depth > 0) {
                            size_t frame = (size_t)var_stack_depth - 1u;
                            size_t src_off = frame * prog.symbol_count + src;
                            if (prog.frame_stamp[src_off] == vm_frame_epoch[frame]) rhs = prog.frame_values[src_off];
                            else if (prog.global_used[src]) rhs = prog.global_values[src];
                        } else if (prog.global_used[src]) {
                            rhs = prog.global_values[src];
                        }

                        if (var_stack_depth > 0) {
                            size_t frame = (size_t)var_stack_depth - 1u;
                            size_t dst_off = frame * prog.symbol_count + dst;
                            if (prog.frame_stamp[dst_off] == vm_frame_epoch[frame]) {
                                prog.frame_values[dst_off].number += rhs.number;
                            } else if (prog.global_used[dst]) {
                                prog.global_values[dst].number += rhs.number;
                            } else {
                                prog.frame_stamp[dst_off] = vm_frame_epoch[frame];
                                prog.frame_values[dst_off] = value_number(rhs.number);
                            }
                        } else {
                            if (!prog.global_used[dst]) {
                                prog.global_used[dst] = 1;
                                prog.global_values[dst] = value_number(rhs.number);
                            } else {
                                prog.global_values[dst].number += rhs.number;
                            }
                        }
                    }
                    break;
                }
                case OP_HALT:
                    SYNC_VM_STATE();
                    free_program(&prog);
                    return 1;
                default:
                    SPLICE_FAIL("BAD_OPCODE");
            }
        }

        SYNC_VM_STATE();
#undef vm_push
#undef vm_pop
#undef fetch_u16
#undef fetch_u32
#undef vm_ip
#undef vm_callsp
#undef var_stack_depth
#undef SYNC_VM_STATE
    }

    free_program(&prog);
    return 1;
}
