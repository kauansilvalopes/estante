// Estante - GPU canvas: the drawing calls the screens use, recorded into the kit's draw list.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Same call surface as the CPU canvas in demo_renderer.hpp (rectangle, rounded_rectangle, circle,
// gradient, blend, shadow, rounded_picture, label, measure, clear, dim), so the screen code in
// main.cpp draws through either. Positions are in the 1920x1080 virtual canvas, y down.
#pragma once

#include "demo_renderer.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/gl_batch.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace estante
{
class GlCanvas final
{
  public:
    using Color = ps5::demo::Color;

    // Starts a frame: `list` is cleared and receives everything drawn until the next begin().
    void begin(hui::gfx::DrawList *list, hui::gfx::GlBatch *batch, const hui::gfx::Font *font,
               std::uint32_t font_texture) noexcept;

    void clear(Color color) noexcept;
    void dim() noexcept;
    void label(unsigned x, unsigned y, std::string_view value, unsigned size, Color color,
               unsigned max_width = 1800) noexcept;
    unsigned measure(std::string_view value, unsigned size) noexcept;
    void gradient(unsigned x, unsigned y, unsigned width, unsigned height, Color top,
                  Color bottom) noexcept;
    void blend(unsigned x, unsigned y, unsigned width, unsigned height, Color color,
               unsigned alpha) noexcept;
    void rounded_picture(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                         unsigned source_edge, const std::uint32_t *pixels) noexcept;
    void shadow(unsigned x, unsigned y, unsigned width, unsigned height, unsigned spread,
                unsigned alpha) noexcept;
    void rounded_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, unsigned radius,
                           Color color) noexcept;
    void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, Color color) noexcept;
    void circle(unsigned center_x, unsigned center_y, unsigned radius, Color color) noexcept;

    // Tells the canvas that the pixels behind this pointer changed (a cover slot was refilled).
    void invalidate(const std::uint32_t *pixels) noexcept;

    // Extra drawing the GPU path offers beyond the CPU canvas's calls.
    hui::gfx::DrawList &list() noexcept
    {
        return *list_;
    }
    std::uint32_t texture_for(const std::uint32_t *pixels, unsigned edge) noexcept;

  private:
    struct CachedTexture
    {
        const std::uint32_t *pixels = nullptr;
        unsigned edge = 0;
        std::uint32_t texture = 0;
        bool dirty = false;
    };

    hui::gfx::DrawList *list_ = nullptr;
    hui::gfx::GlBatch *batch_ = nullptr;
    const hui::gfx::Font *font_ = nullptr;
    std::uint32_t font_texture_ = 0;
    std::vector<CachedTexture> textures_;
};

// Converts the packed 0xAABBGGRR colour the screens use to the kit's float colour.
hui::gfx::Color to_gfx(ps5::demo::Color color) noexcept;
} // namespace estante
