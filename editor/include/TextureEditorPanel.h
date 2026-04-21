#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace Phyxel {
namespace Vulkan { class VulkanDevice; }
namespace Core { class AtlasManager; class MaterialRegistry; }
}

namespace Phyxel::Editor {

/// ImGui pixel art texture editor for 18x18 voxel face textures.
/// Allows painting, color picking, and live hot-reload to the atlas.
class TextureEditorPanel {
public:
    TextureEditorPanel() = default;

    void setVulkanDevice(Vulkan::VulkanDevice* dev) { vulkanDevice_ = dev; }

    /// Main render method — call from Application::renderImGui()
    void render(bool* open = nullptr);

private:
    // --- Sub-renderers ---
    void renderMaterialSelector();
    void renderFaceSelector();
    void renderCanvas();
    void renderToolbar();
    void renderColorPicker();
    void renderPreview();

    // --- Actions ---
    void loadCurrentTexture();
    void applyToAtlas();
    void saveSourcePNG();
    void floodFill(int x, int y, uint32_t targetColor, uint32_t fillColor);

    // --- Helpers ---
    uint32_t getPixel(int x, int y) const;
    void setPixel(int x, int y, uint32_t color);
    uint32_t colorFromFloat(const float col[4]) const;

    // --- State ---
    Vulkan::VulkanDevice* vulkanDevice_ = nullptr;

    // Current selection
    int selectedMaterialID_ = -1;
    int selectedFaceID_ = 0;          // 0=side_n, ..., 5=bottom
    std::string selectedMaterialName_;

    // Canvas: 18x18 RGBA pixels
    static constexpr int TEX_SIZE = 18;
    std::array<uint8_t, TEX_SIZE * TEX_SIZE * 4> pixels_{};
    bool hasUnsavedChanges_ = false;

    // Drawing state
    float currentColor_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int canvasZoom_ = 16;  // pixels per cell
    bool isDrawing_ = false;
    int lastDrawX_ = -1, lastDrawY_ = -1;

    enum class Tool { Pencil, Eraser, Fill, Eyedropper };
    Tool currentTool_ = Tool::Pencil;

    // Face names for display
    static constexpr const char* FACE_NAMES[6] = {
        "Side N (+Z)", "Side S (-Z)", "Side E (+X)", "Side W (-X)", "Top (+Y)", "Bottom (-Y)"
    };
};

} // namespace Phyxel::Editor
