// Estante native PS5 prototype. GPL-3.0-or-later.
// VideoOut and Pad ABI based on the public boilerplate, not firmware offsets.
#include "demo_renderer.hpp"
#include "library.hpp"
#include "cJSON.h"
#include "cover_decode.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
using namespace estante;
using ps5::demo::Canvas;using ps5::demo::Color;
struct PadSample{std::uint32_t buttons;std::uint8_t analog[6];std::uint8_t reserved0[66];std::int32_t connected;std::uint64_t timestamp;std::uint8_t reserved1[32];};
static_assert(sizeof(PadSample)==120);
struct NetAddress{std::uint8_t length,family;std::uint16_t port;std::uint32_t address;std::uint16_t virtual_port;std::uint8_t zero[6];};
extern "C" {
int sceUserServiceInitialize(const void*);int sceUserServiceGetInitialUser(int*);int scePadInit();int scePadOpen(int,int,int,const void*);int scePadRead(int,PadSample*,int);
std::uint64_t sceKernelGetProcessTime();int sceKernelDebugOutText(int,const char*);
int sceNetSocket(const char*,int,int,int);int sceNetConnect(int,const void*,std::uint32_t);int sceNetSend(int,const void*,std::size_t,int);int sceNetRecv(int,void*,std::size_t,int);int sceNetSetsockopt(int,int,int,const void*,std::uint32_t);int sceNetSocketClose(int);
}
namespace {
constexpr Color bg=static_cast<Color>(0xff171310),panel=static_cast<Color>(0xff27211c),accent=static_cast<Color>(0xffb9e8a8),muted=static_cast<Color>(0xffc4d0be),white=Color::white;
constexpr std::uint32_t UP=0x10,RIGHT=0x20,DOWN=0x40,LEFT=0x80,L1=0x400,R1=0x800,TRIANGLE=0x1000,CIRCLE=0x2000,CROSS=0x4000,SQUARE=0x8000,OPTIONS=0x8,R3=0x4;
Library db{};Game games[max_games]{};int count=0;bool selected[max_games]{};
int filtered[max_games]{},filtered_count=0,cursor=0,view=0,nav_cursor=0,nav_views[40]{},nav_count=0;
bool nav_focus=false,storage_ok=false;int pad=-1;std::uint32_t held=0;std::uint64_t retry_pad=0,repeat_at=0;
char message[180]="INICIANDO";char connection[96]="CATALOGO LOCAL";char entry_name[40]{};
int modal=0,menu_cursor=0,keyboard_cursor=0,edit_slot=-1,new_parent=-1,launch_index=-1;
std::atomic<int> work{0};char request_path[160]{};char response[1024*1024]{};int work_kind=0;int pending_kind=0;char pending_path[160]{};bool response_ok=false;std::size_t response_size=0;
constexpr unsigned cover_edge=316;
struct Cover {char id[10];bool ready;std::uint64_t age;std::uint32_t pixels[cover_edge*cover_edge];};
Cover covers[16]{};std::uint64_t cover_clock=0;char requested_cover[10]{};std::uint32_t decoded_cover[cover_edge*cover_edge]{};bool cover_ok=false;
char failed_covers[max_games][10]{};int failed_cover_count=0;
bool record_mode=true;int nav_totals[40]{};std::uint64_t ui_version=1,cover_version=1;
constexpr char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
constexpr int key_count=sizeof(keys)-1;
void log(const char* text){char line[220];std::snprintf(line,sizeof line,"ESTANTE: %s\n",text);sceKernelDebugOutText(0,line);}
void msg(const char* text){std::snprintf(message,sizeof message,"%s",text);log(text);}
void display_text(Canvas& c,unsigned x,unsigned y,const char* source,unsigned scale,Color color,int limit=100){
 char text[200];int n=0;for(std::size_t i=0;source[i]&&n<limit&&n<199;i++){
  unsigned char b=static_cast<unsigned char>(source[i]);if(b<128){text[n++]=(b>='a'&&b<='z')?static_cast<char>(b-32):static_cast<char>(b);}
  else if(b==0xc3&&source[i+1]){unsigned char a=static_cast<unsigned char>(source[++i]);char ch=' ';if((a>=0x80&&a<=0x85)||(a>=0xa0&&a<=0xa5))ch='A';else if(a==0x87||a==0xa7)ch='C';else if((a>=0x88&&a<=0x8b)||(a>=0xa8&&a<=0xab))ch='E';else if((a>=0x8c&&a<=0x8f)||(a>=0xac&&a<=0xaf))ch='I';else if((a>=0x92&&a<=0x96)||(a>=0xb2&&a<=0xb6))ch='O';else if((a>=0x99&&a<=0x9c)||(a>=0xb9&&a<=0xbc))ch='U';text[n++]=ch;}
 }
 text[n]=0;c.text(x,y,text,scale,color);
}
char* read_file(const char* name){
 FILE* f=std::fopen(name,"rb");if(!f)return nullptr;
 if(std::fseek(f,0,SEEK_END)){std::fclose(f);return nullptr;}long size=std::ftell(f);
 if(size<0||size>1024*1024){std::fclose(f);return nullptr;}std::rewind(f);char* s=static_cast<char*>(std::malloc(static_cast<std::size_t>(size)+1));if(!s){std::fclose(f);return nullptr;}
 const auto got=std::fread(s,1,static_cast<std::size_t>(size),f);std::fclose(f);s[got]=0;if(got!=static_cast<std::size_t>(size)){std::free(s);return nullptr;}return s;
}
bool write_file(const char* name,const char* text){
 char tmp[120];std::snprintf(tmp,sizeof tmp,"%s.tmp",name);int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)return false;
 std::size_t n=std::strlen(text),sent=0;while(sent<n){long r=write(fd,text+sent,n-sent);if(r<=0){close(fd);return false;}sent+=static_cast<std::size_t>(r);}
 bool ok=fsync(fd)==0;ok=close(fd)==0&&ok;if(!ok)return false;return rename(tmp,name)==0;
}
bool persist(const Library& next){
 if(!storage_ok){msg("PASTAS NAO SALVAS: /DOWNLOAD0 INDISPONIVEL");return false;}
 char* text=write_library(next);if(!text){msg("ORGANIZACAO INVALIDA");return false;}
 char* previous=write_library(db);if(previous){if(!write_file("/download0/estante-backup.json",previous)){cJSON_free(previous);cJSON_free(text);msg("FALHA NO BACKUP; ALTERACAO CANCELADA");return false;}cJSON_free(previous);}
 bool ok=write_file("/download0/estante.json",text);cJSON_free(text);
 if(ok){db=next;msg("ORGANIZACAO SALVA NO PS5");}else msg("FALHA AO SALVAR; ORGANIZACAO ANTERIOR PRESERVADA");return ok;
}
bool in_view(int v,int i){
 const Meta* m=metadata(db,games[i].id);
 const bool hidden=m&&m->hidden;
 if(v==7)return hidden;
 if(hidden)return false;
 return v==0||(v==1&&platform(games[i],db)==2)||(v==2&&platform(games[i],db)==4)||(v==3&&platform(games[i],db)==5)||(v==4&&platform(games[i],db)==1)||(v==5&&m&&m->favorite)||(v==6&&(!m||m->folders==0))||(v>=100&&m&&(m->folders&(std::uint32_t(1)<<(v-100))));
}
void rebuild(){
 ui_version++;
 nav_count=0;for(int v=0;v<8;v++)nav_views[nav_count++]=v;
 // Parent-first hierarchy, bounded by fixed slot count.
 bool added[max_folders]{};for(int pass=0;pass<max_folders;pass++)for(int i=0;i<max_folders;i++)if(db.folders[i].used&&!added[i]&&(db.folders[i].parent<0||added[db.folders[i].parent])){nav_views[nav_count++]=100+i;added[i]=true;}
 if(view>=100&&!db.folders[view-100].used)view=0;
 for(int i=0;i<nav_count;i++)if(nav_views[i]==view)nav_cursor=i;
 for(int i=0;i<nav_count;i++){int total=0;for(int g=0;g<count;g++)if(in_view(nav_views[i],g))total++;nav_totals[i]=total;}
 filtered_count=0;
 for(int i=0;i<count;i++)if(in_view(view,i))filtered[filtered_count++]=i;
 if(cursor>=filtered_count)cursor=filtered_count?filtered_count-1:0;
}
const char* view_name(int v){static const char* names[]={"TODOS OS TITULOS","PLAYSTATION 2","PLAYSTATION 4","PLAYSTATION 5","OUTROS E APPS","FAVORITOS","SEM PASTA","OCULTOS"};return v<8?names[v]:db.folders[v-100].name;}
int chosen_count(){int n=0;for(int i=0;i<count;i++)if(selected[i])n++;return n;}
int target_index(){return filtered_count?filtered[cursor]:-1;}
template<class Fn> void update_targets(Fn fn){Library next=db;bool batch=chosen_count()>0;int target=target_index();for(int i=0;i<count;i++)if((batch&&selected[i])||(!batch&&i==target)){Meta* m=metadata(next,games[i].id);if(m)fn(*m);}if(persist(next))rebuild();}
bool http_get(const char* path){
 response[0]=0;response_size=0;int s=sceNetSocket("estante",2,1,6);if(s<0)return false;const int timeout=3000000;
 for(int opt:{0x1105,0x1106,0x1109})sceNetSetsockopt(s,0xffff,opt,&timeout,sizeof timeout);
 constexpr std::uint16_t port=6969;NetAddress address{sizeof(NetAddress),2,static_cast<std::uint16_t>((port<<8)|(port>>8)),0x0100007f,0,{}};
 if(sceNetConnect(s,&address,sizeof address)<0){sceNetSocketClose(s);return false;}
 char req[320];int len=std::snprintf(req,sizeof req,"GET %s HTTP/1.0\r\nHost: 127.0.0.1:6969\r\nConnection: close\r\n\r\n",path);int off=0;
 while(off<len){int n=sceNetSend(s,req+off,static_cast<std::size_t>(len-off),0);if(n<=0){sceNetSocketClose(s);return false;}off+=n;}
 std::size_t used=0;bool eof=false;while(used<sizeof(response)-1){int n=sceNetRecv(s,response+used,sizeof(response)-1-used,0);if(n==0){eof=true;break;}if(n<0)break;used+=static_cast<std::size_t>(n);}
 sceNetSocketClose(s);response[used]=0;if(!eof)return false;
 int code=0;if(std::sscanf(response,"HTTP/%*s %d",&code)!=1||code<200||code>=300)return false;
 char* body=std::strstr(response,"\r\n\r\n");if(!body)return false;
 // HTTP/1.0 requested: don't feed unsupported chunked bodies to JSON parser.
 if(std::strstr(response,"Transfer-Encoding: chunked")||std::strstr(response,"transfer-encoding: chunked"))return false;
 body+=4;response_size=used-static_cast<std::size_t>(body-response);std::memmove(response,body,response_size);response[response_size]=0;return true;
}
void* worker(void*){
 response_ok=http_get(request_path);
 if(work_kind==3)cover_ok=response_ok&&decode_cover(reinterpret_cast<unsigned char*>(response),response_size,decoded_cover,cover_edge);
 work.store(2,std::memory_order_release);return nullptr;
}
void start_request(int kind,const char* path){
 if(work.load()!=0){if(kind!=3){pending_kind=kind;std::snprintf(pending_path,sizeof pending_path,"%s",path);msg("AGUARDE A CONSULTA ATUAL");}return;}work_kind=kind;std::snprintf(request_path,sizeof request_path,"%s",path);work.store(1);
 pthread_t thread{};if(pthread_create(&thread,nullptr,worker,nullptr)!=0){work.store(0);msg("NAO FOI POSSIVEL INICIAR A CONSULTA");return;}pthread_detach(thread);
 if(kind!=3)msg(kind==1?"ATUALIZANDO TITULOS DO PS5":"SOLICITANDO LANCAMENTO AO PS5");
}
Cover* find_cover(const char* id){for(auto& cover:covers)if(cover.ready&&!std::strcmp(cover.id,id))return &cover;return nullptr;}
void request_visible_cover(){
 if(work.load()!=0||modal)return;
 int start=(cursor/8)*8;
 for(int row=0;row<8&&start+row<filtered_count;row++){
  const char* id=games[filtered[start+row]].id;
  if(find_cover(id))continue;bool failed=false;
  for(int i=0;i<failed_cover_count;i++)if(!std::strcmp(failed_covers[i],id))failed=true;
  if(failed)continue;
  std::snprintf(requested_cover,sizeof requested_cover,"%s",id);
  char path[80];std::snprintf(path,sizeof path,"/appdb/icon?id=%s",id);start_request(3,path);break;
 }
}
void poll_work(){
 if(work.load(std::memory_order_acquire)!=2)return;
 if(work_kind==1){static Game next[max_games];int n=response_ok?read_catalog(response,next,max_games):-1;if(n>=0){std::memcpy(games,next,sizeof games);count=n;std::memset(selected,0,sizeof selected);if(storage_ok)write_file("/download0/catalog.json",response);std::snprintf(connection,sizeof connection,"CATALOGO AO VIVO - ELF ARSENAL");msg("BIBLIOTECA ATUALIZADA");rebuild();}else{std::snprintf(connection,sizeof connection,"CATALOGO EM CACHE - API INDISPONIVEL");msg("FALHA NA API LOCAL; CATALOGO PRESERVADO");}}
 else if(work_kind==3){
  if(cover_ok){int slot=0;for(int i=0;i<16;i++){if(!covers[i].ready){slot=i;break;}if(covers[i].age<covers[slot].age)slot=i;}
   auto& dest=covers[slot];std::snprintf(dest.id,sizeof dest.id,"%s",requested_cover);std::memcpy(dest.pixels,decoded_cover,sizeof decoded_cover);dest.ready=true;dest.age=++cover_clock;cover_version++;
  }else if(failed_cover_count<max_games){std::snprintf(failed_covers[failed_cover_count++],10,"%s",requested_cover);cover_version++;}
 }
 else msg(response_ok?"PEDIDO ENVIADO. CONFIRA SE O JOGO ABRIU.":"FALHA AO ENVIAR O PEDIDO DE LANCAMENTO");
 work.store(0);
 if(pending_kind){int next_kind=pending_kind;pending_kind=0;start_request(next_kind,pending_path);}
}
std::uint32_t buttons(){
 auto now=sceKernelGetProcessTime();
 if(pad<0&&now>=retry_pad){retry_pad=now+3000000;int user=0;sceUserServiceInitialize(nullptr);if(sceUserServiceGetInitialUser(&user)==0&&scePadInit()>=0)pad=scePadOpen(user,0,0,nullptr);if(pad>=0)log("CONTROLE ABERTO");}
 if(pad<0)return 0;std::array<PadSample,64> samples{};int n=scePadRead(pad,samples.data(),64);if(n<0){pad=-1;held=0;return 0;}if(n>64)n=64;
 std::uint32_t pressed=0;for(int i=0;i<n;i++){pressed|=samples[i].buttons&~held;held=samples[i].buttons;}
 if(pressed&(UP|DOWN|LEFT|RIGHT))repeat_at=now+450000;
 else if((held&(UP|DOWN|LEFT|RIGHT))&&now>repeat_at){pressed|=held&(UP|DOWN|LEFT|RIGHT);repeat_at=now+130000;}
 return pressed;
}
void keyboard(int slot,int parent){edit_slot=slot;new_parent=parent;keyboard_cursor=0;std::snprintf(entry_name,sizeof entry_name,"%s",slot>=0?db.folders[slot].name:"");modal=3;}
void submit_name(){
 if(!entry_name[0])return;Library next=db;int slot=edit_slot;
 if(slot<0){for(int i=0;i<max_folders;i++)if(!next.folders[i].used){slot=i;break;}}
 if(slot<0){msg("LIMITE DE 32 PASTAS");modal=0;return;}
 next.folders[slot].used=true;next.folders[slot].parent=edit_slot>=0?next.folders[slot].parent:new_parent;
 std::snprintf(next.folders[slot].name,sizeof next.folders[slot].name,"%s",entry_name);
 if(persist(next)){modal=0;rebuild();}
}
constexpr const char* menu_items[]={"ADICIONAR A PASTA","MARCAR COMO PS2","MARCAR COMO PS4","MARCAR COMO PS5","MARCAR COMO OUTRO OU APP","ALTERNAR FAVORITO","REMOVER DESTA PASTA","CRIAR PASTA NA RAIZ","CRIAR SUBPASTA AQUI","RENOMEAR PASTA ATUAL","EXCLUIR PASTA ATUAL","SELECIONAR TODOS VISIVEIS","LIMPAR SELECAO","ATUALIZAR BIBLIOTECA","MODO GRAVACAO (LIGAR OU DESLIGAR)","OCULTAR OU MOSTRAR TITULO"};
void input(std::uint32_t b){
 if(modal==3){
  int total=key_count+2;if(b&LEFT)keyboard_cursor=(keyboard_cursor+total-1)%total;if(b&RIGHT)keyboard_cursor=(keyboard_cursor+1)%total;if(b&UP)keyboard_cursor=(keyboard_cursor+total-10)%total;if(b&DOWN)keyboard_cursor=(keyboard_cursor+10)%total;
  if(b&SQUARE){auto n=std::strlen(entry_name);if(n)entry_name[n-1]=0;}
  if(b&CIRCLE)modal=0;
  if(b&OPTIONS)submit_name();
  if(b&CROSS){if(keyboard_cursor==key_count)submit_name();else if(keyboard_cursor==key_count+1){auto n=std::strlen(entry_name);if(n)entry_name[n-1]=0;}else{auto n=std::strlen(entry_name);if(n<sizeof entry_name-1){entry_name[n]=keys[keyboard_cursor];entry_name[n+1]=0;}}}return;
 }
 if(modal==4){if(b&CIRCLE)modal=0;if(b&CROSS){Library next=db;delete_folder(next,view-100);if(persist(next)){view=0;modal=0;rebuild();}}return;}
 if(modal==5){if(b&CIRCLE)modal=0;if((b&CROSS)&&launch_index>=0){char path[100];std::snprintf(path,sizeof path,"/launch?titleId=%s",games[launch_index].id);start_request(2,path);modal=0;}return;}
 if(modal==2){
  int slots[max_folders],n=0;for(int i=0;i<max_folders;i++)if(db.folders[i].used)slots[n++]=i;
  if(b&CIRCLE)modal=0;if(!n)return;if(b&UP)menu_cursor=(menu_cursor+n-1)%n;if(b&DOWN)menu_cursor=(menu_cursor+1)%n;
  if(b&CROSS){int slot=slots[menu_cursor];update_targets([slot](Meta& m){m.folders|=std::uint32_t(1)<<slot;});modal=0;}return;
 }
 if(modal==1){
  constexpr int n=sizeof(menu_items)/sizeof(menu_items[0]);if(b&CIRCLE)modal=0;if(b&UP)menu_cursor=(menu_cursor+n-1)%n;if(b&DOWN)menu_cursor=(menu_cursor+1)%n;
  if(b&CROSS){int action=menu_cursor;modal=0;
   if(action==0){modal=2;menu_cursor=0;}
   else if(action>=1&&action<=4){int p=action==1?2:action==2?4:action==3?5:1;update_targets([p](Meta& m){m.platform=p;});}
   else if(action==5)update_targets([](Meta& m){m.favorite=!m.favorite;});
   else if(action==6){if(view>=100)update_targets([](Meta& m){m.folders&=~(std::uint32_t(1)<<(view-100));});else msg("ABRA UMA PASTA PARA REMOVER VINCULOS");}
   else if(action==7)keyboard(-1,-1);
   else if(action==8){if(view>=100)keyboard(-1,view-100);else msg("ABRA UMA PASTA PARA CRIAR SUBPASTA");}
   else if(action==9){if(view>=100)keyboard(view-100,-1);else msg("SELECIONE UMA PASTA");}
   else if(action==10){if(view>=100)modal=4;else msg("SELECIONE UMA PASTA");}
   else if(action==11){for(int i=0;i<filtered_count;i++)selected[filtered[i]]=true;}
   else if(action==12)std::memset(selected,0,sizeof selected);
   else if(action==13){failed_cover_count=0;start_request(1,"/appdb");}
   else if(action==14){record_mode=!record_mode;ui_version++;}
   else if(action==15)update_targets([](Meta& m){m.hidden=!m.hidden;});
  }return;
 }
 if(b&R3){record_mode=!record_mode;ui_version++;return;}
 if(b&(TRIANGLE|OPTIONS)){modal=1;menu_cursor=0;return;}
 if(nav_focus&&(b&RIGHT))nav_focus=false;
 else if(!nav_focus&&(b&LEFT)){if(cursor%4==0)nav_focus=true;else cursor--;}
 else if(!nav_focus&&(b&RIGHT)&&cursor+1<filtered_count&&cursor%4<3)cursor++;
 if(nav_focus){if(b&UP)nav_cursor=(nav_cursor+nav_count-1)%nav_count;if(b&DOWN)nav_cursor=(nav_cursor+1)%nav_count;if(b&CROSS){view=nav_views[nav_cursor];cursor=0;std::memset(selected,0,sizeof selected);rebuild();nav_focus=false;}}
 else {
  if(filtered_count){if(b&UP)cursor=cursor>=4?cursor-4:cursor;if(b&DOWN)cursor=cursor+4<filtered_count?cursor+4:cursor;if(b&L1)cursor=cursor>=8?cursor-8:0;if(b&R1)cursor=cursor+8<filtered_count?cursor+8:filtered_count-1;
   if(b&SQUARE)selected[filtered[cursor]]=!selected[filtered[cursor]];
   if(b&CROSS){launch_index=filtered[cursor];modal=5;}
  }if(b&CIRCLE)nav_focus=true;
 }
}
Color mix(Color a,Color b,int weight_a){
 const auto ca=static_cast<std::uint32_t>(a),cb=static_cast<std::uint32_t>(b);std::uint32_t out=0xff000000u;
 for(int shift=0;shift<24;shift+=8){const int va=(ca>>shift)&255,vb=(cb>>shift)&255;out|=static_cast<std::uint32_t>((va*weight_a+vb*(256-weight_a))>>8)<<shift;}
 return static_cast<Color>(out);
}
Color average_color(const Cover* cover){
 if(!cover)return bg;std::uint32_t r=0,g=0,b=0,n=0;
 for(unsigned y=0;y<cover_edge;y+=9)for(unsigned x=0;x<cover_edge;x+=9){const std::uint32_t p=cover->pixels[y*cover_edge+x];r+=p&255;g+=(p>>8)&255;b+=(p>>16)&255;n++;}
 if(!n)return bg;return static_cast<Color>(0xff000000u|((b/n)<<16)|((g/n)<<8)|(r/n));
}
Color platform_color(int p){return p==2?static_cast<Color>(0xffffa096):p==4?static_cast<Color>(0xffffb96e):p==5?white:muted;}
std::uint64_t state_signature(){
 std::uint64_t h=1469598103934665603ull;
 auto add=[&h](std::uint64_t v){h^=v;h*=1099511628211ull;};
 add(ui_version);add(cover_version);add(static_cast<std::uint64_t>(cursor));add(static_cast<std::uint64_t>(view));add(nav_focus);add(static_cast<std::uint64_t>(nav_cursor));
 add(static_cast<std::uint64_t>(modal));add(static_cast<std::uint64_t>(menu_cursor));add(static_cast<std::uint64_t>(keyboard_cursor));add(static_cast<std::uint64_t>(edit_slot+1));add(static_cast<std::uint64_t>(launch_index+1));
 add(pad<0);add(storage_ok);add(record_mode);add(static_cast<std::uint64_t>(filtered_count));add(static_cast<std::uint64_t>(count));
 for(int i=0;i<count;i++)add(selected[i]);
 for(const char* t=message;*t;t++)add(static_cast<unsigned char>(*t));
 for(const char* t=connection;*t;t++)add(static_cast<unsigned char>(*t));
 for(const char* t=entry_name;*t;t++)add(static_cast<unsigned char>(*t));
 return h;
}
void draw(Canvas& c) noexcept {
 poll_work();input(buttons());request_visible_cover();
 // The CPU renderer is slow, so redraw only while the state changes (twice: both buffers).
 static std::uint64_t last_signature=0;static int stable_frames=0;
 {const std::uint64_t signature=state_signature();if(signature==last_signature){if(stable_frames>=2)return;stable_frames++;}else{last_signature=signature;stable_frames=0;}}
 const bool rec=record_mode;
 const Cover* focus_cover=(filtered_count&&!nav_focus)?find_cover(games[filtered[cursor]].id):nullptr;
 const Color tint=average_color(focus_cover),header_bg=mix(tint,bg,52),glow=mix(tint,bg,170),edge_line=mix(tint,bg,110);
 c.clear(bg);
 c.rectangle(370,0,1550,150,header_bg);c.rectangle(370,150,1550,3,edge_line);
 c.rectangle(0,0,370,1080,panel);
 display_text(c,52,55,"ESTANTE",6,accent);display_text(c,54,119,"SUA COLECAO. SEU JEITO.",2,muted);
 display_text(c,54,188,"BIBLIOTECA",2,muted);
 int navstart=nav_cursor>=13?nav_cursor-12:0;
 for(int i=navstart;i<nav_count&&i<navstart+14;i++){
  int y=240+(i-navstart)*46,v=nav_views[i];bool focus=nav_focus&&i==nav_cursor;
  if(focus||v==view)c.rounded_rectangle(30,y-11,310,40,9,focus?accent:static_cast<Color>(0xff42382d));
  int depth=0;if(v>=100){int p=db.folders[v-100].parent;while(p>=0&&depth<8){depth++;p=db.folders[p].parent;}}
  display_text(c,50+static_cast<unsigned>(depth*10),y,view_name(v),2,focus?bg:white,16-depth);
  char total[12];std::snprintf(total,sizeof total,"%d",nav_totals[i]);const unsigned width=static_cast<unsigned>(std::strlen(total))*12;
  display_text(c,322-width,y,total,2,focus?bg:muted);
 }
 display_text(c,54,1014,"@KNDOFC",3,accent);
 if(!rec){
  display_text(c,54,925,storage_ok?"ORGANIZACAO SALVA":"SALVAMENTO INDISPONIVEL",2,storage_ok?accent:Color::yellow,25);
  display_text(c,54,961,"TRIANGULO: ORGANIZAR",2,muted);
 }
 display_text(c,415,48,view_name(view),4,white,57);
 char summary[120];if(rec||chosen_count()==0)std::snprintf(summary,sizeof summary,"%d JOGOS",filtered_count);else std::snprintf(summary,sizeof summary,"%d JOGOS   /   %d SELECIONADOS",filtered_count,chosen_count());
 display_text(c,418,108,summary,2,muted);
 const bool show_badge=!(view>=1&&view<=4);
 int start=(cursor/8)*8;
 for(int row=0;row<8&&start+row<filtered_count;row++){
  int pos=start+row,idx=filtered[pos];const auto& g=games[idx];bool focus=!nav_focus&&pos==cursor;
  unsigned x=416+(row%4)*360,y=176+(row/4)*376;
  if(focus){c.rounded_rectangle(x-10,y-10,352,380,19,glow);c.rounded_rectangle(x-5,y-5,342,370,15,accent);}
  c.rounded_rectangle(x,y,332,360,12,panel);
  auto* cover=find_cover(g.id);
  if(cover){cover->age=++cover_clock;c.image(x+8,y+8,cover_edge,cover->pixels);}
  else {c.rounded_rectangle(x+8,y+8,316,316,8,static_cast<Color>(0xff39312a));
   c.rectangle(x+124,y+98,84,7,muted);c.rectangle(x+124,y+130,65,7,muted);c.rectangle(x+124,y+162,84,7,muted);
   display_text(c,x+92,y+230,g.id,2,muted,9);}
  display_text(c,x+14,y+334,g.name,2,focus?accent:white,25);
  const Meta* m=metadata(db,g.id);int p=platform(g,db);
  if(show_badge){c.rounded_rectangle(x+16,y+16,68,30,6,bg);display_text(c,x+28,y+24,p==2?"PS2":p==4?"PS4":p==5?"PS5":"APP",2,platform_color(p));}
  if(selected[idx]){c.rounded_rectangle(x+278,y+16,38,34,7,accent);display_text(c,x+290,y+26,"X",2,bg);}
  else if(m&&m->favorite){c.rounded_rectangle(x+250,y+16,66,30,6,bg);display_text(c,x+263,y+24,"FAV",2,accent);}
 }
 if(!filtered_count){display_text(c,480,330,"SUA PROXIMA COLECAO COMECA AQUI",4,white,48);display_text(c,480,400,"TRIANGULO PARA CRIAR PASTAS OU ATUALIZAR A BIBLIOTECA",2,muted,70);}
 if(rec){
  if(filtered_count)display_text(c,418,945,games[filtered[cursor]].name,3,white,76);
 }else{
  display_text(c,418,930,message,2,accent,110);
  c.rectangle(400,968,1465,2,static_cast<Color>(0xff39312a));
  display_text(c,418,993,"X JOGAR   QUADRADO SELECIONAR   TRIANGULO ORGANIZAR   L1 R1 PAGINAS",2,white,105);
  char page[50];std::snprintf(page,sizeof page,"PAGINA %d / %d",filtered_count?cursor/8+1:0,(filtered_count+7)/8);
  display_text(c,418,1030,page,2,muted);display_text(c,1130,1030,"CIRCULO COLECOES   R3 MODO GRAVACAO   PS SAIR",2,muted);
  if(pad<0)display_text(c,418,950,"AGUARDANDO CONTROLE",2,Color::yellow);
 }
 if(modal){
  c.rectangle(425,170,1430,750,bg);c.rectangle(440,185,1400,720,panel);
  if(modal==1){display_text(c,490,210,"ORGANIZAR TITULOS",4,accent);for(int i=0;i<16;i++){int y=258+i*40;if(i==menu_cursor)c.rectangle(480,y-7,1320,35,accent);display_text(c,500,y,menu_items[i],3,i==menu_cursor?bg:white,68);}}
  else if(modal==2){display_text(c,490,210,"ADICIONAR A PASTA",4,accent);int n=0,total=0;for(auto& f:db.folders)if(f.used)total++;int page=(menu_cursor/12)*12;for(int i=0;i<max_folders;i++)if(db.folders[i].used){if(n>=page&&n<page+12){int y=285+(n-page)*45;if(n==menu_cursor)c.rectangle(480,y-7,1320,38,accent);display_text(c,500,y,db.folders[i].name,3,n==menu_cursor?bg:white);}n++;}if(!total)display_text(c,490,320,"SEM PASTAS. VOLTE E CRIE UMA PASTA.",3,white);}
  else if(modal==3){display_text(c,490,215,edit_slot<0?"NOVA PASTA":"RENOMEAR PASTA",4,accent);display_text(c,490,285,entry_name,4,white,39);for(int i=0;i<key_count+2;i++){int x=490+(i%10)*125,y=395+(i/10)*85;bool focus=i==keyboard_cursor;c.rectangle(x-8,y-12,115,65,focus?accent:bg);char key[2]={i<key_count?keys[i]:' ',0};display_text(c,x,y,i==key_count?"OK":i==key_count+1?"APAGAR":key,2,focus?bg:white);}display_text(c,490,840,"OPTIONS SALVAR   QUADRADO APAGAR   CIRCULO CANCELAR",2,muted);}
  else if(modal==4){display_text(c,490,260,"EXCLUIR PASTA E SUBPASTAS",4,accent);display_text(c,490,370,"OS JOGOS CONTINUAM INSTALADOS.",3,white);display_text(c,490,430,"X CONFIRMAR   CIRCULO CANCELAR",3,white);}
  else if(modal==5&&launch_index>=0){display_text(c,490,260,"INICIAR NO PS5",4,accent);display_text(c,490,350,games[launch_index].name,3,white,66);display_text(c,490,420,games[launch_index].id,3,muted);display_text(c,490,510,"X CONFIRMAR   CIRCULO CANCELAR",3,white);display_text(c,490,590,"O JOGO DEPENDE DOS PAYLOADS DA SESSAO ATUAL.",2,muted);}
 }
}
}
int main(){
 log("BOOT PPSA99841");
 struct stat st{};storage_ok=stat("/download0",&st)==0&&write_file("/download0/estante-probe.txt","ESTANTE 1");
 char* saved=read_file("/download0/estante.json");if(saved){if(!read_library(saved,db)){storage_ok=false;msg("ORGANIZACAO INVALIDA; GRAVACAO BLOQUEADA");}std::free(saved);}
 char* cached=read_file("/download0/catalog.json");if(cached){count=read_catalog(cached,games,max_games);std::free(cached);}
 if(count<=0){char* bundled=read_file("/app0/assets/catalog.json");if(bundled){count=read_catalog(bundled,games,max_games);std::free(bundled);}}
 if(count<0)count=0;rebuild();start_request(1,"/appdb");
 ps5::demo::run_frames(draw,"Estante: biblioteca nativa iniciada");
}