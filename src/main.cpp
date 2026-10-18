// Estante native PS5 prototype. GPL-3.0-or-later.
// VideoOut and Pad ABI based on the public boilerplate, not firmware offsets.
#include "demo_renderer.hpp"
#ifdef ESTANTE_GL
#include "gl_main.hpp"
#endif
#include "library.hpp"
#include "experience.hpp"
#include "i18n.hpp"
#include <algorithm>
#include <ctime>
#include "cJSON.h"
#include "cover_decode.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
using namespace estante;
#ifdef ESTANTE_GL
using Canvas = estante::GlCanvas; // the screens draw through the GPU canvas
estante::GlCanvas *gpu_canvas = nullptr;
#else
using ps5::demo::Canvas;
#endif
using ps5::demo::Color;
struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t analog[6];
    std::uint8_t reserved0[66];
    std::int32_t connected;
    std::uint64_t timestamp;
    std::uint8_t reserved1[32];
};
static_assert(sizeof(PadSample) == 120);
struct NetAddress
{
    std::uint8_t length, family;
    std::uint16_t port;
    std::uint32_t address;
    std::uint16_t virtual_port;
    std::uint8_t zero[6];
};
extern "C"
{
    int sceUserServiceInitialize(const void *);
    int sceUserServiceGetInitialUser(int *);
    int sceSystemServiceParamGetInt(int, int *);
    int scePadInit();
    int scePadOpen(int, int, int, const void *);
    int scePadRead(int, PadSample *, int);
    std::uint64_t sceKernelGetProcessTime();
    int sceKernelDebugOutText(int, const char *);
    int sceNetSocket(const char *, int, int, int);
    int sceNetConnect(int, const void *, std::uint32_t);
    int sceNetSend(int, const void *, std::size_t, int);
    int sceNetRecv(int, void *, std::size_t, int);
    int sceNetSetsockopt(int, int, int, const void *, std::uint32_t);
    int sceNetSocketClose(int);
}
namespace
{
constexpr Color bg = static_cast<Color>(0xff141110), panel = static_cast<Color>(0xff272220),
                accent = static_cast<Color>(0xffffb153), muted = static_cast<Color>(0xffc9beb9),
                white = Color::white;
constexpr std::uint32_t UP = 0x10, RIGHT = 0x20, DOWN = 0x40, LEFT = 0x80, L1 = 0x400, R1 = 0x800,
                        TRIANGLE = 0x1000, CIRCLE = 0x2000, CROSS = 0x4000, SQUARE = 0x8000,
                        OPTIONS = 0x8, R3 = 0x4, TOUCH = 0x100000;
constexpr int thumb_edge = 264; // library card size: pre-scaled once when the cover loads
constexpr int cover_edge = 512; // native icon0 size; downscaled with a box filter when drawn
Library db{};
Game games[max_games]{};
int count = 0;
bool selected[max_games]{};
Query query{};
int filtered[max_games]{}, filtered_count = 0, cursor = 0, nav_cursor = 0;
bool nav_focus = false;
int screen = 0, home_cursor = 0, home_row = 0, home_games[max_games]{}, home_count = 0;

int home_folders[max_folders]{}, home_folder_count = 0, home_folder_cursor = 0;
int collection_parent = -1, collection_entries[34]{}, collection_count = 0, collection_cursor = 0,
    editing_collection = -1;
int settings_cursor = 0, modal = 0, menu_cursor = 0, menu_kind = 0, details_index = -1,
    folder_menu_slot = -1;
int keyboard_kind = 0, keyboard_cursor = 0, edit_slot = -1, new_parent = -1;
char entry_name[80]{};
bool record_mode = false, large_text = false, storage_ok = false;
std::uint64_t ui_version = 1, cover_version = 1;
int pad = -1;
std::uint32_t held = 0;
std::uint64_t retry_pad = 0, repeat_at = 0;
std::uint64_t message_until = 0;
using i18n::tr;
char message[180] = "Bem-vindo ao Estante", connection[96] = "Biblioteca local";
struct Cover
{
    char id[10];
    bool ready;
    std::uint64_t age;
    std::uint32_t tint;
    std::uint32_t thumb[thumb_edge * thumb_edge];
    std::uint32_t pixels[cover_edge * cover_edge];
};
Cover covers[32]{};
std::uint64_t cover_clock = 0;
char failed_covers[max_games][10]{};
int failed_cover_count = 0;
std::atomic<int> work{0};
char request_path[160]{}, response[1024 * 1024]{};
std::size_t response_size = 0;
int work_kind = 0, pending_kind = 0;
char pending_path[160]{};
bool response_ok = false;
char requested_cover[10]{}, launch_id[10]{};
// PS2 images found in the PS5SX2 games folder: file name (with extension) per library slot, empty for
// normal titles. The id is made up ("PS2" + hash) because an image has no title id.
char ps2_file[max_games][144]{};
constexpr const char *ps2_games_dir = "/data/PCSX2/games";
// Boot serial of each PS2 image ("SLUS-20624"), read once from the start of the image and remembered in
// /download0/ps2-serials.txt. state: 0 not read yet, 1 serial known, 2 no serial. The serial names the
// cover PS5SX2 already downloaded to /data/PCSX2/covers.
char ps2_serial[max_games][12]{};
unsigned char ps2_state[max_games]{};
constexpr int ps2_cache_max = 256;
char ps2_cache_name[ps2_cache_max][144]{}, ps2_cache_serial[ps2_cache_max][12]{};
int ps2_cache_count = 0, ps2_job = -1;
char ps2_job_name[144]{};
std::uint32_t decoded_cover[cover_edge * cover_edge]{};
bool cover_ok = false;
std::uint64_t verify_at = 0;
int verify_attempts = 0;
bool launch_pending = false;
// Launch animation: the cover slides aside, a disc slides out and spins, then the request is sent.
bool launch_anim_enabled = true;
int anim_idx = -1;
std::uint64_t anim_start = 0;
constexpr std::uint64_t anim_length = 1300000; // microseconds
std::uint64_t played_last[max_games]{}, activity_now = 0, activity_fetched = 0;
std::uint32_t played_seconds[max_games]{};
constexpr char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
constexpr int key_count = sizeof(keys) - 1;
const char *views[] = {"Todos os jogos", "PlayStation 2 e PSP", "PlayStation 4", "PlayStation 5",
                       "Apps",           "Favoritos",     "Sem coleção",   "Ocultos",
                       "Quero jogar",    "Em andamento",  "Concluídos",    "Fixados"};
// Order shown in the filter list (ids stay as they are): Apps right after "Todos os jogos".
constexpr int view_order[12] = {0, 4, 3, 2, 1, 5, 6, 7, 8, 9, 10, 11};
const char *statuses[] = {"Sem status", "Quero jogar", "Em andamento", "Concluído"};
const char *sorts[] = {"Nome A-Z", "Plataforma", "Últimas escolhas", "Favoritos primeiro"};
const Meta *meta(int idx)
{
    return idx >= 0 && idx < count ? metadata(static_cast<const Library &>(db), games[idx].id)
                                   : nullptr;
}
void rebuild();
void start_request(int, const char *);
void log(const char *text)
{
    char line[220];
    std::snprintf(line, sizeof line, "ESTANTE: %s\n", text);
    sceKernelDebugOutText(0, line);
}
void msg(const char *text)
{
    message_until = sceKernelGetProcessTime() + 6000000;
    std::snprintf(message, sizeof message, "%s", text);
    log(text);
}
char *read_file(const char *name)
{
    FILE *f = std::fopen(name, "rb");
    if (!f)
        return nullptr;
    if (std::fseek(f, 0, SEEK_END))
    {
        std::fclose(f);
        return nullptr;
    }
    long size = std::ftell(f);
    if (size < 0 || size > 1024 * 1024)
    {
        std::fclose(f);
        return nullptr;
    }
    std::rewind(f);
    char *s = static_cast<char *>(std::malloc(static_cast<std::size_t>(size) + 1));
    if (!s)
    {
        std::fclose(f);
        return nullptr;
    }
    const auto got = std::fread(s, 1, static_cast<std::size_t>(size), f);
    std::fclose(f);
    s[got] = 0;
    if (got != static_cast<std::size_t>(size))
    {
        std::free(s);
        return nullptr;
    }
    return s;
}
bool write_file(const char *name, const char *text)
{
    char tmp[120];
    std::snprintf(tmp, sizeof tmp, "%s.tmp", name);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return false;
    std::size_t n = std::strlen(text), sent = 0;
    while (sent < n)
    {
        long r = write(fd, text + sent, n - sent);
        if (r <= 0)
        {
            close(fd);
            return false;
        }
        sent += static_cast<std::size_t>(r);
    }
    bool ok = fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (!ok)
        return false;
    return rename(tmp, name) == 0;
}
bool persist(const Library &next)
{
    if (!storage_ok)
    {
        msg("PASTAS NAO SALVAS: /DOWNLOAD0 INDISPONIVEL");
        return false;
    }
    char *text = write_library(next);
    if (!text)
    {
        msg("ORGANIZACAO INVALIDA");
        return false;
    }
    char *previous = write_library(db);
    if (previous)
    {
        if (!write_file("/download0/estante-backup.json", previous))
        {
            cJSON_free(previous);
            cJSON_free(text);
            msg("FALHA NO BACKUP; ALTERACAO CANCELADA");
            return false;
        }
        cJSON_free(previous);
    }
    bool ok = write_file("/download0/estante.json", text);
    cJSON_free(text);
    if (ok)
    {
        db = next;
        msg("ORGANIZACAO SALVA NO PS5");
    }
    else
        msg("FALHA AO SALVAR; ORGANIZACAO ANTERIOR PRESERVADA");
    return ok;
}

void save_preferences()
{
    if (!storage_ok)
        return;
    char data[140];
    std::snprintf(data, sizeof data, "{\"largeText\":%s,\"recordMode\":%s,\"lang\":\"%s\",\"anim\":%s}",
                  large_text ? "true" : "false", record_mode ? "true" : "false",
                  i18n::language ? "en" : "pt", launch_anim_enabled ? "true" : "false");
    if (!write_file("/download0/estante-preferences.json", data))
        msg("Nao foi possivel salvar as preferencias.");
}
// PS5 system language: Brazilian Portuguese (17) and European Portuguese (7) get Portuguese,
// everything else gets English. A saved preference overrides this.
void detect_language()
{
    int value = 1;
    if (sceSystemServiceParamGetInt(1, &value) == 0)
        i18n::language = (value == 17 || value == 7) ? 0 : 1;
}
void read_preferences()
{
    char *saved = read_file("/download0/estante-preferences.json");
    if (!saved)
        return;
    auto *root = cJSON_Parse(saved);
    std::free(saved);
    auto *big = cJSON_GetObjectItemCaseSensitive(root, "largeText");
    auto *rec = cJSON_GetObjectItemCaseSensitive(root, "recordMode");
    if (cJSON_IsBool(big))
        large_text = cJSON_IsTrue(big);
    if (cJSON_IsBool(rec))
        record_mode = cJSON_IsTrue(rec);
    auto *anim = cJSON_GetObjectItemCaseSensitive(root, "anim");
    if (cJSON_IsBool(anim))
        launch_anim_enabled = cJSON_IsTrue(anim);
    auto *lang = cJSON_GetObjectItemCaseSensitive(root, "lang");
    if (cJSON_IsString(lang) && lang->valuestring)
        i18n::language = std::strcmp(lang->valuestring, "pt") ? 1 : 0;
    cJSON_Delete(root);
}

int chosen_count()
{
    int n = 0;
    for (int i = 0; i < count; i++)
        if (selected[i])
            n++;
    return n;
}
void rebuild()
{
    ui_version++;
    char previous[10]{};
    if (cursor >= 0 && cursor < filtered_count)
        std::snprintf(previous, 10, "%s", games[filtered[cursor]].id);
    if (query.view >= 100 && !db.folders[query.view - 100].used)
        query.view = 0;
    if (query.view >= 100)
        query.sort = db.folders[query.view - 100].sort;
    filtered_count = select_games(db, games, count, query, filtered);
    if (cursor >= filtered_count)
        cursor = filtered_count ? filtered_count - 1 : 0;
    for (int i = 0; i < filtered_count; i++)
        if (!std::strcmp(previous, games[filtered[i]].id))
            cursor = i;
    char focused[10]{};
    if (home_count && home_cursor < home_count)
        std::snprintf(focused, 10, "%s", games[home_games[home_cursor]].id);
    Query homeQuery;
    homeQuery.withApps = true;
    home_count = select_games(db, games, count, homeQuery, home_games);
    auto recency = [](int i)
    {
        const auto *m = meta(i);
        const std::uint64_t chosen = m ? m->lastChosen : 0;
        return played_last[i] > chosen ? played_last[i] : chosen;
    };
    std::stable_sort(home_games, home_games + home_count,
                     [&](int a, int b)
                     {
                         auto *ma = meta(a);
                         auto *mb = meta(b);
                         const bool pa = ma && ma->pinned, pb = mb && mb->pinned;
                         if (pa != pb)
                             return pa;
                         return recency(a) > recency(b);
                     });
    for (int i = 0; i < home_count; i++)
        if (!std::strcmp(focused, games[home_games[i]].id))
            home_cursor = i;
    if (home_cursor >= home_count)
        home_cursor = home_count ? home_count - 1 : 0;
    home_folder_count = 0;
    for (int pinned = 1; pinned >= 0; pinned--)
        for (int i = 0; i < max_folders; i++)
            if (db.folders[i].used && db.folders[i].parent < 0 &&
                db.folders[i].pinned == bool(pinned))
                home_folders[home_folder_count++] = i;
    if (home_folder_cursor > home_folder_count)
        home_folder_cursor = home_folder_count;
    if (collection_parent >= 0 && !db.folders[collection_parent].used)
        collection_parent = -1;
    collection_count = 0;
    if (collection_parent >= 0)
        collection_entries[collection_count++] = -1;
    for (int i = 0; i < max_folders; i++)
        if (db.folders[i].used && db.folders[i].parent == collection_parent)
            collection_entries[collection_count++] = i;
    collection_entries[collection_count++] = -2;
    if (collection_cursor >= collection_count)
        collection_cursor = collection_count - 1;
}
int target_index()
{
    if (details_index >= 0 && modal == 5)
        return details_index;
    if (screen == 0)
        return home_count ? home_games[home_cursor] : -1;
    if (screen == 1)
        return filtered_count ? filtered[cursor] : -1;
    return -1;
}
template <class Fn> void update_targets(Fn fn)
{
    Library next = db;
    bool batch = chosen_count() > 0 && screen == 1;
    int target = target_index();
    for (int i = 0; i < count; i++)
        if ((batch && selected[i]) || (!batch && i == target))
        {
            Meta *m = metadata(next, games[i].id);
            if (m)
                fn(*m);
        }
    if (persist(next))
        rebuild();
}
Cover *find_cover(const char *id)
{
    for (auto &cover : covers)
        if (cover.ready && !std::strcmp(cover.id, id))
            return &cover;
    return nullptr;
}
void build_thumb(std::uint32_t *out, const std::uint32_t *pixels)
{
    for (unsigned y = 0; y < thumb_edge; y++)
    {
        const unsigned sy0 = y * cover_edge / thumb_edge;
        unsigned sy1 = (y + 1) * cover_edge / thumb_edge;
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (unsigned x = 0; x < thumb_edge; x++)
        {
            const unsigned sx0 = x * cover_edge / thumb_edge;
            unsigned sx1 = (x + 1) * cover_edge / thumb_edge;
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            std::uint32_t r = 0, g = 0, b = 0, n = 0;
            for (unsigned sy = sy0; sy < sy1; sy++)
                for (unsigned sx = sx0; sx < sx1; sx++)
                {
                    const std::uint32_t p = pixels[sy * cover_edge + sx];
                    r += p & 255;
                    g += (p >> 8) & 255;
                    b += (p >> 16) & 255;
                    n++;
                }
            out[y * thumb_edge + x] =
                0xff000000u | ((b / n) << 16) | ((g / n) << 8) | (r / n);
        }
    }
}
std::uint32_t average_pixels(const std::uint32_t *pixels)
{
    std::uint32_t r = 0, g = 0, b = 0, n = 0;
    for (unsigned y = 0; y < cover_edge; y += 16)
        for (unsigned x = 0; x < cover_edge; x += 16)
        {
            const std::uint32_t p = pixels[y * cover_edge + x];
            r += p & 255;
            g += (p >> 8) & 255;
            b += (p >> 16) & 255;
            n++;
        }
    if (!n)
        return 0xff272220u;
    return 0xff000000u | ((b / n) << 16) | ((g / n) << 8) | (r / n);
}
bool cached_cover(const char *path, bool header)
{
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    char id[10]{};
    if (header && (std::fread(id, 1, 10, f) != 10 || std::memcmp(id, requested_cover, 10)))
    {
        std::fclose(f);
        return false;
    }
    response_size = std::fread(response, 1, sizeof response, f);
    bool too_large = std::fgetc(f) != EOF;
    std::fclose(f);
    return !too_large && decode_cover(reinterpret_cast<unsigned char *>(response), response_size,
                                      decoded_cover, cover_edge);
}
bool http_get(const char *path, std::size_t prefix = 0)
{
    response[0] = 0;
    response_size = 0;
    int s = sceNetSocket("estante", 2, 1, 6);
    if (s < 0)
        return false;
    const int timeout = 3000000;
    for (int opt : {0x1105, 0x1106, 0x1109})
        sceNetSetsockopt(s, 0xffff, opt, &timeout, sizeof timeout);
    constexpr std::uint16_t port = 6969;
    NetAddress address{sizeof(NetAddress), 2, static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                       0x0100007f,         0, {}};
    if (sceNetConnect(s, &address, sizeof address) < 0)
    {
        sceNetSocketClose(s);
        return false;
    }
    char req[320];
    int len =
        std::snprintf(req, sizeof req,
                      "GET %s HTTP/1.0\r\nHost: 127.0.0.1:6969\r\nConnection: close\r\n\r\n", path);
    int off = 0;
    while (off < len)
    {
        int n = sceNetSend(s, req + off, static_cast<std::size_t>(len - off), 0);
        if (n <= 0)
        {
            sceNetSocketClose(s);
            return false;
        }
        off += n;
    }
    std::size_t used = 0;
    bool eof = false;
    while (used < sizeof(response) - 1)
    {
        int n = sceNetRecv(s, response + used, sizeof(response) - 1 - used, 0);
        if (n == 0)
        {
            eof = true;
            break;
        }
        if (n < 0)
            break;
        used += static_cast<std::size_t>(n);
        if (prefix)
        {
            // Only the start of a big file is wanted: stop once enough of the body has arrived.
            response[used] = 0;
            const char *end = std::strstr(response, "\r\n\r\n");
            if (end && used - static_cast<std::size_t>(end + 4 - response) >= prefix)
            {
                eof = true;
                break;
            }
        }
    }
    sceNetSocketClose(s);
    response[used] = 0;
    if (!eof)
        return false;
    int code = 0;
    if (std::sscanf(response, "HTTP/%*s %d", &code) != 1 || code < 200 || code >= 300)
        return false;
    char *body = std::strstr(response, "\r\n\r\n");
    if (!body)
        return false;
    // HTTP/1.0 requested: don't feed unsupported chunked bodies to JSON parser.
    if (std::strstr(response, "Transfer-Encoding: chunked") ||
        std::strstr(response, "transfer-encoding: chunked"))
        return false;
    body += 4;
    response_size = used - static_cast<std::size_t>(body - response);
    std::memmove(response, body, response_size);
    response[response_size] = 0;
    return true;
}

void *worker(void *)
{
    if (work_kind == 3)
    {
        unsigned hash = 2166136261u;
        for (const char *p = requested_cover; *p; p++)
            hash = (hash ^ static_cast<unsigned char>(*p)) * 16777619u;
        char local[160], cache[160];
        std::snprintf(local, sizeof local, "/download0/artwork/%s.png", requested_cover);
        std::snprintf(cache, sizeof cache, "/download0/estante-cover-%02u.bin", hash % 32);
        cover_ok = cached_cover(local, false) || cached_cover(cache, true);
        if (!cover_ok)
        {
            response_ok = http_get(request_path);
            cover_ok = response_ok && decode_cover(reinterpret_cast<unsigned char *>(response),
                                                   response_size, decoded_cover, cover_edge);
            if (cover_ok)
            {
                char tmp[180];
                std::snprintf(tmp, sizeof tmp, "%s.tmp", cache);
                FILE *f = std::fopen(tmp, "wb");
                if (f)
                {
                    bool ok = std::fwrite(requested_cover, 1, 10, f) == 10 &&
                              std::fwrite(response, 1, response_size, f) == response_size;
                    ok = std::fclose(f) == 0 && ok;
                    if (ok)
                        rename(tmp, cache);
                }
            }
        }
    }
    else if (work_kind == 8)
        response_ok = http_get(request_path, 700000);
    else
        response_ok = http_get(request_path);
    work.store(2, std::memory_order_release);
    return nullptr;
}
void start_request(int kind, const char *path)
{
    if (work.load() != 0)
    {
        if (kind != 3)
        {
            pending_kind = kind;
            std::snprintf(pending_path, sizeof pending_path, "%s", path);
        }
        return;
    }
    work_kind = kind;
    std::snprintf(request_path, sizeof request_path, "%s", path);
    work.store(1);
    pthread_t thread{};
    if (pthread_create(&thread, nullptr, worker, nullptr) != 0)
    {
        work.store(0);
        launch_pending = false;
        msg("Não foi possível iniciar a consulta.");
        return;
    }
    pthread_detach(thread);
    if (kind == 1)
        msg("Atualizando a biblioteca…");
}
bool parse_activity()
{
    cJSON *root = cJSON_Parse(response);
    if (!root)
        return false;
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return false;
    }
    std::memset(played_last, 0, sizeof played_last);
    std::memset(played_seconds, 0, sizeof played_seconds);
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(root, "__nowTs");
    activity_now = cJSON_IsNumber(now) && now->valuedouble > 0
                       ? static_cast<std::uint64_t>(now->valuedouble)
                       : 0;
    for (const cJSON *item = root->child; item; item = item->next)
    {
        if (!item->string || item->string[0] == '_' || !cJSON_IsObject(item))
            continue;
        int idx = -1;
        for (int i = 0; i < count; i++)
            if (!std::strcmp(games[i].id, item->string))
            {
                idx = i;
                break;
            }
        if (idx < 0)
            continue;
        const cJSON *last = cJSON_GetObjectItemCaseSensitive(item, "lastLaunchTs");
        const cJSON *secs = cJSON_GetObjectItemCaseSensitive(item, "totalSeconds");
        if (cJSON_IsNumber(last) && last->valuedouble > 0)
            played_last[idx] = static_cast<std::uint64_t>(last->valuedouble);
        if (cJSON_IsNumber(secs) && secs->valuedouble > 0 && secs->valuedouble < 4e9)
            played_seconds[idx] = static_cast<std::uint32_t>(secs->valuedouble);
    }
    cJSON_Delete(root);
    return true;
}
bool parse_running(char *title, bool &running)
{
    cJSON *root = cJSON_Parse(response);
    if (!root)
        return false;
    auto *flag = cJSON_GetObjectItemCaseSensitive(root, "gameRunning");
    auto *id = cJSON_GetObjectItemCaseSensitive(root, "runningTitleId");
    bool ok = cJSON_IsBool(flag) && cJSON_IsString(id);
    if (ok)
    {
        running = cJSON_IsTrue(flag);
        std::snprintf(title, 10, "%s", id->valuestring);
    }
    cJSON_Delete(root);
    return ok;
}
bool is_ps2_id(const char *id)
{
    return id[0] == 'P' && id[1] == 'S' && id[2] == '2';
}
bool ps2_image_name(const char *name, char *stem, std::size_t cap)
{
    const std::size_t n = std::strlen(name);
    static const char *const exts[] = {".iso", ".chd", ".cso", ".zso"};
    for (const char *ext : exts)
    {
        const std::size_t e = std::strlen(ext);
        if (n > e && n - e < cap && !strcasecmp(name + n - e, ext))
        {
            std::memcpy(stem, name, n - e);
            stem[n - e] = 0;
            return true;
        }
    }
    return false;
}
void save_ps2_cache()
{
    if (!storage_ok)
        return;
    static char text[ps2_cache_max * 170];
    std::size_t used = 0;
    for (int i = 0; i < ps2_cache_count && used + 170 < sizeof text; i++)
        used += static_cast<std::size_t>(
            std::snprintf(text + used, sizeof text - used, "%s|%s\n", ps2_cache_serial[i], ps2_cache_name[i]));
    text[used] = 0;
    write_file("/download0/ps2-serials.txt", text);
}
void load_ps2_cache()
{
    char *text = read_file("/download0/ps2-serials.txt");
    if (!text)
        return;
    char *line = text;
    while (*line && ps2_cache_count < ps2_cache_max)
    {
        char *end = std::strchr(line, '\n');
        if (end)
            *end = 0;
        char *bar = std::strchr(line, '|');
        if (bar && bar - line < 12 && std::strlen(bar + 1) < sizeof ps2_cache_name[0])
        {
            *bar = 0;
            std::snprintf(ps2_cache_serial[ps2_cache_count], sizeof ps2_cache_serial[0], "%s", line);
            std::snprintf(ps2_cache_name[ps2_cache_count], sizeof ps2_cache_name[0], "%s", bar + 1);
            ps2_cache_count++;
        }
        if (!end)
            break;
        line = end + 1;
    }
    std::free(text);
}
// Percent-encodes everything except the characters Elf Arsenal accepts in a path as they are.
void ps2_encode_path(const char *in, char *out, std::size_t cap)
{
    std::size_t o = 0;
    for (const char *p = in; *p && o + 4 < cap; p++)
    {
        const unsigned char ch = static_cast<unsigned char>(*p);
        if (std::isalnum(ch) || std::strchr("/._-()", ch))
            out[o++] = static_cast<char>(ch);
        else
            o += static_cast<std::size_t>(std::snprintf(out + o, cap - o, "%%%02X", ch));
    }
    out[o] = 0;
}
// Next image whose serial has not been read yet: fills `path` for the worker and returns true.
bool next_ps2_job(char *path, std::size_t cap)
{
    for (int i = 0; i < count; i++)
    {
        if (!ps2_file[i][0] || ps2_state[i] != 0)
            continue;
        char file[160], full[200];
        std::snprintf(full, sizeof full, "%s/%s", ps2_games_dir, ps2_file[i]);
        ps2_encode_path(full, file, sizeof file);
        if (std::strlen(file) + 24 >= cap)
        {
            ps2_state[i] = 2; // name too long to ask for
            continue;
        }
        ps2_job = i;
        std::snprintf(ps2_job_name, sizeof ps2_job_name, "%s", ps2_file[i]);
        std::snprintf(path, cap, "/api/fs/download?path=%s", file);
        return true;
    }
    return false;
}
void finish_ps2_job(const char *serial)
{
    int slot = -1;
    for (int i = 0; i < count; i++)
        if (!std::strcmp(ps2_file[i], ps2_job_name))
            slot = i;
    if (slot < 0)
        return;
    std::snprintf(ps2_serial[slot], sizeof ps2_serial[0], "%s", serial ? serial : "");
    ps2_state[slot] = serial ? 1 : 2;
    if (ps2_cache_count < ps2_cache_max)
    {
        std::snprintf(ps2_cache_name[ps2_cache_count], sizeof ps2_cache_name[0], "%s", ps2_job_name);
        std::snprintf(ps2_cache_serial[ps2_cache_count], sizeof ps2_cache_serial[0], "%s", serial ? serial : "-");
        ps2_cache_count++;
        save_ps2_cache();
    }
}
// Adds the PS2 images listed by Elf Arsenal (/api/fs/list) after the catalog.
void add_ps2_games(const char *json)
{
    for (int i = 0; i < count; i++)
        if (is_ps2_id(games[i].id))
        {
            // Drop images from the previous scan; the loop below adds the current ones.
            for (int j = i; j + 1 < count; j++)
            {
                games[j] = games[j + 1];
                std::memcpy(ps2_file[j], ps2_file[j + 1], sizeof ps2_file[j]);
                std::memcpy(ps2_serial[j], ps2_serial[j + 1], sizeof ps2_serial[j]);
                ps2_state[j] = ps2_state[j + 1];
            }
            count--;
            i--;
        }
    std::memset(ps2_file[count], 0, sizeof(ps2_file[0]) * static_cast<std::size_t>(max_games - count));
    std::memset(ps2_state + count, 0, static_cast<std::size_t>(max_games - count));
    std::memset(ps2_serial[count], 0, sizeof(ps2_serial[0]) * static_cast<std::size_t>(max_games - count));
    cJSON *root = cJSON_Parse(json);
    const cJSON *list = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *e = nullptr;
    cJSON_ArrayForEach(e, list)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(e, "name");
        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(e, "dir");
        char stem[144];
        if (count >= max_games || !cJSON_IsString(name) || cJSON_IsTrue(dir) ||
            std::strlen(name->valuestring) >= sizeof ps2_file[0] ||
            !ps2_image_name(name->valuestring, stem, sizeof stem))
            continue;
        unsigned hash = 2166136261u;
        for (const char *q = name->valuestring; *q; q++)
            hash = (hash ^ static_cast<unsigned char>(*q)) * 16777619u;
        char id[10];
        for (unsigned salt = 0;; salt++)
        {
            std::snprintf(id, sizeof id, "PS2%06X", (hash + salt) & 0xffffffu);
            bool taken = false;
            for (int i = 0; i < count; i++)
                taken = taken || !std::strcmp(games[i].id, id);
            if (!taken)
                break;
        }
        std::snprintf(games[count].id, sizeof games[count].id, "%s", id);
        std::snprintf(games[count].name, sizeof games[count].name, "%s", stem);
        std::snprintf(ps2_file[count], sizeof ps2_file[count], "%s", name->valuestring);
        for (int c = 0; c < ps2_cache_count; c++)
            if (!std::strcmp(ps2_cache_name[c], name->valuestring))
            {
                const bool known = std::strcmp(ps2_cache_serial[c], "-") != 0;
                ps2_state[count] = known ? 1 : 2;
                std::snprintf(ps2_serial[count], sizeof ps2_serial[0], "%s", known ? ps2_cache_serial[c] : "");
            }
        count++;
    }
    cJSON_Delete(root);
}
void poll_work()
{
    if (work.load(std::memory_order_acquire) != 2)
        return;
    int next = 0;
    char path[160]{};
    if (work_kind == 1)
    {
        static Game updated[max_games];
        int n = response_ok ? read_catalog(response, updated, max_games) : -1;
        if (n >= 0 && editing_collection >= 0)
        {
            msg("Finalize a seleção antes de atualizar a biblioteca.");
        }
        else if (n >= 0)
        {
            std::memcpy(games, updated, sizeof games);
            count = n;
            std::memset(ps2_file, 0, sizeof ps2_file);
            std::memset(ps2_state, 0, sizeof ps2_state);
            std::memset(selected, 0, sizeof selected);
            std::memset(played_last, 0, sizeof played_last);
            std::memset(played_seconds, 0, sizeof played_seconds);
            if (storage_ok)
                write_file("/download0/catalog.json", response);
            std::snprintf(connection, sizeof connection, "Biblioteca atualizada");
            msg("Seus jogos estão prontos para organizar.");
            rebuild();
            next = 6;
            std::snprintf(path, sizeof path, "/api/activity");
        }
        else
        {
            std::snprintf(connection, sizeof connection, "Usando biblioteca salva");
            msg("Elf Arsenal indisponível. Sua organização foi preservada.");
        }
    }
    else if (work_kind == 6)
    {
        if (response_ok && parse_activity())
        {
            activity_fetched = sceKernelGetProcessTime();
            rebuild();
        }
        next = 7;
        std::snprintf(path, sizeof path, "/api/fs/list?path=%s", ps2_games_dir);
    }
    else if (work_kind == 7)
    {
        if (response_ok && editing_collection < 0)
        {
            add_ps2_games(response);
            rebuild();
            if (next_ps2_job(path, sizeof path))
                next = 8;
        }
    }
    else if (work_kind == 8)
    {
        char serial[16];
        const bool found = response_ok && iso_boot_serial(reinterpret_cast<const unsigned char *>(response),
                                                          response_size, serial, sizeof serial);
        finish_ps2_job(found ? serial : nullptr);
        cover_version++;
        if (next_ps2_job(path, sizeof path))
            next = 8;
    }
    else if (work_kind == 3)
    {
        if (cover_ok)
        {
            int slot = 0;
            for (int i = 0; i < 32; i++)
            {
                if (!covers[i].ready)
                {
                    slot = i;
                    break;
                }
                if (covers[i].age < covers[slot].age)
                    slot = i;
            }
            auto &dest = covers[slot];
            std::snprintf(dest.id, 10, "%s", requested_cover);
            std::memcpy(dest.pixels, decoded_cover, sizeof decoded_cover);
            dest.ready = true;
            dest.age = ++cover_clock;
            dest.tint = average_pixels(decoded_cover);
            build_thumb(dest.thumb, dest.pixels);
#ifdef ESTANTE_GL
            if (gpu_canvas)
            {
                gpu_canvas->invalidate(dest.pixels);
                gpu_canvas->invalidate(dest.thumb);
            }
#endif
        }
        else if (failed_cover_count < max_games)
            std::snprintf(failed_covers[failed_cover_count++], 10, "%s", requested_cover);
        cover_version++;
    }
    else if (work_kind == 4)
    {
        char active[10]{};
        bool running = false;
        if (!response_ok || !parse_running(active, running))
        {
            launch_pending = false;
            msg("Não foi possível verificar o console. Tente novamente.");
        }
        else if (running && std::strcmp(active, "PPSA99841") && std::strcmp(active, launch_id))
        {
            launch_pending = false;
            msg("Outro jogo está aberto. Feche-o pelo menu do PS5.");
        }
        else if (running && !std::strcmp(active, launch_id))
        {
            launch_pending = false;
            msg("Esse jogo já está aberto no PS5.");
        }
        else
        {
            Library nextdb = db;
            auto *m = metadata(nextdb, launch_id);
            auto now = std::time(nullptr);
            if (m && now > 0)
            {
                m->lastChosen = static_cast<std::uint64_t>(now);
                if (storage_ok && persist(nextdb))
                    rebuild();
            }
            next = 2;
            std::snprintf(path, sizeof path, "/launch?titleId=%s", launch_id);
            msg("Abrindo o jogo…");
        }
    }
    else if (work_kind == 2)
    {
        if (response_ok)
        {
            verify_attempts = 0;
            verify_at = sceKernelGetProcessTime() + 1000000;
            msg("Pedido enviado. Aguardando confirmação do PS5…");
        }
        else
        {
            launch_pending = false;
            msg("O pedido falhou. Sua biblioteca continua intacta.");
        }
    }
    else if (work_kind == 5)
    {
        char active[10]{};
        bool running = false;
        if (response_ok && parse_running(active, running) && running &&
            !std::strcmp(active, launch_id))
        {
            launch_pending = false;
            verify_at = 0;
            msg("O PS5 confirmou a abertura do jogo.");
        }
        else if (++verify_attempts >= 4)
        {
            launch_pending = false;
            verify_at = 0;
            msg("Abertura não confirmada. Confira o jogo no menu do PS5.");
        }
        else
            verify_at = sceKernelGetProcessTime() + 1500000;
    }
    work.store(0);
    if (next)
        start_request(next, path);
    else if (pending_kind)
    {
        int kind = pending_kind;
        pending_kind = 0;
        start_request(kind, pending_path);
    }
}
// A PS2 image is not a title the console can start: pressing X opens PS5SX2 itself and the game is picked
// on its shelf.
void queue_ps2_launch()
{
    std::snprintf(launch_id, 10, "PPSA99203");
    launch_pending = true;
    verify_at = 0;
    msg("Verificando o console…");
    start_request(4, "/api/state");
}
void start_launch(int idx)
{
    if (idx < 0 || idx >= count || launch_pending)
        return;
    if (is_ps2_id(games[idx].id) && ps2_file[idx][0])
    {
        queue_ps2_launch();
        return;
    }
    std::snprintf(launch_id, 10, "%s", games[idx].id);
    launch_pending = true;
    verify_at = 0;
    msg("Verificando o console…");
    start_request(4, "/api/state");
}
void queue_launch(int idx)
{
    if (idx < 0 || idx >= count || launch_pending || anim_idx >= 0)
        return;
    if (!launch_anim_enabled)
    {
        start_launch(idx);
        return;
    }
    anim_idx = idx;
    anim_start = sceKernelGetProcessTime();
    ui_version++;
}
// Left stick as virtual d-pad presses: dominant axis only, with a dead zone around the centre.
std::uint32_t stick_directions(unsigned x, unsigned y)
{
    const int dx = static_cast<int>(x) - 128, dy = static_cast<int>(y) - 128;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax < 56 && ay < 56)
        return 0;
    if (ax >= ay)
        return dx < 0 ? LEFT : RIGHT;
    return dy < 0 ? UP : DOWN;
}
std::uint32_t buttons()
{
    auto now = sceKernelGetProcessTime();
    if (pad < 0 && now >= retry_pad)
    {
        retry_pad = now + 3000000;
        int user = 0;
        sceUserServiceInitialize(nullptr);
        if (sceUserServiceGetInitialUser(&user) == 0 && scePadInit() >= 0)
            pad = scePadOpen(user, 0, 0, nullptr);
        if (pad >= 0)
            log("CONTROLE ABERTO");
    }
    if (pad < 0)
        return 0;
    std::array<PadSample, 64> samples{};
    int n = scePadRead(pad, samples.data(), 64);
    if (n < 0)
    {
        pad = -1;
        held = 0;
        return 0;
    }
    if (n > 64)
        n = 64;
    std::uint32_t pressed = 0;
    for (int i = 0; i < n; i++)
    {
        const std::uint32_t current =
            samples[i].buttons | stick_directions(samples[i].analog[0], samples[i].analog[1]);
        pressed |= current & ~held;
        held = current;
    }
    if (pressed & (UP | DOWN | LEFT | RIGHT))
        repeat_at = now + 450000;
    else if ((held & (UP | DOWN | LEFT | RIGHT)) && now > repeat_at)
    {
        pressed |= held & (UP | DOWN | LEFT | RIGHT);
        repeat_at = now + 130000;
    }
    return pressed;
}

