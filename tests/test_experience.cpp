#include "../src/experience.hpp"
#include "../src/cJSON.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace estante;
int main()
{
    Library d{};
    Game games[] = {{"CUSA12345", "Árvore"}, {"PPSA12345", "Corrida"}, {"PPSA54321", "Aventura"}};
    d.folders[0] = {true, -1, "Raiz"};
    d.folders[1] = {true, 0, "Filha"};
    d.folders[2] = {true, -1, "Automática", false, 0, true, 5, 1, true};
    auto *a = metadata(d, games[0].id);
    a->folders = 2;
    a->platform = 2;
    a->lastChosen = 100;
    auto *b = metadata(d, games[1].id);
    b->favorite = true;
    b->status = 1;
    b->lastChosen = 200;
    b->pinned = true;
    auto *c = metadata(d, games[2].id);
    c->hidden = true;
    c->lastChosen = 300;
    assert(name_matches("AÇÃO e coração", "acao"));
    assert(name_matches("Árvore", "ARVORE"));
    assert(!name_matches("Árvore", "corrida"));
    assert(!collection_contains(d, games[0], 0));
    assert(collection_contains(d, games[0], 0, true));
    assert(collection_contains(d, games[1], 2));
    assert(!collection_contains(d, games[0], 2));
    int out[10]{};
    Query q;
    assert(select_games(d, games, 3, q, out) == 2);
    assert(out[0] == 0);
    q.view = 7;
    assert(select_games(d, games, 3, q, out) == 1 && out[0] == 2);
    q = {};
    q.favorites = true;
    q.status = 1;
    q.platform = 5;
    assert(select_games(d, games, 3, q, out) == 1 && out[0] == 1);
    assert(recent_games(d, games, 3, out, 10) == 2 && out[0] == 1 && out[1] == 0);
    char *json = write_library(d);
    assert(json);
    Library back{};
    assert(read_library(json, back));
    cJSON_free(json);
    assert(back.folders[2].smart && back.folders[2].rulePlatform == 5 && back.games[1].pinned &&
           back.games[1].lastChosen == 200);
    const char *v1 =
        "{\"version\":1,\"folders\":[{\"slot\":0,\"parent\":-1,\"name\":\"Antiga\"}],\"games\":[{"
        "\"id\":\"CUSA12345\",\"platform\":2,\"favorite\":true,\"hidden\":true,\"folders\":1}]}";
    assert(read_library(v1, back));
    assert(back.games[0].hidden && back.games[0].favorite && back.games[0].folders == 1 &&
           !back.games[0].pinned && !back.games[0].lastChosen);
    Library unchanged = back;
    assert(!read_library("{\"version\":9,\"folders\":[],\"games\":[]}", back));
    assert(!std::memcmp(&unchanged, &back, sizeof back));
    json = write_library(d);
    cJSON *root = cJSON_Parse(json);
    cJSON_free(json);
    cJSON *entry = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "games"), 0);
    cJSON_ReplaceItemInObject(entry, "lastChosen", cJSON_CreateNumber(-1));
    json = cJSON_PrintUnformatted(root);
    assert(!read_library(json, back));
    cJSON_free(json);
    cJSON_Delete(root);
    {
        // Rows of the catalogue home: games only, hidden ones left out, "Continue" newest first.
        Library r{};
        Game g[] = {{"PPSA10001", "Alfa"}, {"PPSA10002", "Beta"}, {"CUSA10003", "Gama"},
                    {"PS2000004", "Delta"}, {"PPSA99203", "PS5SX2"}, {"PPSA10005", "Oculto"}};
        r.folders[3] = {true, -1, "Fixados", true};
        metadata(r, g[0].id)->lastChosen = 50;
        metadata(r, g[1].id)->lastChosen = 90;
        metadata(r, g[3].id)->lastChosen = 70; // a PS2 image the player started
        metadata(r, g[4].id)->lastChosen = 99; // an app is never "Continue playing"
        metadata(r, g[5].id)->lastChosen = 95;
        metadata(r, g[5].id)->hidden = true;
        metadata(r, g[2].id)->favorite = true;
        metadata(r, g[0].id)->status = 1;
        metadata(r, g[1].id)->folders = 1u << 3;
        int rows[8]{};
        assert(rail_games(r, g, 6, Rail::Continue, -1, rows, 8) == 3);
        assert(rows[0] == 1 && rows[1] == 3 && rows[2] == 0);
        assert(rail_games(r, g, 6, Rail::Continue, -1, rows, 2) == 2); // capacity is respected
        assert(rail_games(r, g, 6, Rail::Favorites, -1, rows, 8) == 1 && rows[0] == 2);
        assert(rail_games(r, g, 6, Rail::Want, -1, rows, 8) == 1 && rows[0] == 0);
        assert(rail_games(r, g, 6, Rail::PS5, -1, rows, 8) == 2); // the app and the hidden game stay out
        assert(rail_games(r, g, 6, Rail::PS4, -1, rows, 8) == 1 && rows[0] == 2);
        assert(rail_games(r, g, 6, Rail::PS2, -1, rows, 8) == 1 && rows[0] == 3);
        assert(rail_games(r, g, 6, Rail::Folder, 3, rows, 8) == 1 && rows[0] == 1);
        assert(rail_games(r, g, 6, Rail::Folder, 4, rows, 8) == 0); // unused slot
        assert(rail_games(r, g, 6, Rail::Continue, -1, rows, 0) == 0);
        Library none{};
        assert(rail_games(none, g, 6, Rail::Continue, -1, rows, 8) == 0);
    }
    {
        // Games imported from emulator libraries have made-up ids ("PS1", "WII", "WIU", "3DS", "NDS"
        // + 6 hex digits). Each system gets its own row and the library filters keep them apart.
        Library r{};
        Game g[] = {{"PPSA10001", "Native"}, {"PS2000001", "Two"},   {"PS1000002", "One"},
                    {"WII000003", "Cube"},   {"WIU000004", "Wii U"}, {"3DS000005", "Handheld"},
                    {"NDS000006", "Dual"},   {"PS1000007", "One B"}};
        r.folders[0] = {true, -1, "PS2 smart", false, 0, true, 2, 0, false};
        int rows[16]{};
        assert(collection_contains(r, g[1], 0));
        assert(!collection_contains(r, g[2], 0));
        assert(!collection_contains(r, g[3], 0));
        assert(rail_games(r, g, 8, Rail::PS2, -1, rows, 16) == 1 && rows[0] == 1);
        assert(rail_games(r, g, 8, Rail::PS1, -1, rows, 16) == 2);
        assert(rail_games(r, g, 8, Rail::Wii, -1, rows, 16) == 1 && rows[0] == 3);
        assert(rail_games(r, g, 8, Rail::WiiU, -1, rows, 16) == 1 && rows[0] == 4);
        assert(rail_games(r, g, 8, Rail::N3DS, -1, rows, 16) == 1 && rows[0] == 5);
        assert(rail_games(r, g, 8, Rail::NDS, -1, rows, 16) == 1 && rows[0] == 6);
        Query q;
        q.view = 12; // PlayStation 1 filter
        assert(select_games(r, g, 8, q, rows) == 2);
        q.view = 13; // Nintendo filter: GameCube/Wii, Wii U, 3DS and DS together
        assert(select_games(r, g, 8, q, rows) == 4);
        q.view = 1; // PlayStation 2 filter must not pick up the other systems
        assert(select_games(r, g, 8, q, rows) == 1);
        q.view = 0; // the PS2 platform chip is PS2 images only, not every emulator import
        q.platform = 2;
        assert(select_games(r, g, 8, q, rows) == 1 && rows[0] == 1);
    }
    puts("PASS: search, hidden isolation, nested and smart collections, sorting, recency, v1 "
         "migration and v2 bounds.");
}
