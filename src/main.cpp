// Estante native PS5 prototype. GPL-3.0-or-later.
// VideoOut and Pad ABI based on the public boilerplate, not firmware offsets.
#include "demo_renderer.hpp"
#include "library.hpp"
#include "experience.hpp"
#include <algorithm>
#include <ctime>
#include "cJSON.h"
#include "cover_decode.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
using namespace estante;
using ps5::demo::Canvas;
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
                        OPTIONS = 0x8, R3 = 0x4;
constexpr int cover_edge = 316;
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
char message[180] = "Bem-vindo ao Estante", connection[96] = "Biblioteca local";
struct Cover
{
    char id[10];
    bool ready;
    std::uint64_t age;
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
std::uint32_t decoded_cover[cover_edge * cover_edge]{};
bool cover_ok = false;
std::uint64_t verify_at = 0;
int verify_attempts = 0;
bool launch_pending = false;
constexpr char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
constexpr int key_count = sizeof(keys) - 1;
const char *views[] = {"Todos os jogos", "PlayStation 2", "PlayStation 4", "PlayStation 5",
                       "Outros e apps",  "Favoritos",     "Sem coleção",   "Ocultos",
                       "Quero jogar",    "Em andamento",  "Concluídos",    "Fixados"};
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
    char data[100];
    std::snprintf(data, sizeof data, "{\"largeText\":%s,\"recordMode\":%s}",
                  large_text ? "true" : "false", record_mode ? "true" : "false");
    if (!write_file("/download0/estante-preferences.json", data))
        msg("Nao foi possivel salvar as preferencias.");
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
    home_count = select_games(db, games, count, homeQuery, home_games);
    std::stable_sort(home_games, home_games + home_count,
                     [](int a, int b)
                     {
                         auto *ma = meta(a);
                         auto *mb = meta(b);
                         return (ma && ma->pinned) && !(mb && mb->pinned);
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
bool http_get(const char *path)
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
            std::memset(selected, 0, sizeof selected);
            if (storage_ok)
                write_file("/download0/catalog.json", response);
            std::snprintf(connection, sizeof connection, "Biblioteca atualizada");
            msg("Seus jogos estão prontos para organizar.");
            rebuild();
        }
        else
        {
            std::snprintf(connection, sizeof connection, "Usando biblioteca salva");
            msg("Elf Arsenal indisponível. Sua organização foi preservada.");
        }
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
void queue_launch(int idx)
{
    if (idx < 0 || idx >= count || launch_pending)
        return;
    std::snprintf(launch_id, 10, "%s", games[idx].id);
    launch_pending = true;
    verify_at = 0;
    msg("Verificando o console…");
    start_request(4, "/api/state");
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
        pressed |= samples[i].buttons & ~held;
        held = samples[i].buttons;
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
    Filter,
    Sort,
    SmartFolder,
    Refresh,
    Restore,
    Record,
    TextSize,
    Pick,
    Export,
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
void input(std::uint32_t b)
{
    if (!b)
        return;
    ui_version++;
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
        if (b & UP)
            home_row = 0;
        if (b & DOWN)
            home_row = 1;
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
                set_screen(0);
            else
                nav_focus = true;
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
                query.view = nav_cursor;
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
                nav_focus = true;
            return;
        }
        if (b & LEFT)
        {
            if (cursor % 6 == 0)
                nav_focus = true;
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
            settings_cursor = (settings_cursor + 5) % 6;
        if (b & DOWN)
            settings_cursor = (settings_cursor + 1) % 6;
        if (b & CIRCLE)
            set_screen(0);
        if (b & CROSS)
        {
            Action actions[] = {TextSize, Record, Refresh, Export, Restore, NewFolder};
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
    c.label(x, y, s, size, color, width);
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
void artwork(Canvas &c, int idx, unsigned x, unsigned y, unsigned edge, bool focus = false)
{
    if (focus)
        c.rounded_rectangle(x - 5, y - 5, edge + 10, edge + 10, 14, accent);
    if (idx >= 0 && idx < count)
    {
        auto *cover = find_cover(games[idx].id);
        if (cover)
        {
            cover->age = ++cover_clock;
            c.picture(x, y, edge, edge, cover_edge, cover->pixels);
            return;
        }
    }
    c.rounded_rectangle(x, y, edge, edge, 10, shade(38, 48, 59));
    unsigned mx = x + edge / 3, my = y + edge / 3, w = edge / 3;
    c.rectangle(mx, my, w, 4, muted);
    c.rectangle(mx, my + edge / 8, w * 3 / 4, 4, muted);
    c.rectangle(mx, my + edge / 4, w, 4, muted);
    if (edge > 180 && idx >= 0)
        text(c, x + 20, y + edge - 55, games[idx].id, 24, muted, edge - 40);
}
void collection_card(Canvas &c, int slot, unsigned x, unsigned y, unsigned width, unsigned height,
                     bool focus)
{
    if (focus)
        c.rounded_rectangle(x - 4, y - 4, width + 8, height + 8, 16, accent);
    c.rounded_rectangle(x, y, width, height, 12, panel);
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
    std::snprintf(detail, sizeof detail, "%d jogos%s", collection_size(slot),
                  db.folders[slot].smart ? " • Automática" : "");
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
    std::snprintf(out, cap, "Coleções");
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
    text(c, 265, 54, "0.3 r2", 24, muted, 130);
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
void draw_home(Canvas &c)
{
    text(c, 76, 164, "Seus jogos", 44, white, 1000);
    char position[80];
    std::snprintf(position, sizeof position, "%d / %d", home_count ? home_cursor + 1 : 0,
                  home_count);
    text(c, 1650, 178, position, 28, muted, 190);
    const int start = (home_cursor / 5) * 5;
    unsigned x = 78;
    for (int i = start; i < home_count && i < start + 5; i++)
    {
        const bool current = i == home_cursor;
        const unsigned edge = current ? 356 : 304, y = current ? 272 : 298;
        artwork(c, home_games[i], x, y, edge, home_row == 0 && current);
        if (!current)
            text(c, x, y + edge + 22, games[home_games[i]].name, 24, muted, edge);
        x += edge + 40;
    }
    if (home_count)
    {
        const int idx = home_games[home_cursor];
        const auto *m = meta(idx);
        text(c, 78, 694, games[idx].name, 44, white, 1310);
        char detail[150];
        const int p = platform(games[idx], db);
        std::snprintf(detail, sizeof detail, "%s%s%s%s",
                      p == 2   ? "PlayStation 2"
                      : p == 4 ? "PlayStation 4"
                      : p == 5 ? "PlayStation 5"
                               : "Aplicativo",
                      m && m->pinned ? "  ·  Fixado" : "", m && m->status ? "  ·  " : "",
                      m && m->status ? statuses[m->status] : "");
        text(c, 80, 756, detail, 28, muted, 1300);
    }
    else
    {
        text(c, 78, 324, "Sua biblioteca está vazia", 44, white, 1450);
        text(c, 80, 400, "Atualize os jogos em Biblioteca > Options.", 28, muted, 1450);
    }
    text(c, 78, 844, "Coleções", 28, muted, 1100);
    int cs = (home_folder_cursor / 4) * 4;
    for (int i = 0; i < 4 && cs + i <= home_folder_count; i++)
    {
        unsigned fx = 78 + i * 447;
        bool focus = home_row == 1 && cs + i == home_folder_cursor;
        if (focus)
            c.rounded_rectangle(fx - 4, 898, 430, 94, 13, white);
        c.rounded_rectangle(fx, 902, 422, 86, 9, panel);
        if (cs + i < home_folder_count)
        {
            int slot = home_folders[cs + i];
            char n[20];
            std::snprintf(n, sizeof n, "%d", collection_size(slot));
            text(c, fx + 22, 923, db.folders[slot].name, 28, white, 330);
            text(c, fx + 358, 925, n, 24, muted, 52);
        }
        else
            text(c, fx + 22, 923, "Ver biblioteca  >", 28, white, 375);
    }
}
void draw_library(Canvas &c)
{
    const char *name = query.view >= 100 ? db.folders[query.view - 100].name : views[query.view];
    text(c, 76, 145, name, 44, white, 1250);
    char detail[200];
    std::snprintf(detail, sizeof detail, "%d jogos  ·  %s%s%s", filtered_count, sorts[query.sort],
                  query.text[0] ? "  ·  " : "", query.text);
    text(c, 78, 207, detail, 24, muted, 1600);
    const int start = (cursor / 12) * 12;
    for (int i = 0; i < 12 && start + i < filtered_count; i++)
    {
        const int pos = start + i, idx = filtered[pos];
        const unsigned x = 80 + (i % 6) * 296, y = 270 + (i / 6) * 314;
        artwork(c, idx, x, y, 264, !nav_focus && cursor == pos);
        if (selected[idx])
            pill(c, x + 172, y + 12, 78, "OK", true);
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
        std::snprintf(status, sizeof status, "%d selecionados%s", chosen_count(),
                      editing_collection >= 0 ? "  ·  Options para salvar" : "");
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
            text(c, 66, y, views[i], 28, focus ? bg : white, 405);
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
        "Exportar organização", "Restaurar último backup", "Criar coleção"};
    const char *desc[] = {"Aumenta os textos menores da interface.",
                          "Esconde dicas e mensagens para capturas.",
                          "Consulta os jogos pelo Elf Arsenal local.",
                          "Salva uma cópia independente no armazenamento do app.",
                          "Recupera a organização anterior. Os jogos não são alterados.",
                          "Comece uma seleção pessoal de jogos."};
    for (int i = 0; i < 6; i++)
    {
        unsigned y = 330 + i * 92;
        bool focus = i == settings_cursor;
        c.rounded_rectangle(70, y, 1770, 80, 12, focus ? panel : bg);
        if (focus)
            c.rectangle(70, y + 12, 4, 56, accent);
        text(c, 96, y + 8, names[i], 32, white, 650);
        text(c, 710, y + 20, desc[i], 24, muted, 1050);
        if (i < 2)
            text(c, 570, y + 17, (i == 0 ? large_text : record_mode) ? "Ligado" : "Desligado", 24,
                 accent, 130);
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
        text(c, 400, 760, games[idx].id, 28, muted, 1090);
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
        std::snprintf(filterlabels[0], 120, "Plataforma: %s",
                      query.platform == 2   ? "PS2"
                      : query.platform == 4 ? "PS4"
                      : query.platform == 5 ? "PS5"
                                            : "Todas");
        std::snprintf(filterlabels[1], 120, "Progresso: %s",
                      query.status ? statuses[query.status] : "Todos");
        std::snprintf(filterlabels[2], 120, "Somente favoritos: %s",
                      query.favorites ? "Sim" : "Não");
        std::snprintf(filterlabels[3], 120, "Limpar filtros e busca");
        std::snprintf(filterlabels[4], 120, "Ver resultados");
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
        if (home_count)
            add(home_games[home_cursor]);
        int start = (home_cursor / 5) * 5;
        for (int i = start; i < home_count && i < start + 5; i++)
            add(home_games[i]);
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
    if (screen == 0 || screen == 2)
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
        bool failed = false;
        for (int i = 0; i < failed_cover_count; i++)
            if (!std::strcmp(id, failed_covers[i]))
                failed = true;
        if (failed)
            continue;
        std::snprintf(requested_cover, 10, "%s", id);
        char path[80];
        std::snprintf(path, 80, "/appdb/icon?id=%s", id);
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
    add(pad < 0);
    add(storage_ok);
    add(launch_pending);
    for (int i = 0; i < count; i++)
        add(selected[i]);
    for (const char *p = message; *p; p++)
        add(static_cast<unsigned char>(*p));
    for (const char *p = entry_name; *p; p++)
        add(static_cast<unsigned char>(*p));
    return h;
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
    static std::uint64_t previous = 0;
    static int stable_frames = 0;
    auto signature = state_signature();
    if (signature == previous)
    {
        if (stable_frames >= 2)
            return;
        stable_frames++;
    }
    else
    {
        previous = signature;
        stable_frames = 0;
    }
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
        text(c, 78, 1031, "L1 / R1  Navegar", 24, muted, 300);
        text(c, 420, 1031,
             screen == 0 && home_row == 0 ? "×  Jogar"
             : screen == 1 ? (editing_collection >= 0 ? "×  Selecionar" : "×  Jogar")
                           : "×  Abrir",
             24, white, 280);
        text(c, 710, 1031, "△  Organizar", 24, muted, 300);
        text(c, 1020, 1031,
             screen == 1   ? "□  Selecionar"
             : screen == 0 ? "□  Buscar"
                           : "○  Voltar",
             24, muted, 300);
        text(c, 1370, 1031, screen == 0 ? "Options  Biblioteca" : "Options  Opções", 24, muted,
             460);
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
    read_preferences();
    if (count < 0)
        count = 0;
    rebuild();
    start_request(1, "/appdb");
    ps5::demo::run_frames(draw, "Estante: sua biblioteca pessoal");
}
