static Value splice_load_variable(BytecodeProgram *prog, uint16_t idx) {
    if (idx >= prog->symbol_count) SPLICE_FAIL("SYMBOL_OOB");
    if (var_stack_depth > 0) {
        size_t frame = (size_t)var_stack_depth - 1u;
        size_t off = frame * prog->symbol_count + idx;
        if (prog->frame_stamp[off] == vm_frame_epoch[frame]) {
            return prog->frame_values[off];
        }
    }
    return prog->global_used[idx] ? prog->global_values[idx] : value_number(0.0);
}

static void splice_store_variable(BytecodeProgram *prog, uint16_t idx, Value v) {
    if (idx >= prog->symbol_count) SPLICE_FAIL("SYMBOL_OOB");
    if (var_stack_depth > 0) {
        size_t frame = (size_t)var_stack_depth - 1u;
        size_t off = frame * prog->symbol_count + idx;
        if (prog->frame_stamp[off] == vm_frame_epoch[frame]) {
            prog->frame_values[off] = v;
        } else if (prog->global_used[idx]) {
            prog->global_values[idx] = v;
        } else {
            prog->frame_stamp[off] = vm_frame_epoch[frame];
            prog->frame_values[off] = v;
        }
    } else {
        prog->global_used[idx] = 1;
        prog->global_values[idx] = v;
    }
}

static void splice_incdec_variable(BytecodeProgram *prog, uint16_t idx, double delta) {
    if (idx >= prog->symbol_count) SPLICE_FAIL("SYMBOL_OOB");
    if (var_stack_depth > 0) {
        size_t frame = (size_t)var_stack_depth - 1u;
        size_t off = frame * prog->symbol_count + idx;
        if (prog->frame_stamp[off] == vm_frame_epoch[frame]) {
            prog->frame_values[off].number += delta;
        } else if (prog->global_used[idx]) {
            prog->global_values[idx].number += delta;
        } else {
            prog->frame_stamp[off] = vm_frame_epoch[frame];
            prog->frame_values[off] = value_number(delta);
        }
    } else {
        if (!prog->global_used[idx]) {
            prog->global_used[idx] = 1;
            prog->global_values[idx] = value_number(delta);
        } else {
            prog->global_values[idx].number += delta;
        }
    }
}

static void splice_iadd_variable(BytecodeProgram *prog, uint16_t dst, uint16_t src) {
    Value rhs;
    if (dst >= prog->symbol_count || src >= prog->symbol_count) SPLICE_FAIL("SYMBOL_OOB");
    rhs = splice_load_variable(prog, src);
    if (var_stack_depth > 0) {
        size_t frame = (size_t)var_stack_depth - 1u;
        size_t dst_off = frame * prog->symbol_count + dst;
        if (prog->frame_stamp[dst_off] == vm_frame_epoch[frame]) {
            prog->frame_values[dst_off].number += rhs.number;
        } else if (prog->global_used[dst]) {
            prog->global_values[dst].number += rhs.number;
        } else {
            prog->frame_stamp[dst_off] = vm_frame_epoch[frame];
            prog->frame_values[dst_off] = value_number(rhs.number);
        }
    } else {
        if (!prog->global_used[dst]) {
            prog->global_used[dst] = 1;
            prog->global_values[dst] = value_number(rhs.number);
        } else {
            prog->global_values[dst].number += rhs.number;
        }
    }
}
