#include "opcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static FILE *bc_out;

static inline void bc_write_byte(unsigned char b) { fputc(b, bc_out); }
static inline void bc_write_string(const char *s) {
    unsigned short len = (unsigned short)strlen(s);
    fwrite(&len, 2, 1, bc_out);
    fwrite(s, 1, len, bc_out);
}

void build_bytecode(const char *src, const char *outfile) {
    bc_out = fopen(outfile, "wb");
    if (!bc_out) { perror("open outfile"); exit(1); }

    const char *p = src;
    while (*p) {
        if (isspace((unsigned char)*p)) { p++; continue; }

        // --- Keywords ---
        if (!strncmp(p,"let",3) && !isalnum(p[3])) { bc_write_byte(OP_LET); p+=3; continue; }
        if (!strncmp(p,"print",5)&&!isalnum(p[5])){ bc_write_byte(OP_PRINT); p+=5; continue; }
        if (!strncmp(p,"raise",5)&&!isalnum(p[5])){ bc_write_byte(OP_RAISE); p+=5; continue; }
        if (!strncmp(p,"warn",4)&&!isalnum(p[4])) { bc_write_byte(OP_WARN); p+=4; continue; }
        if (!strncmp(p,"info",4)&&!isalnum(p[4])) { bc_write_byte(OP_INFO); p+=4; continue; }
        if (!strncmp(p,"while",5)&&!isalnum(p[5])){ bc_write_byte(OP_WHILE); p+=5; continue; }
        if (!strncmp(p,"if",2)&&!isalnum(p[2]))   { bc_write_byte(OP_IF); p+=2; continue; }
        if (!strncmp(p,"else",4)&&!isalnum(p[4])) { bc_write_byte(OP_ELSE); p+=4; continue; }
        if (!strncmp(p,"func",4)&&!isalnum(p[4])) { bc_write_byte(OP_FUNC); p+=4; continue; }
        if (!strncmp(p,"return",6)&&!isalnum(p[6])){ bc_write_byte(OP_RETURN); p+=6; continue; }
        if (!strncmp(p,"import",6)&&!isalnum(p[6])){ bc_write_byte(OP_IMPORT); p+=6; continue; }
        if (!strncmp(p,"for",3)&&!isalnum(p[3]))  { bc_write_byte(OP_FOR); p+=3; continue; }
        if (!strncmp(p,"in",2)&&!isalnum(p[2]))   { bc_write_byte(OP_IN); p+=2; continue; }
        if (!strncmp(p,"true",4)&&!isalnum(p[4])) { bc_write_byte(OP_TRUE); p+=4; continue; }
        if (!strncmp(p,"false",5)&&!isalnum(p[5])){ bc_write_byte(OP_FALSE); p+=5; continue; }
        if (!strncmp(p,"and",3)&&!isalnum(p[3]))  { bc_write_byte(OP_AND); p+=3; continue; }
        if (!strncmp(p,"or",2)&&!isalnum(p[2]))   { bc_write_byte(OP_OR); p+=2; continue; }
        if (!strncmp(p,"not",3)&&!isalnum(p[3]))  { bc_write_byte(OP_NOT); p+=3; continue; }

        // --- Compound operators ---
        if (!strncmp(p,"==",2)) { bc_write_byte(OP_EQ); p+=2; continue; }
        if (!strncmp(p,"!=",2)) { bc_write_byte(OP_NEQ); p+=2; continue; }
        if (!strncmp(p,"<=",2)) { bc_write_byte(OP_LE); p+=2; continue; }
        if (!strncmp(p,">=",2)) { bc_write_byte(OP_GE); p+=2; continue; }
    if (!strncmp(p,"&&",2)) { bc_write_byte(OP_AND); p+=2; continue; }
    if (!strncmp(p,"||",2)) { bc_write_byte(OP_OR); p+=2; continue; }

    // single '!' -> NOT
    if (*p=='!') { bc_write_byte(OP_NOT); p++; continue; }

        // --- Single-char ops ---
        if (*p=='='){ bc_write_byte(OP_ASSIGN); p++; continue; }
        if (*p=='+'){ bc_write_byte(OP_PLUS); p++; continue; }
        if (*p=='-'){ bc_write_byte(OP_MINUS); p++; continue; }
        if (*p=='*'){ bc_write_byte(OP_MULTIPLY); p++; continue; }
        if (*p=='/'){ bc_write_byte(OP_DIVIDE); p++; continue; }
        if (*p=='<'){ bc_write_byte(OP_LT); p++; continue; }
        if (*p=='>'){ bc_write_byte(OP_GT); p++; continue; }
        if (*p==';'){ bc_write_byte(OP_SEMICOLON); p++; continue; }
        if (*p==','){ bc_write_byte(OP_COMMA); p++; continue; }
        if (*p=='.'){ bc_write_byte(OP_DOT); p++; continue; }

        // --- Grouping ---
        if (*p=='('){ bc_write_byte(OP_LPAREN); p++; continue; }
        if (*p==')'){ bc_write_byte(OP_RPAREN); p++; continue; }
        if (*p=='{'){ bc_write_byte(OP_LBRACE); p++; continue; }
        if (*p=='}'){ bc_write_byte(OP_RBRACE); p++; continue; }
        if (*p=='['){ bc_write_byte(OP_LBRACKET); p++; continue; }
        if (*p==']'){ bc_write_byte(OP_RBRACKET); p++; continue; }

        // --- Strings ---
        if (*p=='"') {
            p++; const char *start=p;
            while(*p && *p!='"') p++;
            size_t len=p-start;
            char *buf=(char*)malloc(len+1); memcpy(buf,start,len); buf[len]=0;
            bc_write_byte(OP_STRING);
            bc_write_string(buf);
            free(buf);
            if(*p=='"') p++;
            continue;
        }

        // --- Numbers ---
        if (isdigit((unsigned char)*p)) {
            const char *start=p;
            while(isdigit((unsigned char)*p)) p++;
            if(*p=='.'){ p++; while(isdigit((unsigned char)*p)) p++; }
            size_t len=p-start;
            char *buf=(char*)malloc(len+1); memcpy(buf,start,len); buf[len]=0;
            bc_write_byte(OP_NUMBER);
            bc_write_string(buf);
            free(buf);
            continue;
        }

        // --- Identifiers ---
        if (isalpha((unsigned char)*p) || *p=='_') {
            const char *start=p;
            while(isalnum((unsigned char)*p) || *p=='_') p++;
            size_t len=p-start;
            char *buf=(char*)malloc(len+1); memcpy(buf,start,len); buf[len]=0;
            bc_write_byte(OP_IDENTIFIER);
            bc_write_string(buf);
            free(buf);
            continue;
        }

        p++;
    }
    fclose(bc_out);
}
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.splice> <output.spbc>\n", argv[0]);
        return 1;
    }

    // Read input file
    FILE *in = fopen(argv[1], "r");
    if (!in) { perror("open input"); return 1; }
    fseek(in, 0, SEEK_END);
    long len = ftell(in);
    rewind(in);

    char *src = (char*)malloc(len+1);
    fread(src, 1, len, in);
    src[len] = '\0';
    fclose(in);

    // Build bytecode
    build_bytecode(src, argv[2]);

    free(src);
    printf("Compiled %s -> %s\n", argv[1], argv[2]);
    return 0;
}
