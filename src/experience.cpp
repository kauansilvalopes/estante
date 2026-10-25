#include "experience.hpp"
#include <cstring>
#include <algorithm>
namespace estante
{
static bool id_starts(const char *id, const char *prefix)
{
    return !std::strncmp(id, prefix, std::strlen(prefix));
}
static void normalize(const char *s, char *out, unsigned capacity)
{
    unsigned n = 0;
    for (unsigned i = 0; s[i] && n + 1 < capacity; i++)
    {
        unsigned char c = s[i];
        if (c < 128)
        {
            out[n++] = c >= 'A' && c <= 'Z' ? c + 32 : c;
            continue;
        }
        if (c == 0xc3 && s[i + 1])
        {
            unsigned char a = s[++i];
            char v = '?';
            if ((a >= 0x80 && a <= 0x85) || (a >= 0xa0 && a <= 0xa5))
                v = 'a';
            else if (a == 0x87 || a == 0xa7)
                v = 'c';
            else if ((a >= 0x88 && a <= 0x8b) || (a >= 0xa8 && a <= 0xab))
                v = 'e';
            else if ((a >= 0x8c && a <= 0x8f) || (a >= 0xac && a <= 0xaf))
                v = 'i';
            else if ((a >= 0x92 && a <= 0x96) || (a >= 0xb2 && a <= 0xb6))
                v = 'o';
            else if ((a >= 0x99 && a <= 0x9c) || (a >= 0xb9 && a <= 0xbc))
                v = 'u';
            else if (a == 0x91 || a == 0xb1)
                v = 'n';
            out[n++] = v;
        }
    }
    out[n] = 0;
}
bool name_matches(const char *name, const char *query)
{
    char a[256], b[100];
    normalize(name, a, sizeof a);
    normalize(query, b, sizeof b);
    return std::strstr(a, b) != nullptr;
}
static bool direct_member(const Library &d, const Game &g, int slot)
{
    const auto &f = d.folders[slot];
    const Meta *m = metadata(d, g.id);
    if (f.smart)
        return (!f.rulePlatform ||
                (f.rulePlatform == 2 ? id_starts(g.id, "PS2") : platform(g, d) == f.rulePlatform)) &&
               (!f.ruleStatus || (m && m->status == f.ruleStatus)) &&
               (!f.ruleFavorite || (m && m->favorite));
    return m && (m->folders & (std::uint32_t(1) << slot));
}
bool collection_contains(const Library &d, const Game &g, int slot, bool descendants)
{
    if (slot < 0 || slot >= max_folders || !d.folders[slot].used)
        return false;
    if (direct_member(d, g, slot))
        return true;
    if (!descendants)
        return false;
    for (int i = 0; i < max_folders; i++)
        if (d.folders[i].used)
        {
            int p = d.folders[i].parent;
            for (int depth = 0; p >= 0 && p < max_folders && depth < max_folders;
                 depth++, p = d.folders[p].parent)
                if (p == slot && direct_member(d, g, i))
                    return true;
        }
    return false;
}
int select_games(const Library &d, const Game *games, int count, const Query &q, int *out)
{
    if (count < 0 || count > max_games || !games || !out)
        return 0;
    int n = 0;
    for (int i = 0; i < count; i++)
    {
        const auto &g = games[i];
        const Meta *m = metadata(d, g.id);
        bool hidden = m && m->hidden;
        if ((q.view == 7) != hidden)
            continue;
        int p = platform(g, d);
        const bool nintendo = id_starts(g.id, "WII") || id_starts(g.id, "WIU") ||
                              id_starts(g.id, "3DS") || id_starts(g.id, "NDS") || id_starts(g.id, "NSW");
        bool match = (q.view == 0 && (q.withApps || p != 1)) || q.view == 7 ||
                     (q.view == 1 && id_starts(g.id, "PS2")) || (q.view == 12 && id_starts(g.id, "PS1")) ||
                     (q.view == 13 && nintendo) ||
                     (q.view == 2 && p == 4) || (q.view == 3 && p == 5) ||
                     (q.view == 4 && p == 1) || (q.view == 5 && m && m->favorite) ||
                     (q.view == 6 && (!m || !m->folders)) || (q.view == 8 && m && m->status == 1) ||
                     (q.view == 9 && m && m->status == 2) ||
                     (q.view == 10 && m && m->status == 3) || (q.view == 11 && m && m->pinned) ||
                     (q.view >= 100 && collection_contains(d, g, q.view - 100, q.descendants));
        if (match && (!q.platform || (q.platform == 2 ? id_starts(g.id, "PS2") : p == q.platform)) &&
            (!q.status || (m && m->status == q.status)) && (!q.favorites || (m && m->favorite)) &&
            name_matches(g.name, q.text))
            out[n++] = i;
    }
    std::sort(out, out + n,
              [&](int a, int b)
              {
                  const Meta *ma = metadata(d, games[a].id), *mb = metadata(d, games[b].id);
                  if (q.sort == 1)
                  {
                      int pa = platform(games[a], d), pb = platform(games[b], d);
                      if (pa != pb)
                          return pa < pb;
                  }
                  if (q.sort == 2)
                  {
                      auto ta = ma ? ma->lastChosen : 0, tb = mb ? mb->lastChosen : 0;
                      if (ta != tb)
                          return ta > tb;
                  }
                  if (q.sort == 3)
                  {
                      bool fa = ma && ma->favorite, fb = mb && mb->favorite;
                      if (fa != fb)
                          return fa > fb;
                  }
                  char na[256], nb[256];
                  normalize(games[a].name, na, sizeof na);
                  normalize(games[b].name, nb, sizeof nb);
                  int cmp = std::strcmp(na, nb);
                  return cmp ? cmp < 0 : std::strcmp(games[a].id, games[b].id) < 0;
              });
    return n;
}
int recent_games(const Library &d, const Game *games, int count, int *out, int capacity)
{
    if (capacity <= 0)
        return 0;
    int all[max_games];
    Query q;
    q.sort = 2;
    int n = select_games(d, games, count, q, all), got = 0;
    for (int i = 0; i < n && got < capacity; i++)
    {
        const Meta *m = metadata(d, games[all[i]].id);
        if (m && m->lastChosen)
            out[got++] = all[i];
    }
    return got;
}
int rail_games(const Library &d, const Game *games, int count, Rail kind, int folder, int *out,
               int capacity)
{
    if (capacity <= 0)
        return 0;
    if (kind == Rail::Continue)
        return recent_games(d, games, count, out, capacity);
    Query q;
    switch (kind)
    {
    case Rail::Favorites:
        q.view = 5;
        break;
    case Rail::Want:
        q.view = 8;
        break;
    case Rail::Folder:
        if (folder < 0 || folder >= max_folders || !d.folders[folder].used)
            return 0;
        q.view = 100 + folder;
        q.sort = d.folders[folder].sort;
        q.descendants = true;
        break;
    case Rail::PS5:
        q.view = 3;
        break;
    case Rail::PS4:
        q.view = 2;
        break;
    case Rail::PS2:
        q.view = 1;
        break;
    case Rail::PS1:
        q.view = 12;
        break;
    case Rail::Wii:
    case Rail::WiiU:
    case Rail::N3DS:
    case Rail::NDS:
    case Rail::Switch:
        q.view = 13; // every Nintendo system; the id prefix below picks one
        break;
    case Rail::Continue:
        break;
    }
    int all[max_games];
    const int n = select_games(d, games, count, q, all);
    const char *prefix = nullptr;
    if (kind == Rail::PS1)
        prefix = "PS1";
    else if (kind == Rail::Wii)
        prefix = "WII";
    else if (kind == Rail::WiiU)
        prefix = "WIU";
    else if (kind == Rail::N3DS)
        prefix = "3DS";
    else if (kind == Rail::NDS)
        prefix = "NDS";
    else if (kind == Rail::Switch)
        prefix = "NSW";
    int got = 0;
    for (int i = 0; i < n && got < capacity; i++)
    {
        const char *id = games[all[i]].id;
        const bool match = kind == Rail::PS2 ? id_starts(id, "PS2") : !prefix || id_starts(id, prefix);
        if (match)
            out[got++] = all[i];
    }
    return got;
}
} // namespace estante
