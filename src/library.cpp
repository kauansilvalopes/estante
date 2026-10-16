#include "library.hpp"
#include "cJSON.h"
#include <cstring>
#include <cstdio>
namespace estante {
bool valid_id(const char* id){if(!id||std::strlen(id)!=9)return false;for(int i=0;i<9;i++)if(!((id[i]>='A'&&id[i]<='Z')||(id[i]>='0'&&id[i]<='9')))return false;return true;}
static const cJSON* field(const cJSON* o,const char* k){return cJSON_GetObjectItemCaseSensitive(o,k);}
int read_catalog(const char* text,Game* out,int cap){
 cJSON* root=cJSON_Parse(text);if(!root)return -1;const cJSON* list=field(root,"titles");
 if(!cJSON_IsArray(list)){cJSON_Delete(root);return -1;}
 int n=0;const cJSON* t=nullptr;cJSON_ArrayForEach(t,list){
  const cJSON* id=field(t,"titleId"),*name=field(t,"titleName");
  if(!cJSON_IsString(id)||!valid_id(id->valuestring))continue;
  bool duplicate=false;for(int i=0;i<n;i++)if(!std::strcmp(out[i].id,id->valuestring))duplicate=true;
  if(duplicate)continue;if(n>=cap){cJSON_Delete(root);return -1;}
  std::snprintf(out[n].id,sizeof out[n].id,"%s",id->valuestring);
  std::snprintf(out[n].name,sizeof out[n].name,"%s",cJSON_IsString(name)?name->valuestring:id->valuestring);n++;
 }
 cJSON_Delete(root);return n;
}
Meta* metadata(Library& d,const char* id){for(int i=0;i<d.count;i++)if(!std::strcmp(d.games[i].id,id))return &d.games[i];if(d.count>=max_games||!valid_id(id))return nullptr;Meta* m=&d.games[d.count++];*m={};std::snprintf(m->id,sizeof m->id,"%s",id);return m;}
const Meta* metadata(const Library& d,const char* id){for(int i=0;i<d.count;i++)if(!std::strcmp(d.games[i].id,id))return &d.games[i];return nullptr;}
int platform(const Game& g,const Library& d){const Meta* m=metadata(d,g.id);if(m&&m->platform)return m->platform;return !std::strncmp(g.id,"PPSA",4)?5:!std::strncmp(g.id,"CUSA",4)?4:1;}
bool valid_library(const Library& d){
 if(d.count<0||d.count>max_games)return false;std::uint32_t used=0;
 for(int i=0;i<max_folders;i++)if(d.folders[i].used){
  used|=std::uint32_t(1)<<i;if(!d.folders[i].name[0]||!std::memchr(d.folders[i].name,0,sizeof d.folders[i].name))return false;
  std::uint32_t seen=std::uint32_t(1)<<i;int p=d.folders[i].parent;
  while(p!=-1){if(p<0||p>=max_folders||!d.folders[p].used||(seen&(std::uint32_t(1)<<p)))return false;seen|=std::uint32_t(1)<<p;p=d.folders[p].parent;}
 }
 for(int i=0;i<d.count;i++){const Meta& m=d.games[i];if(!std::memchr(m.id,0,sizeof m.id)||!valid_id(m.id)||(m.platform!=0&&m.platform!=1&&m.platform!=2&&m.platform!=4&&m.platform!=5)||(m.folders&~used))return false;for(int j=0;j<i;j++)if(!std::strcmp(m.id,d.games[j].id))return false;}
 return true;
}
bool read_library(const char* text,Library& out){
 cJSON* root=cJSON_Parse(text);if(!root)return false;Library temp{};bool ok=true;
 const cJSON* ver=field(root,"version"),*fs=field(root,"folders"),*gs=field(root,"games");
 if(!cJSON_IsNumber(ver)||ver->valuedouble!=1||!cJSON_IsArray(fs)||!cJSON_IsArray(gs))ok=false;
 const cJSON* f=nullptr;if(ok)cJSON_ArrayForEach(f,fs){
  const cJSON *idx=field(f,"slot"),*p=field(f,"parent"),*name=field(f,"name");
  if(!cJSON_IsNumber(idx)||idx->valuedouble!=idx->valueint||idx->valueint<0||idx->valueint>=max_folders||!cJSON_IsNumber(p)||p->valuedouble!=p->valueint||!cJSON_IsString(name)||std::strlen(name->valuestring)>=40){ok=false;break;}
  Folder& dest=temp.folders[idx->valueint];if(dest.used){ok=false;break;}dest.used=true;dest.parent=p->valueint;std::snprintf(dest.name,sizeof dest.name,"%s",name->valuestring);
 }
 const cJSON* g=nullptr;if(ok)cJSON_ArrayForEach(g,gs){
  const cJSON *id=field(g,"id"),*p=field(g,"platform"),*mask=field(g,"folders"),*fav=field(g,"favorite");
  if(temp.count>=max_games||!cJSON_IsString(id)||!valid_id(id->valuestring)||!cJSON_IsNumber(p)||p->valuedouble!=p->valueint||!cJSON_IsNumber(mask)||mask->valuedouble<0||mask->valuedouble>4294967295.0||!cJSON_IsBool(fav)){ok=false;break;}
  Meta& dest=temp.games[temp.count++];std::snprintf(dest.id,sizeof dest.id,"%s",id->valuestring);dest.platform=p->valueint;dest.folders=static_cast<std::uint32_t>(mask->valuedouble);if(mask->valuedouble!=dest.folders){ok=false;break;}dest.favorite=cJSON_IsTrue(fav);
 }
 ok=ok&&valid_library(temp);if(ok)out=temp;cJSON_Delete(root);return ok;
}
char* write_library(const Library& d){
 if(!valid_library(d))return nullptr;cJSON* root=cJSON_CreateObject();if(!root)return nullptr;cJSON_AddNumberToObject(root,"version",1);
 cJSON* fs=cJSON_AddArrayToObject(root,"folders"),*gs=cJSON_AddArrayToObject(root,"games");
 for(int i=0;i<max_folders;i++)if(d.folders[i].used){cJSON* f=cJSON_CreateObject();cJSON_AddNumberToObject(f,"slot",i);cJSON_AddNumberToObject(f,"parent",d.folders[i].parent);cJSON_AddStringToObject(f,"name",d.folders[i].name);cJSON_AddItemToArray(fs,f);}
 for(int i=0;i<d.count;i++){const Meta& m=d.games[i];cJSON* g=cJSON_CreateObject();cJSON_AddStringToObject(g,"id",m.id);cJSON_AddNumberToObject(g,"platform",m.platform);cJSON_AddNumberToObject(g,"folders",m.folders);cJSON_AddBoolToObject(g,"favorite",m.favorite);cJSON_AddItemToArray(gs,g);}
 char* text=cJSON_PrintUnformatted(root);cJSON_Delete(root);return text;
}
bool delete_folder(Library& d,int slot){if(slot<0||slot>=max_folders||!d.folders[slot].used)return false;std::uint32_t removed=std::uint32_t(1)<<slot;bool again=true;while(again){again=false;for(int i=0;i<max_folders;i++)if(d.folders[i].used&&d.folders[i].parent>=0&&(removed&(std::uint32_t(1)<<d.folders[i].parent))&&!(removed&(std::uint32_t(1)<<i))){removed|=std::uint32_t(1)<<i;again=true;}}for(int i=0;i<max_folders;i++)if(removed&(std::uint32_t(1)<<i))d.folders[i]={};for(int i=0;i<d.count;i++)d.games[i].folders&=~removed;return true;}
}