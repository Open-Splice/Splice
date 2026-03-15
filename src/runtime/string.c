static char *splice_strdup_owned(const char *s) {
    size_t len;
    char *copy;

    if (!s) s = "";
    len = strlen(s);
    copy = (char *)splice_malloc_bytes(len + 1u);
    if (!copy) SPLICE_FAIL("OOM");
    memcpy(copy, s, len + 1u);
    return copy;
}

static Value value_string(const char *s) {
    Value v = { VAL_STRING, 0.0, s, NULL };
    return v;
}

static int value_eq(Value a, Value b) {
    if (a.type == VAL_STRING && b.type == VAL_STRING) {
        const char *as = a.string ? a.string : "";
        const char *bs = b.string ? b.string : "";
        return strcmp(as, bs) == 0;
    }
    return a.number == b.number;
}

static void splice_print_value(Value v) {
    char buf[64];

    switch (v.type) {
        case VAL_STRING:
            SPLICE_PRINTLN(v.string ? v.string : "(null)");
            break;
        case VAL_NUMBER:
            snprintf(buf, sizeof(buf), "%g", v.number);
            SPLICE_PRINTLN(buf);
            break;
        case VAL_OBJECT:
            SPLICE_PRINTLN("<object>");
            break;
        default:
            SPLICE_PRINTLN("<unknown>");
            break;
    }
}

static char *rd_str(const unsigned char *data, size_t size, size_t *pos) {
    uint32_t len = rd_u32(data, size, pos);
    char *s;

    if ((size_t)len > SIZE_MAX - 1u) SPLICE_FAIL("SPC_STR");
    if (!splice_remaining_at_least(size, *pos, (size_t)len)) SPLICE_FAIL("SPC_STR");
    s = (char *)splice_malloc_bytes((size_t)len + 1u);
    if (!s) SPLICE_FAIL("OOM");
    memcpy(s, data + *pos, len);
    s[len] = 0;
    *pos += len;
    return s;
}
