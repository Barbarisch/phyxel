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

/// Manages the voxel texture set: builds from source PNGs, hot-reloads at runtime.
/// Works with MaterialRegistry for material→texture slot mapping and VulkanDevice for GPU
/// texture upload + SSBO updates.
///
/// Texture container = two sampler2DArrays (mixed-resolution split):
///   class 0 = 512px  (terrain / standard materials)
///   class 1 = 1024px (objects / building detail; materials with "resolution":1024)
/// The per-face textureIndex (u16) encodes the class in bit 15 (RES_CLASS_BIT) and the
/// within-class array layer in bits 0..14 (see MaterialRegistry). Each class is BC7-encoded
/// and cached independently. `pixels` is stored layer-major (layer = within-class index).
class AtlasManager {
public:
    static constexpr int TEXTURE_SIZE = 512;       // class 0 layer size
    static constexpr int TEXTURE_SIZE_HI = 1024;   // class 1 layer size
    static constexpr int NUM_CLASSES = 2;
    static constexpr int PADDING = 1;            // Legacy (unused by array path)
    static constexpr int CELL_SIZE = TEXTURE_SIZE + 2 * PADDING; // legacy
    static constexpr int TEXTURES_PER_ROW = 6;   // Legacy (unused by array path)
    static constexpr int MAX_ATLAS_SIZE = 2048;  // Legacy (unused by array path)

    /// One resolution class's CPU-side build (RGBA layers + BC7-compressed mip chain).
    struct AtlasInfo {
        int atlasWidth = 0;                // = baseSize (per-layer dimensions)
        int atlasHeight = 0;               // = baseSize
        int baseSize = TEXTURE_SIZE;       // layer size for this class (512 or 1024)
        int textureCount = 0;
        int layerCount = 0;                // texture array layer count for this class
        std::vector<uint8_t> pixels;       // RGBA, LAYER-MAJOR (layer = within-class index)
        std::vector<glm::vec4> uvBounds;   // Per-layer full-tile bounds (0,0,1,1); SSBO metadata only
        // BC7-compressed mip chain (set by encodeBC7). Layout: for each mip level (level 0
        // first), all layers contiguous, each layer = ceil(dim/4)^2 * 16 bytes.
        std::vector<uint8_t> bc7Data;
        std::vector<size_t>  bc7LevelOffsets;
        int                  bc7MipLevels = 0;
    };

    AtlasManager();
    ~AtlasManager();

    static AtlasManager& instance() {
        static AtlasManager s;
        return s;
    }

    /// Set the directory containing source PNGs (e.g. "resources/textures/source")
    void setSourceDirectory(const std::string& dir) { sourceDirectory_ = dir; }

    /// Build both resolution-class arrays from MaterialRegistry + source PNGs.
    bool buildAtlas();

    /// Get a resolution class's build info (0 = 512, 1 = 1024). Default = class 0.
    const AtlasInfo& getAtlasInfo(int resClass = 0) const { return atlas_[resClass & 1]; }

    /// Number of texture array layers in class 0 (512).
    int getLayerCount() const { return atlas_[0].layerCount; }

    bool hotReload(Vulkan::VulkanDevice* device);
    bool reloadMaterial(const std::string& materialName, Vulkan::VulkanDevice* device);

    /// Write a single texture slot's pixel data (encoded textureIndex → class + layer).
    /// pixels must be (class baseSize)² * 4 bytes RGBA.
    bool updateTextureSlot(int slotIndex, const uint8_t* pixels);

    /// Upload both class arrays to the Vulkan texture images (binding 1 = 512, binding 5 = 1024).
    bool uploadToGPU(Vulkan::VulkanDevice* device);

    /// Update the atlas-metadata SSBO (per-class layer counts + fallback index).
    void updateUVSSBO(Vulkan::VulkanDevice* device);

    bool saveAtlasPNG(const std::string& path) const;

    /// Get pixel data for a specific encoded texture slot. Size = (class baseSize)² * 4.
    std::vector<uint8_t> getTextureSlotPixels(int slotIndex) const;

    static void calcAtlasDimensions(int textureCount, int& outWidth, int& outHeight);

private:
    /// Load a PNG, resampling to `size` x `size` RGBA. Empty vector on failure.
    std::vector<uint8_t> loadPNG(const std::string& path, int size) const;

    /// Generate a size x size magenta/black fallback texture.
    std::vector<uint8_t> generateFallbackTexture(int size) const;

    /// Build one resolution class's RGBA layer array from its materials' source PNGs.
    bool buildClass(int resClass);

    /// Blit one layer's RGBA into a class array at the given within-class layer index.
    void blitToLayer(int resClass, int layer, const uint8_t* texPixels);

    /// BC7-encode a class's RGBA into atlas_[c].bc7Data with a CPU-generated mip chain
    /// (level-major / layer-minor). Multithreaded across layers.
    bool encodeBC7(int resClass);

    // Per-class BC7 disk cache, keyed by a hash of the source textures + materials.json +
    // format version + class base size, so each class is re-encoded only when it changes.
    static constexpr uint32_t BC7_CACHE_VERSION = 2;  // bumped for mixed-res split
    uint64_t computeSourceHash(int resClass) const;
    bool loadBC7Cache(int resClass, uint64_t hash);
    void writeBC7Cache(int resClass, uint64_t hash) const;

    std::string sourceDirectory_ = "resources/textures/source";
    AtlasInfo atlas_[NUM_CLASSES];   // [0] = 512, [1] = 1024
};

} // namespace Core
} // namespace Phyxel
