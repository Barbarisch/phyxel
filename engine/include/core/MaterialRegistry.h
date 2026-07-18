#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <functional>

namespace Phyxel {
namespace Core {

/**
 * @brief Per-face texture file paths for a material
 */
struct MaterialTextures {
    // Face order: side_n, side_s, side_e, side_w, top, bottom
    std::array<std::string, 6> faceFiles;

    const std::string& sideN()  const { return faceFiles[0]; }
    const std::string& sideS()  const { return faceFiles[1]; }
    const std::string& sideE()  const { return faceFiles[2]; }
    const std::string& sideW()  const { return faceFiles[3]; }
    const std::string& top()    const { return faceFiles[4]; }
    const std::string& bottom() const { return faceFiles[5]; }
};

/**
 * @brief Physics properties for a material (mirrors Physics::MaterialProperties)
 */
struct MaterialPhysics {
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.1f;
    float angularDamping = 0.1f;
    float breakForceMultiplier = 1.0f;
    float bondStrength = 0.5f;
    float angularVelocityScale = 1.0f;
    glm::vec3 colorTint = glm::vec3(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
};

/**
 * @brief Destruction / break response for a material (see docs/DestructionSystemV2.md §5.A).
 *
 * Drives DamageSystem::responseFor. When a material has no "break" block in
 * materials.json, hasProfile stays false and DamageSystem uses its bondStrength-
 * derived fallback (toughness = bondStrength*120, s1=2.5, s2=6.0, absorption=0.6).
 * The defaults below MATCH that fallback so an unset profile is a no-op.
 */
struct MaterialBreak {
    bool  hasProfile = false;   ///< true only if a "break" block was present in JSON
    float toughness  = 0.0f;    ///< energy needed to break one voxel
    float brittleS1  = 2.5f;    ///< overkill ratio >= s1 → shatter to subcubes (1/3)
    float brittleS2  = 6.0f;    ///< overkill ratio >= s2 → shatter to microcubes (1/9)
    float absorption = 0.6f;    ///< shielding: energy lost per solid voxel in the way
};

/**
 * @brief Complete material definition: identity + physics + textures
 */
struct MaterialDef {
    std::string name;
    std::string description;
    std::string category;       // "material", "system" (system = editor-only like hover/placeholder)
    bool emissive = false;
    float alpha = 1.0f;     ///< 1.0 = fully opaque, <1.0 = transparent (OIT)
    bool isMirror = false;  ///< True = reflective surface (mirror pass)
    /// Procedural tiling variation: hash-rotate each tile in the fragment shader to
    /// break the per-cube grid repeat (docs/VoxelOrientation.md, Phase A). Static cube
    /// path only; sets InstanceData.reserved bit 15. Use for NON-directional natural
    /// surfaces (stone/dirt/sand/grass); leave off for directional ones (planks/bricks).
    bool varied = false;
    /// Billboarded foliage: the chunk mesher skips this material's solid subcube faces and the
    /// FoliageRenderPipeline draws cutout leaf cards instead (leaf materials). See the foliage plan.
    bool billboarded = false;

    /// Masked emission (docs/MaskedEmissiveSpec.md): the surface is lit NORMALLY, and the bright
    /// pixels of its albedo (above emissiveThreshold luminance) additionally EMIT light — e.g. an
    /// "enchanted log": normal bark + glowing cracks. Distinct from `emissive` (whole-face self-lit).
    /// emissiveStrength > 0 enables it; the glow colour is the albedo's own bright pixels, and the
    /// block-light it casts uses physics.colorTint. 0 = ordinary material (no behaviour change).
    float emissiveStrength = 0.0f;
    float emissiveThreshold = 0.55f;   ///< albedo luminance above which a pixel glows

    MaterialPhysics physics;
    MaterialBreak    breakProfile;   ///< destruction response (optional "break" block)
    MaterialTextures textures;

    // Target texture resolution (per layer). 512 = standard terrain/material class,
    // 1024 = hi-res class (objects/building detail). Drives the mixed-res two-array split.
    int resolution = 512;

    // Computed at load time by MaterialRegistry
    int materialID = -1;                    // Sequential ID (0-based, stable for atlas ordering)
    std::array<uint16_t, 6> atlasIndices;   // Per-face atlas texture indices (see encoding below)

