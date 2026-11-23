#include <stdint.h>
#include <stdio.h>

int decode(int state, uint8_t byte);
int step(int state, uint8_t byte);
extern int visits;
int main(void) {
    const int tokens[] = {101,19,32,85,11,7};
    for (int s = -1; s <= 5; ++s) {
        for (int byte = 0; byte <= 255; ++byte) {
            int wanted = -1;
            if (s >= 0 && s < 5) {
                int i = byte - 'A';
                wanted = i >= 0 && i <= s ? tokens[i] : tokens[5];
            }
            if (decode(s,(uint8_t)byte) != wanted) return 1;
        }
    }
    if (visits != 5 * 256) return 1;
    const int accept[] = {12,5,4,8,9,3,1,11,2};
    for (int s = -1; s <= 6; ++s) {
        for (int byte = 0; byte <= 255; ++byte) {
            int expected = -1;
            if (s >= 0 && s < 6) {
                int state = s, symbol = byte % 4;
                while (state && state != symbol) { --state; symbol ^= 1; }
                expected = accept[state+symbol];
                if (expected == 5) expected = 50;
            }
            if (step(s,(uint8_t)byte) != expected) return 1;
        }
    }
    puts("3840 cases passed");
    return 0;
}
