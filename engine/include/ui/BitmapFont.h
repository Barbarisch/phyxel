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

    /// Generate the font atlas bitmap and upload it to the UIRenderer.
    bool initialize(UIRenderer* renderer);

    /// Measure text width in pixels at the given scale.
    float measureText(const std::string& text, float scale = 1.0f) const;

    /// Get line height in pixels at the given scale.
    float lineHeight(float scale = 1.0f) const { return GLYPH_H * scale; }

    /// Draw a string. Call between UIRenderer::beginFrame() and endFrame().
    void drawText(UIRenderer* renderer, const std::string& text,
                  glm::vec2 pos, glm::vec4 color, float scale = 1.0f) const;

private:
    /// Get UV rect for a character.
    void getGlyphUV(char c, glm::vec2& uvMin, glm::vec2& uvMax) const;

    bool initialized_ = false;
};

} // namespace UI
} // namespace Phyxel
