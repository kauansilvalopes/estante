// Render the real native draw callback on a host; no console launch is simulated.
#include <cassert>
#define main estante_console_entry
#include "../src/main.cpp"
#undef main
extern "C" {
int sceUserServiceInitialize(const void*){return -1;}int sceUserServiceGetInitialUser(int*){return -1;}int scePadInit(){return -1;}int scePadOpen(int,int,int,const void*){return -1;}int scePadRead(int,PadSample*,int){return 0;}
std::uint64_t sceKernelGetProcessTime(){return 0;}int sceKernelDebugOutText(int,const char*){return 0;}
int sceNetSocket(const char*,int,int,int){return -1;}int sceNetConnect(int,const void*,std::uint32_t){return -1;}int sceNetSend(int,const void*,std::size_t,int){return -1;}int sceNetRecv(int,void*,std::size_t,int){return -1;}int sceNetSetsockopt(int,int,int,const void*,std::uint32_t){return -1;}int sceNetSocketClose(int){return 0;}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 static std::uint32_t frame[0x1000000/4];
 const char* names[]={"HORIZON EXPLORER","GRAND CIRCUIT","THE LAST JOURNEY","STELLAR ADVENTURE","SHADOW OF THE VALLEY","RACING LEGENDS","MOUNTAIN QUEST","LITTLE PLANET"};
 count=8;for(int i=0;i<count;i++){
  std::snprintf(games[i].id,10,"PPSA%05d",90000+i);std::snprintf(games[i].name,192,"%s",names[i]);
  auto& art=covers[i];std::snprintf(art.id,10,"%s",games[i].id);art.ready=true;
  for(unsigned y=0;y<cover_edge;y++)for(unsigned x=0;x<cover_edge;x++)art.pixels[y*cover_edge+x]=0xff000000u|((40+i*19)<<16)|((55+y/3)<<8)|(35+x/4+i*9);
 }
 db.folders[0]={true,-1,"PARA JOGAR"};db.folders[1]={true,-1,"CLASSICOS PS2"};
 selected[2]=true;storage_ok=true;pad=0;work.store(1);rebuild();
 input(RIGHT);assert(cursor==1&&!nav_focus);input(DOWN);assert(cursor==5);input(UP);assert(cursor==1);input(LEFT);assert(cursor==0);input(LEFT);assert(nav_focus);input(RIGHT);assert(!nav_focus);
 start_request(2,"/launch?titleId=PPSA90000");assert(pending_kind==2);pending_kind=0;

 std::snprintf(message,sizeof message,"SUA BIBLIOTECA, ORGANIZADA DO SEU JEITO");
 auto canvas=Canvas::host_preview(frame);draw(canvas);
 FILE* f=std::fopen(argv[1],"wb");if(!f)return 3;std::fprintf(f,"P6\n1920 1080\n255\n");
 for(unsigned y=0;y<1080;y++)for(unsigned x=0;x<1920;x++){
 unsigned offset=((y<<4)&0x70U)^((y<<5)&0xf00U)^((y<<9)&0x1000U)^((y<<8)&0x4000U)^((x<<2)&0xcU)^((x<<5)&0x380U)^((x<<4)&0x400U)^((x<<6)&0x800U)^((x<<9)&0xa000U);
 auto pixel=frame[(((y>>7)*15+(x>>7))*65536+offset)/4];unsigned char rgb[3]={static_cast<unsigned char>(pixel),static_cast<unsigned char>(pixel>>8),static_cast<unsigned char>(pixel>>16)};std::fwrite(rgb,1,3,f);}return std::fclose(f);
}
