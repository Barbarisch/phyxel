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
/// Works with MaterialRegistry for material→texture slot mapping and
/// VulkanDevice for GPU texture upload + SSBO updates.
///
/// NOTE (texture-array migration, feature/texture-array-pbr): the GPU texture is now a
/// sampler2DArray — one layer per texture, indexed directly by the per-face textureIndex.
/// `pixels` is stored layer-major (layer = textureIndex), each layer TEXTURE_SIZE² RGBA.
/// The old 2D packed-atlas + per-tile UV-bounds math is gone; uvBounds is kept only to
/// carry textureCount/fallbackIndex to the fragment shader via the existing SSBO.
class AtlasManager {
public:
    static constexpr int TEXTURE_SIZE = 512;     // Pixels per texture side (per array layer)
    static constexpr int PADDING = 1;            // Legacy (unused by array path)
    static constexpr int CELL_SIZE = TEXTURE_SIZE + 2 * PADDING; // 66 (legacy)
    static constexpr int TEXTURES_PER_ROW = 6;   // Legacy (unused by array path)
    static constexpr int MAX_ATLAS_SIZE = 2048;  // Legacy (unused by array path)

    struct AtlasInfo {
        int atlasWidth = 0;                // = TEXTURE_SIZE (per-layer dimensions)
        int atlasHeight = 0;               // = TEXTURE_SIZE
        int textureCount = 0;
        int layerCount = 0;                // texture array layer count (== textureCount)
        std::vector<uint8_t> pixels;       // RGBA pixel data, LAYER-MAJOR (layer = textureIndex)
        std::vector<glm::vec4> uvBounds;   // Per-layer full-tile bounds (0,0,1,1); SSBO metadata only
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

    /// Number of texture array layers in the last build.
    int getLayerCount() const { return atlasInfo_.layerCount; }

    /// Hot-reload: rebuild atlas from source PNGs, upload to Vulkan, update SSBO.
    /// Call this after editing textures or adding materials.
    /// Returns true on success.
    bool hotReload(Vulkan::VulkanDevice* device);

    /// Per-material hot-reload: re-read source PNGs for a single material and
    /// upload only those atlas slots to the GPU.  Other materials are untouched.
    /// Returns true on success; false if material not found or GPU upload fails.
    bool reloadMaterial(const std::string& materialName, Vulkan::VulkanDevice* device);

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

    /// BC7-encode the layer-major RGBA in atlasInfo_.pixels into bc7Data_ with a full,
    /// CPU-generated mip chain (level-major / layer-minor layout). Multithreaded across
    /// layers. Returns false if there is no pixel data. Result feeds uploadTextureArrayBC7.
    bool encodeBC7();

    // Disk cache for the (slow) BC7 encode, keyed by a hash of the source textures so it is
    // only re-encoded when textures change. computeSourceHash() hashes source file metadata
    // + materials.json + format version; load/write (de)serialize bc7Data_/offsets/mips.
    static constexpr uint32_t BC7_CACHE_VERSION = 1;
    uint64_t computeSourceHash() const;
    bool loadBC7Cache(uint64_t hash);
    void writeBC7Cache(uint64_t hash) const;

    std::string sourceDirectory_ = "resources/textures/source";
    AtlasInfo atlasInfo_;

    // BC7-compressed mip chain (set by encodeBC7()). Layout: for each mip level (level 0
    // first), all layers contiguous, each layer = ceil(dim/4)^2 * 16 bytes.
    std::vector<uint8_t> bc7Data_;
    std::vector<size_t>  bc7LevelOffsets_;  // byte offset of each mip level within bc7Data_
    int                  bc7MipLevels_ = 0;
};

} // namespace Core
} // namespace Phyxel
