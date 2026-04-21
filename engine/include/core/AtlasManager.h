#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Vulkan { class VulkanDevice; }

namespace Core {

class MaterialRegistry;

/// Manages the texture atlas: builds from source PNGs, hot-reloads at runtime.
/// Works with MaterialRegistry for material→texture slot mapping and
/// VulkanDevice for GPU texture upload + SSBO updates.
class AtlasManager {
public:
    static constexpr int TEXTURE_SIZE = 18;      // Pixels per texture side
    static constexpr int PADDING = 1;            // Pixels between textures
    static constexpr int CELL_SIZE = TEXTURE_SIZE + 2 * PADDING; // 20
    static constexpr int TEXTURES_PER_ROW = 6;
    static constexpr int MAX_ATLAS_SIZE = 2048;  // Max atlas dimension

    struct AtlasInfo {
        int atlasWidth = 0;
        int atlasHeight = 0;
        int textureCount = 0;
        std::vector<uint8_t> pixels;       // RGBA pixel data
        std::vector<glm::vec4> uvBounds;   // Per-slot UV bounds (u_min, v_min, u_max, v_max)
    };

    AtlasManager();
    ~AtlasManager();

    static AtlasManager& instance() {
        static AtlasManager s;
        return s;
    }

    /// Set the directory containing source PNGs (e.g. "resources/textures/source")
    void setSourceDirectory(const std::string& dir) { sourceDirectory_ = dir; }

    /// Build the atlas from MaterialRegistry + source PNGs.
    /// Returns true if successful. The result is stored internally.
    bool buildAtlas();

    /// Get the last built atlas info (pixels + UVs).
    const AtlasInfo& getAtlasInfo() const { return atlasInfo_; }

    /// Hot-reload: rebuild atlas from source PNGs, upload to Vulkan, update SSBO.
    /// Call this after editing textures or adding materials.
    /// Returns true on success.
    bool hotReload(Vulkan::VulkanDevice* device);

    /// Write a single texture slot's pixel data into the atlas.
    /// Used by the pixel editor to update a texture in-place without full rebuild.
    /// slotIndex is the atlas texture index (materialID * 6 + faceID style).
    /// pixels must be TEXTURE_SIZE * TEXTURE_SIZE * 4 bytes (RGBA).
    bool updateTextureSlot(int slotIndex, const uint8_t* pixels);

    /// Upload the current atlas pixels to the Vulkan texture image.
    bool uploadToGPU(Vulkan::VulkanDevice* device);

    /// Update the atlas UV SSBO on the GPU from current UV data.
    void updateUVSSBO(Vulkan::VulkanDevice* device);

    /// Save the current atlas to a PNG file.
    bool saveAtlasPNG(const std::string& path) const;

    /// Get pixel data for a specific texture slot (TEXTURE_SIZE x TEXTURE_SIZE, RGBA).
    /// Returns empty vector if slot is out of range.
    std::vector<uint8_t> getTextureSlotPixels(int slotIndex) const;

    /// Calculate required atlas dimensions for a given texture count.
    static void calcAtlasDimensions(int textureCount, int& outWidth, int& outHeight);

private:
    /// Load a single PNG file into RGBA pixels. Returns empty vector on failure.
    std::vector<uint8_t> loadPNG(const std::string& path) const;

    /// Generate a colored fallback texture for a slot.
    std::vector<uint8_t> generateFallbackTexture(int slotIndex) const;

    /// Blit a TEXTURE_SIZE x TEXTURE_SIZE RGBA image into the atlas at the given slot.
    void blitToAtlas(int slotIndex, const uint8_t* texPixels);

    std::string sourceDirectory_ = "resources/textures/source";
    AtlasInfo atlasInfo_;
};

} // namespace Core
} // namespace Phyxel
