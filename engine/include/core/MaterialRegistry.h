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
 * @brief Complete material definition: identity + physics + textures
 */
struct MaterialDef {
    std::string name;
    std::string description;
    std::string category;       // "material", "system" (system = editor-only like hover/placeholder)
    bool emissive = false;

    MaterialPhysics physics;
    MaterialTextures textures;

    // Computed at load time by MaterialRegistry
    int materialID = -1;                    // Sequential ID (0-based, stable for atlas ordering)
    std::array<uint16_t, 6> atlasIndices;   // Per-face atlas texture indices

    bool hasPhysics() const { return category == "material"; }
};

/**
 * @brief Unified data-driven material registry
 *
 * Single source of truth for all material data: physics properties, texture
 * associations, and atlas indices. Loaded from resources/materials.json.
 * Replaces the hardcoded TextureConstants namespace and MaterialManager.
 *
 * Materials are ordered alphabetically with two exceptions:
 * - placeholder is always ID 0
 * - grassdirt is always ID 1
 * This matches the existing atlas layout for backward compatibility.
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

    /// Get total number of texture slots in the atlas
    int getTextureCount() const { return getMaterialCount() * 6; }

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

    uint16_t placeholderIndex_ = 0;     // Atlas index for placeholder fallback
    int defaultMaterialID_ = -1;        // Material ID for "Default"
    int hoverMaterialID_ = -1;          // Material ID for "hover"
    int grassdirtMaterialID_ = -1;      // Material ID for "grassdirt"

    std::vector<ChangeCallback> changeCallbacks_;
};

} // namespace Core
} // namespace Phyxel
