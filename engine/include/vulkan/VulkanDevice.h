#pragma once

#include "core/Types.h"
#include "graphics/Light.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstdlib>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Vulkan {

// Vertex structure for cube rendering
struct Vertex {
    uint32_t vertexID;
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }
    
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(1);
        
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32_UINT;
        attributeDescriptions[0].offset = offsetof(Vertex, vertexID);
        
        return attributeDescriptions;
    }
};

// Instance data structure - compressed format with texture support.
// MUST stay layout-identical to Phyxel::InstanceData (core/Types.h): the CPU writes that
// struct into the instance buffer and this defines how the pipeline reads it.
struct InstanceData {
    uint32_t packedData;      // 15 bits position (5+5+5), 6 bits face mask, 11 bits available for future features
    uint16_t textureIndex;    // Texture atlas index (0-65535)
    uint16_t reserved;        // Flags: bit0 emissive, bit1 transparent, bits2-9 alpha, bit10 mirror, bits11-14 damage
    // U7: bits0-15 used to be per-corner skylight nibbles; sky is traced per fragment now, so they
    // are free. Bits 16-31 are the fine-face-merge extents and are LIVE (shadow.vert reads them).
    uint32_t light;
    uint32_t tint;            // Per-voxel 0xRRGGBB tint multiplier (0xFFFFFF = none). MUST match Phyxel::InstanceData.

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 1;
        bindingDescription.stride = sizeof(InstanceData);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        return bindingDescription;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);  // U7: was 7

        attributeDescriptions[0].binding = 1;
        attributeDescriptions[0].location = 1;
        attributeDescriptions[0].format = VK_FORMAT_R32_UINT;  // uint32 packed data
        attributeDescriptions[0].offset = offsetof(InstanceData, packedData);

        attributeDescriptions[1].binding = 1;
        attributeDescriptions[1].location = 2;
        attributeDescriptions[1].format = VK_FORMAT_R16_UINT;  // uint16 texture index
        attributeDescriptions[1].offset = offsetof(InstanceData, textureIndex);

        attributeDescriptions[2].binding = 1;
        attributeDescriptions[2].location = 3;
        attributeDescriptions[2].format = VK_FORMAT_R16_UINT;  // uint16 reserved (flags)
        attributeDescriptions[2].offset = offsetof(InstanceData, reserved);

        attributeDescriptions[3].binding = 1;
        attributeDescriptions[3].location = 4;
        attributeDescriptions[3].format = VK_FORMAT_R32_UINT;  // uint32 baked light (corner skies)
        attributeDescriptions[3].offset = offsetof(InstanceData, light);

        // U7: the two block-light words are gone, so tint moves from location 7 to 5.
        attributeDescriptions[4].binding = 1;
        attributeDescriptions[4].location = 5;
        attributeDescriptions[4].format = VK_FORMAT_R32_UINT;  // uint32 per-voxel tint (0xRRGGBB)
        attributeDescriptions[4].offset = offsetof(InstanceData, tint);

        return attributeDescriptions;
    }
};

