#pragma once

#include "Light.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace Phyxel {
namespace Graphics {

/// Manages point lights and spot lights. Provides GPU-ready packed buffer for SSBO upload.
class LightManager {
public:
    LightManager() = default;

    // --- Point Lights ---
    /// Add a point light. Returns a unique light ID, or -1 if at capacity.
    int addPointLight(const PointLight& light);
    /// Add a point light with individual parameters.
    int addPointLight(const glm::vec3& position, const glm::vec3& color = glm::vec3(1.0f),
                      float intensity = 1.0f, float radius = 10.0f);
    
    // --- Spot Lights ---
    /// Add a spot light. Returns a unique light ID, or -1 if at capacity.
    int addSpotLight(const SpotLight& light);
    /// Add a spot light with individual parameters.
    int addSpotLight(const glm::vec3& position, const glm::vec3& direction,
                     const glm::vec3& color = glm::vec3(1.0f),
                     float intensity = 1.0f, float radius = 20.0f,
                     float innerCone = 0.9f, float outerCone = 0.8f);

    // --- Common ---
    /// Remove a light (point or spot) by its ID.
    bool removeLight(int lightId);
    /// Update a point light's properties. Returns false if ID not found or not a point light.
    bool updatePointLight(int lightId, const PointLight& light);
    /// Update a spot light's properties. Returns false if ID not found or not a spot light.
    bool updateSpotLight(int lightId, const SpotLight& light);

    /// Enable or disable a light by ID.
    bool setLightEnabled(int lightId, bool enabled);

    /// Get a point light by ID (returns nullptr if not found).
    const PointLight* getPointLight(int lightId) const;
    /// Get a spot light by ID (returns nullptr if not found).
    const SpotLight* getSpotLight(int lightId) const;

    /// Remove all lights.
    void clear();

    // --- Queries ---
    uint32_t getPointLightCount() const { return static_cast<uint32_t>(pointLights_.size()); }
    uint32_t getSpotLightCount() const { return static_cast<uint32_t>(spotLights_.size()); }
    uint32_t getTotalLightCount() const { return getPointLightCount() + getSpotLightCount(); }

    /// Get all point light IDs.
    std::vector<int> getPointLightIds() const;
    /// Get all spot light IDs.
    std::vector<int> getSpotLightIds() const;

    /// Get copies of all point lights (with id fields populated).
    std::vector<PointLight> getPointLights() const;
    /// Get copies of all spot lights (with id fields populated).
    std::vector<SpotLight> getSpotLights() const;

    /// Update just the position of a point light by ID.
    bool updatePointLightPosition(int lightId, const glm::vec3& position);

    /// Remove a point light by its ID.
    bool removePointLight(int lightId) { return removeLight(lightId); }
    /// Remove a spot light by its ID.
    bool removeSpotLight(int lightId) { return removeLight(lightId); }

    // --- GPU Upload ---
    /// Pack all enabled lights into the GPU buffer struct. Call once per frame before upload.
    /// Origin that uploaded light positions are expressed RELATIVE TO.
    ///
    /// ⚠️ This exists because the renderer is camera-relative (docs/CameraRelativeRendering.md):
    /// every position reaching the GPU is (world - camera), and `ubo.cameraPosition` is deliberately
    /// zero. Light positions were being uploaded in ABSOLUTE world space and then subtracted from
    /// camera-relative fragment positions, so every point and spot light was displaced by the
    /// camera's own world position -- correct only near the origin, and increasingly wrong the
    /// further a world extends. Both consumers (voxel.frag and character.frag) work in the relative
    /// space, so relativizing here fixes them together.
    ///
    /// Subtraction happens on the CPU rather than in the shader on purpose: here both operands are
    /// full-range and the result is small, whereas reconstructing an absolute fragment position on
    /// the GPU would do the arithmetic at the worst possible magnitude.
    void setViewerWorld(const glm::vec3& viewerWorld);

    const LightBufferGPU& getGPUData();

    /// Returns true if any lights have changed since the last getGPUData() call.
    bool isDirty() const { return dirty_; }

    /// How many ENABLED point lights exist beyond the upload budget this frame. 0 means every light
    /// in the world is being uploaded. Non-zero is not an error — it is the budget doing its job —
    /// but it is the number to watch when a scene looks under-lit.
    size_t droppedPointLights() const;
    size_t storedPointLights() const { return pointLights_.size(); }
    size_t storedSpotLights() const { return spotLights_.size(); }

private:
    /// Purely a leak tripwire. Storage is unbounded by design (U3.1); this only warns once if the
    /// count reaches a level that suggests something is registering lights and never removing them.
    static constexpr size_t kStorageWarnThreshold = 4096;
    struct PointLightEntry {
        int id;
        PointLight light;
    };
    struct SpotLightEntry {
        int id;
        SpotLight light;
    };

    std::vector<PointLightEntry> pointLights_;
    std::vector<SpotLightEntry> spotLights_;
    int nextId_ = 1;
    glm::vec3 viewerWorld_{0.0f};   ///< see setViewerWorld
    bool dirty_ = true;
    bool warnedPointStorage_ = false;
    bool warnedSpotStorage_ = false;
    LightBufferGPU gpuBuffer_;

    /// Distance from the viewer to a light's sphere of influence (negative = viewer inside it).
    /// Lower is more relevant. See getGPUData for why radius is subtracted.
    float relevance(const glm::vec3& position, float radius) const;

    // Helper to find entries by ID
    PointLightEntry* findPointLight(int id);
    const PointLightEntry* findPointLight(int id) const;
    SpotLightEntry* findSpotLight(int id);
    const SpotLightEntry* findSpotLight(int id) const;
};

} // namespace Graphics
} // namespace Phyxel
