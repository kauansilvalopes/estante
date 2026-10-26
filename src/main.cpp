// Estante native PS5 prototype. GPL-3.0-or-later.
// VideoOut and Pad ABI based on the public boilerplate, not firmware offsets.
#include "demo_renderer.hpp"
#ifdef ESTANTE_GL
#include "gl_main.hpp"
#endif
#include "library.hpp"
#include "experience.hpp"
#include "emulator_import.hpp"
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
bool show_back = false;                 // the focused case is turned to show its back (R2 / L2)
#else
using ps5::demo::Canvas;
#endif
float stick_rx = 0.0f, stick_ry = 0.0f; // right stick, -1..1: turns the focused case by hand
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
constexpr Color rgb_color(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Color>(0xff000000u | (b << 16) | (g << 8) | r);
}
struct Theme
{
    const char *name;
    Color bg, panel, accent, muted;
};
constexpr Theme themes[] = {
    // Black and white with a single Sony blue: the default.
    {"PlayStation", rgb_color(8, 9, 12), rgb_color(26, 28, 33), rgb_color(0, 112, 209),
     rgb_color(152, 156, 166)},
    {"Midnight", rgb_color(16, 17, 20), rgb_color(32, 34, 39), rgb_color(83, 177, 255),
     rgb_color(185, 190, 201)},
    {"Neon", rgb_color(14, 8, 26), rgb_color(34, 20, 58), rgb_color(255, 84, 200),
     rgb_color(198, 178, 226)},
    {"Forest", rgb_color(9, 18, 14), rgb_color(22, 38, 31), rgb_color(110, 231, 160),
     rgb_color(166, 198, 182)},
    {"Ember", rgb_color(23, 13, 10), rgb_color(46, 29, 22), rgb_color(255, 160, 64),
     rgb_color(214, 190, 170)},
};
constexpr int theme_count = 5;
int theme_index = 0;
Color bg = themes[0].bg, panel = themes[0].panel, accent = themes[0].accent,
      muted = themes[0].muted;
constexpr Color white = Color::white;
// Text drawn on the accent colour: white on the dark blues, the dark background on light accents.
Color on_accent()
{
    const auto a = static_cast<std::uint32_t>(accent);
    const unsigned luminance = (299 * (a & 255) + 587 * ((a >> 8) & 255) + 114 * ((a >> 16) & 255)) / 1000;
    return luminance < 150 ? white : bg;
}
void apply_theme(int index)
{
    theme_index = ((index % theme_count) + theme_count) % theme_count;
    bg = themes[theme_index].bg;
    panel = themes[theme_index].panel;
    accent = themes[theme_index].accent;
    muted = themes[theme_index].muted;
}
constexpr std::uint32_t UP = 0x10, RIGHT = 0x20, DOWN = 0x40, LEFT = 0x80, L2 = 0x100, R2 = 0x200,
                        L1 = 0x400, R1 = 0x800, TRIANGLE = 0x1000, CIRCLE = 0x2000, CROSS = 0x4000,
                        SQUARE = 0x8000, OPTIONS = 0x8, R3 = 0x4, TOUCH = 0x100000;
constexpr int lib_cols = 6; // Biblioteca: square icons, two rows visible, scrolls by row
constexpr int thumb_edge = 264; // library card size: pre-scaled once when the cover loads
constexpr int cover_edge = 512; // native icon0 size; downscaled with a box filter when drawn
#ifdef ESTANTE_GL
constexpr int tall_w = 320,
              tall_h = 480; // portrait art for the 3D cases (tools/ps5_covers_sync.py)
constexpr int cover_slots = 24;
#else
constexpr int cover_slots = 32;
#endif
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
constexpr int settings_count = 10;
int settings_cursor = 0, modal = 0, menu_cursor = 0, menu_kind = 0, details_index = -1,
    folder_menu_slot = -1;
bool details_page = false;
int details_button = 0; // focused action on the game page: 0 Jogar, 1 favorito, 2 menu
// Inicio in the catalogue style: horizontal rows with one game focused at a time. The first row is
// "Continue playing" (only games the player started from Estante); empty rows are never shown.
// home_style 1 keeps the earlier shelf (cover flow) as an alternative in Ajustes.
constexpr int max_rails = 16, rail_cap = 40;
struct HomeRail
{
    Rail kind;
    int folder;
    char title[48];
    int items[rail_cap + 1]; // library slots; -1 is the trailing "see all" card
    int count;
    int cursor;
    int shown; // games without the "see all" card
};
HomeRail rails[max_rails]{};
int rail_count = 0;
int home_style = 1;      // 0 catalogue, 1 shelf (cover flow); a new install starts on the shelf
bool style_tip_seen = false; // the one-time "L2 + R2 changes the style" tip was shown
int library_zone = 0;    // 0 grid, 1 the chips above it
int library_chip = 0;    // 0 search, 1 view, 2 sort, 3 filters
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
#ifdef ESTANTE_GL
    bool has_tall;
    std::uint32_t tall[tall_w * tall_h];
#endif
};
Cover covers[cover_slots]{};
std::uint64_t cover_clock = 0;
char failed_covers[max_games][10]{};
int failed_cover_count = 0;
std::atomic<int> work{0};
char request_path[768]{}, response[4 * 1024 * 1024]{};
std::size_t response_size = 0;
int work_kind = 0, pending_kind = 0;
char pending_path[768]{};
bool response_ok = false;
char requested_cover[10]{}, launch_id[10]{};
char requested_fallback[96]{}; // a second place to ask for the same cover when the first has none
char launch_game[10]{}; // the game the player picked (launch_id is PS5SX2 for a PS2 image)
#ifdef ESTANTE_GL
char requested_tall_path[768]{};
#endif
// PS2 images found in the PS5SX2 games folder: file name (with extension) per library slot, empty
// for normal titles. The id is made up ("PS2" + hash) because an image has no title id.
char ps2_file[max_games][144]{};
constexpr const char *ps2_games_dir = "/data/PCSX2/games";
constexpr const char *ps2_listed_serials = "/data/PCSX2/chd-serials.txt";
// Entries imported from another emulator library. They have a stable synthetic id so they can use
// Estante's normal metadata, while `emulator_app` identifies the frontend to open.
char emulator_app[max_games][10]{};
char emulator_path[max_games][200]{};
// Artwork belongs to the emulator's own library, not to /appdb. Keeping the absolute path alongside
// the virtual entry lets the regular cover cache load it without pretending it is a native title.
char emulator_cover[max_games][200]{};
unsigned char emulator_kind[max_games]{}; // 1 PS1, 2 GameCube/Wii, 3 Wii U, 4 Nintendo 3DS, 5 Nintendo DS, 6 Switch
bool emulator_limit_reached = false;
constexpr const char *swan_games_dir = "/data/SwanStationPS5/games";
constexpr const char *swan_boxart_dir = "/data/SwanStationPS5/covers/boxart";
constexpr const char *swan_default_dir = "/data/SwanStationPS5/covers/default";
// SwanStationPS5 keeps two cover sets: boxart/<title>.png and default/<serial>.jpg. A game whose localized
// folder name finds nothing by title still has its cover by serial, so the serial is read from the disc.
bool swan_boxart[max_games]{};        // a cover named after the title exists
char ps1_serial[max_games][12]{};     // boot serial of each PS1 game
unsigned char ps1_state[max_games]{}; // 0 not read yet, 1 serial known, 2 none
char ps1_default[256][16]{};          // the serial covers present in default/
int ps1_default_count = 0, ps1_job = -1;
constexpr int ps1_cache_max = 128;
char ps1_cache_name[ps1_cache_max][100]{}, ps1_cache_serial[ps1_cache_max][12]{};
int ps1_cache_count = 0;
constexpr const char *porpoise_games_dir = "/data/porpoise/games";
// ProsperoEden (Switch): one folder per game under roms/, and covers/<hash>.tga with covers/<hash>.name holding the
// real title. The .name files are read one by one to find which cover belongs to which game.
constexpr const char *eden_roms_dir = "/data/prosperoeden/roms";
constexpr const char *eden_covers_dir = "/data/prosperoeden/covers";
char eden_cover_hash[64][12]{};
int eden_cover_count = 0, eden_cover_job = 0;
constexpr const char *ps5cemu_library = "/data/ps5cemu/library.json";
// Boot serial of each PS2 image ("SLUS-20624"), read once from the start of the image and
// remembered in /download0/ps2-serials.txt. state: 0 not read yet, 1 serial known, 2 no serial. The
// serial names the cover PS5SX2 already downloaded to /data/PCSX2/covers.
char ps2_serial[max_games][12]{};
unsigned char ps2_state[max_games]{};
constexpr int ps2_cache_max = 256;
char ps2_cache_name[ps2_cache_max][144]{}, ps2_cache_serial[ps2_cache_max][12]{};
int ps2_cache_count = 0, ps2_job = -1;
char ps2_job_name[144]{};
std::uint32_t decoded_cover[cover_edge * cover_edge]{};
#ifdef ESTANTE_GL
std::uint32_t decoded_tall[tall_w * tall_h]{};
bool tall_ok = false;
// One wide picture: the hero behind the focused game, the game page and the back of a case.
constexpr int wide_w = 1920, wide_h = 1080; // the console's 4K key art, halved
std::uint32_t wide_art[wide_w * wide_h]{}, decoded_wide[wide_w * wide_h]{};
char wide_art_id[10]{}, wide_have[10]{}; // wide_have: whose picture the buffer holds
int wide_art_state = 0; // 0 nothing yet, 1 loading, 2 ready, 3 none for this game
bool wide_ok = false;
#endif
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
// Trophy data is read-only.  It comes from the console's existing trophy database; Estante never
// unlocks, edits or syncs trophies.  State: 0 = not asked, 1 = loading, 2 = available, 3 = absent.
struct TrophySummary
{
    std::uint16_t total = 0, earned = 0;
    // Per grade, index 1 platinum, 2 gold, 3 silver, 4 bronze (the console's own numbering).
    std::uint16_t total_by[5]{}, earned_by[5]{};
    std::uint64_t first = 0, last = 0;
    unsigned char state = 0;
};
TrophySummary trophies[max_games]{};
char trophy_user_id[32]{};
char trophy_npwr[20]{};
int trophy_index = -1;
constexpr char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
constexpr int key_count = sizeof(keys) - 1;
const char *views[] = {"Todos os jogos", "PlayStation 2", "PlayStation 4", "PlayStation 5",
                       "Apps",           "Favoritos",      "Sem coleção",   "Ocultos",
                       "Quero jogar",    "Em andamento",   "Concluídos",    "Fixados",
                       "PlayStation 1",  "Nintendo"};
// Order shown in the filter list (ids stay as they are): Apps right after "Todos os jogos".
constexpr int view_order[] = {0, 4, 3, 2, 12, 1, 13, 5, 6, 7, 8, 9, 10, 11};
constexpr int view_order_count = sizeof view_order / sizeof *view_order;
const char *statuses[] = {"Sem status", "Quero jogar", "Em andamento", "Concluído"};
const char *sorts[] = {"Nome A-Z", "Plataforma", "Últimas escolhas", "Favoritos primeiro"};
const Meta *meta(int idx)
{
    return idx >= 0 && idx < count ? metadata(static_cast<const Library &>(db), games[idx].id)
                                   : nullptr;
}
void rebuild();
void start_request(int, const char *);
void request_trophies(int);
void open_details(int);
void close_details();
bool is_ps2_id(const char *);
bool is_emulator_game(int);
const char *system_name(int);
const char *system_tag(int);
const char *platform_name(int);
const char *platform_tag(int);
void log(const char *text)
{
    char line[220];
    std::snprintf(line, sizeof line, "ESTANTE: %s\n", text);
    sceKernelDebugOutText(0, line);
}
void msg(const char *text)
{
    message_until = sceKernelGetProcessTime() + 6000000;
    const char *shown = tr(text);
    std::snprintf(message, sizeof message, "%s", shown);
    log(shown);
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
    char data[220];
    std::snprintf(data, sizeof data,
                  "{\"largeText\":%s,\"recordMode\":%s,\"lang\":\"%s\",\"anim\":%s,\"theme\":%d,"
                  "\"home\":%d,\"tip\":%d}",
                  large_text ? "true" : "false", record_mode ? "true" : "false",
                  i18n::language == 2 ? "es" : i18n::language ? "en" : "pt", launch_anim_enabled ? "true" : "false",
                  theme_index, home_style, style_tip_seen ? 1 : 0);
    if (!write_file("/download0/estante-preferences.json", data))
        msg("Nao foi possivel salvar as preferencias.");
}
// PS5 system language: Brazilian Portuguese (17) and European Portuguese (7) get Portuguese, Spanish (3)
// and Latin American Spanish (20) get Spanish, everything else gets English. A saved preference overrides this.
void detect_language()
{
    int value = 1;
    if (sceSystemServiceParamGetInt(1, &value) == 0)
        i18n::language = (value == 17 || value == 7) ? 0 : (value == 3 || value == 20) ? 2 : 1;
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
    auto *theme = cJSON_GetObjectItemCaseSensitive(root, "theme");
    if (cJSON_IsNumber(theme))
        apply_theme(static_cast<int>(theme->valuedouble));
    auto *anim = cJSON_GetObjectItemCaseSensitive(root, "anim");
    if (cJSON_IsBool(anim))
        launch_anim_enabled = cJSON_IsTrue(anim);
    auto *home = cJSON_GetObjectItemCaseSensitive(root, "home");
    if (cJSON_IsNumber(home))
        home_style = home->valuedouble == 1 ? 1 : 0;
    auto *tip = cJSON_GetObjectItemCaseSensitive(root, "tip");
    if (cJSON_IsNumber(tip))
        style_tip_seen = tip->valuedouble == 1;
    auto *lang = cJSON_GetObjectItemCaseSensitive(root, "lang");
    if (cJSON_IsString(lang) && lang->valuestring)
        i18n::language = !std::strcmp(lang->valuestring, "pt") ? 0 : !std::strcmp(lang->valuestring, "es") ? 2 : 1;
    cJSON_Delete(root);
}
void clear_trophy_summaries()
{
    std::memset(trophies, 0, sizeof trophies);
}

