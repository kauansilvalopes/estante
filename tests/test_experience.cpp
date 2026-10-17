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
    puts("PASS: search, hidden isolation, nested and smart collections, sorting, recency, v1 "
         "migration and v2 bounds.");
}
