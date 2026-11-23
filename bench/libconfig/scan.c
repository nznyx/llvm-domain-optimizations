#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "scanctx.h"
#include "parsectx.h"
#include "grammar.h"
#include "scanner.h"

static const char** no_includes(config_t* config, const char* dir, const char* path, const char** error) {
    *error = "benchmark inputs must be self-contained";
    return NULL;
}

bool scan_tokens(const char* input, int size, uint64_t* digest) {
    struct scan_context context;
    libconfig_scanctx_init(&context, NULL);
    config_t config = {.include_fn = no_includes};
    context.config = &config;
    yyscan_t scanner;
    if (libconfig_yylex_init_extra(&context, &scanner)) return false;
    libconfig_yy_scan_bytes(input, size, scanner);
    YYSTYPE value;
    int token;
    while ((token = libconfig_yylex(&value, scanner))) {
        if (token == TOK_ERROR || token == TOK_GARBAGE) break;
        *digest = (*digest * 31 + token) * 31 + libconfig_yyget_leng(scanner);
        if (token == TOK_STRING) free(value.sval);
    }
    libconfig_yylex_destroy(scanner);
    libconfig_strvec_delete(libconfig_scanctx_cleanup(&context));
    return token == 0;
}
