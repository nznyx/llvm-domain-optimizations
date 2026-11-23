#include "cmListFileLexer.h"
#include <stdbool.h>
#include <stdint.h>

bool scan_tokens(const char* input, int size, uint64_t* digest) {
    (void)size; // SetString reads the NUL-terminated string owned by main.
    cmListFileLexer* lexer = cmListFileLexer_New();
    if (!lexer) return false;
    bool ok = cmListFileLexer_SetString(lexer, input);
    cmListFileLexer_Token* token;
    while (ok && (token = cmListFileLexer_Scan(lexer))) {
        ok = token->type < cmListFileLexer_Token_BadCharacter;
        *digest = (*digest * 31 + token->type) * 31 + token->length;
    }
    cmListFileLexer_Delete(lexer);
    return ok;
}