int chosen_count()
{
    int n = 0;
    for (int i = 0; i < count; i++)
        if (selected[i])
            n++;
    return n;
}
// The library slot the catalogue Inicio (or the shelf) has focused, -1 when there is none.
int home_game()
{
    if (home_style == 1)
        return home_count ? home_games[home_cursor] : -1;
    if (home_row < 0 || home_row >= rail_count)
        return -1;
    const HomeRail &r = rails[home_row];
    if (r.cursor < 0 || r.cursor >= r.count)
        return -1;
    const int idx = r.items[r.cursor];
    return idx >= 0 ? idx : r.cursor > 0 ? r.items[r.cursor - 1] : -1; // "see all" keeps the last game
}
void add_rail(Rail kind, int folder, const char *title)
{
    if (rail_count >= max_rails)
        return;
    HomeRail &r = rails[rail_count];
    r.kind = kind;
    r.folder = folder;
    std::snprintf(r.title, sizeof r.title, "%s", title);
    r.cursor = 0;
    r.shown = rail_games(db, games, count, kind, folder, r.items, rail_cap);
    r.count = r.shown;
    if (!r.shown)
        return;
    if (r.shown == rail_cap && kind != Rail::Continue)
        r.items[r.count++] = -1;
    rail_count++;
}
void rebuild_rails()
{
    char focused[10]{};
    Rail focused_kind = Rail::Continue;
    int focused_folder = -1;
    if (home_row < rail_count)
    {
        const int idx = home_game();
        if (idx >= 0)
            std::snprintf(focused, 10, "%s", games[idx].id);
        focused_kind = rails[home_row].kind;
        focused_folder = rails[home_row].folder;
    }
    HomeRail old[max_rails];
    const int old_count = rail_count;
    std::memcpy(old, rails, sizeof old);
    rail_count = 0;
    add_rail(Rail::Continue, -1, "Continuar jogando");
    add_rail(Rail::Favorites, -1, "Favoritos");
    add_rail(Rail::Want, -1, "Quero jogar");
    for (int i = 0; i < max_folders; i++)
        if (db.folders[i].used && db.folders[i].pinned)
            add_rail(Rail::Folder, i, db.folders[i].name);
    add_rail(Rail::PS5, -1, "PlayStation 5");
    add_rail(Rail::PS4, -1, "PlayStation 4");
    add_rail(Rail::PS1, -1, "PlayStation 1");
    add_rail(Rail::PS2, -1, "PlayStation 2");
    add_rail(Rail::Wii, -1, "GameCube / Wii");
    add_rail(Rail::WiiU, -1, "Wii U");
    add_rail(Rail::N3DS, -1, "Nintendo 3DS");
    add_rail(Rail::NDS, -1, "Nintendo DS");
    add_rail(Rail::Switch, -1, "Nintendo Switch");
    // Rows keep their own cursor between rebuilds; the focused game stays focused when it moved.
    for (int i = 0; i < rail_count; i++)
        for (int k = 0; k < old_count; k++)
            if (old[k].kind == rails[i].kind && old[k].folder == rails[i].folder)
            {
                rails[i].cursor = old[k].cursor < rails[i].count ? old[k].cursor : rails[i].count - 1;
                if (old[k].cursor < old[k].count && old[k].items[old[k].cursor] >= 0)
                    for (int c = 0; c < rails[i].shown; c++)
                        if (rails[i].items[c] == old[k].items[old[k].cursor])
                            rails[i].cursor = c;
            }
    int row = -1;
    for (int i = 0; i < rail_count; i++)
        if (rails[i].kind == focused_kind && rails[i].folder == focused_folder)
            row = i;
    if (row < 0 && focused[0])
        for (int i = 0; i < rail_count && row < 0; i++)
            for (int c = 0; c < rails[i].shown; c++)
                if (!std::strcmp(games[rails[i].items[c]].id, focused))
                {
                    row = i;
                    rails[i].cursor = c;
                    break;
                }
    home_row = row >= 0 ? row : 0;
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
    Query homeQuery; // games only: apps and emulators stay in Biblioteca > Apps
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
    rebuild_rails();
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
    if (details_index >= 0 && details_page)
        return details_index;
    if (screen == 0)
        return home_game();
    if (screen == 1)
        return filtered_count ? filtered[cursor] : -1;
    return -1;
}
template <class Fn> void update_targets(Fn fn)
{
    Library next = db;
    bool batch = chosen_count() > 0 && screen == 1 && !details_page;
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
            out[y * thumb_edge + x] = 0xff000000u | ((b / n) << 16) | ((g / n) << 8) | (r / n);
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
bool http_get_to(const char *path, char *response, std::size_t capacity, std::size_t &response_size,
                 std::size_t prefix = 0)
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
    char req[1024];
    if (!estante::build_http_request(path, req, sizeof req))
    {
        sceNetSocketClose(s);
        return false;
    }
    const int len = static_cast<int>(std::strlen(req));
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
    while (used < capacity - 1)
    {
        int n = sceNetRecv(s, response + used, capacity - 1 - used, 0);
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
bool http_get(const char *path, std::size_t prefix = 0)
{
    return http_get_to(path, response, sizeof response, response_size, prefix);
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
        std::snprintf(cache, sizeof cache, "/download0/estante-cover-hd-%02u.bin", hash % 32);
        cover_ok = cached_cover(local, false) || cached_cover(cache, true);
        if (!cover_ok)
        {
            response_ok = http_get(request_path);
            cover_ok = response_ok && decode_cover(reinterpret_cast<unsigned char *>(response),
                                                   response_size, decoded_cover, cover_edge);
            if (!cover_ok && requested_fallback[0])
            {
                response_ok = http_get(requested_fallback);
                cover_ok = response_ok && decode_cover(reinterpret_cast<unsigned char *>(response),
                                                       response_size, decoded_cover, cover_edge);
            }
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
#ifdef ESTANTE_GL
        tall_ok = false;
        if (cover_ok && requested_tall_path[0])
        {
            tall_ok = http_get(requested_tall_path) &&
                      decode_cover_rect(reinterpret_cast<unsigned char *>(response), response_size,
                                        decoded_tall, tall_w, tall_h);
        }
#endif
    }
    else if (work_kind == 8 || work_kind == 20)
        response_ok = http_get(request_path, 700000);
#ifdef ESTANTE_GL
    else if (work_kind == 9)
    {
        // The game's 4K key art is a PNG of up to 16 MB: it is read into a buffer of its own, not the 4 MB one.
        constexpr std::size_t big_capacity = 24u * 1024u * 1024u;
        wide_ok = false;
        if (char *big = static_cast<char *>(std::malloc(big_capacity)))
        {
            std::size_t got = 0;
            wide_ok = http_get_to(request_path, big, big_capacity, got) &&
                      decode_cover_rect_large(reinterpret_cast<unsigned char *>(big), got, decoded_wide, wide_w,
                                              wide_h) != 0;
            std::free(big);
        }
        response_ok = wide_ok;
    }
#endif
    else
        response_ok = http_get(request_path);
    work.store(2, std::memory_order_release);
    return nullptr;
}
void start_request(int kind, const char *path)
{
    char encoded[sizeof request_path]{};
    if (!estante::encode_fs_request(path, encoded, sizeof encoded))
    {
        msg("Caminho longo demais para consultar.");
        launch_pending = false;
        return;
    }
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
    std::snprintf(request_path, sizeof request_path, "%s", encoded);
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
bool parse_trophy_user()
{
    cJSON *root = cJSON_Parse(response);
    const cJSON *value = root ? cJSON_GetObjectItemCaseSensitive(root, "userIdHex") : nullptr;
    bool ok = cJSON_IsString(value) && value->valuestring && value->valuestring[0];
    if (ok)
    {
        const char *src = value->valuestring;
        if (src[0] == '0' && (src[1] == 'x' || src[1] == 'X'))
            src += 2;
        std::snprintf(trophy_user_id, sizeof trophy_user_id, "%s", src);
        for (char *p = trophy_user_id; *p; ++p)
            *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
    if (root)
        cJSON_Delete(root);
    return ok;
}
bool parse_npbind()
{
    trophy_npwr[0] = 0;
    constexpr size_t need = 12; // NPWR + five digits + _00
    for (size_t i = 0; i + need <= response_size; ++i)
    {
        const char *p = response + i;
        if (std::memcmp(p, "NPWR", 4) != 0 || p[9] != '_' || p[10] != '0' || p[11] != '0')
            continue;
        bool digits = true;
        for (size_t d = 4; d < 9; ++d)
            digits = digits && p[d] >= '0' && p[d] <= '9';
        if (digits)
        {
            std::memcpy(trophy_npwr, p, need);
            trophy_npwr[need] = 0;
            return true;
        }
    }
    return false;
}
std::uint32_t be32(const unsigned char *p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
bool parse_trophy_title(TrophySummary &out)
{
    bool seen[1024]{};
    unsigned char grade[1024]{};
    out.total = out.earned = 0;
    std::memset(out.total_by, 0, sizeof out.total_by);
    std::memset(out.earned_by, 0, sizeof out.earned_by);
    out.first = out.last = 0;
    const auto *bytes = reinterpret_cast<const unsigned char *>(response);
    // Trophy descriptions (record type 0x500, 0xc0 bytes): id at +0x10 and grade at +0x14.
    for (size_t offset = 0x100; offset + 0x20 <= response_size; offset += 16)
        if (be32(bytes + offset) == 0x500 && be32(bytes + offset + 4) == 0xc0)
            for (size_t shift : {size_t(0), size_t(0x20)}) // earned trophies carry 0x20 more bytes first
            {
                if (offset + 0x20 + shift > response_size)
                    break;
                const std::uint32_t id = be32(bytes + offset + 0x10 + shift),
                                    g = be32(bytes + offset + 0x14 + shift);
                if (id < 1024 && g >= 1 && g <= 4)
                {
                    grade[id] = static_cast<unsigned char>(g);
                    break;
                }
            }
    for (size_t offset = 0x200; offset + 0x78 <= response_size; offset += 16)
    {
        if (be32(bytes + offset) != 0x800 || be32(bytes + offset + 4) != 0x50)
            continue;
        for (size_t shift : {size_t(0), size_t(0x20)})
        {
            const std::uint32_t id = be32(bytes + offset + 0x10 + shift);
            const std::uint32_t flags = be32(bytes + offset + 0x14 + shift);
            if (id >= 1024 || (flags != 0 && flags != 0x10 && flags != 0x11))
                continue;
            if (!seen[id])
            {
                seen[id] = true;
                ++out.total;
                ++out.total_by[grade[id]];
                if (flags == 0x11)
                {
                    const std::uint64_t when =
                        (static_cast<std::uint64_t>(be32(bytes + offset + 0x20 + shift)) << 32) |
                        be32(bytes + offset + 0x24 + shift);
                    if (when)
                    {
                        ++out.earned;
                        ++out.earned_by[grade[id]];
                        if (!out.first || when < out.first)
                            out.first = when;
                        if (when > out.last)
                            out.last = when;
                    }
                }
            }
            break;
        }
    }
    return out.total > 0;
}
void request_trophies(int idx)
{
    if (idx < 0 || idx >= count || is_ps2_id(games[idx].id) || is_emulator_game(idx) || trophies[idx].state == 1 ||
        trophies[idx].state == 2)
        return;
    trophy_index = idx;
    trophies[idx].state = 1;
    if (!trophy_user_id[0])
    {
        start_request(10, "/api/np/info");
        return;
    }
    char path[180];
    std::snprintf(path, sizeof path,
                  "/api/fs/download?path=/system_data/priv/appmeta/%s/trophy2/npbind.dat",
                  games[idx].id);
    start_request(11, path);
}
// Details are a destination, not a dialog.  Keeping this transition in one place also makes the
// cover, wide art and read-only trophy request belong to the same selected game.
void open_details(int idx)
{
    if (idx < 0 || idx >= count)
        return;
    details_index = idx;
    details_page = true;
    details_button = 0;
    modal = 0;
    request_trophies(idx);
    ui_version++;
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
bool is_emulator_game(int idx)
{
    return idx >= 0 && idx < count && emulator_app[idx][0];
}
const char *system_name(int idx)
{
    if (!is_emulator_game(idx))
        return is_ps2_id(games[idx].id) ? "PlayStation 2" : platform_name(platform(games[idx], db));
    switch (emulator_kind[idx])
    {
    case 1: return "PlayStation";
    case 2: return "GameCube / Wii";
    case 3: return "Wii U";
    case 4: return "Nintendo 3DS";
    case 5: return "Nintendo DS";
    case 6: return "Nintendo Switch";
    default: return "Emulador";
    }
}
const char *system_tag(int idx)
{
    if (!is_emulator_game(idx))
        return platform_tag(platform(games[idx], db));
    switch (emulator_kind[idx])
    {
    case 1: return "PS1";
    case 2: return "WII";
    case 3: return "WII U";
    case 4: return "3DS";
    case 5: return "DS";
    case 6: return "SWITCH";
    default: return "EMU";
    }
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
        used += static_cast<std::size_t>(std::snprintf(text + used, sizeof text - used, "%s|%s\n",
                                                       ps2_cache_serial[i], ps2_cache_name[i]));
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
            std::snprintf(ps2_cache_serial[ps2_cache_count], sizeof ps2_cache_serial[0], "%s",
                          line);
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
        std::snprintf(ps2_cache_name[ps2_cache_count], sizeof ps2_cache_name[0], "%s",
                      ps2_job_name);
        std::snprintf(ps2_cache_serial[ps2_cache_count], sizeof ps2_cache_serial[0], "%s",
                      serial ? serial : "-");
        ps2_cache_count++;
        save_ps2_cache();
    }
}
// PS5SX2 writes /data/PCSX2/chd-serials.txt, one line per image: "<path>\t<size>\t<mtime>\t<serial>". Images the
// disc reader cannot open (CHD, CSO, ZSO) get their serial, and so their cover, from that file. A serial read from
// an ISO is never replaced. Returns true when something changed.
bool apply_ps2_listed_serials(char *text)
{
    bool changed = false;
    char *line = text;
    while (line && *line)
    {
        char *end = std::strchr(line, '\n');
        if (end)
            *end = 0;
        char *field[4]{};
        int n = 0;
        for (char *q = line; n < 4; n++)
        {
            field[n] = q;
            char *tab = std::strchr(q, '\t');
            if (!tab)
            {
                n++;
                break;
            }
            *tab = 0;
            q = tab + 1;
        }
        if (n == 4)
        {
            char *serial = field[3];
            std::size_t length = std::strlen(serial);
            while (length && (serial[length - 1] == '\r' || serial[length - 1] == ' '))
                serial[--length] = 0;
            bool shape = length == 10 && serial[4] == '-';
            for (int k = 0; shape && k < 10; k++)
                if (k != 4 && !(k < 4 ? std::isupper(static_cast<unsigned char>(serial[k]))
                                      : std::isdigit(static_cast<unsigned char>(serial[k]))))
                    shape = false;
            const char *base = std::strrchr(field[0], '/');
            base = base ? base + 1 : field[0];
            for (int i = 0; shape && i < count; i++)
            {
                if (!ps2_file[i][0] || ps2_state[i] == 1 || std::strcmp(ps2_file[i], base))
                    continue;
                std::snprintf(ps2_serial[i], sizeof ps2_serial[0], "%s", serial);
                ps2_state[i] = 1;
                int c = 0;
                while (c < ps2_cache_count && std::strcmp(ps2_cache_name[c], base))
                    c++;
                if (c < ps2_cache_count || ps2_cache_count < ps2_cache_max)
                {
                    if (c == ps2_cache_count)
                    {
                        std::snprintf(ps2_cache_name[c], sizeof ps2_cache_name[0], "%s", base);
                        ps2_cache_count++;
                    }
                    std::snprintf(ps2_cache_serial[c], sizeof ps2_cache_serial[0], "%s", serial);
                }
                changed = true;
            }
        }
        line = end ? end + 1 : nullptr;
    }
    if (changed)
        save_ps2_cache();
    return changed;
}
// Adds the PS2 images listed by Elf Arsenal (/api/fs/list) after the catalog.
// Everything the scan chain imports (PS2 images and the emulator libraries) sits after the catalog titles, and
// the chain runs again whenever Inicio is opened after a while. Dropping the whole imported block before it is
// built again keeps games[] and every per-slot array (ps2_*, emulator_*, ps1_*, covers, trophies) in step;
// shifting only some of them used to pair a game's name with another game's system, cover and path.
void drop_imported_games()
{
    int first = count;
    for (int i = 0; i < count; i++)
        if (is_ps2_id(games[i].id) || emulator_app[i][0])
        {
            first = i;
            break;
        }
    const std::size_t rest = static_cast<std::size_t>(max_games - first);
    std::memset(games + first, 0, sizeof(games[0]) * rest);
    std::memset(selected + first, 0, sizeof(selected[0]) * rest);
    std::memset(ps2_file + first, 0, sizeof(ps2_file[0]) * rest);
    std::memset(ps2_serial + first, 0, sizeof(ps2_serial[0]) * rest);
    std::memset(ps2_state + first, 0, rest);
    std::memset(emulator_app + first, 0, sizeof(emulator_app[0]) * rest);
    std::memset(emulator_path + first, 0, sizeof(emulator_path[0]) * rest);
    std::memset(emulator_cover + first, 0, sizeof(emulator_cover[0]) * rest);
    std::memset(emulator_kind + first, 0, rest);
    std::memset(swan_boxart + first, 0, sizeof(swan_boxart[0]) * rest);
    std::memset(ps1_serial + first, 0, sizeof(ps1_serial[0]) * rest);
    std::memset(ps1_state + first, 0, rest);
    std::memset(played_last + first, 0, sizeof(played_last[0]) * rest);
    std::memset(played_seconds + first, 0, sizeof(played_seconds[0]) * rest);
    std::memset(trophies + first, 0, sizeof(trophies[0]) * rest);
    ps1_job = -1;
    count = first;
}
void add_ps2_games(const char *json)
{
    drop_imported_games();
    cJSON *root = cJSON_Parse(json);
    const cJSON *list = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *e = nullptr;
    cJSON_ArrayForEach(e, list)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(e, "name");
        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(e, "dir");
        char stem[144];
        if (count >= max_games)
        {
            emulator_limit_reached = true;
            continue;
        }
        if (!cJSON_IsString(name) || cJSON_IsTrue(dir) ||
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
                std::snprintf(ps2_serial[count], sizeof ps2_serial[0], "%s",
                              known ? ps2_cache_serial[c] : "");
            }
        count++;
    }
    cJSON_Delete(root);
}
bool emulator_extension(const char *name, const char *const *extensions, int extension_count)
{
    const std::size_t length = std::strlen(name);
    for (int i = 0; i < extension_count; i++)
    {
        const std::size_t suffix = std::strlen(extensions[i]);
        if (length > suffix && !strcasecmp(name + length - suffix, extensions[i]))
            return true;
    }
    return false;
}
void add_emulator_game(const char *prefix, unsigned char kind, const char *app, const char *name,
                       const char *path, const char *cover = nullptr)
{
    if (!estante::import_slot_available(count, max_games, emulator_limit_reached) || !name || !path ||
        std::strlen(name) >= sizeof games[0].name || std::strlen(path) >= sizeof emulator_path[0] ||
        (cover && std::strlen(cover) >= sizeof emulator_cover[0]))
        return;
    for (int i = 0; i < count; i++)
        if (emulator_app[i][0] && !std::strcmp(emulator_app[i], app) &&
            !std::strcmp(emulator_path[i], path))
            return;
    unsigned hash = 2166136261u;
    for (const char *p = path; *p; p++)
        hash = (hash ^ static_cast<unsigned char>(*p)) * 16777619u;
    char id[10]{};
    for (unsigned salt = 0;; salt++)
    {
        std::snprintf(id, sizeof id, "%s%06X", prefix, (hash + salt) & 0xffffffu);
        bool taken = false;
        for (int i = 0; i < count; i++)
            taken = taken || !std::strcmp(games[i].id, id);
        if (!taken)
            break;
    }
    std::snprintf(games[count].id, sizeof games[count].id, "%s", id);
    std::snprintf(games[count].name, sizeof games[count].name, "%s", name);
    std::snprintf(emulator_app[count], sizeof emulator_app[count], "%s", app);
    std::snprintf(emulator_path[count], sizeof emulator_path[count], "%s", path);
    if (cover)
        std::snprintf(emulator_cover[count], sizeof emulator_cover[count], "%s", cover);
    emulator_kind[count] = kind;
    count++;
}
void add_swan_games(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, entries)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(entry, "dir");
        if (!cJSON_IsString(name) || !cJSON_IsTrue(dir))
            continue;
        char path[200], cover[200];
        if (!estante::make_swan_paths(swan_games_dir, "/data/SwanStationPS5/covers/boxart",
                                      name->valuestring, path, sizeof path, cover, sizeof cover))
            continue;
        add_emulator_game("PS1", 1, "PPSA98510", name->valuestring, path, cover);
    }
    cJSON_Delete(root);
}
void save_ps1_cache()
{
    if (!storage_ok)
        return;
    static char text[ps1_cache_max * 120];
    std::size_t used = 0;
    for (int i = 0; i < ps1_cache_count && used + 120 < sizeof text; i++)
        used += static_cast<std::size_t>(std::snprintf(text + used, sizeof text - used, "%s|%s\n",
                                                       ps1_cache_serial[i], ps1_cache_name[i]));
    text[used] = 0;
    write_file("/download0/ps1-serials.txt", text);
}
void load_ps1_cache()
{
    char *text = read_file("/download0/ps1-serials.txt");
    if (!text)
        return;
    char *line = text;
    while (*line && ps1_cache_count < ps1_cache_max)
    {
        char *end = std::strchr(line, '\n');
        if (end)
            *end = 0;
        char *bar = std::strchr(line, '|');
        if (bar && bar - line < 12 && std::strlen(bar + 1) < sizeof ps1_cache_name[0])
        {
            *bar = 0;
            std::snprintf(ps1_cache_serial[ps1_cache_count], sizeof ps1_cache_serial[0], "%s", line);
            std::snprintf(ps1_cache_name[ps1_cache_count], sizeof ps1_cache_name[0], "%s", bar + 1);
            ps1_cache_count++;
        }
        if (!end)
            break;
        line = end + 1;
    }
    std::free(text);
}
int ps1_cached(const char *name)
{
    for (int i = 0; i < ps1_cache_count; i++)
        if (!std::strcmp(ps1_cache_name[i], name))
            return i;
    return -1;
}
void forget_failed_cover(const char *id)
{
    for (int i = 0; i < failed_cover_count; i++)
        if (!std::strcmp(failed_covers[i], id))
        {
            std::memmove(failed_covers[i], failed_covers[i + 1],
                         static_cast<std::size_t>(failed_cover_count - i - 1) * sizeof failed_covers[0]);
            failed_cover_count--;
            return;
        }
}
// Which PS1 games already have a cover named after their title (listing of covers/boxart).
void apply_swan_boxart(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    for (int i = 0; i < count; i++)
    {
        char wanted[200];
        if (emulator_kind[i] != 1 || !estante::swan_cover_filename(games[i].name, wanted, sizeof wanted))
            continue;
        const cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, entries)
        {
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
            if (cJSON_IsString(name) && name->valuestring && !std::strcmp(name->valuestring, wanted))
            {
                swan_boxart[i] = true;
                break;
            }
        }
    }
    cJSON_Delete(root);
}
// The serial covers SwanStationPS5 downloaded (listing of covers/default).
void fill_ps1_default(const char *json)
{
    ps1_default_count = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, entries)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        if (cJSON_IsString(name) && name->valuestring && std::strlen(name->valuestring) < sizeof ps1_default[0] &&
            ps1_default_count < 256)
            std::snprintf(ps1_default[ps1_default_count++], sizeof ps1_default[0], "%s", name->valuestring);
    }
    cJSON_Delete(root);
}
// True when start_request can send this path: after percent-encoding it must fit the request buffer. A request
// that cannot be sent would end the whole scan chain, so a game with a name that long is skipped instead.
bool request_fits(const char *path)
{
    char encoded[sizeof request_path];
    return estante::encode_fs_request(path, encoded, sizeof encoded);
}
// Next PS1 game with no title cover and an unread serial: asks for its folder listing.
bool next_ps1_job(char *path, std::size_t cap)
{
    for (int i = 0; i < count; i++)
    {
        if (emulator_kind[i] != 1 || swan_boxart[i] || ps1_state[i] != 0)
            continue;
        const int cached = ps1_cached(games[i].name);
        if (cached >= 0)
        {
            const bool known = std::strcmp(ps1_cache_serial[cached], "-") != 0;
            ps1_state[i] = known ? 1 : 2;
            std::snprintf(ps1_serial[i], sizeof ps1_serial[0], "%s", known ? ps1_cache_serial[cached] : "");
            continue;
        }
        const int n = std::snprintf(path, cap, "/api/fs/list?path=%s/%s", swan_games_dir, games[i].name);
        if (n < 0 || static_cast<std::size_t>(n) + 1 >= cap || !request_fits(path))
        {
            ps1_state[i] = 2; // folder name too long to ask for
            continue;
        }
        ps1_job = i;
        return true;
    }
    return false;
}
// From the folder listing: the first disc image (the data track of a cue sheet comes first by name).
bool ps1_track_request(const char *json, char *path, std::size_t cap)
{
    static const char *const formats[] = {".bin", ".img", ".iso"};
    char best[200]{};
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, entries)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(entry, "dir");
        if (cJSON_IsString(name) && name->valuestring && !cJSON_IsTrue(dir) &&
            std::strlen(name->valuestring) < sizeof best &&
            emulator_extension(name->valuestring, formats, 3) &&
            (!best[0] || std::strcmp(name->valuestring, best) < 0))
            std::snprintf(best, sizeof best, "%s", name->valuestring);
    }
    cJSON_Delete(root);
    if (!best[0] || ps1_job < 0 || ps1_job >= count)
        return false;
    const int n = std::snprintf(path, cap, "/api/fs/download?path=%s/%s/%s", swan_games_dir,
                                games[ps1_job].name, best);
    return n > 0 && static_cast<std::size_t>(n) + 1 < cap && request_fits(path);
}
void finish_ps1_job(const char *serial)
{
    if (ps1_job < 0 || ps1_job >= count)
        return;
    std::snprintf(ps1_serial[ps1_job], sizeof ps1_serial[0], "%s", serial ? serial : "");
    ps1_state[ps1_job] = serial ? 1 : 2;
    if (std::strlen(games[ps1_job].name) < sizeof ps1_cache_name[0] && ps1_cache_count < ps1_cache_max)
    {
        std::snprintf(ps1_cache_name[ps1_cache_count], sizeof ps1_cache_name[0], "%s", games[ps1_job].name);
        std::snprintf(ps1_cache_serial[ps1_cache_count], sizeof ps1_cache_serial[0], "%s",
                      serial ? serial : "-");
        ps1_cache_count++;
        save_ps1_cache();
    }
    ps1_job = -1;
}
// Games with no cover by title but a serial cover on disk use that one. A cover by title always wins.
void apply_ps1_covers()
{
    bool changed = false;
    for (int i = 0; i < count; i++)
    {
        if (emulator_kind[i] != 1 || swan_boxart[i] || ps1_state[i] != 1)
            continue;
        char file[24];
        std::snprintf(file, sizeof file, "%s.jpg", ps1_serial[i]);
        for (int d = 0; d < ps1_default_count; d++)
            if (!std::strcmp(ps1_default[d], file))
            {
                std::snprintf(emulator_cover[i], sizeof emulator_cover[i], "%s/%s", swan_default_dir, file);
                forget_failed_cover(games[i].id);
                changed = true;
                break;
            }
    }
    if (changed)
        cover_version++;
}
// What to ask for after a PS1 step: the next unread game, or (all done) the Porpoise listing.
int ps1_continue(char *path, std::size_t cap)
{
    if (next_ps1_job(path, cap))
        return 19;
    apply_ps1_covers();
    std::snprintf(path, cap, "/api/fs/list?path=%s", porpoise_games_dir);
    return 14;
}
void add_porpoise_games(const char *json)
{
    static const char *const formats[] = {".iso", ".gcm", ".rvz", ".ciso", ".gcz", ".wbfs",
                                          ".wia", ".wad"};
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, entries)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(entry, "dir");
        if (!cJSON_IsString(name) || cJSON_IsTrue(dir) ||
            !emulator_extension(name->valuestring, formats, 8))
            continue;
        char path[200];
        if (!estante::join_import_path(porpoise_games_dir, name->valuestring, path, sizeof path))
            continue;
        char title[192];
        add_emulator_game("WII", 2, "PPSA99764",
                          estante::clean_rom_title(name->valuestring, title, sizeof title) ? title
                                                                                          : name->valuestring,
                          path);
    }
    cJSON_Delete(root);
}
void apply_porpoise_cache(char *text)
{
    // disc-cache.tsv is written by Porpoise itself. Its first field is the absolute image path and
    // its fifth field is the six-character disc ID used for the local PNG cover.
    for (char *line = text; line && *line;)
    {
        char *next = std::strchr(line, '\n');
        if (next)
            *next = 0;
        char *fields[8]{};
        int field_count = 0;
        char *part = line;
        while (part && field_count < 8)
        {
            fields[field_count++] = part;
            part = std::strchr(part, '\t');
            if (part)
                *part++ = 0;
        }
        if (field_count >= 5 && std::strlen(fields[4]) >= 6)
            for (int i = 0; i < count; ++i)
                if (emulator_kind[i] == 2 && !std::strcmp(emulator_path[i], fields[0]))
                {
                    std::snprintf(emulator_cover[i], sizeof emulator_cover[i],
                                  "/data/porpoise/covers/%.6s.png", fields[4]);
                    // The file name, cleaned of region tags, is what the player recognises. Porpoise's own
                    // disc title ("MarioParty4", "SUPER MARIO GALAXY MORE") is only a fallback for files whose
                    // name says nothing.
                    if (field_count >= 7 && fields[6][0] && estante::meaningless_title(games[i].name))
                        std::snprintf(games[i].name, sizeof games[i].name, "%s", fields[6]);
                    break;
                }
        line = next ? next + 1 : nullptr;
    }
}
void add_eden_games(const char *json)
{
    static const char *const formats[] = {".nsp", ".xci", ".nsz", ".xcz"};
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, entries)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *dir = cJSON_GetObjectItemCaseSensitive(entry, "dir");
        if (!cJSON_IsString(name) || !name->valuestring || name->valuestring[0] == '.' ||
            (!cJSON_IsTrue(dir) && !emulator_extension(name->valuestring, formats, 4)))
            continue;
        char path[200];
        if (!estante::join_import_path(eden_roms_dir, name->valuestring, path, sizeof path))
            continue;
        char title[192];
        add_emulator_game("NSW", 6, "PPSA99008",
                          cJSON_IsTrue(dir) ? name->valuestring
                          : estante::clean_rom_title(name->valuestring, title, sizeof title) ? title
                                                                                           : name->valuestring,
                          path);
    }
    cJSON_Delete(root);
}
// covers/ lists "<hash>.tga" and "<hash>.name" pairs: remember the hashes, then read each .name.
void read_eden_cover_list(const char *json)
{
    eden_cover_count = 0;
    eden_cover_job = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *entries = root ? cJSON_GetObjectItemCaseSensitive(root, "entries") : nullptr;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, entries)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        if (!cJSON_IsString(name) || !name->valuestring || eden_cover_count >= 64)
            continue;
        const std::size_t length = std::strlen(name->valuestring);
        if (length < 6 || length - 5 >= sizeof eden_cover_hash[0] ||
            std::strcmp(name->valuestring + length - 5, ".name") != 0)
            continue;
        std::memcpy(eden_cover_hash[eden_cover_count], name->valuestring, length - 5);
        eden_cover_hash[eden_cover_count][length - 5] = 0;
        eden_cover_count++;
    }
    cJSON_Delete(root);
}
// The title read from covers/<hash>.name belongs to the Switch game with the same title.
void apply_eden_cover(const char *title)
{
    if (eden_cover_job < 0 || eden_cover_job >= eden_cover_count)
        return;
    for (int i = 0; i < count; i++)
        if (emulator_kind[i] == 6 && !emulator_cover[i][0] && estante::same_title(games[i].name, title))
        {
            std::snprintf(emulator_cover[i], sizeof emulator_cover[i], "%s/%s.tga", eden_covers_dir,
                          eden_cover_hash[eden_cover_job]);
            break;
        }
}
bool next_eden_cover_job(char *path, std::size_t cap)
{
    if (eden_cover_job >= eden_cover_count)
        return false;
    std::snprintf(path, cap, "/api/fs/download?path=%s/%s.name", eden_covers_dir,
                  eden_cover_hash[eden_cover_job]);
    return true;
}
void add_ps5cemu_system(const cJSON *items, const char *prefix, const char *fallback, unsigned char kind)
{
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, items)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(entry, "path");
        const cJSON *title_id = cJSON_GetObjectItemCaseSensitive(entry, "titleId");
        char cover[200]{};
        if (cJSON_IsString(title_id) && title_id->valuestring)
            std::snprintf(cover, sizeof cover, "/data/ps5cemu/covers/%s.tga", title_id->valuestring);
        if (cJSON_IsString(name) && cJSON_IsString(path))
            add_emulator_game(prefix, kind, "PPSA99360", name->valuestring, path->valuestring,
                              cover[0] ? cover : nullptr);
        else if (cJSON_IsString(path))
            add_emulator_game(prefix, kind, "PPSA99360", fallback, path->valuestring,
                              cover[0] ? cover : nullptr);
    }
}
void add_ps5cemu_games(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root)
    {
        add_ps5cemu_system(cJSON_GetObjectItemCaseSensitive(root, "wiiu"), "WIU", "Wii U game", 3);
        add_ps5cemu_system(cJSON_GetObjectItemCaseSensitive(root, "3ds"), "3DS", "Nintendo 3DS game", 4);
        add_ps5cemu_system(cJSON_GetObjectItemCaseSensitive(root, "ds"), "NDS", "Nintendo DS game", 5);
    }
    cJSON_Delete(root);
}
// End of the scan chain: the 1024-game warning, or the one-time tip about the style switch.
void finish_scan()
{
    if (estante::take_limit_warning(emulator_limit_reached))
    {
        msg("Limite de 1024 jogos: alguns não foram importados.");
    }
    else if (!style_tip_seen && screen == 0 && count > 0)
    {
        // Once, when the first scan ends: the style switch is easy to miss in Ajustes.
        style_tip_seen = true;
        save_preferences();
        msg("Dica: L2 + R2 troca entre Estante e Catálogo.");
    }
}
void poll_work()
{
    if (work.load(std::memory_order_acquire) != 2)
        return;
    int next = 0;
    char path[320]{};
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
            std::memset(emulator_app, 0, sizeof emulator_app);
            std::memset(emulator_path, 0, sizeof emulator_path);
            std::memset(emulator_cover, 0, sizeof emulator_cover);
            std::memset(emulator_kind, 0, sizeof emulator_kind);
            std::memset(swan_boxart, 0, sizeof swan_boxart);
            std::memset(ps1_serial, 0, sizeof ps1_serial);
            std::memset(ps1_state, 0, sizeof ps1_state);
            ps1_job = -1;
            emulator_limit_reached = false;
            std::memset(selected, 0, sizeof selected);
            std::memset(played_last, 0, sizeof played_last);
            std::memset(played_seconds, 0, sizeof played_seconds);
            clear_trophy_summaries();
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
            next = 21; // serials PS5SX2 already knows, for the images the disc reader cannot open
            std::snprintf(path, sizeof path, "/api/fs/download?path=%s", ps2_listed_serials);
        }
        if (!next)
        {
            next = 13;
            std::snprintf(path, sizeof path, "/api/fs/list?path=%s", swan_games_dir);
        }
    }
    else if (work_kind == 21)
    {
        if (response_ok && apply_ps2_listed_serials(response))
            cover_version++;
        if (next_ps2_job(path, sizeof path))
            next = 8;
        else
        {
            next = 13;
            std::snprintf(path, sizeof path, "/api/fs/list?path=%s", swan_games_dir);
        }
    }
    else if (work_kind == 8)
    {
        char serial[16];
        const bool found =
            response_ok && iso_boot_serial(reinterpret_cast<const unsigned char *>(response),
                                           response_size, serial, sizeof serial);
        finish_ps2_job(found ? serial : nullptr);
        cover_version++;
        if (next_ps2_job(path, sizeof path))
            next = 8;
        else
        {
            next = 13;
            std::snprintf(path, sizeof path, "/api/fs/list?path=%s", swan_games_dir);
        }
    }
    else if (work_kind == 13)
    {
        if (response_ok && editing_collection < 0)
        {
            add_swan_games(response);
            rebuild();
        }
        next = 17;
        std::snprintf(path, sizeof path, "/api/fs/list?path=%s", swan_boxart_dir);
    }
    else if (work_kind == 17)
    {
        if (response_ok && editing_collection < 0)
            apply_swan_boxart(response);
        next = 18;
        std::snprintf(path, sizeof path, "/api/fs/list?path=%s", swan_default_dir);
    }
    else if (work_kind == 18)
    {
        if (response_ok && editing_collection < 0)
            fill_ps1_default(response);
        next = ps1_continue(path, sizeof path);
    }
    else if (work_kind == 19)
    {
        // Folder listing of one PS1 game: ask for the start of its first disc image.
        if (response_ok && ps1_track_request(response, path, sizeof path))
            next = 20;
        else
        {
            finish_ps1_job(nullptr);
            next = ps1_continue(path, sizeof path);
        }
    }
    else if (work_kind == 20)
    {
        char serial[16];
        const bool found = response_ok && estante::ps1_boot_serial(reinterpret_cast<const unsigned char *>(response),
                                                                    response_size, serial, sizeof serial);
        finish_ps1_job(found ? serial : nullptr);
        next = ps1_continue(path, sizeof path);
    }
    else if (work_kind == 14)
    {
        if (response_ok && editing_collection < 0)
        {
            add_porpoise_games(response);
            rebuild();
        }
        next = 16;
        std::snprintf(path, sizeof path, "/api/fs/download?path=/data/porpoise/disc-cache.tsv");
    }
    else if (work_kind == 16)
    {
        if (response_ok && editing_collection < 0)
        {
            apply_porpoise_cache(response);
            rebuild();
        }
        next = 15;
        std::snprintf(path, sizeof path, "/api/fs/download?path=%s", ps5cemu_library);
    }
    else if (work_kind == 15)
    {
        if (response_ok && editing_collection < 0)
        {
            add_ps5cemu_games(response);
            rebuild();
        }
        next = 22;
        std::snprintf(path, sizeof path, "/api/fs/list?path=%s", eden_roms_dir);
    }
    else if (work_kind == 22)
    {
        if (response_ok && editing_collection < 0)
        {
            add_eden_games(response);
            rebuild();
        }
        next = 23;
        std::snprintf(path, sizeof path, "/api/fs/list?path=%s", eden_covers_dir);
    }
    else if (work_kind == 23)
    {
        eden_cover_count = eden_cover_job = 0;
        if (response_ok && editing_collection < 0)
            read_eden_cover_list(response);
        if (next_eden_cover_job(path, sizeof path))
            next = 24;
        else
            finish_scan();
    }
    else if (work_kind == 24)
    {
        if (response_ok)
            apply_eden_cover(response);
        eden_cover_job++;
        cover_version++;
        if (next_eden_cover_job(path, sizeof path))
            next = 24;
        else
            finish_scan();
    }
    else if (work_kind == 10)
    {
        if (response_ok && parse_trophy_user() && trophy_index >= 0 && trophy_index < count)
        {
            next = 11;
            std::snprintf(path, sizeof path,
                          "/api/fs/download?path=/system_data/priv/appmeta/%s/trophy2/npbind.dat",
                          games[trophy_index].id);
        }
        else if (trophy_index >= 0 && trophy_index < count)
            trophies[trophy_index].state = 3;
    }
    else if (work_kind == 11)
    {
        if (response_ok && parse_npbind() && trophy_index >= 0 && trophy_index < count &&
            trophy_user_id[0])
        {
            next = 12;
            std::snprintf(
                path, sizeof path,
                "/api/fs/download?path=/user/home/%s/trophy2/nobackup/data/%s/TRPTITLE.DAT",
                trophy_user_id, trophy_npwr);
        }
        else if (trophy_index >= 0 && trophy_index < count)
            trophies[trophy_index].state = 3;
    }
    else if (work_kind == 12)
    {
        if (trophy_index >= 0 && trophy_index < count)
        {
            TrophySummary &summary = trophies[trophy_index];
            summary.state = response_ok && parse_trophy_title(summary) ? 2 : 3;
        }
    }
    else if (work_kind == 3)
    {
        if (cover_ok)
        {
            int slot = 0;
            for (int i = 0; i < cover_slots; i++)
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
            dest.has_tall = tall_ok;
            if (tall_ok)
                std::memcpy(dest.tall, decoded_tall, sizeof decoded_tall);
            if (gpu_canvas)
            {
                gpu_canvas->invalidate(dest.tall);
                gpu_canvas->invalidate(dest.pixels);
                gpu_canvas->invalidate(dest.thumb);
            }
#endif
        }
        else if (failed_cover_count < max_games)
            std::snprintf(failed_covers[failed_cover_count++], 10, "%s", requested_cover);
        cover_version++;
    }