    bool hasPhysics() const { return category == "material"; }
    int resClass() const { return resolution >= 1024 ? 1 : 0; }  // 0 = 512, 1 = 1024
};

/**
 * @brief Unified data-driven material registry
 *
 * Single source of truth for all material data: physics properties, texture
 * associations, and atlas indices. Loaded from resources/materials.json.
 * Replaces the hardcoded TextureConstants namespace and MaterialManager.
 *
 * Materials are ordered as defined in materials.json.
 * The first material (ID 0) is the fallback/error indicator (Default).
 * "Grass" material is tracked via grassdirtMaterialID_ for terrain face-splitting.
 */
class MaterialRegistry {
public:
    static constexpr uint16_t INVALID_TEXTURE_INDEX = 0xFFFF;
    static constexpr uint16_t MAX_TEXTURE_INDEX = 0xFFFE;
    static constexpr int MAX_MATERIALS = 256;

    MaterialRegistry() = default;

    /// Get the singleton instance
    static MaterialRegistry& instance();

    /// Load materials from JSON file. Returns true on success.
    bool loadFromJson(const std::string& path);

    /// Save current materials back to JSON file.
    bool saveToJson(const std::string& path) const;

    /// Get material ID by name. Returns -1 if not found, or fallbackID if useFallback is true.
    int getMaterialID(const std::string& name) const;

    /// Get texture atlas index for a material + face. Returns placeholder on failure.
    uint16_t getTextureIndex(const std::string& materialName, int faceID) const;

    /// Get texture atlas index by material ID + face. Fastest path.
    uint16_t getTextureIndex(int materialID, int faceID) const;

    /// Get full material definition by name. Returns nullptr if not found.
    const MaterialDef* getMaterial(const std::string& name) const;

    /// Get full material definition by ID. Returns nullptr if out of range.
    const MaterialDef* getMaterial(int materialID) const;

    /// Get the placeholder fallback texture index (placeholder bottom face)
    uint16_t getPlaceholderIndex() const { return placeholderIndex_; }

    /// Get the fallback material ID (Default material)
    int getDefaultMaterialID() const { return defaultMaterialID_; }

    /// Get total number of registered materials
    int getMaterialCount() const { return static_cast<int>(materials_.size()); }

    /// Get total number of texture slots across both resolution classes.
    int getTextureCount() const { return getMaterialCount() * 6; }

    /// Texture-index encoding for the mixed-resolution two-array split:
    ///   bit 15      = resolution class (0 = 512px array, 1 = 1024px array)
    ///   bits 0..14  = layer index within that class's texture array
    ///   0xFFFF      = invalid sentinel (renders the placeholder)
    static constexpr uint16_t RES_CLASS_BIT = 0x8000;
    static constexpr uint16_t LAYER_MASK    = 0x7FFF;

    /// Number of texture-array LAYERS in a resolution class (0 = 512, 1 = 1024).
    int getTextureCount(int resClass) const {
        return resClass == 1 ? textureCount1024_ : textureCount512_;
    }

    /// Get all material names (in registration order)
    std::vector<std::string> getAllMaterialNames() const;

    /// Get all material definitions (in registration order)
    const std::vector<MaterialDef>& getAllMaterials() const { return materials_; }

    /// Check if a material exists
    bool hasMaterial(const std::string& name) const;

    /// Add a new material at runtime. Returns assigned material ID, or -1 on failure.
    int addMaterial(const MaterialDef& def);

    /// Remove a material by name. Returns true if removed.
    bool removeMaterial(const std::string& name);

    /// Register a callback for when materials change (add/remove/reload)
    using ChangeCallback = std::function<void()>;
    void onMaterialsChanged(ChangeCallback cb) { changeCallbacks_.push_back(std::move(cb)); }

    /// Get physics properties for a material by name. Returns default physics on failure.
    const MaterialPhysics& getPhysics(const std::string& name) const;

    // ---- Convenience accessors matching old TextureConstants API ----

    /// Get hover texture index for a face (editor highlight)
    uint16_t getHoverTextureIndex(int faceID) const;

    /// Get grassdirt texture index for a face
    uint16_t getGrassdirtTextureIndex(int faceID) const;

private:
    void assignAtlasIndices();
    void rebuildLookupCache();
    void notifyChanged();

    std::vector<MaterialDef> materials_;                          // Ordered by material ID
    std::unordered_map<std::string, int> nameToID_;              // Name → material ID

    // Fast lookup cache: [materialID][faceID] → atlas index
    // Flat array for cache-friendly access on the hot path
    uint16_t faceIndexCache_[MAX_MATERIALS][6] = {};

    int textureCount512_ = 0;           // layers in the 512px class
    int textureCount1024_ = 0;          // layers in the 1024px class

    uint16_t placeholderIndex_ = 0;     // Atlas index for placeholder fallback
    int defaultMaterialID_ = -1;        // Material ID for "Default"
    int hoverMaterialID_ = -1;          // Material ID for "hover"
    int grassdirtMaterialID_ = -1;      // Material ID for "grassdirt"

    std::vector<ChangeCallback> changeCallbacks_;
};

} // namespace Core
} // namespace Phyxel