void erase_character()
{
    auto n = std::strlen(entry_name);
    if (!n)
        return;
    --n;
    while (n && (static_cast<unsigned char>(entry_name[n]) & 0xc0) == 0x80)
        --n;
    entry_name[n] = 0;
}
void clear_selection()
{
    std::memset(selected, 0, sizeof selected);
    ui_version++;
}
void set_screen(int next)
{
    if (next == 0 && count > 0 && work.load() == 0 && !launch_pending && !pending_kind &&
        (!activity_fetched || sceKernelGetProcessTime() - activity_fetched > 20000000ull))
        start_request(6, "/api/activity");
    screen = next;
    nav_focus = false;
    modal = 0;
    details_index = -1;
    ui_version++;
}
void open_library(int view)
{
    query = {};
    query.view = view;
    cursor = 0;
    filtered_count = 0;
    clear_selection();
    set_screen(1);
    rebuild();
}
void open_collection(int slot)
{
    collection_parent = slot;
    collection_cursor = 0;
    set_screen(2);
    rebuild();
}
void open_keyboard(int kind, int slot = -1, int parent = -1)
{
    keyboard_kind = kind;
    edit_slot = slot;
    new_parent = parent;
    keyboard_cursor = 0;
    std::snprintf(entry_name, sizeof entry_name, "%s",
                  kind == 1   ? query.text
                  : slot >= 0 ? db.folders[slot].name
                              : "");
    modal = 3;
}
void submit_name()
{
    if (keyboard_kind == 1)
    {
        std::snprintf(query.text, sizeof query.text, "%s", entry_name);
        cursor = 0;
        filtered_count = 0;
        modal = 0;
        rebuild();
        return;
    }
    if (!entry_name[0])
        return;
    Library next = db;
    int slot = edit_slot;
    if (slot < 0)
        for (int i = 0; i < max_folders; i++)
            if (!next.folders[i].used)
            {
                slot = i;
                break;
            }
    if (slot < 0)
    {
        msg("Limite de 32 coleções atingido.");
        modal = 0;
        return;
    }
    auto &f = next.folders[slot];
    f.used = true;
    if (edit_slot < 0)
        f.parent = new_parent;
    std::snprintf(f.name, sizeof f.name, "%.39s", entry_name);
    if (keyboard_kind == 4)
    {
        f.smart = true;
        f.rulePlatform = query.platform;
        f.ruleStatus = query.status;
        f.ruleFavorite = query.favorites;
    }
    if (persist(next))
    {
        modal = 0;
        rebuild();
        if (edit_slot < 0)
            open_collection(slot);
    }
}
enum Action
{
    Details,
    Add,
    FavoriteOn,
    FavoriteOff,
    PinOn,
    PinOff,
    Status,
    Hide,
    Show,
    Remove,
    Classify,
    ApplySelection,
    ClearSelection,
    NewFolder,
    NewChild,
    Rename,
    Delete,
    PinFolder,
    EditMembers,
    CollectionSort,
    CollectionChildren,
    Search,
    ClearSearch,
    Filter,
    Sort,
    SmartFolder,
    Refresh,
    Restore,
    Record,
    TextSize,
    Pick,
    Export,
    Language,
    Animation,
    RestoreConfirm
};
struct MenuItem
{
    Action action;
    const char *text;
};
MenuItem menu[20]{};
int menu_count = 0;
void add_menu(Action a, const char *s)
{
    menu[menu_count++] = {a, s};
}
void clear_search()
{
    query.text[0] = 0;
    cursor = 0;
    filtered_count = 0;
    rebuild();
}
void open_menu(int kind)
{
    menu_kind = kind;
    menu_count = 0;
    menu_cursor = 0;
    modal = 1;
    if (kind == 0)
    {
        add_menu(Add, "Adicionar a uma coleção");
        const auto *m = meta(target_index());
        const bool batch = screen == 1 && chosen_count() > 0;
        if (batch || !m || !m->favorite)
            add_menu(FavoriteOn, "Marcar como favorito");
        if (batch || (m && m->favorite))
            add_menu(FavoriteOff, "Remover dos favoritos");
        if (batch || !m || !m->pinned)
            add_menu(PinOn, "Fixar no Início");
        if (batch || (m && m->pinned))
            add_menu(PinOff, "Desafixar do Início");
        if (!batch)
            add_menu(Details, "Detalhes do jogo");
        add_menu(Status, "Meu progresso");
        add_menu(Classify, "Classificar plataforma");
        add_menu(screen == 1 && query.view == 7 ? Show : Hide, screen == 1 && query.view == 7
                                                                   ? "Mostrar na biblioteca"
                                                                   : "Ocultar da biblioteca");
        if (screen == 1 && query.view >= 100 && !db.folders[query.view - 100].smart)
            add_menu(Remove, "Remover desta coleção");
    }
    else if (kind == 1)
    {
        if (editing_collection >= 0)
            add_menu(ApplySelection, "Salvar seleção nesta coleção");
        add_menu(Search, "Buscar jogo");
        if (query.text[0])
            add_menu(ClearSearch, "Limpar busca");
        add_menu(Filter, "Filtrar biblioteca");
        add_menu(Sort, "Ordenar");
        add_menu(NewFolder, "Criar coleção");
        add_menu(SmartFolder, "Criar coleção automática pelos filtros");
        add_menu(ClearSelection, "Limpar seleção");
        add_menu(Refresh, "Atualizar biblioteca");
        if (query.view >= 100)
            add_menu(CollectionChildren, query.descendants ? "Mostrar somente jogos desta coleção"
                                                           : "Incluir jogos das subcoleções");
        add_menu(Pick, "Escolher um jogo desta seleção");
    }
    else if (kind == 2)
    {
        add_menu(NewFolder, "Criar coleção");
        if (folder_menu_slot >= 0)
        {
            add_menu(NewChild, "Criar subcoleção");
            add_menu(Rename, "Renomear coleção");
            add_menu(PinFolder, db.folders[folder_menu_slot].pinned ? "Desafixar do Início"
                                                                    : "Fixar no Início");
            if (!db.folders[folder_menu_slot].smart)
                add_menu(EditMembers, "Escolher jogos desta coleção");
            add_menu(CollectionSort, "Ordenação da coleção");
            add_menu(Delete, "Excluir coleção");
        }
    }
}
void apply_members()
{
    if (editing_collection < 0)
        return;
    Library next = db;
    auto mask = std::uint32_t(1) << editing_collection;
    for (int i = 0; i < count; i++)
    {
        Meta *m = metadata(next, games[i].id);
        if (m)
        {
            if (selected[i])
                m->folders |= mask;
            else
                m->folders &= ~mask;
        }
    }
    if (persist(next))
    {
        int slot = editing_collection;
        editing_collection = -1;
        clear_selection();
        modal = 0;
        open_collection(slot);
    }
}
void action(Action a)
{
    modal = 0;
    switch (a)
    {
    case Details:
        details_index = target_index();
        if (details_index >= 0)
            modal = 5;
        break;
    case Add:
        modal = 2;
        menu_cursor = 0;
        break;
    case FavoriteOn:
        update_targets([](Meta &m) { m.favorite = true; });
        break;
    case FavoriteOff:
        update_targets([](Meta &m) { m.favorite = false; });
        break;
    case PinOn:
        update_targets([](Meta &m) { m.pinned = true; });
        break;
    case PinOff:
        update_targets([](Meta &m) { m.pinned = false; });
        break;
    case Status:
        modal = 6;
        menu_cursor = 0;
        break;
    case Classify:
        modal = 7;
        menu_cursor = 0;
        break;
    case Hide:
        update_targets([](Meta &m) { m.hidden = true; });
        clear_selection();
        break;
    case Show:
        update_targets([](Meta &m) { m.hidden = false; });
        clear_selection();
        break;
    case Remove:
        update_targets([](Meta &m) { m.folders &= ~(std::uint32_t(1) << (query.view - 100)); });
        break;
    case ApplySelection:
        apply_members();
        break;
    case ClearSelection:
        clear_selection();
        break;
    case Search:
        open_keyboard(1);
        break;
    case ClearSearch:
        clear_search();
        break;
    case Language:
        i18n::language = !i18n::language;
        ui_version++;
        save_preferences();
        break;
    case Animation:
        launch_anim_enabled = !launch_anim_enabled;
        ui_version++;
        save_preferences();
        break;
    case NewFolder:
        open_keyboard(0);
        break;
    case NewChild:
        open_keyboard(0, -1, folder_menu_slot);
        break;
    case Rename:
        open_keyboard(0, folder_menu_slot);
        break;
    case Delete:
        modal = 4;
        break;
    case PinFolder:
    {
        Library next = db;
        next.folders[folder_menu_slot].pinned = !next.folders[folder_menu_slot].pinned;
        if (persist(next))
            rebuild();
        break;
    }
    case EditMembers:
        editing_collection = folder_menu_slot;
        open_library(0);
        for (int i = 0; i < count; i++)
            selected[i] = collection_contains(db, games[i], editing_collection);
        msg("Marque os jogos. Options para salvar a seleção.");
        break;
    case Sort:
        modal = 8;
        menu_cursor = query.sort;
        menu_kind = 0;
        break;
    case CollectionSort:
        modal = 8;
        menu_cursor = db.folders[folder_menu_slot].sort;
        menu_kind = 1;
        break;
    case CollectionChildren:
        query.descendants = !query.descendants;
        rebuild();
        break;
    case Filter:
        modal = 9;
        menu_cursor = 0;
        break;
    case SmartFolder:
        if (query.view != 0 || query.text[0])
        {
            msg("Para salvar filtros automáticos, use Todos os jogos e limpe a busca.");
            break;
        }
        open_keyboard(4);
        break;
    case Refresh:
        failed_cover_count = 0;
        start_request(1, "/appdb");
        break;
    case Restore:
        modal = 10;
        break;
    case RestoreConfirm:
    {
        char *saved = read_file("/download0/estante-backup.json");
        Library restored{};
        if (!saved || !read_library(saved, restored))
        {
            msg("Não há um backup válido para restaurar.");
        }
        else if (persist(restored))
        {
            clear_selection();
            rebuild();
            msg("Organização restaurada.");
        }
        std::free(saved);
        break;
    }
    case Record:
        record_mode = !record_mode;
        save_preferences();
        break;
    case TextSize:
        large_text = !large_text;
        save_preferences();
        break;
    case Pick:
        if (filtered_count)
        {
            cursor = static_cast<int>(sceKernelGetProcessTime() % filtered_count);
            details_index = filtered[cursor];
            modal = 5;
        }
        break;
    case Export:
    {
        char *saved = write_library(db);
        if (saved)
        {
            bool ok = write_file("/download0/estante-export.json", saved);
            cJSON_free(saved);
            msg(ok ? "Cópia da organização exportada no armazenamento do app."
                   : "Não foi possível exportar a organização.");
        }
        break;
    }
    }
    ui_version++;
}
void open_filters()
{
    nav_cursor = 0;
    for (int i = 0; i < 12; i++)
        if (view_order[i] == query.view)
            nav_cursor = i;
    nav_focus = true;
}
void input(std::uint32_t b)
{
    if (!b)
        return;
    ui_version++;
    if (anim_idx >= 0)
    {
        if (b & CIRCLE)
            anim_idx = -1; // cancels the launch
        return;
    }
    if (b & R3)
    {
        action(Record);
        return;
    }
    if (modal == 3)
    {
        int total = key_count + 2;
        if (b & LEFT)
            keyboard_cursor = (keyboard_cursor + total - 1) % total;
        if (b & RIGHT)
            keyboard_cursor = (keyboard_cursor + 1) % total;
        if (b & UP)
            keyboard_cursor = (keyboard_cursor + total - 10) % total;
        if (b & DOWN)
            keyboard_cursor = (keyboard_cursor + 10) % total;
        if (b & SQUARE)
        {
            erase_character();
        }
        if (b & CIRCLE)
        {
            modal = 0;
            return;
        }
        if (b & OPTIONS)
        {
            submit_name();
            return;
        }
        if (b & CROSS)
        {
            if (keyboard_cursor == key_count)
                submit_name();
            else if (keyboard_cursor == key_count + 1)
            {
                erase_character();
            }
            else
            {
                auto n = std::strlen(entry_name);
                unsigned limit = keyboard_kind == 1 ? 79 : 39;
                if (n < limit)
                {
                    entry_name[n] = keys[keyboard_cursor];
                    entry_name[n + 1] = 0;
                }
            }
        }
        return;
    }
    if (modal == 4 || modal == 10)
    {
        int which = modal;
        if (b & CIRCLE)
            modal = 0;
        if (b & CROSS)
        {
            modal = 0;
            if (which == 10)
                action(RestoreConfirm);
            else
            {
                Library next = db;
                if (delete_folder(next, folder_menu_slot) && persist(next))
                {
                    collection_parent = -1;
                    rebuild();
                }
            }
        }
        return;
    }
    if (modal == 5)
    {
        if (b & CIRCLE)
        {
            modal = 0;
            details_index = -1;
        }
        else if (b & CROSS)
        {
            queue_launch(details_index);
            modal = 0;
        }
        else if (b & TRIANGLE)
        {
            int idx = details_index;
            modal = 0;
            if (screen == 0)
            {
                for (int i = 0; i < home_count; i++)
                    if (home_games[i] == idx)
                        home_cursor = i;
            }
            open_menu(0);
        }
        return;
    }
    if (modal == 1)
    {
        if (b & CIRCLE)
        {
            modal = 0;
            return;
        }
        if (b & UP)
            menu_cursor = (menu_cursor + menu_count - 1) % menu_count;
        if (b & DOWN)
            menu_cursor = (menu_cursor + 1) % menu_count;
        if (b & CROSS)
            action(menu[menu_cursor].action);
        return;
    }
    if (modal == 2)
    {
        int slots[32], n = 0;
        for (int i = 0; i < 32; i++)
            if (db.folders[i].used && !db.folders[i].smart)
                slots[n++] = i;
        if (b & CIRCLE)
        {
            modal = 0;
            return;
        }
        if (!n)
            return;
        if (b & UP)
            menu_cursor = (menu_cursor + n - 1) % n;
        if (b & DOWN)
            menu_cursor = (menu_cursor + 1) % n;
        if (b & CROSS)
        {
            int slot = slots[menu_cursor];
            update_targets([slot](Meta &m) { m.folders |= std::uint32_t(1) << slot; });
            modal = 0;
        }
        return;
    }
    if (modal >= 6 && modal <= 8)
    {
        int n = modal == 7 ? 4 : 4;
        if (b & CIRCLE)
        {
            modal = 0;
            return;
        }
        if (b & UP)
            menu_cursor = (menu_cursor + n - 1) % n;
        if (b & DOWN)
            menu_cursor = (menu_cursor + 1) % n;
        if (b & CROSS)
        {
            int mode = modal;
            modal = 0;
            if (mode == 6)
            {
                int value = menu_cursor;
                update_targets([value](Meta &m) { m.status = value; });
            }
            else if (mode == 7)
            {
                int value = menu_cursor == 0 ? 2 : menu_cursor == 1 ? 4 : menu_cursor == 2 ? 5 : 1;
                update_targets([value](Meta &m) { m.platform = value; });
            }
            else if (menu_kind == 1)
            {
                Library next = db;
                next.folders[folder_menu_slot].sort = menu_cursor;
                if (persist(next))
                    rebuild();
            }
            else
            {
                query.sort = menu_cursor;
                if (query.view >= 100)
                {
                    Library next = db;
                    next.folders[query.view - 100].sort = menu_cursor;
                    persist(next);
                }
                rebuild();
            }
        }
        return;
    }
    if (modal == 9)
    {
        if (b & CIRCLE)
        {
            modal = 0;
            return;
        }
        if (b & UP)
            menu_cursor = (menu_cursor + 4) % 5;
        if (b & DOWN)
            menu_cursor = (menu_cursor + 1) % 5;
        if (b & CROSS)
        {
            if (menu_cursor == 0)
            {
                int p = query.platform;
                query.platform = p == 0 ? 2 : p == 2 ? 4 : p == 4 ? 5 : 0;
            }
            if (menu_cursor == 1)
                query.status = (query.status + 1) % 4;
            if (menu_cursor == 2)
                query.favorites = !query.favorites;
            if (menu_cursor == 3)
            {
                query.platform = 0;
                query.status = 0;
                query.favorites = false;
                query.text[0] = 0;
            }
            if (menu_cursor == 4)
                modal = 0;
            cursor = 0;
            filtered_count = 0;
            rebuild();
        }
        return;
    }
    if (editing_collection < 0)
    {
        if (b & L1)
        {
            set_screen((screen + 3) % 4);
            return;
        }
        if (b & R1)
        {
            set_screen((screen + 1) % 4);
            return;
        }
    }
    if (screen == 0)
    {
        if (b & SQUARE)
        {
            open_library(0);
            open_keyboard(1);
            return;
        }
        if (home_row == 1 && (b & TRIANGLE) && home_folder_cursor < home_folder_count)
        {
            folder_menu_slot = home_folders[home_folder_cursor];
            open_menu(2);
            return;
        }
        if (b & OPTIONS)
        {
            open_library(0);
            return;
        }
        home_row = 0; // the collections row was removed from Inicio; R1 opens Biblioteca
        if (home_row == 0)
        {
            if ((b & LEFT) && home_cursor > 0)
                home_cursor--;
            if ((b & RIGHT) && home_cursor + 1 < home_count)
                home_cursor++;
            if ((b & CROSS) && home_count)
            {
                queue_launch(home_games[home_cursor]);
            }
            if ((b & TRIANGLE) && home_count)
                open_menu(0);
            if (b & OPTIONS)
                open_library(0);
        }
        else
        {
            if ((b & LEFT) && home_folder_cursor > 0)
                home_folder_cursor--;
            if ((b & RIGHT) && home_folder_cursor < home_folder_count)
                home_folder_cursor++;
            if (b & CROSS)
            {
                if (home_folder_cursor == home_folder_count)
                    open_library(0);
                else
                    open_collection(home_folders[home_folder_cursor]);
            }
        }
        return;
    }
    if (screen == 1)
    {
        if (b & TOUCH)
        {
            if (nav_focus)
                nav_focus = false;
            else
                open_filters();
            return;
        }
        if (b & OPTIONS)
        {
            open_menu(1);
            return;
        }
        if (b & TRIANGLE)
        {
            if (filtered_count)
                open_menu(0);
            return;
        }
        if (b & CIRCLE)
        {
            if (editing_collection >= 0)
            {
                editing_collection = -1;
                clear_selection();
                open_collection(collection_parent);
            }
            else if (nav_focus)
                nav_focus = false;
            else if (query.text[0])
                clear_search();
            else
                open_filters();
            return;
        }
        if (nav_focus)
        {
            if (b & UP)
                nav_cursor = (nav_cursor + 11) % 12;
            if (b & DOWN)
                nav_cursor = (nav_cursor + 1) % 12;
            if (b & (RIGHT | CROSS))
            {
                query.view = view_order[nav_cursor];
                cursor = 0;
                filtered_count = 0;
                nav_focus = false;
                if (editing_collection < 0)
                    clear_selection();
                rebuild();
            }
            return;
        }
        if (!filtered_count)
        {
            if (b & LEFT)
                open_filters();
            return;
        }
        if (b & LEFT)
        {
            if (cursor % 6 == 0)
                open_filters();
            else
                cursor--;
        }
        if ((b & RIGHT) && cursor % 6 < 5 && cursor + 1 < filtered_count)
            cursor++;
        if (b & UP)
            cursor = cursor >= 6 ? cursor - 6 : cursor;
        if (b & DOWN)
            cursor = cursor + 6 < filtered_count ? cursor + 6 : cursor;
        if (b & SQUARE)
            selected[filtered[cursor]] = !selected[filtered[cursor]];
        if (b & CROSS)
        {
            if (editing_collection >= 0)
                selected[filtered[cursor]] = !selected[filtered[cursor]];
            else
            {
                queue_launch(filtered[cursor]);
            }
        }
        return;
    }
    if (screen == 2)
    {
        if (b & CIRCLE)
        {
            if (collection_parent >= 0)
            {
                collection_parent = db.folders[collection_parent].parent;
                collection_cursor = 0;
                rebuild();
            }
            else
                set_screen(0);
            return;
        }
        if ((b & LEFT) && collection_cursor > 0)
            collection_cursor--;
        if ((b & RIGHT) && collection_cursor + 1 < collection_count)
            collection_cursor++;
        if ((b & UP) && collection_cursor >= 3)
            collection_cursor -= 3;
        if ((b & DOWN) && collection_cursor + 3 < collection_count)
            collection_cursor += 3;
        int entry = collection_entries[collection_cursor];
        if (b & (OPTIONS | TRIANGLE))
        {
            folder_menu_slot = entry >= 0 ? entry : collection_parent;
            open_menu(2);
            return;
        }
        if (b & CROSS)
        {
            if (entry == -2)
                open_keyboard(0, -1, collection_parent);
            else if (entry == -1)
                open_library(100 + collection_parent);
            else
                open_collection(entry);
        }
        return;
    }
    if (screen == 3)
    {
        if (b & UP)
            settings_cursor = (settings_cursor + 7) % 8;
        if (b & DOWN)
            settings_cursor = (settings_cursor + 1) % 8;
        if (b & CIRCLE)
            set_screen(0);
        if (b & CROSS)
        {
            Action actions[] = {TextSize, Record, Refresh, Export, Restore, NewFolder, Language, Animation};
            action(actions[settings_cursor]);
        }
    }
}

