#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Phyxel {
namespace UI {

class UIRenderer;

/**
 * @brief Bitmap font renderer using an embedded 8x16 CP437 font.
 *
 * Generates a 128x128 grayscale atlas at startup (16 columns x 8 rows of
 * 8x16 glyphs = first 128 ASCII chars). The top-left pixel (0,0) is solid
 * white, reserved for solid-rectangle drawing by UIRenderer::drawRect().
 */
class BitmapFont {
public:
    /// Glyph size in pixels.
    static constexpr int GLYPH_W = 8;
    static constexpr int GLYPH_H = 16;

    /// Atlas dimensions (16 glyphs wide, 8 rows = 128 chars).
    static constexpr int ATLAS_COLS = 16;
    static constexpr int ATLAS_ROWS = 8;
    static constexpr int ATLAS_W = ATLAS_COLS * GLYPH_W;  // 128
    static constexpr int ATLAS_H = ATLAS_ROWS * GLYPH_H;  // 128

    BitmapFont() = default;

    /// Generate the embedded 8x16 bitmap font atlas and upload it.
    bool initialize(UIRenderer* renderer);

    /// Bake a TrueType font into an R8 atlas (anti-aliased, proportional metrics)
    /// and upload it. Metrics are normalized so a line is GLYPH_H px tall at
    /// scale 1.0 — text keeps the same on-screen size as the bitmap font, so
    /// existing layouts (theme scales) are unchanged. Returns false on failure
    /// (caller should fall back to initialize()).
    bool initializeTTF(UIRenderer* renderer, const std::string& ttfPath, float pixelHeight = 48.0f);

    /// Measure text width in pixels at the given scale.
    float measureText(const std::string& text, float scale = 1.0f) const;

    /// Get line height in pixels at the given scale.
    float lineHeight(float scale = 1.0f) const {
        return (ttf_ ? ttfLineHeightPx_ : static_cast<float>(GLYPH_H)) * scale;
    }

    /// Draw a string. Call between UIRenderer::beginFrame() and endFrame().
    void drawText(UIRenderer* renderer, const std::string& text,
                  glm::vec2 pos, glm::vec4 color, float scale = 1.0f) const;

private:
    /// Get UV rect for a character (bitmap path).
    void getGlyphUV(char c, glm::vec2& uvMin, glm::vec2& uvMax) const;

    bool initialized_ = false;

    // ── TrueType path ───────────────────────────────────────────
    static constexpr int TTF_FIRST = 32;   // first printable ASCII
    static constexpr int TTF_COUNT = 95;   // 32..126

    struct GlyphInfo {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas UV rect
        float xoff = 0, yoff = 0;              // pen offset (baked px)
        float xadvance = 0;                    // advance (baked px)
        float w = 0, h = 0;                    // glyph size (baked px)
    };
    bool  ttf_ = false;
    float ttfNorm_ = 1.0f;          // baked-px -> layout-px (line = GLYPH_H @ scale 1)
    float ttfAscentPx_ = 0.0f;      // baked px
    float ttfLineHeightPx_ = static_cast<float>(GLYPH_H);
    GlyphInfo ttfGlyphs_[TTF_COUNT];
};

} // namespace UI
} // namespace Phyxel
