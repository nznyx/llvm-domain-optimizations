#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
int scan(unsigned char *, unsigned char *, int, void *);
int ref_scan(unsigned char *, unsigned char *, int, void *);
int sentinel(unsigned char *, int);
int ref_sentinel(unsigned char *, int);
int main(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t pairs = 0;
    unsigned char *mem = mmap(NULL,4*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mem==MAP_FAILED || mprotect(mem+page,page,PROT_NONE) || mprotect(mem+3*page,page,PROT_NONE))return 1;
    unsigned char *a=mem+page,*b=mem+3*page;
    for (unsigned seed=0;seed<4;seed++) for(unsigned value=0;value<256;value++)
        for(unsigned len=0;len<=40;len++) {
            unsigned char *x=a-len,*y=b-len;
            for(unsigned i=0;i<len;i++) x[i]=(unsigned char)(i%11==10?value+1:value);
            memcpy(y,x,len);
            uint32_t ma=123, mb=123;
            if(scan(x,a,seed,&ma)!=ref_scan(y,b,seed,&mb) || ma!=mb || memcmp(x,y,len))return 2;
            ++pairs;
            if(len>=4) {
                if(scan(x,a,seed,x+len-4)!=ref_scan(y,b,seed,y+len-4) || memcmp(x,y,len))return 3;
                ++pairs;
            }
            if(len) {
                x[len-1]=y[len-1]=0;
                if(sentinel(x,seed)!=ref_sentinel(y,seed))return 4;
                ++pairs;
            }
        }
    printf("%zu differential checks passed\n",pairs);
    return munmap(mem,4*page)!=0;
}
