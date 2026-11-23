#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

uint64_t scan(const unsigned char *, const unsigned char *, unsigned);
uint64_t ref_scan(const unsigned char *, const unsigned char *, unsigned);

int main(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *mem =
        mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == MAP_FAILED || mprotect(mem + page, page, PROT_NONE))
        return 1;
    unsigned char *end = mem + page;
    unsigned cases = 0;
    for (unsigned state = 0; state < 4; ++state) {
        for (unsigned byte = 0; byte < 256; ++byte) {
            for (unsigned len = 0; len <= 40; ++len) {
                unsigned char *p = end - len;
                memset(p, byte, len);
                for (unsigned i = 9; i < len; i += 11)
                    p[i] = (i % 2) ? 10 : 200;
                if (scan(p, end, state) != ref_scan(p, end, state))
                    return 2;
                ++cases;
            }
        }
    }
    printf("%u bounded self-run comparisons passed\n", cases);
    return munmap(mem, page * 2) != 0;
}
