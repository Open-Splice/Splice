static int splice_mul_overflows_size(size_t a, size_t b) {
    return a != 0 && b > SIZE_MAX / a;
}

static int splice_count_fits(size_t count, size_t elem_size) {
    return !splice_mul_overflows_size(count, elem_size);
}

static int splice_allocation_fits(size_t count, size_t elem_size) {
    if (!splice_count_fits(count, elem_size)) return 0;
    return count * elem_size <= (size_t)SPLICE_MAX_ALLOC_SIZE;
}

static int splice_remaining_at_least(size_t size, size_t pos, size_t needed) {
    return pos <= size && needed <= (size - pos);
}

static void *splice_malloc_bytes(size_t size_bytes) {
    if (size_bytes > (size_t)SPLICE_MAX_ALLOC_SIZE) return NULL;
    return malloc(size_bytes);
}

static void *splice_calloc_checked(size_t count, size_t elem_size) {
    if (!splice_allocation_fits(count, elem_size)) return NULL;
    return calloc(count, elem_size);
}

static int splice_array_capacity_valid(size_t capacity) {
    return capacity <= SPLICE_MAX_ARRAY_CAPACITY;
}

static uint8_t rd_u8(const unsigned char *data, size_t size, size_t *pos) {
    if (*pos >= size) SPLICE_FAIL("SPC_EOF");
    return data[(*pos)++];
}

static uint16_t rd_u16(const unsigned char *data, size_t size, size_t *pos) {
    uint16_t v = 0;
    v |= (uint16_t)rd_u8(data, size, pos);
    v |= (uint16_t)rd_u8(data, size, pos) << 8;
    return v;
}

static uint32_t rd_u32(const unsigned char *data, size_t size, size_t *pos) {
    uint32_t v = 0;
    v |= (uint32_t)rd_u8(data, size, pos);
    v |= (uint32_t)rd_u8(data, size, pos) << 8;
    v |= (uint32_t)rd_u8(data, size, pos) << 16;
    v |= (uint32_t)rd_u8(data, size, pos) << 24;
    return v;
}

static double rd_double(const unsigned char *data, size_t size, size_t *pos) {
    double out;
    if (*pos + 8 > size) SPLICE_FAIL("SPC_EOF");
    memcpy(&out, data + *pos, 8);
    *pos += 8;
    return out;
}
