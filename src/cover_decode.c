// Decode bounded local game artwork. stb_image is MIT / public domain.
#include <stdlib.h>
#define STBI_ASSERT(x) do { if(!(x)) abort(); } while(0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 2048
#include "../third_party/stb/stb_image.h"
#include "cover_decode.h"
int decode_cover(const unsigned char* bytes,size_t length,uint32_t* pixels,unsigned edge) {
 int w=0,h=0,channels=0;
 if(!bytes||!pixels||length==0||length>1024*1024||edge==0||edge>512)return 0;
 if(!stbi_info_from_memory(bytes,(int)length,&w,&h,&channels)||w<=0||h<=0||w>2048||h>2048)return 0;
 unsigned char* rgba=stbi_load_from_memory(bytes,(int)length,&w,&h,&channels,4);
 if(!rgba)return 0;
 // Preserve the complete image with letterboxing instead of cropping artwork.
 unsigned dw=edge,dh=edge;
 if(w>h)dh=(unsigned)((uint64_t)edge*h/w);else dw=(unsigned)((uint64_t)edge*w/h);
 if(!dw)dw=1;if(!dh)dh=1;
 for(unsigned i=0;i<edge*edge;i++)pixels[i]=0xff201a16;
 unsigned ox=(edge-dw)/2,oy=(edge-dh)/2;
 for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
  unsigned sx=(unsigned)((uint64_t)x*w/dw),sy=(unsigned)((uint64_t)y*h/dh);
  const unsigned char* p=rgba+((size_t)sy*w+sx)*4;
  unsigned a=p[3],inv=255-a;
  unsigned red=(p[0]*a+22*inv)/255,green=(p[1]*a+26*inv)/255,blue=(p[2]*a+32*inv)/255;
  pixels[(y+oy)*edge+x+ox]=0xff000000u|(blue<<16)|(green<<8)|red;
 }
 stbi_image_free(rgba);return 1;
}
// Fills dw x dh pixels with the picture scaled to cover the whole rectangle (centre crop, box filtered). Used for
// the portrait cases of the GPU cover flow.
int decode_cover_rect(const unsigned char* bytes,size_t length,uint32_t* pixels,unsigned dw,unsigned dh) {
 int w=0,h=0,channels=0;
 if(!bytes||!pixels||length==0||length>1024*1024||dw==0||dh==0||dw>1024||dh>1024)return 0;
 if(!stbi_info_from_memory(bytes,(int)length,&w,&h,&channels)||w<=0||h<=0||w>2048||h>2048)return 0;
 unsigned char* rgba=stbi_load_from_memory(bytes,(int)length,&w,&h,&channels,4);
 if(!rgba)return 0;
 // Source window with the destination's aspect ratio, centred.
 double sw=w,sh=h;
 if((double)w*dh>(double)h*dw)sw=(double)h*dw/dh;else sh=(double)w*dh/dw;
 double ox=((double)w-sw)/2.0,oy=((double)h-sh)/2.0;
 for(unsigned y=0;y<dh;y++){
  unsigned y0=(unsigned)(oy+sh*y/dh),y1=(unsigned)(oy+sh*(y+1)/dh);if(y1<=y0)y1=y0+1;if(y1>(unsigned)h)y1=(unsigned)h;
  for(unsigned x=0;x<dw;x++){
   unsigned x0=(unsigned)(ox+sw*x/dw),x1=(unsigned)(ox+sw*(x+1)/dw);if(x1<=x0)x1=x0+1;if(x1>(unsigned)w)x1=(unsigned)w;
   unsigned r=0,g=0,b=0,n=0;
   for(unsigned sy=y0;sy<y1;sy++)for(unsigned sx=x0;sx<x1;sx++){
    const unsigned char* p=rgba+((size_t)sy*w+sx)*4;
    unsigned a=p[3],inv=255-a;
    r+=(p[0]*a+22*inv)/255;g+=(p[1]*a+26*inv)/255;b+=(p[2]*a+32*inv)/255;n++;
   }
   if(!n)n=1;
   pixels[y*dw+x]=0xff000000u|((b/n)<<16)|((g/n)<<8)|(r/n);
  }
 }
 stbi_image_free(rgba);return 1;
}
