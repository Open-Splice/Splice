static void free_program(BytecodeProgram *p) {
    if (!p) return;

    if (p->consts) {
        for (uint16_t i = 0; i < p->const_count; i++) {
            if (p->consts[i].type == CONST_STRING) free((void *)p->consts[i].string);
        }
    }
    if (p->symbols) {
        for (uint16_t i = 0; i < p->symbol_count; i++) free((void *)p->symbols[i]);
    }
    if (p->funcs) {
        for (uint16_t i = 0; i < p->func_count; i++) free(p->funcs[i].params);
    }

    free(p->consts);
    free((void *)p->symbols);
    free(p->funcs);
    free(p->func_by_symbol);
    free(p->global_values);
    free(p->global_used);
    free(p->frame_values);
    free(p->frame_stamp);
    if (p->owns_code) free((void *)p->code);
    memset(p, 0, sizeof(*p));
}

static int load_program(const unsigned char *data, size_t size, BytecodeProgram *out) {
    size_t pos = 5;
    size_t const_capacity;
    size_t symbol_capacity;
    size_t func_capacity;
    size_t frame_slots;

    memset(out, 0, sizeof(*out));
    if (size < 5) return 0;
    if (memcmp(data, SPC_MAGIC, 4) != 0) return 0;
    if (data[4] != SPC_VERSION) return 0;

    out->const_count = rd_u16(data, size, &pos);
    const_capacity = out->const_count ? (size_t)out->const_count : 1u;
    if (!splice_remaining_at_least(size, pos, (size_t)out->const_count)) return 0;
    if (out->const_count > SPLICE_MAX_CONST_COUNT) return 0;
    if (!splice_array_capacity_valid(const_capacity)) return 0;
    if (!splice_allocation_fits(const_capacity, sizeof(Constant))) return 0;
    out->consts = (Constant *)splice_calloc_checked(const_capacity, sizeof(Constant));
    if (!out->consts) return 0;
    for (uint16_t i = 0; i < out->const_count; i++) {
        out->consts[i].type = rd_u8(data, size, &pos);
        if (out->consts[i].type == CONST_NUMBER) out->consts[i].number = rd_double(data, size, &pos);
        else if (out->consts[i].type == CONST_STRING) out->consts[i].string = rd_str(data, size, &pos);
        else return 0;
    }

    out->symbol_count = rd_u16(data, size, &pos);
    symbol_capacity = out->symbol_count ? (size_t)out->symbol_count : 1u;
    if (!splice_remaining_at_least(size, pos, symbol_capacity * sizeof(uint32_t))) return 0;
    if (out->symbol_count > SPLICE_MAX_SYMBOL_COUNT) return 0;
    if (!splice_array_capacity_valid(symbol_capacity)) return 0;
    if (!splice_allocation_fits(symbol_capacity, sizeof(char *))) return 0;
    out->symbols = (const char **)splice_calloc_checked(symbol_capacity, sizeof(char *));
    if (!out->symbols) return 0;
    for (uint16_t i = 0; i < out->symbol_count; i++) {
        out->symbols[i] = rd_str(data, size, &pos);
    }

    out->func_count = rd_u16(data, size, &pos);
    func_capacity = out->func_count ? (size_t)out->func_count : 1u;
    if (!splice_remaining_at_least(size, pos, func_capacity * (sizeof(uint16_t) * 2u + sizeof(uint32_t)))) return 0;
    if (out->func_count > SPLICE_MAX_FUNC_COUNT) return 0;
    if (!splice_array_capacity_valid(func_capacity)) return 0;
    if (!splice_allocation_fits(func_capacity, sizeof(FunctionEntry))) return 0;
    out->funcs = (FunctionEntry *)splice_calloc_checked(func_capacity, sizeof(FunctionEntry));
    if (!out->funcs) return 0;
    for (uint16_t i = 0; i < out->func_count; i++) {
        out->funcs[i].symbol = rd_u16(data, size, &pos);
        out->funcs[i].param_count = rd_u16(data, size, &pos);
        if (out->funcs[i].param_count > SPLICE_MAX_PARAM_COUNT) return 0;
        out->funcs[i].addr = rd_u32(data, size, &pos);
        if (out->funcs[i].param_count > 0) {
            size_t param_capacity = (size_t)out->funcs[i].param_count;
            if (!splice_remaining_at_least(size, pos, param_capacity * sizeof(uint16_t))) return 0;
            if (!splice_array_capacity_valid(param_capacity)) return 0;
            if (!splice_allocation_fits(param_capacity, sizeof(uint16_t))) return 0;
            out->funcs[i].params = (uint16_t *)splice_calloc_checked(param_capacity, sizeof(uint16_t));
            if (!out->funcs[i].params) return 0;
            for (uint16_t j = 0; j < out->funcs[i].param_count; j++) {
                out->funcs[i].params[j] = rd_u16(data, size, &pos);
            }
        }
    }

    if (!splice_allocation_fits(symbol_capacity, sizeof(FunctionEntry *))) return 0;
    out->func_by_symbol = (FunctionEntry **)splice_calloc_checked(symbol_capacity, sizeof(FunctionEntry *));
    if (!out->func_by_symbol) return 0;
    for (uint16_t i = 0; i < out->func_count; i++) {
        if (out->funcs[i].symbol < out->symbol_count) out->func_by_symbol[out->funcs[i].symbol] = &out->funcs[i];
    }

    if (!splice_allocation_fits(symbol_capacity, sizeof(Value))) return 0;
    if (!splice_allocation_fits(symbol_capacity, sizeof(uint8_t))) return 0;
    if (splice_mul_overflows_size(symbol_capacity, (size_t)VAR_STACK_MAX)) return 0;
    frame_slots = symbol_capacity * (size_t)VAR_STACK_MAX;
    if (!splice_allocation_fits(frame_slots, sizeof(Value))) return 0;
    if (!splice_allocation_fits(frame_slots, sizeof(uint8_t))) return 0;
    out->global_values = (Value *)splice_calloc_checked(symbol_capacity, sizeof(Value));
    out->global_used = (uint8_t *)splice_calloc_checked(symbol_capacity, sizeof(uint8_t));
    out->frame_values = (Value *)splice_calloc_checked(frame_slots, sizeof(Value));
    out->frame_stamp = (uint8_t *)splice_calloc_checked(frame_slots, sizeof(uint8_t));
    if (!out->global_values || !out->global_used || !out->frame_values || !out->frame_stamp) return 0;

    out->code_size = rd_u32(data, size, &pos);
    if (pos + out->code_size > size) return 0;
    out->code = data + pos;
    out->owns_code = 0;
    return 1;
}
