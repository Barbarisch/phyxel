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
    void renderPackCompare();   ///< Side-by-side pack comparison section

    // --- Actions ---
    void loadCurrentTexture();
    void applyToAtlas();
    void saveSourcePNG();
    void reloadMaterialFromDisk();   ///< Re-reads all 6 source PNGs for the selected material
    void hotReloadAll();             ///< Full atlas rebuild + GPU upload
    void floodFill(int x, int y, uint32_t targetColor, uint32_t fillColor);
    void loadPackPreview(const std::string& packName);  ///< Extract + load one pack's texture into previewPixels_
    void adoptPreview();            ///< Copy previewPixels_ → pixels_ and mark modified

    // --- Helpers ---
    uint32_t getPixel(int x, int y) const;
    void setPixel(int x, int y, uint32_t color);
    uint32_t colorFromFloat(const float col[4]) const;
    void loadPNG(const std::string& path, std::array<uint8_t, 64*64*4>& buf, bool& ok);

    // --- State ---
    Vulkan::VulkanDevice* vulkanDevice_ = nullptr;

    // Current selection
    int selectedMaterialID_ = -1;
    int selectedFaceID_ = 0;          // 0=side_n, ..., 5=bottom
    std::string selectedMaterialName_;

    // Canvas: 64x64 RGBA pixels (matches atlas TEXTURE_SIZE)
    static constexpr int TEX_SIZE = 64;
    std::array<uint8_t, TEX_SIZE * TEX_SIZE * 4> pixels_{};
    bool hasUnsavedChanges_ = false;

    // Drawing state
    float currentColor_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int canvasZoom_ = 8;  // pixels per cell
    bool isDrawing_ = false;
    int lastDrawX_ = -1, lastDrawY_ = -1;

    enum class Tool { Pencil, Eraser, Fill, Eyedropper };
    Tool currentTool_ = Tool::Pencil;

    // Pack compare state
    std::vector<std::string> knownPacks_;          ///< Pack names from mc_texture_map.json
    std::array<uint8_t, TEX_SIZE * TEX_SIZE * 4> previewPixels_{};
    bool hasPreview_ = false;
    std::string previewPackName_;
    int previewZoom_ = 4;   ///< Zoom for the preview thumbnail

    // Face names for display
    static constexpr const char* FACE_NAMES[6] = {
        "Side N (+Z)", "Side S (-Z)", "Side E (+X)", "Side W (-X)", "Top (+Y)", "Bottom (-Y)"
    };
};

} // namespace Phyxel::Editor
