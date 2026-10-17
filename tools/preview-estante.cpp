// Render the real native draw callback on a host; no console launch is simulated.
#include <cassert>
#define main estante_console_entry
#include "../src/main.cpp"
#undef main
extern "C"
{
    int sceUserServiceInitialize(const void *)
    {
        return -1;
    }
    int sceUserServiceGetInitialUser(int *)
    {
        return -1;
    }
    int scePadInit()
    {
        return -1;
    }
    int scePadOpen(int, int, int, const void *)
    {
        return -1;
    }
    int scePadRead(int, PadSample *, int)
    {
        return 0;
    }
    std::uint64_t sceKernelGetProcessTime()
    {
        return 0;
    }
    int sceKernelDebugOutText(int, const char *)
    {
        return 0;
    }
    int sceNetSocket(const char *, int, int, int)
    {
        return -1;
    }
    int sceNetConnect(int, const void *, std::uint32_t)
    {
        return -1;
    }
    int sceNetSend(int, const void *, std::size_t, int)
    {
        return -1;
    }
    int sceNetRecv(int, void *, std::size_t, int)
    {
        return -1;
    }
    int sceNetSetsockopt(int, int, int, const void *, std::uint32_t)
    {
        return -1;
    }
    int sceNetSocketClose(int)
    {
        return 0;
    }
}
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return 2;
    static std::uint32_t frame[0x1000000 / 4];
    const char *names[] = {"Horizon Explorer",  "Ōkami™ – Edição ★",    "The Last Journey",
                           "Stellar Adventure", "Shadow of the Valley", "Racing Legends",
                           "Mountain Quest",    "Little Planet"};
    count = 8;
    for (int i = 0; i < count; i++)
    {
        std::snprintf(games[i].id, 10, "PPSA%05d", 90000 + i);
        std::snprintf(games[i].name, 192, "%s", names[i]);
        auto &art = covers[i];
        std::snprintf(art.id, 10, "%s", games[i].id);
        art.ready = true;
        for (unsigned y = 0; y < cover_edge; y++)
            for (unsigned x = 0; x < cover_edge; x++)
            {
                unsigned rr = 20 + i * 13 + x / 8, gg = 40 + y / 4, bb = 90 + i * 15;
                bool ridge = y > 160 + (static_cast<int>(x) % 120) / 2;
                if (ridge)
                {
                    rr /= 2;
                    gg /= 2;
                    bb /= 2;
                }
                if ((x + i * 31) % 83 < 2 && (y + i * 17) % 47 < 2)
                {
                    rr = gg = bb = 220;
                }
                art.pixels[y * cover_edge + x] =
                    0xff000000u | ((bb & 255) << 16) | ((gg & 255) << 8) | (rr & 255);
            }
        art.tint = average_pixels(art.pixels);
        auto *m = metadata(db, games[i].id);
        m->lastChosen = 1000 - i;
        m->folders = std::uint32_t(1) << (i % 3);
        m->status = i % 4;
    }
    db.folders[0] = {true, -1, "Próximas aventuras", true};
    db.folders[1] = {true, -1, "No sofá com amigos"};
    db.folders[2] = {true, -1, "Clássicos de sempre"};
    storage_ok = true;
    pad = 0;
    work.store(1);
    rebuild();
    assert(screen == 0);
    input(RIGHT);
    assert(home_cursor == 1);
    input(LEFT);
    assert(home_cursor == 0);
    input(R1);
    assert(screen == 1);
    input(RIGHT);
    assert(cursor == 1);
    input(DOWN);
    assert(cursor == 7);
    input(UP);
    assert(cursor == 1);
    input(LEFT);
    assert(cursor == 0);
    input(LEFT);
    assert(nav_focus);
    input(RIGHT);
    assert(!nav_focus);
    input(TOUCH);
    assert(nav_focus);
    input(TOUCH);
    assert(!nav_focus);
    input(L1);
    assert(screen == 0);
    start_request(4, "/api/state");
    assert(pending_kind == 4);
    pending_kind = 0;
    auto before = state_signature();
    selected[0] = true;
    assert(state_signature() != before);
    selected[0] = false;
    std::snprintf(entry_name, sizeof entry_name, "café");
    erase_character();
    assert(!std::strcmp(entry_name, "caf"));
    entry_name[0] = 0;
    std::snprintf(response, sizeof response,
                  "{\"gameRunning\":true,\"runningTitleId\":\"PPSA12345\"}");
    char active[10];
    bool running = false;
    assert(parse_running(active, running) && running && !std::strcmp(active, "PPSA12345"));
    std::snprintf(response, sizeof response,
                  "{\"gameRunning\":\"true\",\"runningTitleId\":\"PPSA12345\"}");
    assert(!parse_running(active, running));
    open_collection(0);
    assert(screen == 2 && collection_entries[0] == -1);
    input(CIRCLE);
    assert(collection_parent == -1);
    set_screen(0);
    input(CROSS);
    assert(launch_pending && pending_kind == 4 && modal == 0);
    launch_pending = false;
    pending_kind = 0;
    message_until = 0;
    input(SQUARE);
    assert(screen == 1 && modal == 3);
    input(CIRCLE);
    set_screen(0);
    home_cursor = 5;
    rebuild();
    assert(home_cursor == 5);
    home_cursor = 0;
    const char *mode = argc == 3 ? argv[2] : "home";
    if (!std::strcmp(mode, "library"))
    {
        screen = 1;
        selected[2] = true;
    }
    if (!std::strcmp(mode, "sidebar"))
    {
        screen = 1;
        nav_focus = true;
    }
    if (!std::strcmp(mode, "collections"))
        screen = 2;
    if (!std::strcmp(mode, "settings"))
        screen = 3;
    if (!std::strcmp(mode, "details"))
    {
        details_index = 0;
        modal = 5;
    }
    if (!std::strcmp(mode, "menu"))
        open_menu(0);
    if (!std::strcmp(mode, "search"))
        open_keyboard(1);
    if (!std::strcmp(mode, "large"))
    {
        screen = 1;
        large_text = true;
    }
    if (!std::strcmp(mode, "rec"))
        record_mode = true;
    if (!std::strcmp(mode, "empty"))
    {
        count = 0;
        db = {};
        rebuild();
    }
    auto canvas = Canvas::host_preview(frame);
    draw(canvas);
    FILE *f = std::fopen(argv[1], "wb");
    if (!f)
        return 3;
    std::fprintf(f, "P6\n1920 1080\n255\n");
    for (unsigned y = 0; y < 1080; y++)
        for (unsigned x = 0; x < 1920; x++)
        {
            unsigned offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                              ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                              ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
            auto pixel = frame[(((y >> 7) * 15 + (x >> 7)) * 65536 + offset) / 4];
            unsigned char rgb[3] = {static_cast<unsigned char>(pixel),
                                    static_cast<unsigned char>(pixel >> 8),
                                    static_cast<unsigned char>(pixel >> 16)};
            std::fwrite(rgb, 1, 3, f);
        }
    return std::fclose(f);
}
