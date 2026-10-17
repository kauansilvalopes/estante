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
    int sort = 0;
    char text[80]{};
};
bool name_matches(const char *, const char *);
bool collection_contains(const Library &, const Game &, int slot, bool descendants = false);
int select_games(const Library &, const Game *, int count, const Query &, int *indices);
int recent_games(const Library &, const Game *, int count, int *indices, int capacity);
} // namespace estante