// Uniform buffer object
struct UniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix; // For shadow mapping
    alignas(16) glm::vec3 sunDirection; // Direction of the sun
    alignas(16) glm::vec3 sunColor;     // Color of the sun
    alignas(4) uint32_t numInstances;
    alignas(4) float ambientLight; // Ambient light strength
    alignas(4) float emissiveMultiplier; // Brightness of emissive objects
    alignas(16) glm::vec3 cameraPosition; // World-space camera position for specular
    alignas(16) glm::mat4 reflectedViewProj; // Reflected camera VP matrix (identity if no mirrors)
    alignas(4) float elapsedTime; // Seconds since engine start — drives grass wind + growth. Trailing field: safe to append.
    // Precombined per-frame products so vertex shaders do one mat4*vec4 instead of a
    // per-vertex mat4*mat4 (docs/ShaderMathRedundancyPlan.md Increment 1). GLSL UBO
    // blocks must declare fields in this exact order for std140 offsets to match.
    alignas(16) glm::mat4 viewProj;         // proj * view
    alignas(16) glm::mat4 biasedLightSpace; // clip->UV bias * lightSpaceMatrix (shadow sampling)
    // Camera-relative rendering (docs/CameraRelativeRendering.md): with the eye-at-origin
    // view, cameraPosition above becomes (0,0,0) and every position reaching the GPU is
    // world - camera. cameraWorld carries the TRUE world-space camera position for shaders
    // that must reconstruct ABSOLUTE coordinates for stable hashing (varied tile rotation,
    // grass/foliage wind seeds): abs = rel + cameraWorld — the ~4 mm float error at 60 km is
    // harmless to floor()-to-voxel hashes. Appended LAST: GLSL blocks are std140 prefixes,
    // so existing truncated declarations stay valid.
    alignas(16) glm::vec3 cameraWorld;
    // SHADOW DEBUG VIEW (0 = off, 1 = shadow-only). Deliberately packed into cameraWorld's
    // std140 tail padding (vec3 = 16-byte slot, 12 bytes used), so it costs no space and
    // shifts NO following offsets — every existing truncated GLSL declaration stays valid.
    // Strips albedo/ambient so only the shadow term is visible: white = lit, black = shadowed.
    // Thin casters (grass blades) are invisible against textured ground; this is the standard
    // way engines make them readable (cf. Unreal's Lighting Only view mode).
    int debugShadowMode = 0;
    /// Depth span of the fitted light volume in WORLD units (2*radius + 2*casterBack). Shadow
    /// bias is authored in world units and divided by this, so a bias means the same physical
    /// distance at every shadow distance. Previously the bias was a raw normalized-depth
    /// constant, so it silently scaled with the volume: 0.26 u at a 40 u shadow distance but
    /// 0.85 u at 420 — larger than a grass blade is tall, which rejected every blade shadow.
    float shadowDepthRange = 1.0f;
    // Grass interaction displacers (docs/VegetationWindPlan.md Phase 4 v1) — characters near the
    // camera that bend grass aside. xyz = CAMERA-RELATIVE position (world - camera, matching every
    // other GPU position), w = push radius. Only grass.vert declares these (std140 prefix rule keeps
    // every other shader's truncated block valid). Patched AFTER updateUniformBuffer each frame via
    // setGrassDisplacers (updateUniformBuffer zero-fills them — count 0 = feature inert).
    alignas(16) glm::vec4 grassDisplacers[16];
    alignas(16) glm::vec4 grassDisplacersAux[16];  // x = strength envelope 0..1 (eased on CPU), yzw reserved
    alignas(16) glm::ivec4 grassDisplacerMeta;  // x = active count, yzw unused
    // ---- Near shadow cascade (docs/NearShadowCascade.md; 2026-08-05) ------------------------
    // A second, tight shadow map over the near field: one map fitted to 420 u gives a
    // 0.1125 u texel, and a grass blade's 0.080 u shadow proxy is 0.71 texel — sub-texel
    // casters rasterize as unstructured noise (measured: 40.1% of the view as blobs).
    // The near map fits ~40 u at 4096² = 0.0195 u/texel, where the same blade spans 4 texels.
    // Appended AFTER grassDisplacerMeta per the trailing-field rule: every existing truncated
    // GLSL declaration stays valid; only shaders that read these declare this far.
    alignas(16) glm::mat4 biasedLightSpaceNear;  // clip->UV bias * near-cascade light matrix
    // x = near cascade range end (world u; 0 = cascade OFF — shaders must fall back to the
    // mid map), y = near depthRange (world u, for world-unit bias), z = blend band half-width
    // (world u) across the split, w reserved.
    alignas(16) glm::vec4 shadowCascadeNear{0.0f, 1.0f, 6.0f, 0.0f};
    // RAW (unbiased) near-cascade light matrix — the near CASTER pass's grass shadow vert
    // projects with this (grass casts ONLY into the near map; its proxy is sub-texel in the
    // mid map and rasterizes as noise there).
    alignas(16) glm::mat4 lightSpaceMatrixNear;
    // ---- FAR shadow cascade (2026-08-06): shadows for the LOD band. The mid map ends at
    // 420 u; everything beyond (far terrain tiles, far-tree meshes, structure proxies)
    // rendered UNSHADOWED — distant forests looked flat-lit. The far map fits ~1600 u
    // (the tree-mesh band) at 4096² ≈ 0.9 u/texel: coarse, but a tree is small at 1 km.
    // Far receivers min-compose it exactly like near receivers do the near map.
    alignas(16) glm::mat4 biasedLightSpaceFar;
    // x = far cascade range end (0 = OFF), y = far depthRange, z/w reserved.
    alignas(16) glm::vec4 shadowCascadeFar{0.0f, 1.0f, 0.0f, 0.0f};
    alignas(16) glm::mat4 lightSpaceMatrixFar;   // raw matrix for the far CASTER passes
    // ---- Atmosphere-derived lighting + exposure (2026-08-10) --------------------------------
    // The sky is now a physical scattering model (graphics/Atmosphere.h, shaders/atmosphere.glsl)
    // and it is the SOURCE OF TRUTH for these: sunColor above, this ambient colour, and the haze
    // endpoints all come out of the same transmittance, so the sun you see and the light you get
    // cannot drift apart. They used to be three independently hand-tuned constant ramps.
    //
    // ⚠️ EXPOSURE IS NOT OPTIONAL POLISH. A physical model returns RADIANCE — a noon sky is ~0.02
    // and a lit diffuse surface ~0.1 — whereas the flat clear colour it replaced was a
    // display-referred 0.45-0.95. Rendering physical radiance straight to an 8-bit display gives a
    // nearly black frame. Exposure is the unit conversion that makes the model viewable at all.
    // Appended per the trailing-field rule: GLSL blocks are std140 PREFIXES, so every existing
    // truncated declaration stays valid and only shaders that read this far declare these.
    alignas(16) glm::vec3 ambientColor{1.0f};        // sky irradiance colour (hemispheric fill)
    alignas(16) glm::vec3 hazeHorizonColor{0.66f, 0.76f, 0.92f};  // aerial perspective, horizon end
    alignas(16) glm::vec3 hazeZenithColor{0.40f, 0.55f, 0.85f};   // ... and zenith end
    alignas(16) glm::vec3 moonDirection{0.0f, -1.0f, 0.0f};  // direction light TRAVELS (like sunDirection)
    alignas(16) glm::vec3 moonColor{0.0f};           // moonlight, already scaled by lunar phase
    alignas(4)  float exposure = 1.0f;               // linear scale applied before the tonemap
    alignas(4)  int   tonemapCurve = 1;              // 0 = none (raw linear), 1 = AgX
    // ---- Celestial bodies (graphics/CelestialBody.h; 2026-08-13) -----------------------------
    // The sky's suns and moons as DATA, so "two moons" is configuration rather than code. Capped at
    // kMaxSkyBodies: the arrays cost 16 bytes per vec4 per body and the shaders loop over them, so
    // this is deliberately a small number rather than unbounded. Raise it here and in
    // atmosphere.glsl's kMaxSkyBodies together.
    // Appended per the trailing-field rule: every existing truncated GLSL block stays valid.
    //   dirRadius : xyz = unit vector TOWARD the body, w = drawn angular radius (radians)
    //   disc      : rgb = disc colour x brightness,    w = 1 if REFLECTIVE (has a phase), else 0
    //   litDir    : xyz = unit vector toward whatever lights it, w = 1 if it owns the shadow cascades
    //   light     : rgb = light this body delivers to the world (0 if it contributes none)
    alignas(16) glm::vec4 skyBodyDirRadius[4]{};
    alignas(16) glm::vec4 skyBodyDisc[4]{};
    alignas(16) glm::vec4 skyBodyLitDir[4]{};
    alignas(16) glm::vec4 skyBodyLight[4]{};
    alignas(4)  int skyBodyCount = 0;
    // ---- Sub-voxel light occupancy (docs/UnifiedLightingPlan.md M1b/M2) ----------------------
    // Appended per the trailing-field rule: every existing truncated GLSL block stays valid.
    // occupancyBox : xyz = min corner of the covered box in CHUNK coords (it follows the viewer),
    //                w   = 1 when bindings 11/12 hold real occupancy, 0 when they hold the inert
    //                      fallback. The shader MUST check w before reading either buffer.
    alignas(16) glm::ivec4 occupancyBox{0, 0, 0, 0};
};