Color shade(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Color>(0xff000000u | (b << 16) | (g << 8) | r);
}
void text(Canvas &c, unsigned x, unsigned y, const char *s, unsigned size = 28, Color color = white,
          unsigned width = 1700)
{
    if (large_text && size < 32)
        size = 32;
    c.label(x, y, tr(s), size, color, width);
}
void line(Canvas &c, unsigned x, unsigned y, unsigned width)
{
    c.rectangle(x, y, width, 1, shade(56, 65, 77));
}
void pill(Canvas &c, unsigned x, unsigned y, unsigned width, const char *label, bool active = false)
{
    c.rounded_rectangle(x, y, width, 44, 12, active ? accent : panel);
    text(c, x + 16, y + 3, label, 24, active ? bg : muted, width - 24);
}
int collection_size(int slot)
{
    int n = 0;
    for (int i = 0; i < count; i++)
    {
        auto *m = meta(i);
        if ((!m || !m->hidden) && collection_contains(db, games[i], slot, true))
            n++;
    }
    return n;
}
Color mix_color(Color a, Color b, unsigned weight_a)
{
    const auto ca = static_cast<std::uint32_t>(a), cb = static_cast<std::uint32_t>(b);
    std::uint32_t out = 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
    {
        const std::uint32_t va = (ca >> shift) & 255U, vb = (cb >> shift) & 255U;
        out |= ((va * weight_a + vb * (255U - weight_a)) / 255U) << shift;
    }
    return static_cast<Color>(out);
}
Color brighten(Color color)
{
    return mix_color(color, white, 150);
}
void artwork(Canvas &c, int idx, unsigned x, unsigned y, unsigned edge, bool focus = false)
{
    const unsigned radius = edge > 200 ? 20 : edge > 100 ? 14 : 8;
    const Cover *cover = idx >= 0 && idx < count ? find_cover(games[idx].id) : nullptr;
    if (edge > 90)
        c.shadow(x, y, edge, edge, focus ? 30 : 18, focus ? 170 : 120);
    if (focus)
    {
        const Color glow = cover ? mix_color(brighten(static_cast<Color>(cover->tint)), bg, 150)
                                 : mix_color(accent, bg, 120);
        c.rounded_rectangle(x - 11, y - 11, edge + 22, edge + 22, radius + 11, glow);
        c.rounded_rectangle(x - 4, y - 4, edge + 8, edge + 8, radius + 4, white);
    }
    if (cover)
    {
        const_cast<Cover *>(cover)->age = ++cover_clock;
        if (edge == thumb_edge)
            c.rounded_picture(x, y, edge, edge, radius, thumb_edge, cover->thumb);
        else
            c.rounded_picture(x, y, edge, edge, radius, cover_edge, cover->pixels);
        return;
    }
    c.rounded_rectangle(x, y, edge, edge, radius, shade(38, 48, 59));
    unsigned mx = x + edge / 3, my = y + edge / 3, w = edge / 3;
    c.rectangle(mx, my, w, 4, muted);
    c.rectangle(mx, my + edge / 8, w * 3 / 4, 4, muted);
    c.rectangle(mx, my + edge / 4, w, 4, muted);
    if (edge > 180 && idx >= 0)
        text(c, x + 20, y + edge - 55, is_ps2_id(games[idx].id) ? games[idx].name : games[idx].id, 24, muted,
             edge - 40);
}
const Cover *focus_cover()
{
    int idx = -1;
    if (screen == 0 && home_row == 0 && home_count)
        idx = home_games[home_cursor];
    else if (screen == 1 && !nav_focus && filtered_count)
        idx = filtered[cursor];
    return idx >= 0 && idx < count ? find_cover(games[idx].id) : nullptr;
}
int first_game_in(int slot)
{
    for (int i = 0; i < count; i++)
    {
        auto *m = meta(i);
        if ((!m || !m->hidden) && collection_contains(db, games[i], slot, true))
            return i;
    }
    return -1;
}
void collection_card(Canvas &c, int slot, unsigned x, unsigned y, unsigned width, unsigned height,
                     bool focus)
{
    c.shadow(x, y, width, height, focus ? 26 : 16, focus ? 150 : 100);
    if (focus)
        c.rounded_rectangle(x - 4, y - 4, width + 8, height + 8, 18, white);
    c.rounded_rectangle(x, y, width, height, 14, panel);
    int indices[4]{}, n = 0;
    for (int i = 0; i < count && n < 4; i++)
    {
        auto *m = meta(i);
        if ((!m || !m->hidden) && collection_contains(db, games[i], slot, true))
            indices[n++] = i;
    }
    unsigned tile = height > 160 ? 86 : 40;
    for (unsigned a = 0; a < 4; a++)
        artwork(c, a < static_cast<unsigned>(n) ? indices[a] : -1, x + 16 + (a % 2) * (tile + 4),
                y + 16 + (a / 2) * (tile + 4), tile);
    unsigned tx = x + tile * 2 + 42;
    text(c, tx, y + 20, db.folders[slot].name, 32, white, width - (tx - x) - 16);
    char detail[90];
    std::snprintf(detail, sizeof detail, tr("%d jogos%s"), collection_size(slot),
                  db.folders[slot].smart ? tr(" • Automática") : "");
    text(c, tx, y + 68, detail, 24, muted, width - (tx - x) - 16);
    if (height > 160)
        text(c, tx, y + 125, db.folders[slot].pinned ? "Fixada no Início" : "Sua coleção", 24,
             accent, width - (tx - x) - 16);
}
void breadcrumb(char *out, std::size_t cap, int slot)
{
    int path[32], n = 0;
    while (slot >= 0 && n < 32)
    {
        path[n++] = slot;
        slot = db.folders[slot].parent;
    }
    std::snprintf(out, cap, "%s", tr("Coleções"));
    for (int i = n - 1; i >= 0; i--)
    {
        std::size_t len = std::strlen(out);
        if (len + 4 >= cap)
            break;
        std::snprintf(out + len, cap - len, " / %s", db.folders[path[i]].name);
    }
}
void draw_top(Canvas &c)
{
    text(c, 64, 28, "estante", 44, white, 260);
    text(c, 265, 54, "0.4", 24, muted, 130);
    const char *tabs[] = {"Início", "Biblioteca", "Coleções", "Ajustes"};
    unsigned xs[] = {430, 630, 890, 1130};
    for (int i = 0; i < 4; i++)
    {
        text(c, xs[i], 42, tabs[i], 28, screen == i ? white : muted, 220);
        if (screen == i)
            c.rounded_rectangle(xs[i], 87, i == 1 ? 140 : 100, 4, 2, accent);
    }
    text(c, 1590, 43, "@kndofc", 28, accent, 230);
    line(c, 64, 110, 1792);
}
void format_ago(std::uint64_t last, char *out, std::size_t cap)
{
    out[0] = 0;
    if (!last || !activity_now || activity_now < last)
        return;
    const std::uint64_t age = activity_now - last;
    if (age < 120)
        std::snprintf(out, cap, "%s", tr("jogado agora há pouco"));
    else if (age < 3600)
        std::snprintf(out, cap, tr("jogado há %u min"), static_cast<unsigned>(age / 60));
    else if (age < 86400)
        std::snprintf(out, cap, tr("jogado há %u h"), static_cast<unsigned>(age / 3600));
    else
        std::snprintf(out, cap, tr("jogado há %u dias"), static_cast<unsigned>(age / 86400));
}
void format_playtime(std::uint32_t seconds, char *out, std::size_t cap)
{
    out[0] = 0;
    if (seconds < 60)
        return;
    if (seconds < 3600)
        std::snprintf(out, cap, tr("%u min de jogo"), seconds / 60);
    else
    {
        const unsigned hours = seconds / 3600, minutes = (seconds % 3600) / 60;
        if (hours >= 10 || !minutes)
            std::snprintf(out, cap, tr("%u h de jogo"), hours);
        else
            std::snprintf(out, cap, tr("%u h %u min de jogo"), hours, minutes);
    }
}
void centered(Canvas &c, unsigned y, const char *s, unsigned size, Color color, unsigned max_width)
{
    s = tr(s);
    unsigned width = c.measure(s, size);
    if (width > max_width)
        width = max_width;
    text(c, (1920 - width) / 2, y, s, size, color, max_width);
}
void draw_home(Canvas &c)
{
    const bool recents =
        home_count && (played_last[home_games[0]] > 0 || (meta(home_games[0]) && meta(home_games[0])->lastChosen));
    text(c, 76, 150, recents ? "Jogos recentes" : "Seus jogos", 44, white, 1000);
    char position[80];
    std::snprintf(position, sizeof position, "%d / %d", home_count ? home_cursor + 1 : 0,
                  home_count);
    text(c, 1650, 164, position, 28, muted, 190);
    if (home_count)
    {
        // Cover flow: the focused game is large and centred, neighbours shrink to each side.
        static constexpr int offsets[] = {-2, 2, -1, 1, 0};
        for (int offset : offsets)
        {
            const int i = home_cursor + offset;
            if (i < 0 || i >= home_count)
                continue;
            const int distance = offset < 0 ? -offset : offset;
            const unsigned edge = distance == 0 ? 470 : distance == 1 ? 340 : 250;
            const unsigned x = offset == -2   ? 70
                               : offset == -1 ? 350
                               : offset == 0  ? 725
                               : offset == 1  ? 1230
                                              : 1600;
            artwork(c, home_games[i], x, 500 - edge / 2, edge, home_row == 0 && offset == 0);
        }
        const int idx = home_games[home_cursor];
        const auto *m = meta(idx);
        centered(c, 772, games[idx].name, 44, white, 1500);
        char detail[220], ago[48], played[48];
        format_ago(played_last[idx], ago, sizeof ago);
        format_playtime(played_seconds[idx], played, sizeof played);
        const int p = platform(games[idx], db);
        std::snprintf(detail, sizeof detail, "%s%s%s%s%s%s%s%s",
                      p == 2   ? "PlayStation 2"
                      : p == 4 ? "PlayStation 4"
                      : p == 5 ? "PlayStation 5"
                               : tr("Aplicativo"),
                      m && m->pinned ? tr("  ·  Fixado") : "", m && m->status ? "  ·  " : "",
                      m && m->status ? tr(statuses[m->status]) : "", ago[0] ? "  ·  " : "", ago,
                      played[0] ? "  ·  " : "", played);
        centered(c, 840, detail, 28, muted, 1500);
    }
    else
    {
        text(c, 78, 324, "Sua biblioteca está vazia", 44, white, 1450);
        text(c, 80, 400, "Atualize os jogos em Biblioteca > Options.", 28, muted, 1450);
    }
}
void draw_library(Canvas &c)
{
    const char *name = query.view >= 100 ? db.folders[query.view - 100].name : views[query.view];
    text(c, 76, 145, name, 44, white, 1250);
    char detail[200];
    std::snprintf(detail, sizeof detail, "%d %s  ·  %s%s%s", filtered_count, query.view == 4 ? "apps" : tr("jogos"), tr(sorts[query.sort]),
                  query.text[0] ? "  ·  " : "", query.text);
    text(c, 78, 207, detail, 24, muted, 1600);
    const int start = (cursor / 12) * 12;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 12 && start + i < filtered_count; i++)
        {
            const int pos = start + i, idx = filtered[pos];
            const bool current = !nav_focus && cursor == pos;
            if (current != (pass == 1))
                continue;
            const unsigned x = 80 + (i % 6) * 296, y = 270 + (i / 6) * 314;
            if (current)
                artwork(c, idx, x - 10, y - 10, 284, true);
            else
                artwork(c, idx, x, y, 264, false);
            if (selected[idx])
                pill(c, current ? x + 182 : x + 172, current ? y + 2 : y + 12, 78, "OK", true);
        }
    if (filtered_count)
        text(c, 78, 908, games[filtered[cursor]].name, 32, white, 1470);
    else
    {
        text(c, 80, 400, "Nenhum jogo nesta seleção", 44, white, 1500);
        text(c, 80, 475, "Options: buscar, filtrar ou atualizar.", 28, muted, 1500);
    }
    if (chosen_count() || editing_collection >= 0)
    {
        char status[120];
        std::snprintf(status, sizeof status, tr("%d selecionados%s"), chosen_count(),
                      editing_collection >= 0 ? tr("  ·  Options para salvar") : "");
        text(c, 78, 962, status, 24, accent, 1700);
    }
    if (nav_focus)
    {
        c.dim();
        c.rectangle(32, 126, 470, 864, panel);
        text(c, 60, 150, "Filtrar biblioteca", 32, white, 410);
        for (int i = 0; i < 12; i++)
        {
            unsigned y = 220 + i * 59;
            bool focus = nav_cursor == i;
            if (focus)
                c.rounded_rectangle(48, y - 3, 438, 51, 6, accent);
            text(c, 66, y, views[view_order[i]], 28, focus ? bg : white, 405);
        }
    }
}
void draw_collections(Canvas &c)
{
    char path[300];
    breadcrumb(path, sizeof path, collection_parent);
    text(c, 68, 145, path, 44, white, 1550);
    text(c, 72, 213, "Organize histórias, fases e jogos para cada ocasião.", 28, muted, 1500);
    int start = (collection_cursor / 6) * 6;
    for (int i = 0; i < 6 && start + i < collection_count; i++)
    {
        unsigned x = 76 + (i % 3) * 594, y = 300 + (i / 3) * 300;
        int entry = collection_entries[start + i];
        bool focus = start + i == collection_cursor;
        if (entry >= 0)
            collection_card(c, entry, x, y, 550, 240, focus);
        else
        {
            if (focus)
                c.rounded_rectangle(x - 5, y - 5, 560, 250, 16, accent);
            c.rounded_rectangle(x, y, 550, 240, 12, panel);
            text(c, x + 28, y + 43, entry == -1 ? "Abrir os jogos >" : "+ Nova coleção", 44, white,
                 485);
            text(c, x + 30, y + 128,
                 entry == -1 ? "Ver esta seleção na Biblioteca" : "Dê um lugar aos seus favoritos.",
                 28, muted, 480);
        }
    }
    if (collection_parent >= 0)
        text(c, 74, 929,
             db.folders[collection_parent].smart
                 ? "Coleção automática: os jogos acompanham seus filtros."
                 : "Options: escolher jogos, renomear e organizar.",
             28, muted, 1600);
}
void draw_settings(Canvas &c)
{
    text(c, 72, 150, "Do seu jeito", 64, white, 1250);
    text(c, 76, 238, "Preferências e cuidado com a sua biblioteca.", 28, muted, 1300);
    const char *names[] = {
        "Texto maior",          "Modo gravação",           "Atualizar biblioteca",
        "Exportar organização", "Restaurar último backup", "Criar coleção",
        "Idioma / Language",    "Animação ao abrir"};
    const char *desc[] = {"Aumenta os textos menores da interface.",
                          "Esconde dicas e mensagens para capturas.",
                          "Consulta os jogos pelo Elf Arsenal local.",
                          "Salva uma cópia independente no armazenamento do app.",
                          "Recupera a organização anterior. Os jogos não são alterados.",
                          "Comece uma seleção pessoal de jogos.",
                          "Alterna entre português e inglês.",
                          "Mostra a capa e um disco girando antes de abrir o jogo."};
    for (int i = 0; i < 8; i++)
    {
        unsigned y = 310 + i * 88;
        bool focus = i == settings_cursor;
        c.rounded_rectangle(70, y, 1770, 76, 12, focus ? panel : bg);
        if (focus)
            c.rectangle(70, y + 12, 4, 56, accent);
        text(c, 96, y + 8, names[i], 32, white, 650);
        text(c, 710, y + 20, desc[i], 24, muted, 1050);
        if (i < 2)
            text(c, 570, y + 17, (i == 0 ? large_text : record_mode) ? "Ligado" : "Desligado", 24,
                 accent, 130);
        if (i == 6)
            text(c, 570, y + 17, i18n::language ? "English" : "Português", 24, accent, 130);
        if (i == 7)
            text(c, 570, y + 17, launch_anim_enabled ? "Ligado" : "Desligado", 24, accent, 130);
    }
}
void draw_modal(Canvas &c)
{
    if (!modal)
        return;
    c.dim();
    c.rounded_rectangle(328, 150, 1264, 790, 20, shade(8, 11, 15));
    c.rounded_rectangle(340, 162, 1240, 766, 16, panel);
    if (modal == 5 && details_index >= 0)
    {
        int idx = details_index;
        artwork(c, idx, 400, 245, 390);
        text(c, 845, 225, games[idx].name, 44, white, 670);
        auto *m = meta(idx);
        text(c, 848, 340, m ? statuses[m->status] : statuses[0], 28, accent, 650);
        text(c, 848, 395, "Sua escolha. Seu próximo jogo.", 28, muted, 650);
        pill(c, 848, 500, 330, launch_pending ? "Abrindo…" : "×  Jogar", true);
        pill(c, 848, 560, 360, "△  Organizar");
        text(c, 400, 760, is_ps2_id(games[idx].id) ? "PS2" : games[idx].id, 28, muted, 1090);
        text(c, 400, 830, "○  Voltar", 28, white, 600);
        return;
    }
    if (modal == 3)
    {
        text(c, 395, 195,
             keyboard_kind == 1   ? "Buscar na biblioteca"
             : edit_slot >= 0     ? "Renomear coleção"
             : keyboard_kind == 4 ? "Nova coleção automática"
                                  : "Nova coleção",
             44, white, 1100);
        c.rounded_rectangle(395, 290, 1120, 70, 10, bg);
        text(c, 415, 302, entry_name, 32, white, 1070);
        for (int i = 0; i < key_count + 2; i++)
        {
            unsigned x = 402 + (i % 10) * 110, y = 410 + (i / 10) * 88;
            bool focus = i == keyboard_cursor;
            c.rounded_rectangle(x, y, 96, 65, 10, focus ? accent : bg);
            char key[2] = {i < key_count ? keys[i] : ' ', 0};
            text(c, x + 13, y + 9,
                 i == key_count       ? "OK"
                 : i == key_count + 1 ? "<"
                                      : key,
                 32, focus ? bg : white, 82);
        }
        text(c, 398, 812, "Options  Salvar   •   □  Apagar   •   ○  Voltar", 28, muted, 1100);
        return;
    }
    if (modal == 4 || modal == 10)
    {
        text(c, 400, 220, modal == 4 ? "Excluir esta coleção?" : "Restaurar sua organização?", 44,
             white, 1100);
        text(c, 400, 335,
             modal == 4 ? "As subcoleções também serão excluídas."
                        : "A organização atual será substituída pelo último backup.",
             28, muted, 1100);
        text(c, 400, 400, "Seus jogos e saves continuam onde estão.", 28, white, 1100);
        pill(c, 400, 550, 330, "×  Confirmar", true);
        text(c, 400, 650, "○  Cancelar", 28, white, 900);
        return;
    }
    const char *title = modal == 1   ? (menu_kind == 0   ? "Este jogo / seleção"
                                        : menu_kind == 1 ? "Sua biblioteca"
                                                         : "Esta coleção")
                        : modal == 2 ? "Adicionar a uma coleção"
                        : modal == 6 ? "Meu progresso"
                        : modal == 7 ? "Plataforma"
                        : modal == 8 ? "Ordenar jogos"
                                     : "Filtros";
    text(c, 395, 195, title, 44, white, 1100);
    int total = 0;
    const char *entries[32]{};
    char filterlabels[5][120]{};
    if (modal == 1)
    {
        total = menu_count;
        for (int i = 0; i < total; i++)
            entries[i] = menu[i].text;
    }
    if (modal == 2)
        for (int i = 0; i < 32; i++)
            if (db.folders[i].used && !db.folders[i].smart)
                entries[total++] = db.folders[i].name;
    if (modal == 6)
    {
        total = 4;
        for (int i = 0; i < 4; i++)
            entries[i] = statuses[i];
    }
    if (modal == 7)
    {
        total = 4;
        entries[0] = "PlayStation 2";
        entries[1] = "PlayStation 4";
        entries[2] = "PlayStation 5";
        entries[3] = "Outro / aplicativo";
    }
    if (modal == 8)
    {
        total = 4;
        for (int i = 0; i < 4; i++)
            entries[i] = sorts[i];
    }
    if (modal == 9)
    {
        total = 5;
        std::snprintf(filterlabels[0], 120, tr("Plataforma: %s"),
                      query.platform == 2   ? "PS2"
                      : query.platform == 4 ? "PS4"
                      : query.platform == 5 ? "PS5"
                                            : tr("Todas"));
        std::snprintf(filterlabels[1], 120, tr("Progresso: %s"),
                      query.status ? tr(statuses[query.status]) : tr("Todos"));
        std::snprintf(filterlabels[2], 120, tr("Somente favoritos: %s"),
                      query.favorites ? tr("Sim") : tr("Não"));
        std::snprintf(filterlabels[3], 120, "%s", tr("Limpar filtros e busca"));
        std::snprintf(filterlabels[4], 120, "%s", tr("Ver resultados"));
        for (int i = 0; i < 5; i++)
            entries[i] = filterlabels[i];
    }
    int start = (menu_cursor / 8) * 8;
    for (int i = start; i < total && i < start + 8; i++)
    {
        unsigned y = 292 + (i - start) * 65;
        bool focus = i == menu_cursor;
        if (focus)
            c.rounded_rectangle(390, y - 3, 1130, 58, 10, accent);
        text(c, 412, y + 4, entries[i], 28, focus ? bg : white, 1080);
    }
    if (!total)
        text(c, 400, 350, "Crie uma coleção manual antes de adicionar jogos.", 28, muted, 1090);
    text(c, 400, 861, "×  Escolher   •   ○  Voltar", 24, muted, 1050);
}
std::uint64_t state_signature();
void request_visible_cover()
{
    if (work.load() != 0 || launch_pending)
        return;
    static std::uint64_t last_queue_state = 0;
    const auto state = state_signature();
    if (last_queue_state == state)
        return;
    last_queue_state = state;
    int wanted[32]{}, n = 0;
    auto add = [&](int i)
    {
        if (i < 0 || i >= count || n >= 32)
            return;
        for (int k = 0; k < n; k++)
            if (wanted[k] == i)
                return;
        wanted[n++] = i;
    };
    if (modal == 5)
        add(details_index);
    if (screen == 0)
    {
        for (int d = 0; d <= 3; d++)
        {
            if (home_cursor - d >= 0 && home_cursor - d < home_count)
                add(home_games[home_cursor - d]);
            if (d && home_cursor + d < home_count)
                add(home_games[home_cursor + d]);
        }
    }
    if (screen == 1)
    {
        int start = (cursor / 12) * 12;
        if (filtered_count)
            add(filtered[cursor]);
        for (int i = start; i < filtered_count && i < start + 12; i++)
            add(filtered[i]);
    }
    // Collection mosaics use the same bounded queue and never block navigation.
    if (screen == 2)
    {
        int folders[6], nf = 0;
        if (screen == 0)
        {
            int start = (home_folder_cursor / 4) * 4;
            for (int i = start; i < home_folder_count && i < start + 4; i++)
                folders[nf++] = home_folders[i];
        }
        else
        {
            int start = (collection_cursor / 6) * 6;
            for (int i = start; i < collection_count && i < start + 6; i++)
                if (collection_entries[i] >= 0)
                    folders[nf++] = collection_entries[i];
        }
        for (int f = 0; f < nf; f++)
        {
            int got = 0;
            for (int i = 0; i < count && got < 4; i++)
            {
                auto *m = meta(i);
                if ((!m || !m->hidden) && collection_contains(db, games[i], folders[f], true))
                {
                    add(i);
                    got++;
                }
            }
        }
    }
    for (int k = 0; k < n; k++)
    {
        const char *id = games[wanted[k]].id;
        if (find_cover(id))
            continue;
        if (is_ps2_id(id) && ps2_state[wanted[k]] != 1)
            continue;
        bool failed = false;
        for (int i = 0; i < failed_cover_count; i++)
            if (!std::strcmp(id, failed_covers[i]))
                failed = true;
        if (failed)
            continue;
        std::snprintf(requested_cover, 10, "%s", id);
        char path[96];
        if (is_ps2_id(id))
            std::snprintf(path, sizeof path, "/api/fs/download?path=/data/PCSX2/covers/%s.jpg",
                          ps2_serial[wanted[k]]);
        else
            std::snprintf(path, sizeof path, "/appdb/icon?id=%s", id);
        start_request(3, path);
        break;
    }
}
std::uint64_t state_signature()
{
    std::uint64_t h = 1469598103934665603ull;
    auto add = [&](std::uint64_t v)
    {
        h ^= v;
        h *= 1099511628211ull;
    };
    add(ui_version);
    add(cover_version);
    add(screen);
    add(cursor);
    add(home_cursor);
    add(home_row);
    add(home_folder_cursor);
    add(collection_parent + 1);
    add(collection_cursor);
    add(nav_focus);
    add(nav_cursor);
    add(settings_cursor);
    add(modal);
    add(menu_cursor);
    add(keyboard_cursor);
    add(record_mode);
    add(large_text);
    add(i18n::language);
    add(launch_anim_enabled);
    add(pad < 0);
    add(storage_ok);
    add(launch_pending);
    for (int i = 0; i < count; i++)
        add(selected[i]);
    for (const char *p = message; *p; p++)
        add(static_cast<unsigned char>(*p));
    for (const char *p = entry_name; *p; p++)
        add(static_cast<unsigned char>(*p));
    for (const char *p = query.text; *p; p++)
        add(static_cast<unsigned char>(*p));
    return h;
}
// Full-screen launch scene: the focused game's cover moves left while a disc slides out from behind it and spins.
void draw_launch_anim(Canvas &c, std::uint64_t elapsed)
{
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; };
    auto ease_out = [](float v) { return 1.0f - (1.0f - v) * (1.0f - v) * (1.0f - v); };
    const float t = clamp01(static_cast<float>(elapsed) / static_cast<float>(anim_length));
    const float slide = ease_out(clamp01(t * 1.7f));
    const float out = ease_out(clamp01((t - 0.10f) / 0.65f));
    const Cover *cover = find_cover(games[anim_idx].id);
    const Color tint = cover ? static_cast<Color>(cover->tint) : accent;
    c.gradient(0, 0, 1920, 1080, mix_color(tint, bg, 80), bg);
    const unsigned edge = 420, cover_y = 250;
    const unsigned cover_x = static_cast<unsigned>(750.0f - 330.0f * slide);
    const int cy = static_cast<int>(cover_y + edge / 2);
    const int disc_x = static_cast<int>(960.0f + 220.0f * out);
    const unsigned radius = 190;
    if (out > 0.0f)
    {
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius, shade(216, 220, 230));
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 86 / 100,
                 mix_color(tint, shade(190, 196, 210), 30));
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 58 / 100, shade(206, 211, 224));
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 32 / 100, shade(150, 156, 172));
        // Sheen: three short arcs of dots that rotate with time (about 2.5 turns per second).
        const float spin = static_cast<float>(elapsed) * 1.57e-5f;
        for (int arm = 0; arm < 3; arm++)
            for (int dot = 0; dot < 8; dot++)
            {
                const float a = spin + arm * 2.0944f + dot * 0.085f;
                const float r = radius * 0.72f;
                const unsigned level = static_cast<unsigned>(255 - dot * 12);
                c.circle(static_cast<unsigned>(disc_x + static_cast<int>(r * std::cos(a))),
                         static_cast<unsigned>(cy + static_cast<int>(r * std::sin(a))), 11, shade(level, level, level));
            }
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 13 / 100, bg);
    }
    artwork(c, anim_idx, cover_x, cover_y, edge, false);
    centered(c, 790, games[anim_idx].name, 44, white, 1500);
    centered(c, 860, "Abrindo…", 28, muted, 900);
    if (!record_mode)
        text(c, 78, 1031, "○  Cancelar", 24, muted, 300);
}
void draw(Canvas &c) noexcept
{
    poll_work();
    input(buttons());
    if (message_until && sceKernelGetProcessTime() >= message_until)
    {
        message_until = 0;
        ui_version++;
    }
    if (verify_at && sceKernelGetProcessTime() >= verify_at && work.load() == 0)
    {
        verify_at = 0;
        start_request(5, "/api/state");
    }
    request_visible_cover();
    if (anim_idx >= 0)
    {
        const std::uint64_t elapsed = sceKernelGetProcessTime() - anim_start;
        if (elapsed < anim_length)
        {
            draw_launch_anim(c, elapsed);
            return;
        }
        const int idx = anim_idx;
        anim_idx = -1;
        ui_version++;
        start_launch(idx);
    }
    // The renderer keeps the finished frame in a cached buffer, so an unchanged state needs no redraw.
#ifdef ESTANTE_GL
    gpu_canvas = &c; // every frame is drawn again: the GPU path has no cached frame to keep
#else
    static std::uint64_t previous = 0;
    const auto signature = state_signature();
    if (signature == previous)
        return;
    previous = signature;
#endif
    if (const Cover *tinted = focus_cover())
    {
        const Color top = mix_color(static_cast<Color>(tinted->tint), bg, 92);
        c.gradient(0, 0, 1920, 700, top, bg);
        c.rectangle(0, 700, 1920, 380, bg); // the gradient already covers the top, no double write
    }
    else
        c.clear(bg);
    draw_top(c);
    if (screen == 0)
        draw_home(c);
    else if (screen == 1)
        draw_library(c);
    else if (screen == 2)
        draw_collections(c);
    else
        draw_settings(c);
    if (!record_mode)
    {
        if (screen == 1)
        {
            text(c, 78, 1031, "L1 / R1  Abas", 24, muted, 250);
            text(c, 360, 1031, editing_collection >= 0 ? "×  Selecionar" : "×  Jogar", 24, white, 250);
            text(c, 620, 1031, "△  Organizar", 24, muted, 250);
            text(c, 900, 1031, "□  Selecionar", 24, muted, 250);
            text(c, 1190, 1031, query.text[0] ? "○  Limpar busca" : "Touchpad  Filtros", 24, accent, 300);
            text(c, 1540, 1031, "Options  Opções", 24, muted, 300);
        }
        else
        {
            text(c, 78, 1031, "L1 / R1  Navegar", 24, muted, 300);
            text(c, 420, 1031, screen == 0 && home_row == 0 ? "×  Jogar" : "×  Abrir", 24, white, 280);
            text(c, 710, 1031, "△  Organizar", 24, muted, 300);
            text(c, 1020, 1031, screen == 0 ? "□  Buscar" : "○  Voltar", 24, muted, 300);
            text(c, 1370, 1031, screen == 0 ? "Options  Biblioteca" : "Options  Opções", 24, muted,
                 460);
        }
        if (launch_pending || !storage_ok)
            text(c, 70, screen == 0 ? 804 : 935,
                 !storage_ok ? "Salvamento indisponível - confira o armazenamento do app."
                             : message,
                 24, Color::yellow, 1750);
    }
    draw_modal(c);
    if (!record_mode && message_until)
    {
        c.rounded_rectangle(360, 945, 1200, 68, 12, panel);
        text(c, 380, 958, message, 24, accent, 1160);
    }
}
} // namespace
int main()
{
    log("BOOT ESTANTE FOCUS R2 PPSA99841");
    struct stat st
    {
    };
    storage_ok =
        stat("/download0", &st) == 0 && write_file("/download0/estante-probe.txt", "ESTANTE 2");
    char *saved = read_file("/download0/estante.json");
    if (saved)
    {
        if (!read_library(saved, db))
        {
            storage_ok = false;
            msg("Organização inválida. Gravação bloqueada para preservar os dados.");
        }
        else
        {
            cJSON *root = cJSON_Parse(saved);
            auto *version = cJSON_GetObjectItemCaseSensitive(root, "version");
            if (cJSON_IsNumber(version) && version->valueint == 1 &&
                stat("/download0/estante-before-v2.json", &st) != 0)
            {
                if (!write_file("/download0/estante-before-v2.json", saved))
                {
                    storage_ok = false;
                    msg("Não foi possível guardar a organização anterior. Gravação bloqueada.");
                }
            }
            cJSON_Delete(root);
        }
        std::free(saved);
    }
    char *cached = read_file("/download0/catalog.json");
    if (cached)
    {
        count = read_catalog(cached, games, max_games);
        std::free(cached);
    }
    if (count <= 0)
    {
        char *bundled = read_file("/app0/assets/catalog.json");
        if (bundled)
        {
            count = read_catalog(bundled, games, max_games);
            std::free(bundled);
        }
    }
    detect_language();
    load_ps2_cache();
    read_preferences();
    if (count < 0)
        count = 0;
    rebuild();
    start_request(1, "/appdb");
#ifdef ESTANTE_GL
    estante::run_gl(draw);
#else
    ps5::demo::run_frames(draw, "Estante: sua biblioteca pessoal");
#endif
}