#ifdef ESTANTE_GL
    else if (work_kind == 9)
    {
        if (wide_ok)
        {
            std::memcpy(wide_art, decoded_wide, sizeof wide_art);
            wide_art_state = 2;
            std::snprintf(wide_have, sizeof wide_have, "%s", wide_art_id);
            if (gpu_canvas)
                gpu_canvas->invalidate(wide_art);
        }
        else
            wide_art_state = 3;
    }
#endif
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
            // "Continue playing" is built from this: only what the player started from Estante, and
            // for a PS2 image the image itself (launch_id is PS5SX2 there).
            Library nextdb = db;
            auto *m = metadata(nextdb, launch_game);
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
// A PS2 image is not a title the console can start: pressing X opens PS5SX2 itself and the game is
// picked on its shelf.
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
    std::snprintf(launch_game, 10, "%s", games[idx].id);
    if (emulator_app[idx][0])
    {
        // Elf Arsenal launches PS5 titles by id only.  Until a per-game forwarder exists for this
        // frontend, this opens the right library and preserves the selected virtual game as the
        // player's Continue entry.
        std::snprintf(launch_id, sizeof launch_id, "%s", emulator_app[idx]);
        launch_pending = true;
        verify_at = 0;
        msg("Abrindo o emulador.");
        start_request(4, "/api/state");
        return;
    }
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
    if (n > 0)
    {
        auto axis = [](std::uint8_t v)
        {
            const float f = (static_cast<float>(v) - 128.0f) / 127.0f;
            return f > -0.18f && f < 0.18f ? 0.0f : f;
        };
        stick_rx = axis(samples[n - 1].analog[2]);
        stick_ry = axis(samples[n - 1].analog[3]);
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
    details_page = false;
    details_index = -1;
    library_zone = 0;
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
    ThemeNext,
    HomeStyle,
    RestoreConfirm
};
// Ajustes, in the order shown: how it looks first, then how it behaves, then the library chores.
constexpr Action settings_actions[settings_count] = {HomeStyle, ThemeNext, TextSize, Animation, Language,
                                                     Record,    Refresh,   NewFolder, Export,    Restore};
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
        const bool batch = screen == 1 && chosen_count() > 0 && !details_page;
        if (batch || !m || !m->favorite)
            add_menu(FavoriteOn, "Marcar como favorito");
        if (batch || (m && m->favorite))
            add_menu(FavoriteOff, "Remover dos favoritos");
        if (batch || !m || !m->pinned)
            add_menu(PinOn, "Fixar no Início");
        if (batch || (m && m->pinned))
            add_menu(PinOff, "Desafixar do Início");
        if (!batch && !details_page)
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
// Catalogue rows <-> the 3D shelf. Also reachable with L2 + R2 on the Inicio, so nobody has to find Ajustes.
void toggle_home_style()
{
    home_style = !home_style;
#ifdef ESTANTE_GL
    show_back = false;
#endif
    rebuild();
    save_preferences();
}
void action(Action a)
{
    modal = 0;
    switch (a)
    {
    case Details:
        open_details(target_index());
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
        if (details_page)
            close_details();
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
        i18n::language = (i18n::language + 1) % i18n::language_count;
        ui_version++;
        save_preferences();
        break;
    case Animation:
        launch_anim_enabled = !launch_anim_enabled;
        ui_version++;
        save_preferences();
        break;
    case ThemeNext:
        apply_theme(theme_index + 1);
        ui_version++;
        save_preferences();
        break;
    case HomeStyle:
        toggle_home_style();
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
#ifdef ESTANTE_GL
        if (wide_art_state == 3)
            wide_art_state = 0; // look for the wide pictures again
#endif
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
            open_details(filtered[cursor]);
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
void close_details()
{
    details_page = false;
    details_index = -1;
    ui_version++;
}
void toggle_favorite(int idx)
{
    const auto *m = meta(idx);
    const bool on = !(m && m->favorite);
    update_targets([on](Meta &x) { x.favorite = on; });
}
void open_filters()
{
    nav_cursor = 0;
    for (int i = 0; i < view_order_count; i++)
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
    // L2 + R2 together on the Inicio switch between the catalogue and the shelf. Both buttons are checked as
    // held, so pressing them at once or one after the other works; the chord replaces the single-button action.
    if (screen == 0 && !details_page && !modal && editing_collection < 0 && (b & (L2 | R2)) &&
        (held & (L2 | R2)) == (L2 | R2))
    {
        toggle_home_style();
        style_tip_seen = true;
        save_preferences();
        msg(home_style ? "Estilo Estante. L2 + R2 volta ao Catálogo."
                       : "Estilo Catálogo. L2 + R2 volta à Estante.");
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
    if (details_page)
    {
        const int idx = details_index;
        if (b & CIRCLE)
            close_details();
        else if ((b & LEFT) && details_button > 0)
            details_button--;
        else if ((b & RIGHT) && details_button < 2)
            details_button++;
        else if (b & (TRIANGLE | OPTIONS))
            open_menu(0);
        else if (b & SQUARE)
            toggle_favorite(idx);
        else if (b & CROSS)
        {
            if (details_button == 0)
                queue_launch(idx);
            else if (details_button == 1)
                toggle_favorite(idx);
            else
                open_menu(0);
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
    if (screen == 0 && home_style == 0)
    {
        const int idx = home_game();
        if (b & SQUARE)
        {
            open_library(0);
            open_keyboard(1);
            return;
        }
        if (b & OPTIONS)
        {
            open_library(0);
            return;
        }
        if ((b & TRIANGLE) && idx >= 0)
        {
            open_menu(0);
            return;
        }
        if (!rail_count)
            return;
        HomeRail &r = rails[home_row];
        if ((b & LEFT) && r.cursor > 0)
            r.cursor--;
        if ((b & RIGHT) && r.cursor + 1 < r.count)
            r.cursor++;
        if ((b & UP) && home_row > 0)
            home_row--;
        if ((b & DOWN) && home_row + 1 < rail_count)
            home_row++;
        if ((b & R2) && idx >= 0)
        {
            open_details(idx);
            return;
        }
        if (b & CROSS)
        {
            const int item = r.items[r.cursor];
            if (item >= 0)
                queue_launch(item);
            else
                open_library(r.kind == Rail::Favorites ? 5
                             : r.kind == Rail::Want     ? 8
                             : r.kind == Rail::PS5      ? 3
                             : r.kind == Rail::PS4      ? 2
                             : r.kind == Rail::PS2      ? 1
                                                        : 100 + r.folder);
        }
        return;
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
                open_details(home_games[home_cursor]);
            }
            if ((b & TRIANGLE) && home_count)
                open_menu(0);
#ifdef ESTANTE_GL
            if ((b & (R2 | L2)) && home_count)
                show_back = !show_back; // turn the case over
            if ((b & CIRCLE) && show_back)
                show_back = false;
#endif
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
        if (b & TRIANGLE) // search; organizing a game moved to Options and the game page
        {
            open_keyboard(1);
            return;
        }
        if ((b & CIRCLE) && library_zone)
        {
            library_zone = 0;
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
                nav_cursor = (nav_cursor + view_order_count - 1) % view_order_count;
            if (b & DOWN)
                nav_cursor = (nav_cursor + 1) % view_order_count;
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
        if (library_zone == 1)
        {
            if ((b & LEFT) && library_chip > 0)
                library_chip--;
            if ((b & RIGHT) && library_chip < 3)
                library_chip++;
            if (b & DOWN)
                library_zone = 0;
            if (b & CROSS)
            {
                if (library_chip == 0)
                    open_keyboard(1);
                else if (library_chip == 1)
                    open_filters();
                else if (library_chip == 2)
                    action(Sort);
                else
                    action(Filter);
            }
            return;
        }
        if (!filtered_count)
        {
            if (b & LEFT)
                open_filters();
            if (b & UP)
                library_zone = 1;
            return;
        }
        if (b & LEFT)
        {
            if (cursor % lib_cols == 0)
                open_filters();
            else
                cursor--;
        }
        if ((b & RIGHT) && cursor % lib_cols < lib_cols - 1 && cursor + 1 < filtered_count)
            cursor++;
        if (b & UP)
        {
            if (cursor >= lib_cols)
                cursor -= lib_cols;
            else
                library_zone = 1; // the chips above the grid: search, view, sort, filters
        }
        if ((b & DOWN) && cursor / lib_cols < (filtered_count - 1) / lib_cols)
            cursor = std::min(cursor + lib_cols, filtered_count - 1);
        if (b & SQUARE) // choosing games for a collection, otherwise the organize menu for this game
        {
            if (editing_collection >= 0)
                selected[filtered[cursor]] = !selected[filtered[cursor]];
            else
                open_menu(0);
        }
        if (b & CROSS)
        {
            if (editing_collection >= 0)
                selected[filtered[cursor]] = !selected[filtered[cursor]];
            else
            {
                open_details(filtered[cursor]);
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
            settings_cursor = (settings_cursor + settings_count - 1) % settings_count;
        if (b & DOWN)
            settings_cursor = (settings_cursor + 1) % settings_count;
        if (b & CIRCLE)
            set_screen(0);
        if (b & CROSS)
            action(settings_actions[settings_cursor]);
    }
}

Color shade(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Color>(0xff000000u | (b << 16) | (g << 8) | r);
}
// The GPU font draws any size; the CPU font only has a few baked faces, so sizes snap to the nearest.
unsigned face(unsigned size)
{
#ifdef ESTANTE_GL
    return size;
#else
    static constexpr unsigned faces[] = {24, 28, 32, 44, 64};
    unsigned best = faces[0];
    for (unsigned f : faces)
        if ((f > size ? f - size : size - f) <= (best > size ? best - size : size - best))
            best = f;
    return best;
#endif
}
unsigned measure_face(Canvas &c, const char *s, unsigned size)
{
    return c.measure(s, face(size));
}
void text(Canvas &c, unsigned x, unsigned y, const char *s, unsigned size = 28, Color color = white,
          unsigned width = 1700)
{
    if (large_text && size < 32)
        size = 32;
    c.label(x, y, tr(s), face(size), color, width);
}
void line(Canvas &c, unsigned x, unsigned y, unsigned width)
{
    c.rectangle(x, y, width, 1, shade(56, 65, 77));
}
void pill(Canvas &c, unsigned x, unsigned y, unsigned width, const char *label, bool active = false)
{
    c.rounded_rectangle(x, y, width, 44, 12, active ? accent : panel);
    text(c, x + 16, y + 3, label, 24, active ? on_accent() : muted, width - 24);
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
        text(c, x + 20, y + edge - 55, is_ps2_id(games[idx].id) ? games[idx].name : games[idx].id,
             24, muted, edge - 40);
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
    text(c, 100, 30, "estante", 40, white, 260);
    text(c, 285, 44, "0.5.5", 22, muted, 90);
    const char *tabs[] = {"Início", "Biblioteca", "Coleções", "Ajustes"};
    unsigned widths[4], total = 0;
    for (int i = 0; i < 4; i++)
    {
        widths[i] = measure_face(c, tr(tabs[i]), 28) + 56;
        total += widths[i] + (i ? 8 : 0);
    }
    unsigned x = (1920 - total) / 2; // the tabs sit in the middle of the screen
    for (int i = 0; i < 4; i++)
    {
        if (screen == i)
            c.rounded_rectangle(x, 24, widths[i], 56, 28, white);
        text(c, x + 28, 35, tabs[i], 28, screen == i ? bg : muted, widths[i]);
        x += widths[i] + 8;
    }
    const unsigned handle = measure_face(c, "@kndofc", 26);
    text(c, 1820 - handle, 38, "@kndofc", 26, muted, 220);
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
#ifdef ESTANTE_GL
// GPU cover flow: positions are fractional, so the carousel slides with a spring when the cursor
// moves, cards shrink and fade toward the sides, and each cover has a soft reflection on the floor
// below it. The back of a case, drawn into a 640x960 picture: the game's data in the player's own
// numbers.
void draw_case_back(Canvas &c, int idx, const Cover *cover)
{
    const auto *m = meta(idx);
    const Color tint = cover ? static_cast<Color>(cover->tint) : accent;
    const bool art = wide_art_state == 2 && !std::strcmp(wide_art_id, games[idx].id);
    const Color base = shade(14, 16, 22);
    c.begin_back();
    c.gradient(0, 0, 640, 960, mix_color(tint, shade(18, 20, 27), 150), base);
    if (art)
    {
        c.list().image(c.texture_for(wide_art, wide_w, wide_h), {0, 0, 640, 360}, hui::gfx::kFullUv,
                       {1, 1, 1, 1}, 0.0f);
        c.gradient(0, 250, 640, 112,
                   static_cast<Color>(static_cast<std::uint32_t>(base) & 0x00ffffffu), base);
    }
    else
    {
        if (cover && cover->has_tall)
            c.list().image(c.texture_for(cover->tall, tall_w, tall_h), {0, 0, 640, 960},
                           hui::gfx::kFullUv, {1, 1, 1, 0.13f}, 0.0f);
        c.blend(0, 0, 640, 960, shade(11, 13, 18), 70);
    }
    const unsigned top = art ? 372 : 50;
    c.rounded_rectangle(44, top + 6, 8, 112, 4, accent);
    c.label(72, top, games[idx].name, 44, white, 530);
    c.label(72, top + 68, system_name(idx), 26, muted, 530);
    const unsigned line = art ? 480 : 196, step = art ? 92 : 120, first = line + (art ? 14 : 34);
    c.rectangle(44, line, 552, 2, mix_color(white, base, 40));
    auto row = [&](unsigned y, const char *label, const char *value, Color color)
    {
        c.label(44, y, tr(label), 22, muted, 552);
        c.label(44, y + 30, value, 32, color, 552);
    };
    char played[48], ago[48], names[160];
    format_playtime(played_seconds[idx], played, sizeof played);
    format_ago(played_last[idx], ago, sizeof ago);
    names[0] = 0;
    for (int f = 0; f < max_folders; f++)
        if (db.folders[f].used && collection_contains(db, games[idx], f, false))
        {
            const std::size_t len = std::strlen(names);
            if (len + std::strlen(db.folders[f].name) + 4 < sizeof names)
                std::snprintf(names + len, sizeof names - len, "%s%s", len ? ", " : "",
                              db.folders[f].name);
        }
    row(first, "Tempo de jogo", played[0] ? played : tr("Sem tempo registrado"), white);
    row(first + step, "Última vez", ago[0] ? ago : tr("Nunca jogado"), white);
    row(first + step * 2, "Status", tr(statuses[m ? m->status : 0]),
        m && m->status == 3 ? accent : white);
    row(first + step * 3, "Coleções", names[0] ? names : tr("Sem coleção"), white);
    c.rectangle(44, 880, 552, 2, mix_color(white, base, 40));
    c.label(44, 898, games[idx].id, 24, muted, 300);
    c.label(380, 898, "estante", 24, accent, 216);
    c.end_back();
}
// The spine: the name runs down the left edge of the case.
void draw_case_spine(Canvas &c, int idx, const Cover *cover)
{
    const Color tint = cover ? static_cast<Color>(cover->tint) : accent;
    c.begin_spine();
    c.gradient(0, 0, 960, 128, mix_color(tint, shade(18, 20, 27), 120), shade(12, 14, 19));
    c.rectangle(0, 0, 14, 128, accent);
    c.label(48, 30, games[idx].name, 56, white, 700);
    c.label(800, 38, system_tag(idx), 44, accent, 140);
    c.end_spine();
}
float flow_lerp(const float *table, float distance)
{
    if (distance >= 3.0f)
        return table[3];
    const int i = static_cast<int>(distance);
    return table[i] + (table[i + 1] - table[i]) * (distance - static_cast<float>(i));
}
void draw_cover_flow(Canvas &c)
{
    static float position = -1000.0f;
    static std::uint64_t last_time = 0, flip_start = 0;
    static int last_cursor = -1;
    const float target = static_cast<float>(home_cursor);
    const std::uint64_t now = sceKernelGetProcessTime();
    if (home_cursor != last_cursor)
    {
        if (last_cursor >= 0)
            flip_start = now; // the newly focused cover turns in like a card
        last_cursor = home_cursor;
        show_back = false;
    }
    static float user_yaw = 0.0f, user_pitch = 0.0f;
    static int last_cursor_seen = -1;
    {
        // The right stick turns the focused case freely; let go and it eases back to rest.
        const float step =
            last_time && now > last_time ? static_cast<float>(now - last_time) / 1.0e6f : 0.0f;
        const float dts = step > 0.05f ? 0.05f : step;
        if (home_cursor != last_cursor_seen)
        {
            last_cursor_seen = home_cursor;
            user_yaw = user_pitch = 0.0f;
        }
        user_yaw += stick_rx * 3.6f * dts;
        user_pitch += stick_ry * 2.2f * dts;
        user_pitch = user_pitch < -0.7f ? -0.7f : user_pitch > 0.7f ? 0.7f : user_pitch;
        if (stick_rx == 0.0f)
        {
            const float turn = 6.2831853f;
            user_yaw -= turn * std::floor(user_yaw / turn + 0.5f); // nearest way home
            user_yaw *= std::exp(-7.0f * dts);
            if (std::fabs(user_yaw) < 0.002f)
                user_yaw = 0.0f;
        }
        if (stick_ry == 0.0f)
            user_pitch *= std::exp(-7.0f * dts);
    }
    static float back_amount = 0.0f;
    {
        const float goal = show_back ? 1.0f : 0.0f;
        const float step = last_time && now > last_time
                               ? static_cast<float>(now - last_time) / 1.0e6f * 3.2f
                               : 0.0f;
        back_amount += goal > back_amount
                           ? (goal - back_amount < step ? goal - back_amount : step)
                           : -(back_amount - goal < step ? back_amount - goal : step);
    }
    const float flip_t =
        flip_start && now > flip_start ? static_cast<float>(now - flip_start) / 260000.0f : 1.0f;
    const float flip =
        flip_t >= 1.0f ? 1.0f : 1.0f - (1.0f - flip_t) * (1.0f - flip_t) * (1.0f - flip_t);
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now) * 2.6e-6f);
    const bool recents = home_count && (played_last[home_games[0]] > 0 ||
                                        (meta(home_games[0]) && meta(home_games[0])->lastChosen));
    const float dt =
        last_time && now > last_time ? static_cast<float>(now - last_time) / 1.0e6f : 0.0f;
    last_time = now;
    if (position < -500.0f || target - position > 8.0f || position - target > 8.0f)
        position = target;
    else
    {
        // Critically damped feel: close the gap exponentially, about 90% in 170 ms.
        const float k = 1.0f - std::exp(-13.5f * (dt > 0.05f ? 0.05f : dt));
        position += (target - position) * k;
        if (std::fabs(target - position) < 0.002f)
            position = target;
    }
    static constexpr float centers[4] = {0.0f, 440.0f, 765.0f, 960.0f};
    static constexpr float edges[4] = {470.0f, 340.0f, 250.0f, 190.0f};
    const int first = static_cast<int>(std::floor(position)) - 3,
              last = static_cast<int>(std::ceil(position)) + 3;
    for (int ring = 3; ring >= 0; ring--)
        for (int i = first; i <= last; i++)
        {
            if (i < 0 || i >= home_count)
                continue;
            const float d = static_cast<float>(i) - position;
            const float ad = d < 0 ? -d : d;
            const int band = ad >= 3.0f ? 3 : static_cast<int>(ad + 0.5f);
            if (band != ring || ad > 3.0f)
                continue;
            const float edge = flow_lerp(edges, ad);
            const float center_x = 960.0f + (d < 0 ? -1.0f : 1.0f) * flow_lerp(centers, ad);
            const float fade = ad <= 2.0f ? 1.0f : 1.0f - (ad - 2.0f);
            const float turn =
                i == home_cursor ? (flip < 0.12f ? 0.12f : flip) : 1.0f; // width while turning
            const float width = edge * turn;
            const float x = center_x - width / 2.0f, y = 500.0f - edge / 2.0f;
            const bool focus = home_row == 0 && ad < 0.35f;
            const unsigned radius = 20;
            const int idx = home_games[i];
            const Cover *cover = find_cover(games[idx].id);
            if (cover)
                const_cast<Cover *>(cover)->age = ++cover_clock;
            if (c.has_3d())
            {
                // A real case in perspective: portrait art when the PC tool uploaded it, the square
                // icon otherwise.
                const bool tall = cover && cover->has_tall;
                const float amount = ad < 1.0f ? ad : 1.0f;
                const float side = d < 0 ? -1.0f : 1.0f;
                const float height = edge * (tall ? 1.15f : 1.0f);
                const float case_width =
                    tall ? height * (static_cast<float>(tall_w) / tall_h) : height;
                const float z = 36.0f * (1.0f - amount) - 230.0f * amount;
                const float persp =
                    (2015.0f - z) / 2015.0f; // keeps the layout the same size on screen
                const float world_x = 960.0f + (center_x - 960.0f) * persp;
                const float world_y = 540.0f + (500.0f - 540.0f) * persp;
                float yaw = -side * amount * 0.95f, pitch = 0.0f;
                if (i == home_cursor)
                {
                    yaw += (1.0f - flip) * 0.9f * side * -1.0f + back_amount * 3.14159265f * side +
                           user_yaw;
                    pitch = user_pitch;
                }
                const float apparent = case_width * persp * std::fabs(std::cos(yaw));
                const float floor_y = world_y + height * persp * 0.5f + 12.0f;
                const bool front_view = std::fabs(std::cos(yaw)) > 0.5f;
                const float shade_level = 1.0f - 0.30f * amount;
                c.list().shadow(
                    {world_x - apparent * 0.5f - 6.0f, floor_y - 10.0f, apparent + 12.0f, 18.0f},
                    9.0f, 26.0f, {0, 0, 0, 0.62f * fade});
                if (focus && front_view)
                {
                    const Color tint_color =
                        cover ? brighten(static_cast<Color>(cover->tint)) : accent;
                    hui::gfx::Color light = to_gfx(tint_color);
                    light.a = 0.34f + 0.22f * pulse;
                    c.list().glow({world_x - apparent * 0.5f, world_y - height * persp * 0.5f,
                                   apparent, height * persp},
                                  14.0f, 34.0f + 12.0f * pulse, light);
                }
                CaseOp op;
                op.x = world_x;
                op.y = world_y;
                op.z = z;
                op.width = case_width;
                op.height = height;
                op.depth = height * 0.10f;
                op.scale = persp;
                op.yaw = yaw;
                op.pitch = pitch;
                if (cover)
                    op.front = tall ? c.texture_for(cover->tall, tall_w, tall_h)
                                    : c.texture_for(cover->pixels, cover_edge);
                op.back = 0;
                op.alpha = fade;
                op.shade = shade_level;
                if (cover)
                {
                    // The spine takes a dark version of the cover's own colour.
                    const auto tint = static_cast<std::uint32_t>(cover->tint);
                    op.side_r = static_cast<float>(tint & 255) / 255.0f * 0.42f + 0.04f;
                    op.side_g = static_cast<float>((tint >> 8) & 255) / 255.0f * 0.42f + 0.04f;
                    op.side_b = static_cast<float>((tint >> 16) & 255) / 255.0f * 0.42f + 0.04f;
                }
                else
                {
                    op.side_r = 0.16f;
                    op.side_g = 0.20f;
                    op.side_b = 0.25f;
                }
                if (i == home_cursor && show_back && wide_art_state != 1 && work.load() == 0 &&
                    (std::strcmp(wide_art_id, games[idx].id) || wide_art_state == 0) &&
                    !is_ps2_id(games[idx].id))
                {
                    std::snprintf(wide_art_id, 10, "%s", games[idx].id);
                    wide_art_state = 1;
                    char wide_path[120];
                    std::snprintf(wide_path, sizeof wide_path,
                                  "/api/fs/download?path=/user/appmeta/%s/pic0.png", games[idx].id);
                    start_request(9, wide_path);
                }
                if (i == home_cursor)
                {
                    draw_case_spine(c, idx, cover);
                    op.spine = c.spine_texture();
                    op.shine = 0.55f;
                }
                if (i == home_cursor && back_amount > 0.001f)
                {
                    draw_case_back(c, idx, cover);
                    op.back = c.back_texture();
                    op.flip_back = true;
                }
                CaseOp mirrored = op;
                mirrored.reflection = true;
                mirrored.floor_y = floor_y;
                mirrored.fade_length = height * persp * 0.19f;
                c.case3d(mirrored);
                c.case3d(op);
                if (recents && i == 0 && ad < 0.15f && back_amount < 0.05f &&
                    std::fabs(user_yaw) < 0.05f && flip >= 1.0f && fade > 0.5f)
                {
                    const float bx = world_x - apparent * 0.5f + 16.0f,
                                by = world_y - height * persp * 0.5f + 16.0f;
                    c.rounded_rectangle(static_cast<unsigned>(bx), static_cast<unsigned>(by), 156,
                                        38, 12, accent);
                    text(c, static_cast<unsigned>(bx) + 12, static_cast<unsigned>(by) + 3,
                         "Continuar", 24, bg, 140);
                }
                continue;
            }
            c.list().shadow({x, y + 14.0f, width, edge}, 22.0f, focus ? 34.0f : 22.0f,
                            {0, 0, 0, (focus ? 0.66f : 0.47f) * fade});
            if (focus)
            {
                // Pulsing light around the focused cover, then a crisp white ring.
                const Color tint_color = cover ? brighten(static_cast<Color>(cover->tint)) : accent;
                hui::gfx::Color light = to_gfx(tint_color);
                light.a = 0.42f + 0.26f * pulse;
                c.list().glow({x, y, width, edge}, static_cast<float>(radius),
                              30.0f + 10.0f * pulse, light);
                c.list().bordered_rect({x - 5.0f, y - 5.0f, width + 10.0f, edge + 10.0f},
                                       static_cast<float>(radius) + 5.0f, {0, 0, 0, 0}, 4.0f,
                                       {1, 1, 1, 1});
            }
            if (cover)
            {
                const std::uint32_t texture = c.texture_for(cover->pixels, cover_edge);
                c.list().image(texture, {x, y, width, edge}, hui::gfx::kFullUv, {1, 1, 1, fade},
                               static_cast<float>(radius));
                // Reflection: the lower part of the cover, mirrored under it and fading out.
                c.list().image_gradient(texture, {x, y + edge + 8.0f, width, edge * 0.18f},
                                        {0.0f, 1.0f, 1.0f, -0.18f}, {1, 1, 1, 0.28f * fade},
                                        {1, 1, 1, 0.0f}, 6.0f);
            }
            else
                c.rounded_rectangle(static_cast<unsigned>(x), static_cast<unsigned>(y),
                                    static_cast<unsigned>(width), static_cast<unsigned>(edge),
                                    radius, shade(38, 48, 59));
            if (recents && i == 0 && flip >= 1.0f && fade > 0.5f)
            {
                // The last game played: a badge to carry on where the player left off.
                c.rounded_rectangle(static_cast<unsigned>(x) + 14, static_cast<unsigned>(y) + 14,
                                    156, 38, 12, accent);
                text(c, static_cast<unsigned>(x) + 26, static_cast<unsigned>(y) + 17, "Continuar",
                     24, bg, 140);
            }
        }
}
#endif
void draw_shelf_home(Canvas &c)
{
    const bool recents = home_count && (played_last[home_games[0]] > 0 ||
                                        (meta(home_games[0]) && meta(home_games[0])->lastChosen));
    text(c, 76, 150, recents ? "Jogos recentes" : "Seus jogos", 44, white, 1000);
    char position[80];
    std::snprintf(position, sizeof position, "%d / %d", home_count ? home_cursor + 1 : 0,
                  home_count);
    text(c, 1650, 164, position, 28, muted, 190);
    if (home_count)
    {
#ifdef ESTANTE_GL
        draw_cover_flow(c);
#else
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
#endif
        const int idx = home_games[home_cursor];
        const auto *m = meta(idx);
#ifdef ESTANTE_GL
        constexpr unsigned name_y = 812, detail_y = 880; // below the floor reflection
#else
        constexpr unsigned name_y = 772, detail_y = 840;
#endif
        centered(c, name_y, games[idx].name, 44, white, 1500);
        char detail[220], ago[48], played[48];
        format_ago(played_last[idx], ago, sizeof ago);
        format_playtime(played_seconds[idx], played, sizeof played);
        std::snprintf(detail, sizeof detail, "%s%s%s%s%s%s%s%s",
                      system_name(idx),
                      m && m->pinned ? tr("  ·  Fixado") : "", m && m->status ? "  ·  " : "",
                      m && m->status ? tr(statuses[m->status]) : "", ago[0] ? "  ·  " : "", ago,
                      played[0] ? "  ·  " : "", played);
        centered(c, detail_y, detail, 28, muted, 1500);
    }
    else
    {
        text(c, 78, 324, count ? "Nenhum jogo no Início" : "Sua biblioteca está vazia", 44, white, 1450);
        text(c, 80, 400,
             count ? "Seus apps ficam em Biblioteca > Apps." : "Atualize os jogos em Biblioteca > Options.", 28,
             muted, 1450);
    }
}
// ---- Catalogue-style screens: Inicio (hero + rows), Biblioteca (portrait grid) and the game page ----
// Everything on screen comes from the local library, the console's own trophy data and the art the
// player put on the console. No synopsis, rating or size is made up.
float frame_dt = 0.016f;
// Moves `value` toward `target`: a short exponential glide on the GPU path, a jump on the CPU path
// (which only redraws when the state changes).
void approach(float &value, float target, float rate = 14.0f)
{
#ifdef ESTANTE_GL
    if (std::fabs(target - value) > 8.0f)
        value = target;
    else
    {
        value += (target - value) * (1.0f - std::exp(-rate * frame_dt));
        if (std::fabs(target - value) < 0.003f)
            value = target;
    }
#else
    (void)rate;
    value = target;
#endif
}
#ifdef ESTANTE_GL
hui::gfx::Color gfx_alpha(Color color, float alpha)
{
    hui::gfx::Color out = to_gfx(color);
    out.a = alpha;
    return out;
}
#endif
const char *platform_name(int p)
{
    return p == 2 ? "PlayStation 2" : p == 4 ? "PlayStation 4" : p == 5 ? "PlayStation 5" : "Aplicativo";
}
const char *platform_tag(int p)
{
    return p == 2 ? "PS2" : p == 4 ? "PS4" : p == 5 ? "PS5" : "APP";
}
// Text that is already translated or made of data (names, ids, joined facts): no second lookup.
void raw_text(Canvas &c, unsigned x, unsigned y, const char *s, unsigned size = 28,
              Color color = white, unsigned width = 1700)
{
    if (large_text && size < 32)
        size = 32;
    c.label(x, y, s, face(size), color, width);
}
// Appends " · part" (or the first part) to a line.
void join_part(char *line, std::size_t cap, const char *part)
{
    if (!part || !part[0])
        return;
    const std::size_t used = std::strlen(line);
    if (used + std::strlen(part) + 6 >= cap)
        return;
    std::snprintf(line + used, cap - used, "%s%s", used ? "  ·  " : "", part);
}
// The facts line under a title: status, last time played and play time, whichever exist.
void fact_line(int idx, char *out, std::size_t cap)
{
    out[0] = 0;
    const auto *m = meta(idx);
    char ago[48], played[48];
    format_ago(played_last[idx], ago, sizeof ago);
    format_playtime(played_seconds[idx], played, sizeof played);
    if (m && m->status)
        join_part(out, cap, tr(statuses[m->status]));
    join_part(out, cap, ago);
    join_part(out, cap, played);
}
// True when the wide picture in the buffer may be drawn for this game. For the hero a previous
// picture stays until the next one arrives, so moving along a row does not flash the fallback.
bool wide_visible(int idx, bool allow_stale)
{
#ifdef ESTANTE_GL
    if (idx < 0 || idx >= count || is_ps2_id(games[idx].id) || !wide_have[0])
        return false;
    if (!std::strcmp(wide_art_id, games[idx].id))
        return wide_art_state == 2 || (wide_art_state == 1 && allow_stale);
    return allow_stale;
#else
    (void)idx;
    (void)allow_stale;
    return false;
#endif
}
// Full-screen art behind the focused game: the wide picture when there is one, otherwise the
// square icon enlarged on the right. Reading gradients keep the left side and the rows legible.
void hero_backdrop(Canvas &c, int idx, bool allow_stale)
{
    const Cover *cover = idx >= 0 && idx < count ? find_cover(games[idx].id) : nullptr;
    // Box art of a PS2 image or an emulator game: a portrait picture inside a square, often small.
    // Stretched over the screen it runs under the tabs and turns blocky, so it is framed instead.
    const bool boxed = idx >= 0 && idx < count && (is_ps2_id(games[idx].id) || is_emulator_game(idx));
    c.clear(bg);
#ifdef ESTANTE_GL
    if (wide_visible(idx, allow_stale))
        c.list().image(c.texture_for(wide_art, wide_w, wide_h), {0, 0, 1920, 1080},
                       hui::gfx::kFullUv, {1, 1, 1, 1}, 0.0f);
    else if (cover && boxed)
    {
        // Drawn after the scrims below, so it stays bright.
    }
    else if (cover)
    {
        // No wide picture: the square icon enlarged on the right.
        c.list().image(c.texture_for(cover->pixels, cover_edge), {900, 0, 1080, 1080},
                       hui::gfx::kFullUv, {1, 1, 1, 0.9f}, 0.0f);
        c.list().gradient_rect_h({900, 0, 520, 1080}, 0, gfx_alpha(bg, 1.0f), gfx_alpha(bg, 0.0f));
    }
    // A black scrim keeps text readable over any picture: solid on the left, fading out, then a
    // fade to black under the rows.
    // Narrow solid edge, then two long fades: the picture bleeds in from the left instead of being cut by a
    // black panel, and the title still sits on a dark enough background.
    c.list().rounded_rect({0, 0, 260, 1080}, 0, gfx_alpha(bg, 0.94f));
    c.list().gradient_rect_h({260, 0, 760, 1080}, 0, gfx_alpha(bg, 0.94f), gfx_alpha(bg, 0.6f));
    c.list().gradient_rect_h({1020, 0, 760, 1080}, 0, gfx_alpha(bg, 0.6f), gfx_alpha(bg, 0.0f));
    // A lighter fade under the rows: the picture still shows behind the cards and their names instead of a
    // near-black band; the names stay readable on top of it.
    c.list().gradient_rect({0, 470, 1920, 610}, 0, gfx_alpha(bg, 0.0f), gfx_alpha(bg, 0.86f));
    c.list().gradient_rect({0, 0, 1920, 170}, 0, gfx_alpha(bg, 0.8f), gfx_alpha(bg, 0.0f));
    if (cover && boxed && !wide_visible(idx, allow_stale))
    {
        // Between the tabs and the first row (the rows start near y 614), clear of both.
        c.shadow(1380, 190, 400, 400, 36, 200);
        c.list().image(c.texture_for(cover->pixels, cover_edge), {1380, 190, 400, 400},
                       hui::gfx::kFullUv, {1, 1, 1, 1}, 24.0f);
    }
#else
    (void)allow_stale;
    (void)boxed;
    if (cover)
        c.rounded_picture(1060, 150, 700, 700, 28, cover_edge, cover->pixels);
#endif
}
// A button of the hero and the game page. The primary one is white; focus adds a ring.
void action_button(Canvas &c, unsigned x, unsigned y, unsigned width, unsigned height,
                   const char *label, bool primary, bool focus)
{
    if (focus)
    {
        c.shadow(x, y, width, height, 30, 190);
        c.rounded_rectangle(x - 5, y - 5, width + 10, height + 10, height / 2 + 5,
                            primary ? accent : white);
    }
    c.rounded_rectangle(x, y, width, height, height / 2, primary ? white : mix_color(white, bg, 46));
    const char *shown = tr(label);
    const unsigned size = 30, measured = measure_face(c, shown, size);
    raw_text(c, x + (measured < width ? (width - measured) / 2 : 0), y + height / 2 - 20, shown, size,
             primary ? bg : white, width);
}
constexpr unsigned card_w = 236, card_h = 236; // square game icons, as the console draws them
// A game's art inside a card: the portrait picture, else the middle of the square icon, else a plain
// placeholder with the name. Marks the cover as used so the cache keeps it.
void card_art(Canvas &c, int idx, unsigned x, unsigned y, unsigned w, unsigned h, unsigned radius)
{
    const Cover *cover = idx >= 0 && idx < count ? find_cover(games[idx].id) : nullptr;
    if (cover)
    {
        const_cast<Cover *>(cover)->age = ++cover_clock;
#ifdef ESTANTE_GL
        c.list().image(c.texture_for(cover->pixels, cover_edge),
                       {static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                        static_cast<float>(h)},
                       hui::gfx::kFullUv, {1, 1, 1, 1}, static_cast<float>(radius));
#else
        c.rounded_picture(x, y, w, h, radius, thumb_edge, cover->thumb);
#endif
        return;
    }
    c.rounded_rectangle(x, y, w, h, radius, panel);
    if (idx >= 0 && idx < count)
        raw_text(c, x + 14, y + h / 2 - 14, games[idx].name[0] ? games[idx].name : games[idx].id, 22,
                 muted, w - 28);
}
void centered_in(Canvas &c, unsigned x, unsigned width, unsigned y, const char *s, unsigned size,
                 Color color)
{
    s = tr(s);
    unsigned measured = measure_face(c, s, size);
    if (measured > width)
        measured = width;
    raw_text(c, x + (width - measured) / 2, y, s, size, color, width);
}
// A small label on a card corner identifies both native platforms and emulator systems.
void card_tag(Canvas &c, unsigned x, unsigned y, int idx)
{
    // Wide enough for the longest label ("WII U"): the tag grows with its text instead of cutting it.
    const unsigned measured = measure_face(c, system_tag(idx), 18) + 20;
#ifdef ESTANTE_GL
    const unsigned tag_w = measured < 54 ? 54 : measured;
#else
    const unsigned tag_w = measured < 82 ? 82 : measured;
#endif
    c.rounded_rectangle(x, y, tag_w, 28, 8, shade(14, 16, 20));
    raw_text(c, x + 9, y + 2, system_tag(idx), 18, white, tag_w - 10);
}
void card_ring(Canvas &c, unsigned x, unsigned y, unsigned w, unsigned h, unsigned radius)
{
    c.shadow(x, y, w, h, 30, 200);
    c.rounded_rectangle(x - 5, y - 5, w + 10, h + 10, radius + 5, white);
}
// One row of Inicio: cards scroll sideways so the focused one stays in view.
void draw_rail_cards(Canvas &c, int row, unsigned y)
{
    HomeRail &r = rails[row];
    static float position[max_rails];
    constexpr int pitch = card_w + 28;
    const int far = r.count - 6 > 0 ? r.count - 6 : 0;
    const int near_ = r.cursor - 2 > 0 ? r.cursor - 2 : 0;
    approach(position[row], static_cast<float>(near_ < far ? near_ : far));
    const bool tags = r.kind == Rail::Continue || r.kind == Rail::Favorites ||
                      r.kind == Rail::Want || r.kind == Rail::Folder;
    auto place = [&](int i, bool focus)
    {
        const int x = 100 + static_cast<int>((static_cast<float>(i) - position[row]) * pitch);
        if (x + static_cast<int>(card_w) < 0 || x > 1920)
            return;
#ifndef ESTANTE_GL
        if (x < 0 || x + static_cast<int>(card_w) > 1920)
            return;
#endif
        const int idx = r.items[i];
        const unsigned grow = focus ? 16 : 0;
        const unsigned px = static_cast<unsigned>(x) - grow / 2, py = y - grow / 2;
        const unsigned w = card_w + grow, h = card_h + grow;
        if (focus)
            card_ring(c, px, py, w, h, 18);
        if (idx < 0)
        {
            c.rounded_rectangle(px, py, w, h, 18, panel);
            centered_in(c, px, w, py + h / 2 - 18, "Ver tudo", 28, white);
            return;
        }
        card_art(c, idx, px, py, w, h, 18);
        if (tags)
            card_tag(c, px + 12, py + 12, idx);
        // the focused card is larger, but its name keeps the slot of the others so labels never overlap
        raw_text(c, static_cast<unsigned>(x), y + card_h + 22, games[idx].name, focus ? 24 : 22,
                 focus ? white : muted, card_w - 8);
    };
    for (int i = 0; i < r.count; i++)
        if (i != r.cursor)
            place(i, false);
    if (r.cursor < r.count)
        place(r.cursor, true);
}
// Inicio, catalogue style: the focused game fills the screen, its rows sit underneath. There are no
// buttons: X plays the focused game, R2 opens its page.
void draw_discover(Canvas &c)
{
    if (!rail_count)
    {
        text(c, 100, 300, count ? "Nenhum jogo no Início" : "Sua biblioteca está vazia", 64, white, 1500);
        text(c, 102, 400,
             count ? "Seus apps ficam em Biblioteca > Apps." : "Atualize os jogos em Biblioteca > Options.", 28,
             muted, 1500);
        return;
    }
    const int idx = home_game();
    text(c, 100, 176, system_name(idx), 24, muted, 700);
    unsigned size = 96;
    while (size > 52 && measure_face(c, games[idx].name, size) > 1000)
        size -= 12;
    raw_text(c, 100, 214, games[idx].name, size, white, 1000);
    char facts[220];
    fact_line(idx, facts, sizeof facts);
    raw_text(c, 102, 214 + size + 28, facts, 28, muted, 1000);
    text(c, 100, 548, rails[home_row].title, 32, white, 1000);
    draw_rail_cards(c, home_row, 604);
    if (home_row + 1 < rail_count)
        text(c, 100, 946, rails[home_row + 1].title, 26, muted, 1000);
}
// Biblioteca: two rows of square icons visible, scrolling a row at a time. The grid keeps cards
// clean; the focused game's full name and facts sit under it.
int lib_top = 0;
int library_top_row()
{
    const int rows = (filtered_count + lib_cols - 1) / lib_cols;
    const int row = cursor / lib_cols;
    if (row < lib_top)
        lib_top = row;
    if (row > lib_top + 1)
        lib_top = row - 1;
    if (lib_top > rows - 2)
        lib_top = rows > 2 ? rows - 2 : 0;
    if (lib_top < 0)
        lib_top = 0;
    return lib_top;
}
void chip(Canvas &c, unsigned x, unsigned y, unsigned width, const char *label, bool focus,
          bool raw = false)
{
    if (focus)
        c.rounded_rectangle(x - 4, y - 4, width + 8, 64, 32, accent);
    c.rounded_rectangle(x, y, width, 56, 28, focus ? white : mix_color(white, bg, 34));
    if (raw)
        raw_text(c, x + 26, y + 11, label, 26, focus ? bg : white, width - 40);
    else
        text(c, x + 26, y + 11, label, 26, focus ? bg : white, width - 40);
}
void draw_library(Canvas &c)
{
    const char *view = query.view >= 100 ? db.folders[query.view - 100].name : views[query.view];
    text(c, 100, 104, "Biblioteca", 64, white, 700);
    char total[48];
    std::snprintf(total, sizeof total, "%d %s", filtered_count,
                  query.view == 4 ? "apps" : tr("jogos"));
    raw_text(c, 100 + measure_face(c, tr("Biblioteca"), 64) + 24, 128, total, 28, muted, 300);
    // chips: search, view, sort, filters
    const bool chips = library_zone == 1 && !nav_focus;
    unsigned x = 100;
    char search[100];
    std::snprintf(search, sizeof search, "%s%s", query.text[0] ? "" : tr("Buscar jogos"), query.text);
    chip(c, x, 200, 440, search, chips && library_chip == 0, true);
    x += 440 + 16;
    const unsigned view_w = measure_face(c, tr(view), 26) + 66;
    chip(c, x, 200, view_w < 200 ? 200 : view_w, view, chips && library_chip == 1);
    x += (view_w < 200 ? 200 : view_w) + 16;
    const unsigned sort_w = measure_face(c, tr(sorts[query.sort]), 26) + 66;
    chip(c, x, 200, sort_w, sorts[query.sort], chips && library_chip == 2);
    x += sort_w + 16;
    const int active = (query.platform ? 1 : 0) + (query.status ? 1 : 0) + (query.favorites ? 1 : 0);
    char filters[40];
    std::snprintf(filters, sizeof filters, active ? "%s (%d)" : "%s", tr("Filtros"), active);
    chip(c, x, 200, measure_face(c, filters, 26) + 66, filters, chips && library_chip == 3, true);
    if (chosen_count() || editing_collection >= 0)
    {
        char status[120];
        std::snprintf(status, sizeof status, tr("%d selecionados%s"), chosen_count(),
                      editing_collection >= 0 ? tr("  ·  Options para salvar") : "");
        raw_text(c, 1360, 212, status, 24, accent, 460);
    }
    if (!filtered_count)
    {
        text(c, 100, 420, "Nenhum jogo nesta seleção", 44, white, 1500);
        text(c, 102, 495, "Options: buscar, filtrar ou atualizar.", 28, muted, 1500);
    }
    else
    {
        constexpr unsigned gx = 99, gy = 290, w = 262, h = 262, step_x = 292, step_y = 292;
        static float scroll = 0.0f;
        approach(scroll, static_cast<float>(library_top_row()));
#ifdef ESTANTE_GL
        c.list().push_clip({0.0f, 270.0f, 1920.0f, 596.0f});
        const int first = static_cast<int>(std::floor(scroll));
        const int last = first + 2;
#else
        const int first = lib_top, last = lib_top + 1;
#endif
        for (int pass = 0; pass < 2; pass++)
            for (int row = first; row <= last; row++)
                for (int col = 0; col < lib_cols; col++)
                {
                    const int pos = row * lib_cols + col;
                    if (pos >= filtered_count)
                        break;
                    const bool current = !nav_focus && library_zone == 0 && cursor == pos;
                    if (current != (pass == 1))
                        continue;
                    const int idx = filtered[pos];
                    const int px = static_cast<int>(gx + col * step_x);
                    const int py = static_cast<int>(gy) +
                                   static_cast<int>((static_cast<float>(row) - scroll) * step_y);
                    const unsigned grow = current ? 16 : 0;
                    const unsigned cx = static_cast<unsigned>(px) - grow / 2,
                                   cy = static_cast<unsigned>(py) - grow / 2;
                    const unsigned cw = w + grow, ch = h + grow;
                    if (current)
                        card_ring(c, cx, cy, cw, ch, 18);
                    card_art(c, idx, cx, cy, cw, ch, 16);
                    card_tag(c, cx + 12, cy + 12, idx);
                    const auto *m = meta(idx);
                    if (m && m->favorite)
                        raw_text(c, cx + cw - 44, cy + 10, "★", 28, shade(255, 196, 0), 30);
                    if (selected[idx])
                        pill(c, cx + cw - 88, cy + ch - 56, 78, "OK", true);
                }
#ifdef ESTANTE_GL
        c.list().pop_clip();
#endif
        if (!nav_focus && library_zone == 0)
        {
            const int idx = filtered[cursor];
            raw_text(c, 100, 900, games[idx].name, 34, white, 1500);
            char facts[220];
            fact_line(idx, facts, sizeof facts);
            char line[260];
            std::snprintf(line, sizeof line, "%s", tr(system_name(idx)));
            join_part(line, sizeof line, facts);
            raw_text(c, 102, 948, line, 24, muted, 1500);
        }
    }
    if (nav_focus)
    {
        c.dim();
        c.rectangle(32, 126, 470, 864, panel);
        text(c, 60, 150, "Filtrar biblioteca", 32, white, 410);
        for (int i = 0; i < view_order_count; i++)
        {
            unsigned y = 220 + i * 53;
            bool focus = nav_cursor == i;
            if (focus)
                c.rounded_rectangle(48, y - 3, 438, 51, 6, accent);
            text(c, 66, y, views[view_order[i]], 28, focus ? on_accent() : white, 405);
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
        "Estilo do Início",     "Tema",                    "Texto maior",
        "Animação ao abrir",    "Idioma / Language",       "Modo gravação",
        "Atualizar biblioteca", "Criar coleção",           "Exportar organização",
        "Restaurar último backup"};
    const char *desc[] = {"Catálogo com fileiras ou a estante com capas em 3D. L2 + R2 no Início também troca.",
                          "Troca as cores da interface.",
                          "Aumenta os textos menores da interface.",
                          "Mostra a capa e um disco girando antes de abrir o jogo.",
                          "Alterna entre português, inglês e espanhol.",
                          "Esconde dicas e mensagens para capturas.",
                          "Consulta os jogos pelo Elf Arsenal local.",
                          "Comece uma seleção pessoal de jogos.",
                          "Salva uma cópia independente no armazenamento do app.",
                          "Recupera a organização anterior. Os jogos não são alterados."};
    for (int i = 0; i < settings_count; i++)
    {
        unsigned y = 282 + i * 70;
        bool focus = i == settings_cursor;
        c.rounded_rectangle(70, y, 1770, 62, 12, focus ? panel : bg);
        if (focus)
            c.rectangle(70, y + 9, 4, 44, accent);
        text(c, 96, y + 5, names[i], 32, white, 650);
        text(c, 770, y + 17, desc[i], 24, muted, 1050);
        switch (settings_actions[i])
        {
        case TextSize:
            text(c, 570, y + 14, large_text ? "Ligado" : "Desligado", 24, accent, 190);
            break;
        case Record:
            text(c, 570, y + 14, record_mode ? "Ligado" : "Desligado", 24, accent, 190);
            break;
        case Language:
            text(c, 570, y + 14, i18n::language == 2 ? "Español" : i18n::language ? "English" : "Português", 24, accent, 190);
            break;
        case Animation:
            text(c, 570, y + 14, launch_anim_enabled ? "Ligado" : "Desligado", 24, accent, 190);
            break;
        case ThemeNext:
            text(c, 570, y + 14, themes[theme_index].name, 24, accent, 190);
            break;
        case HomeStyle:
            text(c, 570, y + 14, home_style ? "Estante" : "Catálogo", 24, accent, 190);
            break;
        default:
            break;
        }
    }
}
// The game page: a full-screen product page, not a dialog. The wide art fills the screen under a black
// scrim; title, facts, actions and the trophy summary sit on the left. Only facts the console or the
// library really holds are shown, and a fact with no data is left out instead of shown empty.
Color trophy_color(int grade)
{
    return grade == 1   ? shade(166, 214, 255) // platinum
           : grade == 2 ? shade(255, 196, 36)  // gold
           : grade == 3 ? shade(196, 202, 212) // silver
                        : shade(205, 127, 62); // bronze
}
// A small cup in the grade's colour: bowl, two handles, stem and base.
void trophy_icon(Canvas &c, int grade, unsigned x, unsigned y, unsigned size)
{
    const Color body = trophy_color(grade);
    const Color shadow_tone = mix_color(body, shade(0, 0, 0), 150);
    auto u = [size](float f) { return static_cast<unsigned>(f * static_cast<float>(size)); };
    c.circle(x + u(0.20f), y + u(0.22f), u(0.15f), body); // handles
    c.circle(x + u(0.20f), y + u(0.22f), u(0.085f), bg);
    c.circle(x + u(0.80f), y + u(0.22f), u(0.15f), body);
    c.circle(x + u(0.80f), y + u(0.22f), u(0.085f), bg);
    c.rounded_rectangle(x + u(0.24f), y, u(0.52f), u(0.54f), u(0.24f), body); // bowl
    c.rectangle(x + u(0.24f), y, u(0.52f), u(0.16f), body);
    c.rectangle(x + u(0.30f), y + u(0.05f), u(0.06f), u(0.30f), mix_color(white, body, 70));
    c.rectangle(x + u(0.44f), y + u(0.50f), u(0.12f), u(0.22f), shadow_tone); // stem
    c.rounded_rectangle(x + u(0.28f), y + u(0.70f), u(0.44f), u(0.14f), u(0.05f), body); // base
}
// A round button holding an icon. kind 0: favourite star (gold when on), kind 1: three dots.
void icon_button(Canvas &c, unsigned x, unsigned y, unsigned size, int kind, bool focus, bool on)
{
    if (focus)
    {
        c.shadow(x, y, size, size, 30, 190);
        c.circle(x + size / 2, y + size / 2, size / 2 + 5, white);
    }
    c.circle(x + size / 2, y + size / 2, size / 2, focus ? white : mix_color(white, bg, 38));
    if (kind == 0)
    {
        const Color star = on ? shade(255, 196, 0) : focus ? shade(120, 124, 132) : muted;
        raw_text(c, x + size / 2 - 17, y + size / 2 - 22, "★", 44, star, 40);
    }
    else
        for (int i = -1; i <= 1; i++)
            c.circle(x + size / 2 + i * 14, y + size / 2, 4, focus ? bg : white);
}
// Each fact is a small label over its value.
void fact(Canvas &c, unsigned &x, unsigned y, unsigned width, const char *label, const char *value)
{
    text(c, x, y, label, 22, muted, width - 20);
    raw_text(c, x, y + 32, value, 32, white, width - 20);
    x += width;
}
void draw_game_page(Canvas &c)
{
    if (details_index < 0 || details_index >= count)
        return;
    const int idx = details_index;
    const auto *m = meta(idx);
    hero_backdrop(c, idx, false);
    c.circle(126, 52, 26, mix_color(white, bg, 38)); // back
    raw_text(c, 117, 31, "<", 32, white, 30);

    char label[160];
    std::snprintf(label, sizeof label, "%s", tr(system_name(idx)));
    if (!is_ps2_id(games[idx].id) && !is_emulator_game(idx))
        join_part(label, sizeof label, games[idx].id);
    raw_text(c, 100, 176, label, 24, muted, 900);
    unsigned size = 96;
    while (size > 52 && measure_face(c, games[idx].name, size) > 1000)
        size -= 12;
    raw_text(c, 100, 214, games[idx].name, size, white, 1000);

    char played[48], ago[48], folders[260];
    format_playtime(played_seconds[idx], played, sizeof played);
    format_ago(played_last[idx], ago, sizeof ago);
    folders[0] = 0;
    for (int f = 0; f < max_folders; f++)
        if (db.folders[f].used && collection_contains(db, games[idx], f, false))
            join_part(folders, sizeof folders, db.folders[f].name);
    // "12 h de jogo" / "12 h played" is shown as "12 h" under its own label, and "jogado há 3 dias" /
    // "played 3 days ago" as "há 3 dias" / "3 days ago"
    for (const char *suffix : {" de jogo", " played"})
        if (char *tail = std::strstr(played, suffix))
            *tail = 0;
    const char *ago_value = ago;
    for (const char *prefix : {"jogado ", "played "})
        if (!std::strncmp(ago_value, prefix, std::strlen(prefix)))
            ago_value += std::strlen(prefix);
    unsigned fx = 100;
    constexpr unsigned facts_y = 214 + 96 + 52;
    if (played[0])
        fact(c, fx, facts_y, 250, "Tempo de jogo", played);
    if (ago[0])
        fact(c, fx, facts_y, 380, "Última vez", ago_value);
    if (m && m->status)
        fact(c, fx, facts_y, 300, "Status", tr(statuses[m->status]));
    if (folders[0])
        fact(c, fx, facts_y, 520, "Coleções", folders);

    constexpr unsigned buttons_y = 560;
    action_button(c, 100, buttons_y, 300, 76, launch_pending ? "Abrindo…" : "Jogar", true,
                  details_button == 0);
    icon_button(c, 424, buttons_y, 76, 0, details_button == 1, m && m->favorite);
    icon_button(c, 524, buttons_y, 76, 1, details_button == 2, false);

    const TrophySummary &trophy = trophies[idx];
    if (!is_ps2_id(games[idx].id) && !is_emulator_game(idx) &&
        (trophy.state == 1 || (trophy.state == 2 && trophy.total)))
    {
        constexpr unsigned trophy_y = 720;
        text(c, 100, trophy_y, "Troféus", 22, muted, 300);
        if (trophy.state == 1)
            text(c, 100, trophy_y + 36, "Carregando troféus…", 28, muted, 700);
        else
        {
            unsigned tx = 100;
            for (int grade = 1; grade <= 4; grade++)
            {
                if (!trophy.total_by[grade])
                    continue;
                trophy_icon(c, grade, tx, trophy_y + 38, 56);
                char got[16], of[16];
                std::snprintf(got, sizeof got, "%u", static_cast<unsigned>(trophy.earned_by[grade]));
                std::snprintf(of, sizeof of, "/ %u", static_cast<unsigned>(trophy.total_by[grade]));
                raw_text(c, tx + 68, trophy_y + 44, got, 32, white, 80);
                raw_text(c, tx + 68 + measure_face(c, got, 32) + 8, trophy_y + 52, of, 24, muted, 90);
                tx += 190;
            }
            const unsigned percent = 100u * trophy.earned / trophy.total;
            char percent_text[24];
            std::snprintf(percent_text, sizeof percent_text, "%u%%", percent);
            constexpr unsigned bar_y = trophy_y + 124, bar_w = 760;
            c.rounded_rectangle(100, bar_y, bar_w, 8, 4, mix_color(white, bg, 40));
            if (trophy.earned)
                c.rounded_rectangle(100, bar_y, bar_w * trophy.earned / trophy.total, 8, 4, accent);
            raw_text(c, 100 + bar_w + 24, bar_y - 14, percent_text, 32, white, 120);
        }
    }
}
void draw_modal(Canvas &c)
{
    if (!modal)
        return;
    c.dim();
    c.rounded_rectangle(328, 150, 1264, 790, 20, shade(8, 11, 15));
    c.rounded_rectangle(340, 162, 1240, 766, 16, panel);
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
                 32, focus ? on_accent() : white, 82);
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
        text(c, 412, y + 4, entries[i], 28, focus ? on_accent() : white, 1080);
    }
    if (!total)
        text(c, 400, 350, "Crie uma coleção manual antes de adicionar jogos.", 28, muted, 1090);
    text(c, 400, 861, "×  Escolher   •   ○  Voltar", 24, muted, 1050);
}
std::uint64_t state_signature();
// The game whose wide picture is wanted: the one on the game page, or the focused one in Inicio.
int wide_target()
{
    if (details_page)
        return details_index;
    if (screen == 0 && home_style == 0 && !modal)
        return home_game();
    return -1;
}
void request_visible_cover()
{
    if (work.load() != 0 || launch_pending)
        return;
#ifdef ESTANTE_GL
    {
        // The wide picture is asked for once the focus has rested for a moment, so moving quickly along
        // a row does not queue a download per card. Failure is remembered (state 3) and not retried.
        static int last_target = -2;
        static std::uint64_t since = 0;
        const int target = wide_target();
        const auto now = sceKernelGetProcessTime();
        if (target != last_target)
        {
            last_target = target;
            since = now;
        }
        if (target >= 0 && target < count && !is_ps2_id(games[target].id) && wide_art_state != 1 &&
            now - since > 180000 &&
            (wide_art_state == 0 || std::strcmp(wide_art_id, games[target].id)))
        {
            std::snprintf(wide_art_id, sizeof wide_art_id, "%s", games[target].id);
            wide_art_state = 1;
            char wide_path[120];
            std::snprintf(wide_path, sizeof wide_path,
                          "/api/fs/download?path=/user/appmeta/%s/pic0.png", games[target].id);
            start_request(9, wide_path);
            return;
        }
    }
#endif
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
    if (details_page)
        add(details_index);
    if (screen == 0 && home_style == 0)
    {
        if (rail_count)
        {
            const HomeRail &r = rails[home_row];
            add(home_game());
            // The row shows six cards from `first` (it stops scrolling when the last card is in view), so near
            // the end the visible cards start well before cursor - 2: ask for those too.
            const int far = r.count - 6 > 0 ? r.count - 6 : 0, near_ = r.cursor > 2 ? r.cursor - 2 : 0;
            const int first = near_ < far ? near_ : far;
            for (int i = first; i < r.count && i < first + 9; i++)
                if (r.items[i] >= 0)
                    add(r.items[i]);
        }
    }
    else if (screen == 0)
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
        const int start = library_top_row() * lib_cols;
        if (filtered_count)
            add(filtered[cursor]);
        for (int i = start; i < filtered_count && i < start + 2 * lib_cols; i++)
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
        requested_fallback[0] = 0;
        char path[320];
#ifdef ESTANTE_GL
        requested_tall_path[0] = 0;
#endif
        if (is_emulator_game(wanted[k]) && emulator_cover[wanted[k]][0])
        {
            std::snprintf(path, sizeof path, "/api/fs/download?path=%s", emulator_cover[wanted[k]]);
#ifdef ESTANTE_GL
            std::snprintf(requested_tall_path, sizeof requested_tall_path,
                          "/api/fs/download?path=%s", emulator_cover[wanted[k]]);
#endif
        }
        else if (is_ps2_id(id))
        {
            std::snprintf(path, sizeof path, "/api/fs/download?path=/data/PCSX2/covers/%s.jpg",
                          ps2_serial[wanted[k]]);
#ifdef ESTANTE_GL
            std::snprintf(requested_tall_path, sizeof requested_tall_path,
                          "/api/fs/download?path=/data/PCSX2/covers/%s.jpg", ps2_serial[wanted[k]]);
#endif
        }
        else
        {
            // The 512 x 512 icon0.png the console keeps for every installed title; /appdb/icon only serves a
            // 256 x 256 JPEG of it, which looked blurry and jagged once drawn at 450 pixels.
            std::snprintf(path, sizeof path, "/api/fs/download?path=/user/appmeta/%s/icon0.png", id);
            std::snprintf(requested_fallback, sizeof requested_fallback, "/appdb/icon?id=%s", id);
#ifdef ESTANTE_GL
            std::snprintf(requested_tall_path, sizeof requested_tall_path,
                          "/api/fs/download?path=/data/estante/covers/%s.jpg", id);
#endif
        }
#ifdef ESTANTE_GL
        if (home_style != 1)
            requested_tall_path[0] = 0; // portrait art is only used by the 3D shelf
#endif
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
    add(details_page);
    add(details_index + 1);
    add(details_button);
    add(home_style);
    add(rail_count);
    if (home_row < rail_count)
        add(rails[home_row].cursor);
    add(library_zone);
    add(library_chip);
#ifdef ESTANTE_GL
    add(wide_art_state);
#endif
    if (details_page && details_index >= 0 && details_index < count)
    {
        add(trophies[details_index].state);
        add(trophies[details_index].earned);
        add(trophies[details_index].total);
    }
    add(menu_cursor);
    add(keyboard_cursor);
    add(record_mode);
    add(large_text);
    add(i18n::language);
    add(launch_anim_enabled);
    add(theme_index);
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
#ifdef ESTANTE_GL
// GPU launch scene: the cover is the door of a jewel case. It swings open on its left hinge, the
// disc inside slides out to the right and spins, then the game opens.
void draw_launch_anim_gl(Canvas &c, std::uint64_t elapsed)
{
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; };
    auto ease_out = [](float v) { return 1.0f - (1.0f - v) * (1.0f - v) * (1.0f - v); };
    const float t = clamp01(static_cast<float>(elapsed) / static_cast<float>(anim_length));
    const float slide = ease_out(clamp01(t * 1.6f));
    const float door = ease_out(clamp01(t / 0.38f));
    const float out = ease_out(clamp01((t - 0.30f) / 0.55f));
    const Cover *cover = find_cover(games[anim_idx].id);
    const Color tint = cover ? static_cast<Color>(cover->tint) : accent;
    c.gradient(0, 0, 1920, 1080, mix_color(tint, bg, 80), bg);

    const bool tall = cover && cover->has_tall;
    const float height = 560.0f;
    const float case_width = tall ? height * (static_cast<float>(tall_w) / tall_h) : height;
    const float radius = height * 0.31f;
    const float case_x = 960.0f - 220.0f * slide, case_y = 470.0f;
    const float yaw = -0.32f * (0.4f + 0.6f * door); // a little turned, so the depth shows
    const float spin = static_cast<float>(elapsed) * 1.57e-5f;
    const float disc_x = case_x + out * (case_width * 0.5f + radius + 60.0f);

    const float floor_y = case_y + height * 0.5f + 14.0f;
    c.list().shadow(
        {case_x - case_width * 0.5f - 10.0f, floor_y - 10.0f, case_width + 20.0f, 20.0f}, 10.0f,
        34.0f, {0, 0, 0, 0.6f});
    if (out > 0.02f)
        c.list().shadow({disc_x - radius, floor_y - 8.0f, radius * 2.0f, 16.0f}, 8.0f, 30.0f,
                        {0, 0, 0, 0.45f * out});

    CaseOp op;
    op.x = case_x;
    op.y = case_y;
    op.width = case_width;
    op.height = height;
    op.depth = height * 0.10f;
    op.yaw = yaw;
    op.opening = door;
    op.part = 1; // everything behind the door
    if (cover)
    {
        op.front = tall ? c.texture_for(cover->tall, tall_w, tall_h)
                        : c.texture_for(cover->pixels, cover_edge);
        const auto tint_bits = static_cast<std::uint32_t>(cover->tint);
        op.side_r = static_cast<float>(tint_bits & 255) / 255.0f * 0.42f + 0.04f;
        op.side_g = static_cast<float>((tint_bits >> 8) & 255) / 255.0f * 0.42f + 0.04f;
        op.side_b = static_cast<float>((tint_bits >> 16) & 255) / 255.0f * 0.42f + 0.04f;
    }
    else
    {
        op.side_r = 0.16f;
        op.side_g = 0.20f;
        op.side_b = 0.25f;
    }
    c.case3d(op);

    // The disc: it rests inside the case, then rolls out to the right with the game's art on its
    // label.
    DiscOp disc;
    disc.x = disc_x;
    disc.y = case_y;
    disc.z = -height * 0.02f;
    disc.radius = radius;
    disc.spin = spin;
    disc.yaw = yaw * 0.6f;
    disc.sheen = 0.38f;
    disc.label = cover ? c.texture_for(cover->pixels, cover_edge) : 0;
    const hui::gfx::Color silver = to_gfx(mix_color(tint, shade(222, 226, 236), 50));
    disc.tint_r = silver.r;
    disc.tint_g = silver.g;
    disc.tint_b = silver.b;
    c.disc3d(disc);

    op.part = 2;
    c.case3d(op);

    centered(c, 830, games[anim_idx].name, 44, white, 1500);
    centered(c, 900, "Abrindo…", 28, muted, 900);
    if (!record_mode)
        text(c, 78, 1031, "○  Cancelar", 24, muted, 300);
}
#endif
// Full-screen launch scene: the focused game's cover moves left while a disc slides out from behind
// it and spins.
void draw_launch_anim(Canvas &c, std::uint64_t elapsed)
{
#ifdef ESTANTE_GL
    draw_launch_anim_gl(c, elapsed);
    return;
#endif
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
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius,
                 shade(216, 220, 230));
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 86 / 100,
                 mix_color(tint, shade(190, 196, 210), 30));
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 58 / 100,
                 shade(206, 211, 224));
        c.circle(static_cast<unsigned>(disc_x), static_cast<unsigned>(cy), radius * 32 / 100,
                 shade(150, 156, 172));
        // Sheen: three short arcs of dots that rotate with time (about 2.5 turns per second).
        const float spin = static_cast<float>(elapsed) * 1.57e-5f;
        for (int arm = 0; arm < 3; arm++)
            for (int dot = 0; dot < 8; dot++)
            {
                const float a = spin + arm * 2.0944f + dot * 0.085f;
                const float r = radius * 0.72f;
                const unsigned level = static_cast<unsigned>(255 - dot * 12);
                c.circle(static_cast<unsigned>(disc_x + static_cast<int>(r * std::cos(a))),
                         static_cast<unsigned>(cy + static_cast<int>(r * std::sin(a))), 11,
                         shade(level, level, level));
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
    {
        static std::uint64_t last_frame = 0;
        const std::uint64_t now = sceKernelGetProcessTime();
        frame_dt = last_frame && now > last_frame ? static_cast<float>(now - last_frame) / 1.0e6f : 0.016f;
        if (frame_dt > 0.05f)
            frame_dt = 0.05f;
        last_frame = now;
    }
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
    // The renderer keeps the finished frame in a cached buffer, so an unchanged state needs no
    // redraw.
#ifdef ESTANTE_GL
    gpu_canvas = &c; // every frame is drawn again: the GPU path has no cached frame to keep
#else
    static std::uint64_t previous = 0;
    const auto signature = state_signature();
    if (signature == previous)
        return;
    previous = signature;
#endif
    const bool catalogue_home = screen == 0 && home_style == 0;
    if (details_page)
        draw_game_page(c);
    else
    {
        if (catalogue_home)
            hero_backdrop(c, home_game(), true);
        else if (const Cover *tinted = screen == 0 ? focus_cover() : nullptr)
        {
            const Color top = mix_color(static_cast<Color>(tinted->tint), bg, 92);
            c.gradient(0, 0, 1920, 700, top, bg);
            c.rectangle(0, 700, 1920, 380, bg); // the gradient already covers the top, no double write
        }
        else
            c.clear(bg);
        draw_top(c);
        if (screen == 0)
        {
            if (catalogue_home)
                draw_discover(c);
            else
                draw_shelf_home(c);
        }
        else if (screen == 1)
            draw_library(c);
        else if (screen == 2)
            draw_collections(c);
        else
            draw_settings(c);
    }
    if (!record_mode)
    {
        if (details_page)
        {
            text(c, 100, 1031, "×  Selecionar", 24, white, 250);
            text(c, 380, 1031, "○  Voltar", 24, muted, 250);
            text(c, 620, 1031, "△  Organizar", 24, muted, 250);
            text(c, 900, 1031, "□  Favoritar", 24, muted, 250);
        }
        else if (screen == 1)
        {
            text(c, 100, 1031, "L1 / R1  Abas", 24, muted, 250);
            text(c, 380, 1031, editing_collection >= 0 ? "×  Selecionar" : "×  Abrir", 24, white,
                 250);
            text(c, 640, 1031, "△  Buscar", 24, muted, 250);
            text(c, 920, 1031, editing_collection >= 0 ? "□  Selecionar" : "□  Organizar", 24, muted, 250);
            text(c, 1210, 1031, query.text[0] ? "○  Limpar busca" : "Touchpad  Filtros", 24, accent,
                 300);
            text(c, 1540, 1031, "Options  Opções", 24, muted, 300);
        }
        else if (screen == 3)
        {
            text(c, 100, 1031, "L1 / R1  Navegar", 24, muted, 250);
            text(c, 380, 1031, "×  Alterar", 24, white, 250);
            text(c, 640, 1031, "○  Voltar", 24, muted, 250);
        }
        else
        {
            const bool plays = screen == 0 && (home_style == 0 || home_row == 0);
            const bool catalogue = screen == 0 && home_style == 0;
            unsigned x = 100;
            auto hint = [&](const char *label, Color color, unsigned width)
            {
                text(c, x, 1031, label, 24, color, width);
                x += width + 40;
            };
            hint("L1 / R1  Navegar", muted, 250);
            hint(plays ? "×  Jogar" : "×  Abrir", white, 160);
            if (catalogue)
                hint("R2  Detalhes", muted, 220);
            hint("△  Organizar", muted, 220);
            hint(screen == 0 ? "□  Buscar" : "○  Voltar", muted, 180);
#ifdef ESTANTE_GL
            if (screen == 0 && home_style == 1 && home_count)
                hint(show_back ? "R2  Frente" : "R2  Verso", muted, 160);
#endif
            if (screen == 0)
                hint("L2 + R2  Estilo", muted, 230);
            hint(screen == 0 ? "Options  Biblioteca" : "Options  Opções", muted, 300);
        }
        if (!storage_ok)
            text(c, 100, 985, "Salvamento indisponível - confira o armazenamento do app.", 24,
                 Color::yellow, 1750);
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
    detect_language();
    read_preferences();
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
    load_ps2_cache();
    load_ps1_cache();
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
