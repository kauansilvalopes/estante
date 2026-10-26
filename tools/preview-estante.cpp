// Render the real native draw callback on a host; no console launch is simulated.
#include <cassert>
#include <chrono>
#include <set>
#include <string>
#define main estante_console_entry
#include "../src/main.cpp"
#undef main
#ifdef ESTANTE_GL
#include "core/save_file.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <vector>
#endif
static std::set<std::string> preview_untranslated;
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
    std::uint64_t fake_process_time = 0;
    std::uint64_t sceKernelGetProcessTime()
    {
        return fake_process_time;
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

#ifdef ESTANTE_GL
// Renders the real draw callback through the kit's GL renderer on Mesa's surfaceless EGL (llvmpipe).
struct GlHost
{
    hui::gfx::Renderer renderer;
    hui::gfx::Font font;
    hui::gfx::DrawList list;
    estante::GlCanvas canvas;
    std::uint32_t font_texture = 0;
    GLuint fbo = 0, color = 0;

    bool init()
    {
        auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        EGLDisplay display = get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                         : eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
            return false;
        const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
        EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
        if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
            return false;
        hui::gfx::set_glsl_prefix("#version 450 core\n");
        if (!renderer.init())
            return false;
        std::string bytes;
        if (!hui::save::read_file("assets/fonts/inter.huifont", &bytes) || !font.load(bytes))
            return false;
        font_texture = renderer.batch().create_font_texture(font);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    void render()
    {
        canvas.begin(&renderer.batch(), &font, font_texture);
        draw(canvas);
        renderer.begin();
        canvas.submit(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        renderer.present(fbo, 1920, 1080);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
    bool write_ppm(FILE *f)
    {
        std::vector<unsigned char> pixels(1920u * 1080u * 4u);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 1079; y >= 0; y--)
            for (int x = 0; x < 1920; x++)
                std::fwrite(&pixels[(static_cast<std::size_t>(y) * 1920 + x) * 4], 1, 3, f);
        return true;
    }
};
namespace estante
{
[[noreturn]] void run_gl(GlDrawScene) noexcept
{
    std::abort(); // the preview draws frames itself
}
} // namespace estante
static GlHost *gl_host = nullptr;
Canvas &preview_canvas(std::uint32_t *)
{
    if (!gl_host)
    {
        gl_host = new GlHost;
        if (!gl_host->init())
        {
            std::fprintf(stderr, "no GL context or renderer (is Mesa EGL installed?)%s", "\n");
            std::exit(4);
        }
    }
    return gl_host->canvas;
}
void render_frame(Canvas &)
{
    gl_host->render();
}
#else
Canvas &preview_canvas(std::uint32_t *frame)
{
    static Canvas canvas = Canvas::host_preview(frame);
    return canvas;
}
void render_frame(Canvas &canvas)
{
    draw(canvas); // the CPU canvas draws into the preview's tiled frame buffer
}
#endif
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return 2;
    static std::uint32_t frame[0x1000000 / 4];
    const char *names[] = {"Horizon Explorer",  "Ōkami™ – Edição ★",    "The Last Journey",
                           "Stellar Adventure", "Shadow of the Valley", "Racing Legends",
                           "Mountain Quest",    "Little Planet"};
    count = 20;
    for (int i = 0; i < count; i++)
    {
        std::snprintf(games[i].id, 10, "PPSA%05d", 1000 + i);
        if (i < 8)
            std::snprintf(games[i].name, 192, "%s", names[i]);
        else
            std::snprintf(games[i].name, 192, "%s %d", names[i % 8], i / 8 + 1);
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
#ifdef ESTANTE_GL
        art.has_tall = true;
        for (unsigned y = 0; y < tall_h; y++)
            for (unsigned x = 0; x < tall_w; x++)
                art.tall[y * tall_w + x] = art.pixels[(y * cover_edge / tall_h) * cover_edge + x * cover_edge / tall_w];
#endif
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
    assert(home_style == 1); // a new install starts on the shelf
    home_style = 0;          // this part of the preview walks the catalogue rows
    rebuild();
    assert(screen == 0 && home_style == 0);
    // rows: Continue (every game was started once), Want to play (status 1), the pinned collection
    // and PS5
    assert(rail_count == 4 && rails[0].kind == Rail::Continue && rails[0].count == 20 &&
           rails[1].kind == Rail::Want && rails[1].count == 5 && rails[2].kind == Rail::Folder &&
           rails[2].count == 7 && rails[3].kind == Rail::PS5 && rails[3].count == 20);
    assert(home_game() == rails[0].items[0]);
    input(RIGHT);
    assert(rails[0].cursor == 1);
    input(LEFT);
    assert(rails[0].cursor == 0);
    input(DOWN);
    assert(home_row == 1);
    input(RIGHT);
    input(UP);
    assert(home_row == 0 && rails[1].cursor == 1); // each row remembers its own card
    input(UP);
    assert(home_row == 0); // nothing above the first row
    input(R1);
    assert(screen == 1);
    input(RIGHT);
    assert(cursor == 1);
    input(DOWN);
    assert(cursor == 7); // six icons per row
    input(UP);
    assert(cursor == 1);
    input(UP);
    assert(library_zone == 1);
    input(RIGHT);
    assert(library_chip == 1);
    input(LEFT);
    input(DOWN);
    assert(library_zone == 0);
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
    before = state_signature();
    details_button = 2;
    assert(details_page || state_signature() != before);
    details_button = 0;
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
    {
        // Trophy file: three trophies, grades platinum/gold/bronze; the gold one is earned and carries
        // 0x20 extra bytes before its fields, as earned records do on the console.
        std::memset(response, 0, 0x2000);
        auto put32 = [](std::size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; i++)
                response[at + i] = static_cast<char>(v >> (24 - 8 * i));
        };
        const std::uint32_t grades[3] = {1, 2, 4};
        for (int i = 0; i < 3; i++)
        {
            const std::size_t at = 0x300 + i * 0xe0, shift = i == 1 ? 0x20 : 0;
            put32(at, 0x500);
            put32(at + 4, 0xc0);
            put32(at + 0x10 + shift, i);
            put32(at + 0x14 + shift, grades[i]);
            const std::size_t state = 0x1000 + i * 0x60;
            put32(state, 0x800);
            put32(state + 4, 0x50);
            put32(state + 0x10, i);
            put32(state + 0x14, i == 1 ? 0x11 : 0x10);
            put32(state + 0x24, i == 1 ? 1700000000u : 0);
        }
        response_size = 0x2000;
        TrophySummary parsed;
        assert(parse_trophy_title(parsed));
        assert(parsed.total == 3 && parsed.earned == 1 && parsed.total_by[1] == 1 &&
               parsed.total_by[2] == 1 && parsed.total_by[4] == 1 && parsed.earned_by[2] == 1 &&
               parsed.earned_by[1] == 0 && parsed.earned_by[4] == 0);
    }
    trophies[0].state = 2;
    clear_trophy_summaries();
    assert(trophies[0].state == 0);
    open_collection(0);
    assert(screen == 2 && collection_entries[0] == -1);
    input(CIRCLE);
    assert(collection_parent == -1);
    set_screen(0);
    {
        const bool previous_storage = storage_ok;
        storage_ok = false; // don't persist preview input into the host's /download0.
        home_style = 0;
        details_page = false;
        modal = 0;
        editing_collection = -1;
        held = L2;
        input(L2);
        assert(home_style == 0 && !details_page);
        held = L2 | R2;
        input(R2); // second trigger completes the chord; it must not open details.
        assert(home_style == 1 && !details_page);
        held = 0;
        input(0);
        modal = 1;
        held = L2 | R2;
        input(R2);
        assert(home_style == 1);
        modal = 0;
        details_page = true;
        input(R2);
        assert(home_style == 1);
        details_page = false;
        screen = 1;
        input(R2);
        assert(home_style == 1);
        screen = 0;
        editing_collection = 0;
        input(R2);
        assert(home_style == 1);
        editing_collection = -1;
        home_style = 0;
        held = 0;
        message_until = 0;
        storage_ok = previous_storage;
    }
    // X on a card plays it (the launch animation can be cancelled); R2 opens the game page
    input(CROSS);
    assert(anim_idx == rails[0].items[0] && !details_page);
    input(CIRCLE);
    assert(anim_idx < 0);
    input(R2);
    assert(details_page && details_index == rails[0].items[0] && details_button == 0);
    input(RIGHT);
    assert(details_button == 1);
    input(RIGHT);
    input(RIGHT);
    assert(details_button == 2);
    input(LEFT);
    input(LEFT);
    input(CROSS);
    assert(anim_idx >= 0 && !launch_pending); // the launch animation runs first
    input(CIRCLE);
    assert(anim_idx < 0 && !launch_pending); // Circle cancels it
    launch_anim_enabled = false;
    input(CROSS);
    assert(launch_pending && pending_kind == 4 && modal == 0 && !std::strcmp(launch_game, games[details_index].id));
    launch_anim_enabled = true;
    launch_pending = false;
    pending_kind = 0;
    message_until = 0;
    input(SQUARE); // favourite from the game page (saving needs the console's /download0)
    assert(details_page);
    input(TRIANGLE); // the organize menu opens over the page
    assert(modal == 1 && details_page);
    input(CIRCLE);
    assert(modal == 0 && details_page);
    input(CIRCLE);
    assert(!details_page);
    // PS2: the picked image, not PS5SX2, becomes the "continue" entry
    std::snprintf(games[count].id, 10, "PS2ABCDEF");
    std::snprintf(games[count].name, 192, "Image");
    std::snprintf(ps2_file[count], sizeof ps2_file[0], "Image.iso");
    count++;
    rebuild();
    start_launch(count - 1);
    assert(!std::strcmp(launch_game, "PS2ABCDEF") && !std::strcmp(launch_id, "PPSA99203"));
    launch_pending = false;
    pending_kind = 0;
    count--;
    rebuild();
    input(SQUARE);
    assert(screen == 1 && modal == 3);
    input(CIRCLE);
    set_screen(0);
    home_style = 1; // the earlier shelf keeps working
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
    home_style = 0;
    rebuild();
    screen = 0;
    message_until = 0;
    message[0] = 0;
    if (!std::strcmp(argc == 3 ? argv[2] : "", "i18n"))
    {
        // Walk every screen and modal in English and report text that has no translation.
        preview_untranslated.clear();
        i18n::on_miss = [](const char *t)
        {
            for (int i = 0; i < count; i++)
                if (!std::strcmp(t, games[i].name) || !std::strcmp(t, games[i].id))
                    return;
            for (int i = 0; i < 32; i++)
                if (db.folders[i].used && !std::strcmp(t, db.folders[i].name))
                    return;
            if (!std::strcmp(t, "@kndofc") || !std::strcmp(t, "Idioma / Language") ||
                !std::strcmp(t, "PlayStation") || !std::strcmp(t, "Apps") ||
                !std::strcmp(t, "OK") || !std::strcmp(t, "<") ||
                !std::strncmp(t, "Platform: ", 10) || !std::strncmp(t, "Progress: ", 10) ||
                !std::strncmp(t, "Favorites only: ", 16) || (t[0] && !t[1]) ||
                !std::strcmp(t, "English") || !std::strcmp(t, "estante") ||
                !std::strcmp(t, "0.5.5") || !t[0] || (t[0] >= '0' && t[0] <= '9') ||
                ((!std::strncmp(t, "PlayStation ", 12) || !std::strncmp(t, "Nintendo ", 9) ||
                  !std::strncmp(t, "GameCube", 8)) && std::strstr(t, "  ·  ")))
                return; // brand, keyboard labels and already-formatted dynamic labels
            preview_untranslated.emplace(t);
        };
        auto &canvas_i = preview_canvas(frame);
        for (int s = 0; s < 4; s++)
        {
            screen = s;
            for (int m = 0; m <= 9; m++)
            {
                modal = m;
                if (m == 1)
                {
                    open_menu(0);
                    render_frame(canvas_i);
                    open_menu(1);
                }
                if (m == 5)
                    details_index = 0;
                ui_version++;
                render_frame(canvas_i);
            }
        }
        details_index = 0;
        details_page = true;
        ui_version++;
        render_frame(canvas_i);
        details_page = false;
        modal = 0;
        screen = 0;
        home_style = 1;
        ui_version++;
        render_frame(canvas_i);
        home_style = 0;
        for (int v = 0; v < 12; v++)
        {
            screen = 1;
            query.view = v;
            rebuild();
            ui_version++;
            render_frame(canvas_i);
        }
        i18n::language = 1;
        for (const auto &t : preview_untranslated)
            std::printf("UNTRANSLATED: [%s]\n", t.c_str());
        std::printf("i18n walk done\n");
        return 0;
    }
    const char *mode = argc == 3 ? argv[2] : "home";
#ifdef ESTANTE_GL
    // Synthetic wide art (layout review only: it says nothing about real pictures).
    auto fake_wide = [](int idx)
    {
        std::snprintf(wide_art_id, sizeof wide_art_id, "%s", games[idx].id);
        std::snprintf(wide_have, sizeof wide_have, "%s", games[idx].id);
        wide_art_state = 2;
        for (unsigned y = 0; y < wide_h; y++)
            for (unsigned x = 0; x < wide_w; x++)
            {
                const int dx = static_cast<int>(x) - 640, dy = static_cast<int>(y) - 250;
                const bool glow = dx * dx + dy * dy < 150 * 150;
                const unsigned r = glow ? 255 : 60 + y / 5 + (idx * 37) % 60,
                               g = glow ? 230 : 70 + x / 12 + (idx * 53) % 50,
                               b = glow ? 180 : 120 + y / 6;
                wide_art[y * wide_w + x] = 0xff000000u | ((b & 255) << 16) | ((g & 255) << 8) | (r & 255);
            }
    };
    if (!std::strcmp(mode, "home") || !std::strcmp(mode, "homemid") ||
        !std::strcmp(mode, "details") || !std::strcmp(mode, "detailstroph"))
        fake_wide(0);
    if (!std::strcmp(mode, "homemid"))
        fake_wide(3);
#endif
    if (!std::strcmp(mode, "homemid"))
    {
        home_row = 0;
        rails[0].cursor = 3;
    }
    if (!std::strncmp(mode, "es", 2))
    {
        i18n::language = 2; // Spanish layout
        if (!std::strcmp(mode, "eslibrary"))
            screen = 1;
        if (!std::strcmp(mode, "esmenu"))
            open_menu(0);
    }
    if (!std::strncmp(mode, "pt", 2))
    {
        i18n::language = 0; // Portuguese layout: longer words
        if (!std::strcmp(mode, "ptdetails"))
        {
            details_index = 0;
            details_page = true;
            trophies[0].state = 1;
        }
        if (!std::strcmp(mode, "ptlibrary"))
            screen = 1;
    }
    if (!std::strcmp(mode, "homewant"))
    {
        home_row = 1;
        rails[1].cursor = 2;
    }
    if (!std::strcmp(mode, "shelf"))
        home_style = 1;
    if (!std::strcmp(mode, "library") || !std::strcmp(mode, "libraryscroll") || !std::strcmp(mode, "libchips"))
    {
        screen = 1;
        selected[2] = true;
        cursor = 3;
        if (!std::strcmp(mode, "libraryscroll"))
            cursor = 18;
        if (!std::strcmp(mode, "libchips"))
            library_zone = 1;
        library_chip = 2;
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
        assert(count == 23 && is_ps2_id(games[20].id) && !std::strcmp(ps2_file[21], "Toy Story 3 (BR).iso"));
        add_ps2_games("{\"entries\":[{\"name\":\"UP (BR).iso\",\"dir\":false}]}"); // rescan replaces
        assert(count == 21 && !std::strcmp(games[20].name, "UP (BR)") && platform(games[20], db) == 2);
        add_ps2_games("{\"entries\":[{\"name\":\"Shadow of the Colossus (BR) (Dub-e-Leg).iso\",\"dir\":false},"
                      "{\"name\":\"Toy Story 3 (BR).iso\",\"dir\":false},{\"name\":\"Wall-E (BR).iso\",\"dir\":false}]}");
        rebuild();
        query.view = 1;
        rebuild();
        screen = 1;
    }
    if (!std::strncmp(mode, "anim", 4))
    {
        // animN: N tenths of the way through the launch animation (anim0 ... anim10).
        const int tenth = std::atoi(mode + 4);
        fake_process_time = 5000000;
        anim_idx = 3;
        anim_start = fake_process_time - anim_length * static_cast<unsigned>(tenth) / 10;
        screen = 0;
    }
    if (!std::strcmp(mode, "flip"))
    {
        // Renders a frame, then moves the cursor and renders again 90 ms later: the carousel is mid-slide.
        home_style = 1;
        home_cursor = 3;
        fake_process_time = 5000000;
        render_frame(preview_canvas(frame));
        home_cursor = 4;
        fake_process_time += 90000;
    }
#ifdef ESTANTE_GL
    if (!std::strncmp(mode, "turn", 4))
    {
        // turnN: the right stick held to the right for N frames of 100 ms.
        home_style = 1;
        home_cursor = 3;
        fake_process_time = 5000000;
        render_frame(preview_canvas(frame));
        stick_rx = 1.0f;
        for (int step = 0; step < std::atoi(mode + 4); step++)
        {
            fake_process_time += 100000;
            render_frame(preview_canvas(frame));
        }
    }
    if (!std::strncmp(mode, "back", 4))
    {
        // backN: the case has been turning toward its back for N tenths of a second.
        home_style = 1;
        home_cursor = 3;
        fake_process_time = 5000000;
        render_frame(preview_canvas(frame));
        show_back = true;
        if (std::strcmp(mode, "back0") != 0 && std::strcmp(mode, "backnoart") != 0)
        {
            std::snprintf(wide_art_id, 10, "%s", games[home_games[home_cursor]].id);
            wide_art_state = 2;
            for (unsigned y = 0; y < wide_h; y++)
                for (unsigned x = 0; x < wide_w; x++)
                    wide_art[y * wide_w + x] = 0xff000000u | ((60 + y / 2) << 16) | ((90 + x / 4) << 8) | (150 - y / 3);
        }
        for (int step = 0; step < std::atoi(mode + 4); step++)
        {
            fake_process_time += 100000;
            render_frame(preview_canvas(frame));
        }
    }
#endif
    if (!std::strncmp(mode, "theme", 5))
        apply_theme(std::atoi(mode + 5));
    if (!std::strcmp(mode, "sidebar"))
    {
        screen = 1;
        nav_focus = true;
    }
    if (!std::strcmp(mode, "collections"))
        screen = 2;
    if (!std::strcmp(mode, "ptcollections"))
        screen = 2;
    if (!std::strcmp(mode, "settings"))
        screen = 3;
    if (!std::strcmp(mode, "ptsettings") || !std::strcmp(mode, "essettings"))
        screen = 3;
    if (!std::strncmp(mode, "details", 7))
    {
        details_index = 0;
        details_page = true;
        details_button = !std::strcmp(mode, "detailstroph") ? 1 : 0;
        activity_now = 1800000000ull;
        played_last[0] = activity_now - 3 * 86400;
        played_seconds[0] = 80000;
        metadata(db, games[0].id)->folders = 3;
        if (!std::strcmp(mode, "detailstroph"))
        {
            trophies[0].state = 2;
            const unsigned short total_by[5] = {0, 1, 3, 9, 27}, earned_by[5] = {0, 0, 1, 4, 9};
            for (int g = 1; g <= 4; g++)
            {
                trophies[0].total_by[g] = total_by[g];
                trophies[0].earned_by[g] = earned_by[g];
                trophies[0].total += total_by[g];
                trophies[0].earned += earned_by[g];
            }
            metadata(db, games[0].id)->favorite = true;
        }
    }
    if (!std::strcmp(mode, "detailsnowide"))
    {
        details_index = 2;
        details_page = true;
    }
#ifdef ESTANTE_GL
    if (!std::strcmp(mode, "homenowide"))
        wide_art_state = 0;
#endif
    if (!std::strcmp(mode, "menu"))
        open_menu(0);
    if (!std::strcmp(mode, "ptmenu"))
        open_menu(0);
    if (!std::strcmp(mode, "search"))
        open_keyboard(1);
    if (!std::strcmp(mode, "ptsearch"))
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
    auto &canvas = preview_canvas(frame);
    if (!std::strcmp(mode, "bench"))
    {
        // Host timing of full redraws per screen (relative numbers only; the console CPU differs).
        for (int target : {0, 1, 2, 3})
        {
            screen = target;
            render_frame(canvas);
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 30; i++)
            {
                ui_version++;
                render_frame(canvas);
            }
            const auto t1 = std::chrono::steady_clock::now();
            std::printf("screen %d: %.2f ms/frame\n", target,
                        std::chrono::duration<double, std::milli>(t1 - t0).count() / 30.0);
        }
        // Launch animation frame (full-screen gradient, cover, disc): timed with the clock moving.
        anim_idx = 3;
        fake_process_time = 9000000;
        anim_start = fake_process_time - anim_length / 2;
        render_frame(canvas);
        const auto a0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; i++)
        {
            fake_process_time += 16000;
            anim_start = fake_process_time - anim_length / 2;
            render_frame(canvas);
        }
        const auto a1 = std::chrono::steady_clock::now();
        std::printf("launch animation: %.2f ms/frame\n",
                    std::chrono::duration<double, std::milli>(a1 - a0).count() / 30.0);
        return 0;
    }
    render_frame(canvas);
    FILE *f = std::fopen(argv[1], "wb");
    if (!f)
        return 3;
    std::fprintf(f, "P6\n1920 1080\n255\n");
#ifdef ESTANTE_GL
    gl_host->write_ppm(f);
    return std::fclose(f);
#endif
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
