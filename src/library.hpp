// Estante native prototype. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
namespace estante {
constexpr int max_games=1024,max_folders=32;
struct Game { char id[10]; char name[192]; };
struct Meta { char id[10]; int platform; bool favorite; std::uint32_t folders; };
struct Folder { bool used; int parent; char name[40]; };
struct Library { Folder folders[max_folders]; Meta games[max_games]; int count; };
bool valid_id(const char*);
int read_catalog(const char*,Game*,int);
bool read_library(const char*,Library&);
char* write_library(const Library&);
Meta* metadata(Library&,const char*);
const Meta* metadata(const Library&,const char*);
int platform(const Game&,const Library&);
bool delete_folder(Library&,int);
bool valid_library(const Library&);
}