class VulkanDevice {
public:
    VulkanDevice();
    ~VulkanDevice();

    // Initialization
    bool initialize();
    void cleanup();

    // Instance management
    bool createInstance();
    bool setupDebugMessenger();
    bool createSurface(void* window);

    // Physical device selection
    bool pickPhysicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    // Logical device creation
    bool createLogicalDevice();

    // Swapchain management
    bool createSwapChain(int windowWidth, int windowHeight);
    bool createFramebuffers(VkRenderPass renderPass);
    bool createCommandBuffers();
    bool createSyncObjects();
    
    // Rendering resources
    bool createVertexBuffer();
    bool createIndexBuffer();
    bool createInstanceBuffer();
    bool createUniformBuffers();
    bool createLightBuffers();
    void updateLightBuffer(uint32_t frameIndex, const Graphics::LightBufferGPU& lightData);
    void cleanupLightBuffers();
    bool createAtlasUVBuffers();
    void updateAtlasUVBuffer(const std::vector<glm::vec4>& uvs, uint32_t fallbackIndex,
                             uint32_t count512, uint32_t count1024);
    void cleanupAtlasUVBuffers();
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createDescriptorSets();
    void updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, const glm::vec3& sunDirection, const glm::vec3& sunColor, uint32_t numInstances, float ambientLight = 1.0f, float emissiveMultiplier = 2.0f, const glm::vec3& cameraPosition = glm::vec3(0.0f), float elapsedTime = 0.0f);

    /// Shadow-only debug view (0 = off, 1 = on). Uploaded with the per-frame UBO, so every
    /// pass that declares `debugShadowMode` honours it in the same frame.
    /// Atmosphere-derived lighting + exposure, pushed into the UBO's trailing fields.
    /// Set once per frame from graphics/Atmosphere.h so the sky, the sun's colour, the ambient fill
    /// and the distance haze all come out of ONE scattering model rather than three tuned ramps.
    /// Carried as a struct through a setter (rather than seven more updateUniformBuffer parameters)
    /// for the same reason setDebugShadowMode is: that signature is already long enough.
    struct AtmosphereUniforms {
        glm::vec3 ambientColor{1.0f};
        glm::vec3 hazeHorizonColor{0.66f, 0.76f, 0.92f};
        glm::vec3 hazeZenithColor{0.40f, 0.55f, 0.85f};
        glm::vec3 moonDirection{0.0f, -1.0f, 0.0f};
        glm::vec3 moonColor{0.0f};
        float exposure = 1.0f;
        int   tonemapCurve = 1;   // 0 = none (raw linear), 1 = AgX
        // Celestial bodies, already placed and lit for this frame (see graphics/CelestialBody.h).
        static constexpr int kMaxSkyBodies = 4;
        glm::vec4 bodyDirRadius[kMaxSkyBodies]{};
        glm::vec4 bodyDisc[kMaxSkyBodies]{};
        glm::vec4 bodyLitDir[kMaxSkyBodies]{};
        glm::vec4 bodyLight[kMaxSkyBodies]{};
        int bodyCount = 0;
    };
    void setAtmosphereUniforms(const AtmosphereUniforms& a) { m_atmosphere = a; }
    const AtmosphereUniforms& getAtmosphereUniforms() const { return m_atmosphere; }

    void setDebugShadowMode(int mode) { m_debugShadowMode = mode; }
    int  getDebugShadowMode() const   { return m_debugShadowMode; }

    /// World-unit depth span of the fitted light volume (see UniformBufferObject::shadowDepthRange).
    void setShadowDepthRange(float r) { m_shadowDepthRange = (r > 1e-3f) ? r : 1.0f; }
    void setReflectedViewProj(uint32_t frameIndex, const glm::mat4& reflectedVP);
    /// Patch the grass displacer arrays in this frame's UBO (call AFTER updateUniformBuffer,
    /// which zero-fills them). displacers: xyz = camera-relative position, w = push radius;
    /// aux: x = strength envelope 0..1 (CPU-eased attack/release).
    void setGrassDisplacers(uint32_t frameIndex, const glm::vec4* displacers,
                            const glm::vec4* aux, int count);

    // Reflection UBO buffers (separate per-frame UBOs for rendering from reflected camera)
    bool createReflectionBuffers();
    void updateReflectionUniformBuffer(uint32_t frameIndex, const glm::mat4& reflectedView, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, const glm::vec3& sunDirection, const glm::vec3& sunColor, uint32_t numInstances, float ambientLight, float emissiveMultiplier, const glm::vec3& cameraPosition);
    void bindReflectionDescriptorSets(uint32_t frameIndex, VkPipelineLayout layout);
    void cleanupReflectionBuffers();
    void updateInstanceBuffer(const std::vector<InstanceData>& instances);
    
    // Dynamic subcube buffer management
    bool createDynamicSubcubeBuffer(uint32_t maxDynamicSubcubes);
    void updateDynamicSubcubeBuffer(const std::vector<DynamicSubcubeInstanceData>& dynamicSubcubes);
    void bindDynamicSubcubeBuffer(uint32_t frameIndex);
    void cleanupDynamicSubcubeBuffer();
    uint32_t getMaxDynamicSubcubes() const { return maxDynamicSubcubes; }

    // Character instance buffer management
    bool createCharacterInstanceBuffer(uint32_t maxInstances);
    void updateCharacterInstanceBuffer(const std::vector<CharacterInstanceData>& instances);
    void bindCharacterInstanceBuffer(VkCommandBuffer commandBuffer);
    void cleanupCharacterInstanceBuffer();
    /// Capacity of the shared character instance buffer. Callers MUST clamp their
    /// batches to this — a vkCmdDraw with firstInstance past it reads stale memory
    /// and the character silently vanishes (see RenderCoordinator::batchParts).
    uint32_t getMaxCharacterInstances() const { return maxCharacterInstances; }

    // Character bone-transform SSBO (descriptor set 0, binding 8). Holds one model
    // matrix per bone group so instances can index their own transform, which is what
    // lets a whole character render in a single draw.
    bool createCharacterBoneBuffer(uint32_t maxBones);
    void updateCharacterBoneBuffer(uint32_t frameIndex, const std::vector<glm::mat4>& bones);
    void cleanupCharacterBoneBuffer();
    uint32_t getMaxCharacterBones() const { return maxCharacterBones; }
    
    // Buffer creation helpers
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    // Compute infrastructure
    bool initComputeResources();
    void cleanupComputeResources();
    // Device-local storage buffer (SSBO). extraUsage: e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    void createStorageBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, VkBufferUsageFlags extraUsage = 0);
    // Host-coherent staging buffer, persistently mapped. Call vkUnmapMemory before destroying.
    void createPersistentStagingBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory, void** mappedPtr);

    VkQueue          getComputeQueue()  const { return computeQueue; }
    VkCommandPool    getComputeCommandPool() const { return computeCommandPool; }
    VkCommandBuffer  getComputeCommandBuffer(uint32_t frameIndex) const;
    uint32_t         getComputeQueueFamily() const { return computeQueueFamily; }
    bool             computeSharesGraphicsQueue() const { return computeQueueFamily == graphicsQueueFamily; }
    
    // Depth buffer management
        bool createDepthResources();
        VkFormat findDepthFormat();
        VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
        void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, 
                        VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);    
        
    // Texture atlas management
    bool loadTextureAtlas(const std::string& atlasPath);
    bool uploadTextureAtlasPixels(const uint8_t* pixels, int width, int height);
    // Upload a layer-major RGBA texture array (one texSize² layer per textureIndex) into the
    // voxel texture image for resolution class `target` (0 = 512 array @ binding 1, 1 = 1024
    // array @ binding 5) as a 2D array with a full mip chain. size = texSize*texSize*4*layers.
    bool uploadTextureArray(int target, const uint8_t* pixels, int texSize, int layerCount);

    /// Whether the device supports BC texture compression (BC7). If false, callers must use
    /// the uncompressed uploadTextureArray() path.
    bool bc7Supported() const { return bc7Supported_; }

    /// Whether the pipelineStatisticsQuery feature was enabled (for the D0 overdraw counter).
    bool pipelineStatsSupported() const { return pipelineStatsSupported_; }

    /// Upload a pre-compressed BC7 texture array for resolution class `target` (0/1). `data`
    /// holds all mip levels tightly packed in level-major / layer-minor order (level 0 first;
    /// within a level, layer 0..N-1, each layer = ceil(w/4)*ceil(h/4)*16 bytes).
    /// `levelByteOffsets` has mipLevels entries (start of each level in `data`).
    bool uploadTextureArrayBC7(int target, const uint8_t* data, size_t dataSize,
                               const std::vector<size_t>& levelByteOffsets,
                               int baseSize, int layerCount, int mipLevels);
    bool createTextureAtlasSampler();
    void updateDescriptorSetsWithTexture();
    void cleanupTextureAtlas();

    /// Load a PNG from disk and register it as an ImGui texture.
    /// Returns an ImTextureID (cast from VkDescriptorSet), or nullptr on failure.
    /// Textures are cached by path; repeated calls return the same handle.
    /// All ImGui textures are freed in cleanupImGuiTextures().
    void* loadImGuiTexture(const std::string& path);

    /// Free all textures loaded via loadImGuiTexture.
    void cleanupImGuiTextures();

    /// Free ONE cached ImGui texture so the same path can be reloaded with fresh pixels
    /// (waits for the device to go idle — refresh-button cadence, never per frame).
    void releaseImGuiTexture(const std::string& path);
    
    // Shadow map resources
    void setShadowMapResources(VkImageView imageView, VkSampler sampler) {
        shadowMapImageView = imageView;
        shadowMapSampler = sampler;
    }
    /// Near-cascade shadow map (binding 9). Absent -> the descriptor falls back to the mid
    /// map and shadowCascadeNear.x stays 0 so shaders never select the near cascade.
    void setShadowMapNearResources(VkImageView imageView, VkSampler sampler) {
        shadowMapNearImageView = imageView;
        shadowMapNearSampler = sampler;
    }
    /// Per-frame near-cascade state (pre-bias light matrix; rangeEnd 0 disables).
    void setNearShadowCascade(const glm::mat4& lightSpace, float rangeEnd, float depthRange) {
        m_nearLightSpace = lightSpace;
        m_nearCascadeRangeEnd = rangeEnd;
        m_nearCascadeDepthRange = depthRange;
    }
    /// Far-cascade shadow map (binding 10); same fallback contract as the near map.
    void setShadowMapFarResources(VkImageView imageView, VkSampler sampler) {
        shadowMapFarImageView = imageView;
        shadowMapFarSampler = sampler;
    }
    /// Sub-voxel light occupancy buffers (bindings 11/12) — docs/UnifiedLightingPlan.md.
    /// Both null is legal: the descriptors then fall back to a valid buffer that the shader never
    /// reads, because setLightOccupancyReady(false) leaves the guard flag clear.
    void setLightOccupancyResources(VkBuffer directory, VkBuffer pool) {
        lightOccupancyDirBuffer = directory;
        lightOccupancyPoolBuffer = pool;
    }
    VkBuffer getLightOccupancyDirBuffer() const { return lightOccupancyDirBuffer; }

    /// Per-frame: where the covered box sits (chunk coords) and whether it holds real data.
    /// `ready == false` leaves the shader guard clear, so the fallback binding is never read.
    /// w is a BITFIELD, so no extra std140 field is needed:
    ///   bit 0 (1) = occupancy readable (0 = buffers hold the inert fallback — do not read them)
    ///   bit 1 (2) = M2 point/spot light visibility tracing on
    ///   bit 2 (4) = M3 sky visibility tracing on
    void setLightOccupancyBox(const glm::ivec3& boxMinChunk, bool ready) {
        int w = 0;
        if (ready) {
            w |= 1;
            if (m_lightTracing) w |= 2;
            if (m_skyTracing)   w |= 4;
        }
        m_occupancyBox = glm::ivec4(boxMinChunk, w);
    }
    /// A/B switches for the M2 and M3 terms, so each one's effect and cost are measurable
    /// independently against the same scene rather than argued about.
    void setLightTracingEnabled(bool on) { m_lightTracing = on; }
    bool isLightTracingEnabled() const { return m_lightTracing; }
    void setSkyTracingEnabled(bool on) { m_skyTracing = on; }
    bool isSkyTracingEnabled() const { return m_skyTracing; }

    /// Per-frame far-cascade state (rangeEnd 0 disables).
    void setFarShadowCascade(const glm::mat4& lightSpace, float rangeEnd, float depthRange) {
        m_farLightSpace = lightSpace;
        m_farCascadeRangeEnd = rangeEnd;
        m_farCascadeDepthRange = depthRange;
    }
        
        // Command buffer operations
        void waitForFence(uint32_t frameIndex);
        void resetFence(uint32_t frameIndex);
        VkResult acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex);
        void resetCommandBuffer(uint32_t frameIndex);
        void beginCommandBuffer(uint32_t frameIndex);
        void beginRenderPass(uint32_t frameIndex, uint32_t imageIndex, VkRenderPass renderPass);
        void endRenderPass(uint32_t frameIndex);
        void endCommandBuffer(uint32_t frameIndex);
        bool submitCommandBuffer(uint32_t frameIndex);
        VkResult presentFrame(uint32_t imageIndex, uint32_t frameIndex);
        VkCommandBuffer getCommandBuffer(uint32_t frameIndex);
        
        // Rendering command recording
        void bindVertexBuffers(uint32_t frameIndex);
        void bindIndexBuffer(uint32_t frameIndex);
        void bindDescriptorSets(uint32_t frameIndex, VkPipelineLayout pipelineLayout);
        void drawIndexed(uint32_t frameIndex, uint32_t indexCount, uint32_t instanceCount,
                         uint32_t firstInstance = 0);

        // D1 (docs/RenderDensityPlan.md): each chunk face-instance is drawn with the cube's 36
        // indices, but the shader collapses them onto the instance's single face quad → ~12
        // triangles/face (6× the 2 a quad needs). s_quadDraw ON draws only the first 6 indices
        // (the +Z front quad {4,6,5,6,7,5}), which the shader repositions per faceID → one quad/face.
        // MAIN OPAQUE PASS ONLY (back-culled): pixel-identical there. The SHADOW pass also
        // back-culls (ShadowMap.cpp:392 — the front-cull claim recorded here before was WRONG),
        // yet D1 measured a ~1.1% pixel break applying the quad to all passes; cause unknown, so
        // the shadow pass stays 36-index until M5 re-derives it (docs/ContinuousLodPlan.md §7b);
        // likewise reflection/OIT/mirror keep 36 (winding unverified). See renderShadowPass.
        static bool s_quadDraw;
        static uint32_t chunkIndexCount() { return s_quadDraw ? 6u : 36u; }

        // Push constants and chunk rendering. chunkBaseOffset is CAMERA-RELATIVE (positions);
        // chunkBaseAbs is the EXACT absolute chunk origin (varied-hash seed) — both required
        // so tile rotations stay world-stable while geometry stays precision-safe.
        void pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, const glm::vec3& chunkBaseAbs);
        void pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, uint32_t debugMode, const glm::vec3& chunkBaseAbs);
        void bindInstanceBufferWithOffset(uint32_t frameIndex, VkDeviceSize offset);
        void drawChunk(uint32_t frameIndex, VkPipelineLayout pipelineLayout, 
                      const glm::vec3& chunkBaseOffset, VkDeviceSize instanceOffset, uint32_t instanceCount);
        
        // Convenience function for multi-chunk rendering
        struct ChunkRenderData {
            glm::vec3 worldPosition;     // World position of chunk (e.g., chunkX*32, chunkY*32, chunkZ*32)
            VkDeviceSize instanceOffset; // Offset in instance buffer for this chunk's data
            uint32_t instanceCount;      // Number of visible cubes in this chunk
        };
        void drawChunks(uint32_t frameIndex, VkPipelineLayout pipelineLayout, 
                       const std::vector<ChunkRenderData>& chunks);
        
        // Static helper for creating push constant range (use in pipeline layout creation)
        static VkPushConstantRange getPushConstantRange() {
            VkPushConstantRange range{};
            range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            range.offset = 0;
            // vec3 chunkBaseOffset (camera-relative) + uint debugMode + vec3 chunkBaseAbs
            // (exact absolute chunk origin — seeds the `varied` tile-rotation hash, which
            // must not re-roll with camera motion; docs/CameraRelativeRendering.md).
            range.size = 32;
            return range;
        }

        // Getters
        VkDevice getDevice() const { return device; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkInstance getInstance() const { return instance; }
        VkSurfaceKHR getSurface() const { return surface; }
        VkQueue getGraphicsQueue() const { return graphicsQueue; }
        VkQueue getPresentQueue() const { return presentQueue; }
        VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
        VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
        VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }
        // Set-0 descriptor whose UBO holds the reflected camera (used by the mirror reflection pass).
        VkDescriptorSet getReflectionDescriptorSet(uint32_t frameIndex) const { return reflectionDescriptorSets[frameIndex]; }
        uint32_t getSwapChainImageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }
        VkImage getSwapChainImage(uint32_t index) const { return swapChainImages[index]; }
        VkFramebuffer getSwapChainFramebuffer(uint32_t index) const { return swapChainFramebuffers[index]; }
        uint32_t getGraphicsQueueFamily() const;
        VkCommandBuffer getCommandBuffer(uint32_t frameIndex) const;
        
        // Command buffer utilities
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        
        // Synchronization / device idle
        void deviceWaitIdle();
        void recreateSyncObjects();

        // Window resize handling
        void setFramebufferResized(bool resized) { framebufferResized = resized; }
        bool getFramebufferResized() const { return framebufferResized; }
        bool recreateSwapChain(int windowWidth, int windowHeight, VkRenderPass renderPass);
        void cleanupSwapChain();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;

    // Compute resources
    uint32_t         computeQueueFamily = 0;
    uint32_t         graphicsQueueFamily = 0;
    VkCommandPool    computeCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> computeCommandBuffers;

    // Swapchain
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // Depth buffer
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    // Rendering resources
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceBufferMemory = VK_NULL_HANDLE;
    
    // Dynamic subcube buffer
    VkBuffer dynamicSubcubeBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dynamicSubcubeBufferMemory = VK_NULL_HANDLE;
    uint32_t maxDynamicSubcubes = 0;

    // Character instance buffer
    VkBuffer characterInstanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory characterInstanceBufferMemory = VK_NULL_HANDLE;
    void* characterInstanceMapped = nullptr;  // persistent map (HOST_COHERENT)
    uint32_t maxCharacterInstances = 0;

    // Character bone-transform SSBO (binding 8)
    // Character bone SSBO — PER FRAME IN FLIGHT. A single shared buffer memcpy'd every
    // frame raced the GPU: an in-flight frame read the NEXT frame's camera-relative bone
    // matrices against its own older view, shifting the character by the camera's
    // between-frame translation — the "character jitters in third person orbit, never in
    // free-cam rotation" bug (rotation leaves camera-relative positions unchanged).
    std::vector<VkBuffer> characterBoneBuffers;
    std::vector<VkDeviceMemory> characterBoneBufferMemories;
    std::vector<void*> characterBoneMapped;   // persistent maps (HOST_COHERENT)
    uint32_t maxCharacterBones = 0;
    
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Reflection UBO buffers (per-frame, separate from main UBOs)
    std::vector<VkBuffer> reflectionUniformBuffers;
    std::vector<VkDeviceMemory> reflectionUniformBuffersMemory;
    VkDescriptorPool reflectionDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> reflectionDescriptorSets;

    // Texture array resources — class 0 (512px, binding 1)
    VkImage textureAtlasImage = VK_NULL_HANDLE;
    VkDeviceMemory textureAtlasImageMemory = VK_NULL_HANDLE;
    VkImageView textureAtlasImageView = VK_NULL_HANDLE;
    VkSampler textureAtlasSampler = VK_NULL_HANDLE;
    // Class 1 (1024px, binding 5) — hi-res object/detail array
    VkImage textureArrayHiImage = VK_NULL_HANDLE;
    VkDeviceMemory textureArrayHiImageMemory = VK_NULL_HANDLE;
    VkImageView textureArrayHiImageView = VK_NULL_HANDLE;
    // Normal+roughness arrays (RGB=normal, A=roughness; UNORM): binding 6 (512), binding 7 (1024)
    VkImage textureNormal512Image = VK_NULL_HANDLE;
    VkDeviceMemory textureNormal512ImageMemory = VK_NULL_HANDLE;
    VkImageView textureNormal512ImageView = VK_NULL_HANDLE;
    VkImage textureNormal1024Image = VK_NULL_HANDLE;
    VkDeviceMemory textureNormal1024ImageMemory = VK_NULL_HANDLE;
    VkImageView textureNormal1024ImageView = VK_NULL_HANDLE;
    bool bc7Supported_ = false;  // set during logical-device creation
    bool pipelineStatsSupported_ = false;  // pipelineStatisticsQuery feature (D0 overdraw counter)

    // ImGui texture cache (for menu images, logos, etc.)
    struct ImGuiTextureEntry {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    std::unordered_map<std::string, ImGuiTextureEntry> imguiTextureCache_;

    // Shadow map resources
    VkImageView shadowMapImageView = VK_NULL_HANDLE;
    VkImageView shadowMapNearImageView = VK_NULL_HANDLE;   // near cascade (binding 9)
    VkSampler   shadowMapNearSampler = VK_NULL_HANDLE;
    glm::mat4   m_nearLightSpace{1.0f};
    float       m_nearCascadeRangeEnd = 0.0f;   // 0 = near cascade off
    float       m_nearCascadeDepthRange = 1.0f;
    VkImageView shadowMapFarImageView = VK_NULL_HANDLE;    // far cascade (binding 10)
    // Sub-voxel light occupancy (bindings 11/12). NOT owned here — VoxelLightOccupancyGpu owns
    // the memory; these are borrowed handles for descriptor writes only.
    VkBuffer lightOccupancyDirBuffer = VK_NULL_HANDLE;
    VkBuffer lightOccupancyPoolBuffer = VK_NULL_HANDLE;
    VkSampler   shadowMapFarSampler = VK_NULL_HANDLE;
    glm::mat4   m_farLightSpace{1.0f};
    float       m_farCascadeRangeEnd = 0.0f;    // 0 = far cascade off
    float       m_farCascadeDepthRange = 1.0f;
    VkSampler shadowMapSampler = VK_NULL_HANDLE;

    // Light SSBO resources
    std::vector<VkBuffer> lightBuffers;
    std::vector<VkDeviceMemory> lightBuffersMemory;

    // Atlas UV SSBO resources
    std::vector<VkBuffer> atlasUVBuffers;
    std::vector<VkDeviceMemory> atlasUVBuffersMemory;

    // Command buffers
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    
    // Window resize handling
    bool framebufferResized = false;
    AtmosphereUniforms m_atmosphere{};  ///< see setAtmosphereUniforms
    int  m_debugShadowMode = 0;   ///< shadow-only debug view (see setDebugShadowMode)
    glm::ivec4 m_occupancyBox{0, 0, 0, 0};   ///< xyz = box min chunk, w = 0 none / 1 read / 2 trace
    bool m_lightTracing = true;              ///< M2 point/spot visibility (default ON)
    /// M3 traced sky access. DEFAULT OFF — measured at 24.6 ms/frame on a generated medieval town
    /// (Release, GpuProfiler): Static Geometry 0.142 -> 24.604 ms, 275 -> 35 fps. Correct, and far
    /// too expensive to ship per-fragment. See docs/UnifiedLightingPlan.md D1 / M3-REDESIGN.
    /// Enable for measurement with POST /api/debug/light_occupancy?sky=1.
    // DEFAULT ON since 2026-09-01. This is M3 as the directive specified it: sky visibility TRACED
    // against real geometry, with no stored per-cell field.
    //
    // It shipped off because D1 measured it at 24.6 ms/frame -- and that measurement is what pushed
    // sky visibility into a per-cell bake, reinstating the exact storage M0 existed to delete. But
    // D1 ran the shader at 9 rays / reach 24 / 512 cells, while the bake itself was shipped at
    // 5 rays / reach 16 after measurement showed those still seal a room at every wall thickness.
    // The per-fragment path was never re-measured at the bake's own settings.
    //
    // Measured on Release, generated town, fixed pose, sky OFF/ON interleaved (Static Geometry):
    //     9 rays / 24 u / 512   24.604 ms      <- the number that retired this path
    //     5 rays / 16 u / 288    5.166 ms
    //     + normal-ray gate      2.997 ms      <- vs a 0.318 ms sky-OFF control
    // The sky term costs +2.68 ms, not +24.46 ms: 9.1x less. And it seals a generated interior --
    // sky_probe reads 0.0 inside an engine-built hall_house, 0.79-0.92 outside.
    bool m_skyTracing = true;
    float m_shadowDepthRange = 1.0f;  ///< world-unit light-volume depth span (bias normalization)

    // Helper methods for swapchain
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkPresentModeKHR preferredPresentMode_ = VK_PRESENT_MODE_IMMEDIATE_KHR;
public:
    void setPreferredPresentMode(VkPresentModeKHR mode) { preferredPresentMode_ = mode; }
    VkPresentModeKHR getPreferredPresentMode() const { return preferredPresentMode_; }
private:
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, int windowWidth, int windowHeight);

    // Synchronization
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    // Helper functions
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    // Validation layers and extensions
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // ---- C2 (docs/ContinuousLodPlan.md) capability set, resolved at device creation ----
    uint32_t instanceApiVersion_ = VK_API_VERSION_1_0;
    uint32_t deviceApiVersion_   = VK_API_VERSION_1_0;
    bool multiDrawIndirectSupported_ = false;
    bool drawIndirectFirstInstanceSupported_ = false;
    bool shaderDrawParametersAvailable_ = false;   ///< gl_DrawID (Vulkan 1.1 core)
    bool drawIndirectCountAvailable_ = false;      ///< vkCmdDrawIndexedIndirectCount (1.2 core)

