#include "library.hpp"
#include "cJSON.h"
#include <cstring>
#include <cctype>
#include <strings.h>
#include <cstdio>
namespace estante
{
bool valid_id(const char *id)
{
    if (!id || std::strlen(id) != 9)
        return false;
    for (int i = 0; i < 9; i++)
        if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9')))
            return false;
    return true;
}
static bool optional_int(const cJSON *o, const char *key, int &dest, int max)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!n)
        return true;
    if (!cJSON_IsNumber(n) || n->valuedouble != n->valueint || n->valueint < 0 || n->valueint > max)
        return false;
    dest = n->valueint;
    return true;
}
static bool optional_bool(const cJSON *o, const char *key, bool &dest)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!n)
        return true;
    if (!cJSON_IsBool(n))
        return false;
    dest = cJSON_IsTrue(n);
    return true;
}
static const cJSON *field(const cJSON *o, const char *k)
{
    return cJSON_GetObjectItemCaseSensitive(o, k);
}
int read_catalog(const char *text, Game *out, int cap)
{
    cJSON *root = cJSON_Parse(text);
    if (!root)
        return -1;
    const cJSON *list = field(root, "titles");
    if (!cJSON_IsArray(list))
    {
        cJSON_Delete(root);
        return -1;
    }
    int n = 0;
    const cJSON *t = nullptr;
    cJSON_ArrayForEach(t, list)
    {
        const cJSON *id = field(t, "titleId"), *name = field(t, "titleName");
        if (!cJSON_IsString(id) || !valid_id(id->valuestring))
            continue;
        bool duplicate = false;
        for (int i = 0; i < n; i++)
            if (!std::strcmp(out[i].id, id->valuestring))
                duplicate = true;
        if (duplicate)
            continue;
        if (n >= cap)
        {
            cJSON_Delete(root);
            return -1;
        }
        std::snprintf(out[n].id, sizeof out[n].id, "%s", id->valuestring);
        std::snprintf(out[n].name, sizeof out[n].name, "%s",
                      cJSON_IsString(name) ? name->valuestring : id->valuestring);
        n++;
    }
    cJSON_Delete(root);
    return n;
}
Meta *metadata(Library &d, const char *id)
{
    for (int i = 0; i < d.count; i++)
        if (!std::strcmp(d.games[i].id, id))
            return &d.games[i];
    if (d.count >= max_games || !valid_id(id))
        return nullptr;
    Meta *m = &d.games[d.count++];
    *m = {};
    std::snprintf(m->id, sizeof m->id, "%s", id);
    return m;
}
const Meta *metadata(const Library &d, const char *id)
{
    for (int i = 0; i < d.count; i++)
        if (!std::strcmp(d.games[i].id, id))
            return &d.games[i];
    return nullptr;
}
static bool starts_with(const char *id, const char *prefix)
{
    return !std::strncmp(id, prefix, std::strlen(prefix));
}
// Default platform from the title id when the user has not chosen one: retail PPSA/CUSA are PS5/PS4;
// the 9xxxx range is used by homebrew apps; PS2/PSP serials are PS2; anything else is an app (1).
static int default_platform(const char *id)
{
    // "PS2" + 6 hex digits: made up by the app for PS2 images, which have no title id.
    if (starts_with(id, "PS2") || starts_with(id, "PS1") || starts_with(id, "WII") ||
        starts_with(id, "WIU") || starts_with(id, "3DS") || starts_with(id, "NDS") ||
        starts_with(id, "NSW"))
        return 2;
    const bool ps5 = starts_with(id, "PPSA"), ps4 = starts_with(id, "CUSA");
    if (ps5 || ps4)
    {
        unsigned number = 0;
        for (int i = 4; i < 9 && id[i] >= '0' && id[i] <= '9'; i++)
            number = number * 10 + static_cast<unsigned>(id[i] - '0');
        return number >= 90000 ? 1 : (ps5 ? 5 : 4);
    }
    static const char *const ps2_prefixes[] = {"SLUS", "SLES", "SCUS", "SCES", "SLPM", "SLPS", "SCPS",
                                               "SCPM", "SCAJ", "SLKA", "SCKA", "ULUS", "ULES", "UCUS",
                                               "UCES", "ULJM", "ULJS", "UCJS"};
    for (const char *prefix : ps2_prefixes)
        if (starts_with(id, prefix))
            return 2;
    return 1;
}
static std::uint32_t le32(const unsigned char *p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
// Looks for `name` (without the ";1" version) in the directory whose extent starts at `lba`.
static bool iso_find(const unsigned char *d, std::size_t n, std::uint32_t lba, std::uint32_t length,
                     const char *name, std::uint32_t &out_lba, std::uint32_t &out_len)
{
    const std::size_t start = static_cast<std::size_t>(lba) * 2048;
    if (start >= n || length > 65536)
        return false;
    const std::size_t end = start + length < n ? start + length : n;
    const std::size_t want = std::strlen(name);
    std::size_t pos = start;
    while (pos + 34 <= end)
    {
        const unsigned rec = d[pos];
        if (rec == 0)
        {
            pos = (pos / 2048 + 1) * 2048; // records never cross a sector
            continue;
        }
        const unsigned name_len = d[pos + 32];
        if (pos + rec > end || rec < 33 + name_len)
            return false;
        if (name_len >= want && !strncasecmp(reinterpret_cast<const char *>(d + pos + 33), name, want) &&
            (name_len == want || d[pos + 33 + want] == ';'))
        {
            out_lba = le32(d + pos + 2);
            out_len = le32(d + pos + 10);
            return true;
        }
        pos += rec;
    }
    return false;
}
// The boot executable sits in the root directory named after the serial ("SLUS_200.62;1").
static bool iso_root_serial(const unsigned char *d, std::size_t n, std::uint32_t lba, std::uint32_t length,
                            char *out, std::size_t cap)
{
    const std::size_t start = static_cast<std::size_t>(lba) * 2048;
    if (start >= n || length > 65536)
        return false;
    const std::size_t end = start + length < n ? start + length : n;
    std::size_t pos = start;
    while (pos + 34 <= end)
    {
        const unsigned rec = d[pos];
        if (rec == 0)
        {
            pos = (pos / 2048 + 1) * 2048;
            continue;
        }
        const unsigned name_len = d[pos + 32];
        if (pos + rec > end || rec < 33 + name_len)
            return false;
        const unsigned char *nm = d + pos + 33;
        if (name_len >= 11 && std::isupper(nm[0]) && std::isupper(nm[1]) && std::isupper(nm[2]) &&
            std::isupper(nm[3]) && nm[4] == '_' && std::isdigit(nm[5]) && std::isdigit(nm[6]) &&
            std::isdigit(nm[7]) && nm[8] == '.' && std::isdigit(nm[9]) && std::isdigit(nm[10]))
        {
            std::snprintf(out, cap, "%c%c%c%c-%c%c%c%c%c", nm[0], nm[1], nm[2], nm[3], nm[5], nm[6], nm[7],
                          nm[9], nm[10]);
            return true;
        }
        pos += rec;
    }
    return false;
}
bool iso_boot_serial(const unsigned char *d, std::size_t n, char *out, std::size_t cap)
{
    if (!d || !out || cap < 12 || n < 0x8000 + 2048 || std::memcmp(d + 0x8001, "CD001", 5) != 0)
        return false;
    const unsigned char *root = d + 0x8000 + 156;
    if (iso_root_serial(d, n, le32(root + 2), le32(root + 10), out, cap))
        return true;
    std::uint32_t cnf_lba = 0, cnf_len = 0;
    if (!iso_find(d, n, le32(root + 2), le32(root + 10), "SYSTEM.CNF", cnf_lba, cnf_len))
        return false;
    const std::size_t at = static_cast<std::size_t>(cnf_lba) * 2048;
    if (at >= n)
        return false;
    const std::size_t len = at + (cnf_len < 1024 ? cnf_len : 1024) < n ? (cnf_len < 1024 ? cnf_len : 1024) : n - at;
    // Line like: BOOT2 = cdrom0:\SLUS_206.24;1
    for (std::size_t i = 0; i + 7 < len; i++)
    {
        if (strncasecmp(reinterpret_cast<const char *>(d + at + i), "cdrom0:", 7) != 0)
            continue;
        std::size_t j = i + 7;
        while (j < len && (d[at + j] == '\\' || d[at + j] == '/'))
            j++;
        char letters[5]{};
        int nl2 = 0;
        while (j < len && nl2 < 4 && d[at + j] >= 'A' && d[at + j] <= 'z' && std::isalpha(d[at + j]))
            letters[nl2++] = static_cast<char>(std::toupper(d[at + j++]));
        char digits[8]{};
        int nd = 0;
        while (j < len && d[at + j] != ';' && d[at + j] > ' ' && nd < 7)
        {
            if (std::isdigit(d[at + j]))
                digits[nd++] = static_cast<char>(d[at + j]);
            j++;
        }
        if (nl2 != 4 || nd < 4)
            return false;
        std::snprintf(out, cap, "%s-%s", letters, digits);
        return true;
    }
    return false;
}
int platform(const Game &g, const Library &d)
{
    const Meta *m = metadata(d, g.id);
    if (m && m->platform)
        return m->platform;
    return default_platform(g.id);
}
bool valid_library(const Library &d)
{
    if (d.count < 0 || d.count > max_games)
        return false;
    std::uint32_t used = 0;
    for (int i = 0; i < max_folders; i++)
        if (d.folders[i].used)
        {
            if (d.folders[i].sort < 0 || d.folders[i].sort > 3 || d.folders[i].rulePlatform < 0 ||
                d.folders[i].rulePlatform > 5 || d.folders[i].rulePlatform == 3 ||
                d.folders[i].ruleStatus < 0 || d.folders[i].ruleStatus > 3)
                return false;
            used |= std::uint32_t(1) << i;
            if (!d.folders[i].name[0] ||
                !std::memchr(d.folders[i].name, 0, sizeof d.folders[i].name))
                return false;
            std::uint32_t seen = std::uint32_t(1) << i;
            int p = d.folders[i].parent;
            while (p != -1)
            {
                if (p < 0 || p >= max_folders || !d.folders[p].used ||
                    (seen & (std::uint32_t(1) << p)))
                    return false;
                seen |= std::uint32_t(1) << p;
                p = d.folders[p].parent;
            }
        }
    for (int i = 0; i < d.count; i++)
    {
        const Meta &m = d.games[i];
        if (m.status < 0 || m.status > 3 || m.lastChosen > 9007199254740991ULL)
            return false;
        if (!std::memchr(m.id, 0, sizeof m.id) || !valid_id(m.id) ||
            (m.platform != 0 && m.platform != 1 && m.platform != 2 && m.platform != 4 &&
             m.platform != 5) ||
            (m.folders & ~used))
            return false;
        for (int j = 0; j < i; j++)
            if (!std::strcmp(m.id, d.games[j].id))
                return false;
    }
    return true;
}
bool read_library(const char *text, Library &out)
{
    cJSON *root = cJSON_Parse(text);
    if (!root)
        return false;
    Library temp{};
    bool ok = true;
    const cJSON *ver = field(root, "version"), *fs = field(root, "folders"),
                *gs = field(root, "games");
    if (!cJSON_IsNumber(ver) || (ver->valuedouble != 1 && ver->valuedouble != 2) ||
        !cJSON_IsArray(fs) || !cJSON_IsArray(gs))
        ok = false;
    const cJSON *f = nullptr;
    if (ok)
        cJSON_ArrayForEach(f, fs)
        {
            const cJSON *idx = field(f, "slot"), *p = field(f, "parent"), *name = field(f, "name");
            if (!cJSON_IsNumber(idx) || idx->valuedouble != idx->valueint || idx->valueint < 0 ||
                idx->valueint >= max_folders || !cJSON_IsNumber(p) ||
                p->valuedouble != p->valueint || !cJSON_IsString(name) ||
                std::strlen(name->valuestring) >= 40)
            {
                ok = false;
                break;
            }
            Folder &dest = temp.folders[idx->valueint];
            if (dest.used)
            {
                ok = false;
                break;
            }
            dest.used = true;
            dest.parent = p->valueint;
            std::snprintf(dest.name, sizeof dest.name, "%s", name->valuestring);
            if (!optional_bool(f, "pinned", dest.pinned) ||
                !optional_int(f, "sort", dest.sort, 3) || !optional_bool(f, "smart", dest.smart) ||
                !optional_int(f, "rulePlatform", dest.rulePlatform, 5) ||
                !optional_int(f, "ruleStatus", dest.ruleStatus, 3) ||
                !optional_bool(f, "ruleFavorite", dest.ruleFavorite))
            {
                ok = false;
                break;
            }
        }
    const cJSON *g = nullptr;
    if (ok)
        cJSON_ArrayForEach(g, gs)
        {
            const cJSON *id = field(g, "id"), *p = field(g, "platform"),
                        *mask = field(g, "folders"), *fav = field(g, "favorite");
            if (temp.count >= max_games || !cJSON_IsString(id) || !valid_id(id->valuestring) ||
                !cJSON_IsNumber(p) || p->valuedouble != p->valueint || !cJSON_IsNumber(mask) ||
                mask->valuedouble < 0 || mask->valuedouble > 4294967295.0 || !cJSON_IsBool(fav))
            {
                ok = false;
                break;
            }
            Meta &dest = temp.games[temp.count++];
            std::snprintf(dest.id, sizeof dest.id, "%s", id->valuestring);
            dest.platform = p->valueint;
            dest.folders = static_cast<std::uint32_t>(mask->valuedouble);
            if (mask->valuedouble != dest.folders)
            {
                ok = false;
                break;
            }
            dest.favorite = cJSON_IsTrue(fav);
            {
                const cJSON *hid = field(g, "hidden");
                if (hid && !cJSON_IsBool(hid))
                {
                    ok = false;
                    break;
                }
                dest.hidden = cJSON_IsTrue(hid);
            }
            if (!optional_bool(g, "pinned", dest.pinned) ||
                !optional_int(g, "status", dest.status, 3))
            {
                ok = false;
                break;
            }
            const cJSON *recent = field(g, "lastChosen");
            if (recent)
            {
                if (!cJSON_IsNumber(recent) || recent->valuedouble < 0 ||
                    recent->valuedouble > 9007199254740991.0)
                {
                    ok = false;
                    break;
                }
                dest.lastChosen = static_cast<std::uint64_t>(recent->valuedouble);
                if (static_cast<double>(dest.lastChosen) != recent->valuedouble)
                {
                    ok = false;
                    break;
                }
            }
        }
    ok = ok && valid_library(temp);
    if (ok)
        out = temp;
    cJSON_Delete(root);
    return ok;
}
char *write_library(const Library &d)
{
    if (!valid_library(d))
        return nullptr;
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return nullptr;
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON *fs = cJSON_AddArrayToObject(root, "folders"),
          *gs = cJSON_AddArrayToObject(root, "games");
    for (int i = 0; i < max_folders; i++)
        if (d.folders[i].used)
        {
            cJSON *f = cJSON_CreateObject();
            cJSON_AddNumberToObject(f, "slot", i);
            cJSON_AddNumberToObject(f, "parent", d.folders[i].parent);
            cJSON_AddStringToObject(f, "name", d.folders[i].name);
            cJSON_AddBoolToObject(f, "pinned", d.folders[i].pinned);
            cJSON_AddNumberToObject(f, "sort", d.folders[i].sort);
            cJSON_AddBoolToObject(f, "smart", d.folders[i].smart);
            cJSON_AddNumberToObject(f, "rulePlatform", d.folders[i].rulePlatform);
            cJSON_AddNumberToObject(f, "ruleStatus", d.folders[i].ruleStatus);
            cJSON_AddBoolToObject(f, "ruleFavorite", d.folders[i].ruleFavorite);
            cJSON_AddItemToArray(fs, f);
        }
    for (int i = 0; i < d.count; i++)
    {
        const Meta &m = d.games[i];
        cJSON *g = cJSON_CreateObject();
        cJSON_AddStringToObject(g, "id", m.id);
        cJSON_AddNumberToObject(g, "platform", m.platform);
        cJSON_AddNumberToObject(g, "folders", m.folders);
        cJSON_AddBoolToObject(g, "favorite", m.favorite);
        cJSON_AddBoolToObject(g, "hidden", m.hidden);
        cJSON_AddBoolToObject(g, "pinned", m.pinned);
        cJSON_AddNumberToObject(g, "status", m.status);
        cJSON_AddNumberToObject(g, "lastChosen", static_cast<double>(m.lastChosen));
        cJSON_AddItemToArray(gs, g);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}
bool delete_folder(Library &d, int slot)
{
    if (slot < 0 || slot >= max_folders || !d.folders[slot].used)
        return false;
    std::uint32_t removed = std::uint32_t(1) << slot;
    bool again = true;
    while (again)
    {
        again = false;
        for (int i = 0; i < max_folders; i++)
            if (d.folders[i].used && d.folders[i].parent >= 0 &&
                (removed & (std::uint32_t(1) << d.folders[i].parent)) &&
                !(removed & (std::uint32_t(1) << i)))
            {
                removed |= std::uint32_t(1) << i;
                again = true;
            }
    }
    for (int i = 0; i < max_folders; i++)
        if (removed & (std::uint32_t(1) << i))
            d.folders[i] = {};
    for (int i = 0; i < d.count; i++)
        d.games[i].folders &= ~removed;
    return true;
}
} // namespace estante
