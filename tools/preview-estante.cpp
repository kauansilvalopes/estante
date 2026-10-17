// Render the real native draw callback on a host; no console launch is simulated.
#include <cassert>
#include <chrono>
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
    int sceSystemServiceParamGetInt(int, int *)
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
        std::snprintf(games[i].id, 10, "PPSA%05d", 1000 + i);
        std::snprintf(games[i].name, 192, "%s", names[i]);
        auto &art = covers[i];
        std::snprintf(art.id, 10, "%s", games[i].id);
        art.ready = true;
        for (unsigned y = 0; y < cover_edge; y++)
            for (unsigned x = 0; x < cover_edge; x++)
            {
                // Placeholder art: a different sky palette, horizon and sun per title.
                static const unsigned sky[8][6] = {
                    {250, 120, 80, 40, 20, 70},  {40, 160, 190, 10, 40, 90},
                    {150, 60, 180, 30, 10, 80},  {240, 190, 90, 120, 40, 60},
                    {60, 200, 140, 10, 60, 70},  {220, 80, 120, 40, 10, 60},
                    {90, 120, 240, 10, 20, 100}, {250, 150, 60, 60, 20, 50}};
                const unsigned *c = sky[i % 8];
                unsigned t = y * 256 / cover_edge;
                unsigned rr = c[0] + (c[3] - c[0]) * t / 256, gg = c[1] + (c[4] - c[1]) * t / 256,
                          bb = c[2] + (c[5] - c[2]) * t / 256;
                int dx = static_cast<int>(x) - (150 + i * 37 % 220), dy = static_cast<int>(y) - 170;
                if (dx * dx + dy * dy < 62 * 62)
                {
                    rr = 255;
                    gg = 235;
                    bb = 190;
                }
                bool ridge = y > 300 + ((static_cast<int>(x) + i * 53) % (90 + i * 17)) / 3;
                if (ridge)
                {
                    rr = rr / 4 + 8;
                    gg = gg / 4 + 10;
                    bb = bb / 3 + 14;
                }
                art.pixels[y * cover_edge + x] =
                    0xff000000u | ((bb & 255) << 16) | ((gg & 255) << 8) | (rr & 255);
            }
        art.tint = average_pixels(art.pixels);
        build_thumb(art.thumb, art.pixels);
        auto *m = metadata(db, games[i].id);
        m->lastChosen = 1000 - i;
        m->folders = std::uint32_t(1) << (i % 3);
        m->status = i % 4;
    }
    db.folders[0] = {true, -1, "Próximas aventuras", true};
    db.folders[1] = {true, -1, "No sofá com amigos"};
    db.folders[2] = {true, -1, "Clássicos de sempre"};
    storage_ok = true;
    i18n::language = 1;
    assert(!std::strcmp(i18n::tr("Início"), "Home") && !std::strcmp(i18n::tr("nome do jogo"), "nome do jogo"));
    i18n::language = 0;
    assert(!std::strcmp(i18n::tr("Início"), "Início"));
    i18n::language = 1;
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
    input(TOUCH);
    input(CIRCLE);
    assert(screen == 1 && !nav_focus);
    std::snprintf(query.text, sizeof query.text, "last");
    rebuild();
    assert(filtered_count < count);
    input(CIRCLE);
    assert(!query.text[0] && filtered_count == count && screen == 1);
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
    assert(stick_directions(0, 128) == LEFT && stick_directions(255, 128) == RIGHT &&
           stick_directions(128, 0) == UP && stick_directions(128, 255) == DOWN &&
           stick_directions(128, 128) == 0 && stick_directions(150, 140) == 0);
    input(DOWN);
    assert(home_row == 0);
    home_cursor = 0;
    std::memset(played_last, 0, sizeof played_last);
    activity_now = 1800000000ull;
    played_last[3] = activity_now - 600;
    played_seconds[3] = 14500;
    played_last[1] = activity_now - 7200;
    played_seconds[1] = 2700;
    played_last[6] = activity_now - 3 * 86400;
    played_seconds[6] = 80000;
    rebuild();
    assert(home_games[0] == 3 && home_games[1] == 1 && home_games[2] == 6);
    home_cursor = 0;
    if (!std::strcmp(argc == 3 ? argv[2] : "", "i18n"))
    {
        // Walk every screen and modal in English and report text that has no translation.
        i18n::on_miss = [](const char *t)
        {
            for (int i = 0; i < count; i++)
                if (!std::strcmp(t, games[i].name) || !std::strcmp(t, games[i].id))
                    return;
            for (int i = 0; i < 32; i++)
                if (db.folders[i].used && !std::strcmp(t, db.folders[i].name))
                    return;
            if (t[0] && !std::strstr(t, "PPSA") && std::strcmp(t, "estante") && std::strcmp(t, "0.4") &&
                std::strcmp(t, "English") && !(t[0] >= '0' && t[0] <= '9'))
                std::printf("UNTRANSLATED: [%s]\n", t);
        };
        auto canvas_i = Canvas::host_preview(frame);
        for (int s = 0; s < 4; s++)
        {
            screen = s;
            for (int m = 0; m <= 9; m++)
            {
                modal = m;
                if (m == 1)
                {
                    open_menu(0);
                    draw(canvas_i);
                    open_menu(1);
                }
                if (m == 5)
                    details_index = 0;
                ui_version++;
                draw(canvas_i);
            }
        }
        modal = 0;
        for (int v = 0; v < 12; v++)
        {
            screen = 1;
            query.view = v;
            rebuild();
            ui_version++;
            draw(canvas_i);
        }
        std::printf("i18n walk done\n");
        return 0;
    }
    const char *mode = argc == 3 ? argv[2] : "home";
    if (!std::strcmp(mode, "homemid"))
        home_cursor = 3;
    if (!std::strcmp(mode, "library"))
    {
        screen = 1;
        selected[2] = true;
    }
    if (!std::strcmp(mode, "apps"))
    {
        std::snprintf(games[count].id, 10, "PPSA99203");
        std::snprintf(games[count].name, 192, "PS5SX2");
        std::snprintf(games[count + 1].id, 10, "PSPS69691");
        std::snprintf(games[count + 1].name, 192, "Elf Arsenal");
        count += 2;
        rebuild();
        query.view = 4;
        rebuild();
        screen = 1;
    }
    if (!std::strcmp(mode, "ps2"))
    {
        add_ps2_games("{\"entries\":[{\"name\":\"Shadow of the Colossus (BR) (Dub-e-Leg).iso\",\"dir\":false},"
                      "{\"name\":\"Toy Story 3 (BR).iso\",\"dir\":false},{\"name\":\"notes.txt\",\"dir\":false},"
                      "{\"name\":\"Jackie Chan Adventures (PT).iso.partial\",\"dir\":false},"
                      "{\"name\":\"Wall-E (BR).iso\",\"dir\":false}]}");
        assert(count == 11 && is_ps2_id(games[8].id) && !std::strcmp(ps2_file[9], "Toy Story 3 (BR).iso"));
        add_ps2_games("{\"entries\":[{\"name\":\"UP (BR).iso\",\"dir\":false}]}"); // rescan replaces
        assert(count == 9 && !std::strcmp(games[8].name, "UP (BR)") && platform(games[8], db) == 2);
        add_ps2_games("{\"entries\":[{\"name\":\"Shadow of the Colossus (BR) (Dub-e-Leg).iso\",\"dir\":false},"
                      "{\"name\":\"Toy Story 3 (BR).iso\",\"dir\":false},{\"name\":\"Wall-E (BR).iso\",\"dir\":false}]}");
        rebuild();
        query.view = 1;
        rebuild();
        screen = 1;
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
    if (!std::strcmp(mode, "bench"))
    {
        // Host timing of full redraws per screen (relative numbers only; the console CPU differs).
        for (int target : {0, 1, 2, 3})
        {
            screen = target;
            draw(canvas);
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 30; i++)
            {
                ui_version++;
                draw(canvas);
            }
            const auto t1 = std::chrono::steady_clock::now();
            std::printf("screen %d: %.2f ms/frame\n", target,
                        std::chrono::duration<double, std::milli>(t1 - t0).count() / 30.0);
        }
        return 0;
    }
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