public:
    /// C2 prerequisites. The plan assumed "portable Vulkan 1.2"; the instance was pinned to
    /// 1.0 with only VK_KHR_swapchain and no multiDrawIndirect, so every one of these had to
    /// be established before GPU-driven submission was even expressible.
    uint32_t getInstanceApiVersion() const { return instanceApiVersion_; }
    uint32_t getDeviceApiVersion() const { return deviceApiVersion_; }
    bool supportsMultiDrawIndirect() const { return multiDrawIndirectSupported_; }
    bool supportsDrawIndirectFirstInstance() const { return drawIndirectFirstInstanceSupported_; }
    bool supportsShaderDrawParameters() const { return shaderDrawParametersAvailable_; }
    bool supportsDrawIndirectCount() const { return drawIndirectCountAvailable_; }
    /// True when every prerequisite for one-multidraw-per-region is present.
    bool supportsGpuDrivenSubmission() const {
        return multiDrawIndirectSupported_ && drawIndirectFirstInstanceSupported_ &&
               shaderDrawParametersAvailable_;
    }
private:

#ifdef NDEBUG
    // Release: validation layers compiled out entirely (zero overhead).
    const bool enableValidationLayers = false;
#else
    // Debug: validation layers are OPT-IN to keep normal debug iteration fast.
    // They add heavy per-Vulkan-call CPU overhead (can drop FPS ~3-5x). Enable
    // only when actually debugging GPU/Vulkan issues:
    //   set PHYXEL_VALIDATION=1   (PowerShell: $env:PHYXEL_VALIDATION=1)
    const bool enableValidationLayers = (std::getenv("PHYXEL_VALIDATION") != nullptr);
#endif

    // Cleanup state tracking
    bool m_cleanedUp = false;
};

} // namespace Vulkan
} // namespace Phyxel
