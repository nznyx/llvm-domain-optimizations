#include <stdbool.h>
#include <stdint.h>
#include "compile.h"
#include "parser.h"
#include "lexer.h"

bool scan_tokens(const char* input, int size, uint64_t* digest) {
    yyscan_t scanner;
    if (jq_yylex_init(&scanner)) return false;
    jq_yy_scan_bytes(input, size, scanner);
    YYSTYPE value;
    location loc;
    int token;
    while ((token = jq_yylex(&value, &loc, scanner))) {
        if (token == INVALID_CHARACTER) break;
        *digest = (*digest * 31 + token) * 31 + (loc.end - loc.start);
        if (token == FORMAT || token == LITERAL || token == QQSTRING_TEXT ||
            token == IDENT || token == FIELD || token == BINDING) {
            bool valid = jv_is_valid(value.literal);
            jv_free(value.literal);
            if (!valid) break;
        }
    }
    jq_yylex_destroy(scanner);
    return token == 0;
}
