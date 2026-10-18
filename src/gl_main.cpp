// Estante - GPU frame loop. SPDX-License-Identifier: GPL-3.0-or-later
#include "gl_main.hpp"

#include "core/save_file.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/system.hpp"

#include <GL/glcorearb.h>

#include <cstdint>
#include <string>

namespace estante
{
namespace
{
constexpr const char *font_path = "/app0/assets/fonts/noto.huifont";
}

void run_gl(GlDrawScene draw) noexcept
{
    static hui::ps5::Display display;
    static hui::gfx::Renderer renderer;
    static hui::gfx::Font font;
    static hui::gfx::DrawList list;
    static GlCanvas canvas;

    hui::sys::log("[ESTANTE] gl: opening display");
    if (!display.open(1920, 1080))
    {
        hui::sys::log("[ESTANTE] gl: display open failed (egl error %s)",
                      hui::ps5::egl_error_name(display.last_error()));
        hui::sys::park();
    }
    if (!renderer.init())
    {
        hui::sys::log("[ESTANTE] gl: renderer init failed");
        hui::sys::park();
    }
    std::string bytes;
    if (!hui::save::read_file(font_path, &bytes) || !font.load(bytes))
    {
        hui::sys::log("[ESTANTE] gl: font %s failed: %s", font_path, font.error().c_str());
        hui::sys::park();
    }
    const std::uint32_t font_texture = renderer.batch().create_font_texture(font);
    hui::sys::log("[ESTANTE] gl: ready %dx%d", display.width(), display.height());

    std::uint64_t frames = 0;
    std::int64_t last_report = hui::sys::monotonic_us();
    for (;;)
    {
        canvas.begin(&list, &renderer.batch(), &font, font_texture);
        draw(canvas);
        renderer.begin();
        renderer.draw(list);
        renderer.present(0, display.width(), display.height());
        if (!display.swap())
        {
            hui::sys::log("[ESTANTE] gl: swap failed");
            hui::sys::park();
        }
        frames++;
        const std::int64_t now = hui::sys::monotonic_us();
        if (now - last_report >= 5000000)
        {
            hui::sys::log("[ESTANTE] gl: frames=%llu fps=%.1f draw_calls=%zu",
                          static_cast<unsigned long long>(frames),
                          static_cast<double>(frames) * 1e6 / static_cast<double>(now - last_report),
                          renderer.last_draw_calls());
            frames = 0;
            last_report = now;
        }
    }
}
} // namespace estante
