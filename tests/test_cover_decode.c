#include "../src/cover_decode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const unsigned char png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,208,240,31,0,6,0,2,127,33,2,115,59,0,0,0,0,73,69,78,68,174,66,96,130};
static unsigned crc32_png(const unsigned char *p,size_t n){unsigned c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static const unsigned char tga[]={0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,8,0x80,0x40,0x20,0xff};
static unsigned char *large_png(size_t *out_size){
 const size_t payload=1024u*1024u, chunk=payload+12, prefix=sizeof png-12;
 unsigned char *b=malloc(prefix+chunk+12);assert(b);memcpy(b,png,prefix);
 unsigned char *c=b+prefix;c[0]=(payload>>24)&255;c[1]=(payload>>16)&255;c[2]=(payload>>8)&255;c[3]=payload&255;
 memcpy(c+4,"tEXt",4);memcpy(c+8,"Large",5);c[13]=0;memset(c+14,'x',payload-6);
 unsigned crc=crc32_png(c+4,payload+4);c[8+payload]=(crc>>24)&255;c[9+payload]=(crc>>16)&255;c[10+payload]=(crc>>8)&255;c[11+payload]=crc&255;
 memcpy(c+chunk,png+prefix,12);*out_size=prefix+chunk+12;return b;
}
int main(int argc,char** argv){
 uint32_t pixels[256*256];
 assert(decode_cover(png,sizeof png,pixels,256));
 for(unsigned i=0;i<256*256;i++)assert(pixels[i]==0xff8000ffu);
 assert(!decode_cover(NULL,0,pixels,256));assert(!decode_cover(png,8,pixels,256));
 assert(!decode_cover(png,sizeof png,pixels,513));assert(!decode_cover(png,sizeof png,NULL,256));
 size_t big_size=0;unsigned char *big=large_png(&big_size);assert(big_size>1024*1024);assert(decode_cover(big,big_size,pixels,256));assert(decode_cover_rect(big,big_size,pixels,256,256));free(big);
 assert(decode_cover(tga,sizeof tga,pixels,256));
 {
  /* 4K box art is about 1530 x 2160: taller than 2048 but accepted; beyond 2400 a side is refused. */
  size_t tall_size=18+4*2200;unsigned char *tall=calloc(tall_size,1);assert(tall);
  tall[2]=2;tall[12]=1;tall[14]=2200&255;tall[15]=2200>>8;tall[16]=32;tall[17]=8;
  for(size_t i=0;i<2200;i++){tall[18+4*i]=255;tall[18+4*i+3]=255;}
  assert(decode_cover(tall,tall_size,pixels,256));assert(decode_cover_rect(tall,tall_size,pixels,128,128));
  tall[14]=2500&255;tall[15]=2500>>8;tall_size=18+4*2500;tall=realloc(tall,tall_size);assert(tall);
  assert(!decode_cover(tall,tall_size,pixels,256));assert(!decode_cover_rect(tall,tall_size,pixels,128,128));
  free(tall);
 }
 {
  // A wide red picture (2x1) leaves bars above and below: they must take the picture's colour, not flat black.
  static const unsigned char wide[]={0,0,2,0,0,0,0,0,0,0,0,0,2,0,1,0,32,0x28,0,0,255,255,0,0,255,255};
  assert(decode_cover(wide,sizeof wide,pixels,256));
  const uint32_t bar=pixels[5*256+128],mid=pixels[128*256+128];
  assert((bar&255)>(bar>>8&255)+10&&(bar&255)>(bar>>16&255)+10); /* reddish, not grey */
  assert(bar!=0xff201a16u&&mid==0xff0000ffu);                      /* the picture itself is untouched */
 }
 if(argc>1){FILE* f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0);unsigned char* bytes=malloc((size_t)n);assert(bytes);assert(fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);assert(decode_cover(bytes,(size_t)n,pixels,256));free(bytes);}
 if(argc>2){FILE* f=fopen(argv[2],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n>0);unsigned char* bytes=malloc((size_t)n);assert(bytes);assert(fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);assert(decode_cover(bytes,(size_t)n,pixels,256));free(bytes);}
 puts("PASS: PNG channels, resize, corrupt and large input; optional real PNG/TGA artwork.");return 0;
}
