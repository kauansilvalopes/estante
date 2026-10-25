#pragma once
#include "library.hpp"
namespace estante
{
struct Query
{
    int view = 0;
    int platform = 0;
    int status = 0;
    bool favorites = false;
    bool descendants = false;
    bool withApps = false; // Todos os jogos hides apps unless asked (no screen asks today)
    int sort = 0;
    char text[80]{};
};
bool name_matches(const char *, const char *);
bool collection_contains(const Library &, const Game &, int slot, bool descendants = false);
int select_games(const Library &, const Game *, int count, const Query &, int *indices);
int recent_games(const Library &, const Game *, int count, int *indices, int capacity);
// Rows of the catalogue-style Inicio. Every row holds games only (apps never appear) and hidden
// games are left out. "Continue" lists titles the player started from Estante, newest first.
enum class Rail
{
    Continue,
    Favorites,
    Want,
    Folder, // a collection; `folder` is its slot
    PS5,
    PS4,
    PS2,
    PS1,
    Wii,
    WiiU,
    N3DS,
    NDS,
    Switch
};
int rail_games(const Library &, const Game *, int count, Rail kind, int folder, int *indices,
               int capacity);
} // namespace estante
