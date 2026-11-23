#include <stdbool.h>
#include <stdint.h>
#include <cgraph/cghdr.h>
#include "grammar.h"

struct yy_buffer_state* aag_scan_bytes(const char* bytes, int size);
int aaglex_destroy(void);
int aagget_leng(void);

bool scan_tokens(const char* input, int size, uint64_t* digest) {
    aglexinit(NULL, NULL);
    agsetfile(NULL);
    agreadline(1);
    aag_scan_bytes(input, size);
    int token;
    while ((token = aaglex()) > 0) {
        *digest = (*digest * 31 + token) * 31 + aagget_leng();
        if (token == T_atom || token == T_qatom)
            agstrfree(NULL, aaglval.str);
    }
    aaglex_destroy();
    return token == 0;
}
