#include "../src/cover_decode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static const unsigned char png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,208,240,31,0,6,0,2,127,33,2,115,59,0,0,0,0,73,69,78,68,174,66,96,130};
int main(int argc,char** argv){
 uint32_t pixels[256*256];
 assert(decode_cover(png,sizeof png,pixels,256));
 for(unsigned i=0;i<256*256;i++)assert(pixels[i]==0xff8000ffu);
 assert(!decode_cover(NULL,0,pixels,256));assert(!decode_cover(png,8,pixels,256));
 assert(!decode_cover(png,sizeof png,pixels,513));assert(!decode_cover(png,sizeof png,NULL,256));
 assert(!decode_cover(png,1024*1024+1,pixels,256));
 if(argc>1){FILE* f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0&&n<=1024*1024);unsigned char* bytes=malloc(n);assert(bytes);assert(fread(bytes,1,n,f)==(size_t)n);fclose(f);assert(decode_cover(bytes,n,pixels,256));free(bytes);}
 puts("PASS: PNG channels, resize, corrupt and bounded input; optional local artwork.");return 0;
}
