#include "../src/library.hpp"
#include "../src/cJSON.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace estante;
int main(){
 Library d{};d.folders[0]={true,-1,"PS2"};d.folders[1]={true,0,"CORRIDA"};d.folders[2]={true,-1,"FAVORITOS"};
 auto* m=metadata(d,"CUSA12345");assert(m);m->platform=2;m->folders=7;m->favorite=true;
 {Library empty{};
  auto plat=[&](const char*id){Game g{};std::snprintf(g.id,10,"%s",id);return platform(g,empty);};
  assert(plat("PPSA01575")==5&&plat("CUSA03507")==4&&plat("PPSA99203")==1&&plat("PPSA99841")==1);
  assert(plat("SLUS20624")==2&&plat("SLES54510")==2&&plat("ULUS10564")==2&&plat("FAKE00000")==1&&plat("PSPS69691")==1&&plat("PS2A1B2C3")==2);}
 {
  // Minimal ISO 9660 image: root directory at sector 20 holding the boot executable named after its serial.
  static unsigned char iso[40*2048]; std::memset(iso,0,sizeof iso);
  std::memcpy(iso+0x8001,"CD001",5);
  unsigned char*root=iso+0x8000+156; root[2]=20; root[10]=0x00; root[11]=0x08; // lba 20, length 2048
  unsigned char*rec=iso+20*2048; const char nm[]="SLUS_206.24;1"; rec[0]=static_cast<unsigned char>(33+sizeof nm-1); rec[32]=sizeof nm-1; std::memcpy(rec+33,nm,sizeof nm-1);
  char serial[16]; assert(estante::iso_boot_serial(iso,sizeof iso,serial,sizeof serial)&&!std::strcmp(serial,"SLUS-20624"));
  assert(!estante::iso_boot_serial(iso,100,serial,sizeof serial));
  iso[0x8001]='X'; assert(!estante::iso_boot_serial(iso,sizeof iso,serial,sizeof serial));
 }
 assert(valid_library(d));Game g{"CUSA12345","Teste"};assert(platform(g,d)==2);
 char* s=write_library(d);assert(s);Library copy{};assert(read_library(s,copy));cJSON_free(s);
 assert(copy.games[0].folders==7&&copy.games[0].favorite&&!copy.games[0].hidden);
 {Library h=copy;h.games[0].hidden=true;char* t=write_library(h);assert(t);Library back{};assert(read_library(t,back));assert(back.games[0].hidden);cJSON_free(t);}
 Library extra{};assert(read_library("{\"version\":1,\"folders\":[],\"games\":[{\"id\":\"CUSA12345\",\"platform\":2,\"favorite\":false,\"folders\":0}]}",extra)&&!extra.games[0].hidden);
 assert(!read_library("{\"version\":1,\"folders\":[],\"games\":[{\"id\":\"CUSA12345\",\"platform\":2,\"favorite\":false,\"folders\":0,\"hidden\":1}]}",extra));
 assert(delete_folder(copy,0));assert(copy.games[0].folders==4);assert(!copy.folders[1].used&&copy.folders[2].used);
 d.folders[0].parent=1;assert(!valid_library(d));
 Library unchanged=copy;assert(!read_library("{\"version\":2}",copy));assert(!std::memcmp(&copy,&unchanged,sizeof copy));
 Game out[3]{};assert(read_catalog("{\"titles\":[{\"titleId\":\"CUSA12345\",\"titleName\":\"A\"},{\"titleId\":\"CUSA12345\",\"titleName\":\"B\"},{\"titleId\":\"../bad\"}]}",out,3)==1);
 assert(read_catalog("{\"titles\":[{\"titleId\":\"CUSA12345\"},{\"titleId\":\"PPSA12345\"}]}",out,1)==-1);
 assert(!read_library("{\"version\":1,\"folders\":[],\"games\":[{\"id\":\"CUSA12345\",\"platform\":2,\"favorite\":false,\"folders\":1}]}",copy));
 puts("PASS: catalog, bounds, duplicates, platform override, roundtrip, folder deletion, cycle and corrupt data rejection.");
}