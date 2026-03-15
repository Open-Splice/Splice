static inline FunctionEntry *find_function(const BytecodeProgram *p, uint16_t symbol_idx) {
    if (symbol_idx >= p->symbol_count) return NULL;
    return p->func_by_symbol[symbol_idx];
}

static Value call_builtin_or_native(const char *name, int argc, Value *argv) {
#if SPLICE_EMBED
#define splice_sleep_ms(ms) SPLICE_EMBED_DELAY_MS(ms)
#elif defined(_WIN32)
#define splice_sleep_ms(ms) Sleep((DWORD)(ms))
#else
#define splice_sleep_ms(ms) usleep((useconds_t)((ms) * 1000))
#endif

    if (strcmp(name, "print") == 0) {
        if (argc > 0) splice_print_value(argv[0]);
        return value_number(0.0);
    }

    if (strcmp(name, "input") == 0) {
        size_t n;
        if (argc > 0) {
            if (argv[0].type == VAL_STRING) {
#if SPLICE_EMBED
                SPLICE_EMBED_PRINT(argv[0].string ? argv[0].string : "");
#else
                fputs(argv[0].string ? argv[0].string : "", stdout);
#endif
            } else {
                char pbuf[64];
                snprintf(pbuf, sizeof(pbuf), "%g", argv[0].number);
#if SPLICE_EMBED
                SPLICE_EMBED_PRINT(pbuf);
#else
                fputs(pbuf, stdout);
#endif
            }
#if !SPLICE_EMBED
            fflush(stdout);
#endif
        }

#if SPLICE_EMBED
        char in[128];
        n = 0;
#if !SPLICE_EMBED_HAS_INPUT
        in[0] = '\0';
#else
        while (n + 1 < sizeof(in)) {
            while (!SPLICE_EMBED_INPUT_AVAILABLE()) {
                SPLICE_EMBED_DELAY_MS(1);
            }
            {
                int ch = SPLICE_EMBED_INPUT_READ();
                if (ch < 0) continue;
                if (ch == '\r') continue;
                if (ch == '\n') break;
                in[n++] = (char)ch;
            }
        }
        in[n] = '\0';
#endif
#else
        char in[512];
        if (!fgets(in, sizeof(in), stdin)) return value_string("");
        n = strlen(in);
        if (n > 0 && in[n - 1] == '\n') in[n - 1] = '\0';
        n = strlen(in);
#endif

        return value_string(splice_strdup_owned(in));
    }

    if (strcmp(name, "sleep") == 0) {
        double secs = (argc > 0) ? argv[0].number : 0.0;
        if (secs < 0.0) secs = 0.0;
        splice_sleep_ms((unsigned int)(secs * 1000.0));
        return value_number(0.0);
    }

    if (strcmp(name, "noop") == 0) return value_number(0.0);

    if (strcmp(name, "len") == 0) {
        if (argc < 1) return value_number(0.0);
        if (argv[0].type == VAL_STRING) return value_number((double)strlen(argv[0].string ? argv[0].string : ""));
        if (argv[0].type == VAL_OBJECT && argv[0].object) {
            ObjArray *oa = (ObjArray *)argv[0].object;
            if (oa->type == OBJ_ARRAY || oa->type == OBJ_TUPLE) return value_number((double)oa->count);
        }
        return value_number(0.0);
    }

    if (strcmp(name, "append") == 0) {
        Value target;
        Value val;
        ObjArray *oa;
        if (argc < 2) return value_number(0.0);
        target = argv[0];
        val = argv[1];
        if (target.type != VAL_OBJECT || !target.object) SPLICE_FAIL("APPEND_TARGET");
        oa = (ObjArray *)target.object;
        if (oa->type != OBJ_ARRAY) SPLICE_FAIL("APPEND_TARGET");
        if (oa->count >= oa->capacity && !splice_array_reserve(oa, (size_t)oa->count + 1u)) {
            SPLICE_FAIL("ARRAY_OOM");
        }
        if (val.type == VAL_STRING) {
            Value copy = val;
            copy.string = splice_strdup_owned(val.string);
            oa->items[oa->count++] = copy;
        } else {
            oa->items[oa->count++] = val;
        }
        return value_number((double)oa->count);
    }

    if (strcmp(name, "sin") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(sin(argv[0].number));
    }
    if (strcmp(name, "cos") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(cos(argv[0].number));
    }
    if (strcmp(name, "tan") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(tan(argv[0].number));
    }
    if (strcmp(name, "sqrt") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER || argv[0].number < 0.0) return value_number(0.0);
        return value_number(sqrt(argv[0].number));
    }
    if (strcmp(name, "pow") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER) return value_number(0.0);
        return value_number(pow(argv[0].number, argv[1].number));
    }
    if (strcmp(name, "mod") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER || argv[1].number == 0.0) {
            return value_number(0.0);
        }
        return value_number(fmod(argv[0].number, argv[1].number));
    }
    if (strcmp(name, "abs") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(fabs(argv[0].number));
    }
    if (strcmp(name, "floor") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(floor(argv[0].number));
    }
    if (strcmp(name, "ceil") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(ceil(argv[0].number));
    }
    if (strcmp(name, "round") == 0) {
        if (argc < 1 || argv[0].type != VAL_NUMBER) return value_number(0.0);
        return value_number(round(argv[0].number));
    }
    if (strcmp(name, "min") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER) return value_number(0.0);
        return value_number(argv[0].number < argv[1].number ? argv[0].number : argv[1].number);
    }
    if (strcmp(name, "max") == 0) {
        if (argc < 2 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER) return value_number(0.0);
        return value_number(argv[0].number > argv[1].number ? argv[0].number : argv[1].number);
    }
    if (strcmp(name, "clamp") == 0) {
        double x;
        if (argc < 3 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER || argv[2].type != VAL_NUMBER) {
            return value_number(0.0);
        }
        x = argv[0].number;
        if (x < argv[1].number) x = argv[1].number;
        if (x > argv[2].number) x = argv[2].number;
        return value_number(x);
    }
    if (strcmp(name, "to_number") == 0) {
        if (argc < 1) return value_number(0.0);
        if (argv[0].type == VAL_NUMBER) return argv[0];
        if (argv[0].type == VAL_STRING) return value_number(strtod(argv[0].string ? argv[0].string : "", NULL));
        return value_number(0.0);
    }
    if (strcmp(name, "lerp") == 0) {
        if (argc < 3 || argv[0].type != VAL_NUMBER || argv[1].type != VAL_NUMBER || argv[2].type != VAL_NUMBER) {
            return value_number(0.0);
        }
        return value_number(argv[0].number + (argv[1].number - argv[0].number) * argv[2].number);
    }
    if (strcmp(name, "slice") == 0) {
        ObjArray *src;
        ObjArray *oa;
        int start;
        int end;
        int count;
        if (argc < 3 || argv[0].type != VAL_OBJECT || !argv[0].object) return value_number(0.0);
        src = (ObjArray *)argv[0].object;
        start = (int)argv[1].number;
        end = (int)argv[2].number;
        if (start < 0) start = 0;
        if (end > src->count) end = src->count;
        if (end < start) end = start;
        count = end - start;
        oa = (ObjArray *)malloc(sizeof(ObjArray));
        if (!oa) SPLICE_FAIL("ARRAY_OOM");
        oa->type = OBJ_ARRAY;
        oa->count = count;
        oa->capacity = count;
        if (count < 0 || !splice_array_capacity_valid((size_t)count) || !splice_count_fits((size_t)count, sizeof(Value))) {
            SPLICE_FAIL("ARRAY_OOM");
        }
        oa->items = count > 0 ? (Value *)malloc(sizeof(Value) * (size_t)count) : NULL;
        if (count > 0 && !oa->items) SPLICE_FAIL("ARRAY_OOM");
        for (int i = 0; i < count; i++) oa->items[i] = src->items[start + i];
        return (Value){ VAL_OBJECT, 0.0, NULL, oa };
    }
    if (strcmp(name, "split") == 0) {
        ObjArray *oa;
        char *copy;
        char *tok;
        const char *str;
        const char *sep;
        if (argc < 2) return value_number(0.0);
        str = argv[0].string ? argv[0].string : "";
        sep = argv[1].string ? argv[1].string : "";
        oa = (ObjArray *)malloc(sizeof(ObjArray));
        if (!oa) SPLICE_FAIL("ARRAY_OOM");
        oa->type = OBJ_ARRAY;
        oa->count = 0;
        oa->capacity = 8;
        oa->items = (Value *)malloc(sizeof(Value) * (size_t)oa->capacity);
        if (!oa->items) SPLICE_FAIL("ARRAY_OOM");
        copy = splice_strdup_owned(str);
        tok = strtok(copy, sep);
        while (tok) {
            if (oa->count >= oa->capacity && !splice_array_reserve(oa, (size_t)oa->count + 1u)) {
                free(copy);
                SPLICE_FAIL("ARRAY_OOM");
            }
            oa->items[oa->count++] = value_string(splice_strdup_owned(tok));
            tok = strtok(NULL, sep);
        }
        free(copy);
        return (Value){ VAL_OBJECT, 0.0, NULL, oa };
    }

    {
        SpliceCFunc native = Splice_get_native(name);
        if (!native) SPLICE_FAIL("UNDEF_FUNC");
        return native(argc, argv);
    }
}
