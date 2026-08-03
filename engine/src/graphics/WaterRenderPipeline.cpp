#include "graphics/WaterRenderPipeline.h"
#include "graphics/SeaMesh.h"
#include "graphics/Camera.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Phyxel {
namespace Graphics {

// ── SEA MESH ────────────────────────────────────
// The geometry lives in SeaMesh.h/.cpp — a camera-relative Cartesian clipmap. Read that header
// before changing it: it records why the previous camera-centred RADIAL mesh had to go (its
// angular spacing grew with radius, so the swell aliased azimuthally, and aliasing inherits the
// sampling pattern's symmetry — radial sampling gave spokes converging on the viewer), and why the
// outer levels are allowed to be coarser than Nyquist (water.vert fades each wave component where
// the local spacing can no longer sample it, instead of drawing garbage).

// Push constants shared by both stages. 128 bytes — exactly the minimum every Vulkan implementation
// is guaranteed to offer, so this cannot grow again without moving to a uniform buffer.
struct WaterPushConstants {
    glm::mat4 viewProj;    // 64
    glm::vec4 camPosTime;  // 16  (camera xyz, time seconds)
    glm::vec4 params;      // 16  (seaLevel, hydro grid originX, wave amplitude, wind dir radians)
    glm::vec4 params2;     // 16  (screen width, screen height, reflectionEnabled, wave length)
    // params3.xy: the clipmap core spacing and half-extent — water.vert reconstructs the LOCAL grid
    // spacing from these to decide which wave components it can still sample. Passed rather than
    // duplicated as shader literals: hand-synced definitions drift.
    // params3.zw + params.y: the WATER-LAYER grid transform (hydro originZ, invCellSize; 0 = flat-
    // sea mode) — per-column basin levels sampled by both stages (water-layer P1).
    glm::vec4 params3;     // 16  (core spacing, core half-extent, hydro originZ, hydro invCellSize)
};

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

WaterRenderPipeline::WaterRenderPipeline() : m_startTime(std::chrono::high_resolution_clock::now()) {}

WaterRenderPipeline::~WaterRenderPipeline() { cleanup(); }

void WaterRenderPipeline::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        destroyHydrologyResources();
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
        if (m_indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
        if (m_indexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_underwaterPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_device, m_underwaterPipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    }
}

void WaterRenderPipeline::destroyHydrologyResources() {
    if (m_hydroSampler != VK_NULL_HANDLE) vkDestroySampler(m_device, m_hydroSampler, nullptr);
    if (m_hydroView != VK_NULL_HANDLE) vkDestroyImageView(m_device, m_hydroView, nullptr);
    if (m_hydroImage != VK_NULL_HANDLE) vkDestroyImage(m_device, m_hydroImage, nullptr);
    if (m_hydroImageMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_hydroImageMemory, nullptr);
    if (m_hydroStagingMapped) vkUnmapMemory(m_device, m_hydroStagingMemory);
    if (m_hydroStaging != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_hydroStaging, nullptr);
    if (m_hydroStagingMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_hydroStagingMemory, nullptr);
    m_hydroSampler = VK_NULL_HANDLE; m_hydroView = VK_NULL_HANDLE;
    m_hydroImage = VK_NULL_HANDLE; m_hydroImageMemory = VK_NULL_HANDLE;
    m_hydroStaging = VK_NULL_HANDLE; m_hydroStagingMemory = VK_NULL_HANDLE;
    m_hydroStagingMapped = nullptr; m_hydroStagingBytes = 0;
    m_hydroCellsX = m_hydroCellsZ = 0;
}

void WaterRenderPipeline::recordHydrologyUpload(VkCommandBuffer cmd, const float* levels,
                                                int cellsX, int cellsZ,
                                                float originX, float originZ, float cellSize) {
    if (m_device == VK_NULL_HANDLE || m_descriptorSet == VK_NULL_HANDLE) return;

    // The "no layer" sentinel: a 1×1 dry texel, per-column lookup disabled — the shaders take
    // the flat-sea path (pixel-identical to pre-P1), but binding 3 is VALID from the first draw.
    const float kNoWater = -1e6f;
    // RGBA: dry level + zero energy + NEUTRAL profile (turbidity 0, roughness 1). The neutral
    // values matter even here: a world with no bake still samples this texel on any code path that
    // reads the profile, and neutral is defined as "exactly today's look" (v4 W1).
    static const float sentinel[4] = {kNoWater, 0.0f, 0.0f, 1.0f};
    if (!levels) { levels = sentinel; cellsX = cellsZ = 1; cellSize = 0.0f; }

    // (Re)create the image when the grid size changes. The caller guarantees device idleness when
    // replacing a live grid (world change) — documented on the declaration.
    if (cellsX != m_hydroCellsX || cellsZ != m_hydroCellsZ) {
        destroyHydrologyResources();
        m_hydroCellsX = cellsX; m_hydroCellsZ = cellsZ;

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = { static_cast<uint32_t>(cellsX), static_cast<uint32_t>(cellsZ), 1 };
        ii.mipLevels = 1; ii.arrayLayers = 1;
        // R = level (water-layer P1), G = body wave energy (tangible-water F),
        // B = turbidity, A = roughness (Water Appearance v4 W1). Widened from RG32F because the
        // sea shader's push block is EXACTLY full at 128 B — the texture is the only vehicle left
        // for per-column data. 256² × 16 B = 1 MB, uploaded once per world.
        ii.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &ii, nullptr, &m_hydroImage) != VK_SUCCESS)
            throw std::runtime_error("failed to create water-layer level image!");
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(m_device, m_hydroImage, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_hydroImageMemory) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate water-layer level memory!");
        vkBindImageMemory(m_device, m_hydroImage, m_hydroImageMemory, 0);

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_hydroImage;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &vi, nullptr, &m_hydroView) != VK_SUCCESS)
            throw std::runtime_error("failed to create water-layer level view!");

        // NEAREST on purpose: basin levels are piecewise-constant; bilinear filtering would tilt
        // water surfaces across basin divides. (Also R32F linear filtering is optional in Vulkan
        // — the ripple-texture lesson.)
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(m_device, &si, nullptr, &m_hydroSampler) != VK_SUCCESS)
            throw std::runtime_error("failed to create water-layer level sampler!");

        VkDescriptorImageInfo info{ m_hydroSampler, m_hydroView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_descriptorSet; w.dstBinding = 3; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
    }

    // Stage + record the copy into the caller's one-shot command buffer. The staging buffer is
    // transient: freed by the caller's queue-idle boundary… we cannot free it here safely, so use
    // a small persistent member sized to the largest grid seen.
    const VkDeviceSize bytes = VkDeviceSize(cellsX) * cellsZ * 4 * sizeof(float);   // RGBA
    if (bytes > m_hydroStagingBytes) {
        if (m_hydroStagingMapped) vkUnmapMemory(m_device, m_hydroStagingMemory);
        if (m_hydroStaging != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_hydroStaging, nullptr);
        if (m_hydroStagingMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_hydroStagingMemory, nullptr);
        m_hydroStaging = VK_NULL_HANDLE; m_hydroStagingMemory = VK_NULL_HANDLE; m_hydroStagingMapped = nullptr;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_hydroStaging) != VK_SUCCESS)
            throw std::runtime_error("failed to create water-layer staging buffer!");
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(m_device, m_hydroStaging, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_hydroStagingMemory) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate water-layer staging memory!");
        vkBindBufferMemory(m_device, m_hydroStaging, m_hydroStagingMemory, 0);
        vkMapMemory(m_device, m_hydroStagingMemory, 0, bytes, 0, &m_hydroStagingMapped);
        m_hydroStagingBytes = bytes;
    }
    memcpy(m_hydroStagingMapped, levels, static_cast<size_t>(bytes));

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.image = m_hydroImage;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { static_cast<uint32_t>(cellsX), static_cast<uint32_t>(cellsZ), 1 };
    vkCmdCopyBufferToImage(cmd, m_hydroStaging, m_hydroImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);

    m_hydroParams = glm::vec3(originX, originZ, cellSize > 0.0f ? 1.0f / cellSize : 0.0f);
    m_hydroBound = true;
    LOG_INFO("WaterPipeline", "Water-layer levels bound: {}x{} cells, origin ({}, {}), cell {} "
             "(invCell 0 = flat-sea mode)", cellsX, cellsZ, originX, originZ, cellSize);
}

void WaterRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkRenderPass renderPass, VkExtent2D swapChainExtent,
                                     VkDescriptorSetLayout uboLayout) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    createBuffers();
    createDescriptorSetLayout(uboLayout);
    createDescriptorPool();
    createPipeline(renderPass, swapChainExtent);
}

void WaterRenderPipeline::createBuffers() {
    // Cartesian clipmap (SeaMesh.h). Replaces a camera-centred polar grid whose ANGULAR spacing grew
    // with radius, aliasing the swell into radial spokes that converged on the viewer — the "waves
    // emanate from the camera" vortex. Cost no longer scales with the render distance: the clipmap
    // adds one LEVEL per doubling of reach, where the polar mesh added a ring every 4.1 units.
    const SeaMesh mesh = buildSeaClipmap(m_waveRadius);
    const std::vector<glm::vec3>& verts   = mesh.vertices;
    const std::vector<uint32_t>&  indices = mesh.indices;
    m_seaOuterExtent = mesh.outerExtent;
    m_indexCount = static_cast<uint32_t>(indices.size());
    LOG_INFO("Water", "sea clipmap: " + std::to_string(mesh.levels) + " levels, " +
                          std::to_string(verts.size()) + " verts, " +
                          std::to_string(mesh.triangles()) + " tris, reach " +
                          std::to_string(static_cast<int>(mesh.outerExtent)) + "u");

    auto makeBuffer = [&](const void* src, VkDeviceSize bytes, VkBufferUsageFlags usage,
                          VkBuffer& buf, VkDeviceMemory& mem) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("failed to create water buffer!");
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(m_device, buf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate water buffer memory!");
        vkBindBufferMemory(m_device, buf, mem, 0);
        void* data;
        vkMapMemory(m_device, mem, 0, bytes, 0, &data);
        memcpy(data, src, static_cast<size_t>(bytes));
        vkUnmapMemory(m_device, mem);
    };

    makeBuffer(verts.data(), sizeof(glm::vec3) * verts.size(),
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vertexBuffer, m_vertexBufferMemory);
    makeBuffer(indices.data(), sizeof(uint32_t) * indices.size(),
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_indexBuffer, m_indexBufferMemory);
}

void WaterRenderPipeline::createDescriptorSetLayout(VkDescriptorSetLayout uboLayout) {
    // Set 1 — the water pipeline's own samplers:
    //   0 = half-res scene colour copy (refraction)
    //   1 = scene depth (seabed distance → absorption thickness + soft shoreline)
    //   2 = planar reflection (dormant; see docs/WaterSystemV3.md Phase 5)
    //   3 = the water-layer level grid (per-column basin levels; water-layer P1 — VERTEX too,
    //       because the level moves the surface GEOMETRY, not just the shading)
    std::array<VkDescriptorSetLayoutBinding, 4> binds{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1] = binds[0]; binds[1].binding = 1;
    binds[2] = binds[0]; binds[2].binding = 2;
    binds[3] = binds[0]; binds[3].binding = 3;
    binds[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(binds.size());
    layoutInfo.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create water descriptor set layout!");

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(WaterPushConstants);

    // Set 0 = the shared scene UBO (live sun/ambient + view/proj), set 1 = ours.
    std::array<VkDescriptorSetLayout, 2> sets = { uboLayout, m_descriptorSetLayout };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(sets.size());
    pipelineLayoutInfo.pSetLayouts = sets.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create water pipeline layout!");
}

void WaterRenderPipeline::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4;   // refraction + scene depth + reflection + water-layer levels

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create water descriptor pool!");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate water descriptor set!");
}

void WaterRenderPipeline::setReflectionTexture(VkImageView reflectionView, VkSampler reflectionSampler) {
    if (m_descriptorSet == VK_NULL_HANDLE || reflectionView == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = reflectionView;
    imageInfo.sampler = reflectionSampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 2;   // binding 0/1 are the refraction + depth taps now
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    m_reflectionBound = true;
}

void WaterRenderPipeline::setWaveRadius(float radius) {
    radius = std::max(120.0f, radius);
    if (std::fabs(radius - m_waveRadius) < 16.0f) return;   // not worth a rebuild
    m_waveRadius = radius;
    if (m_device == VK_NULL_HANDLE) return;                 // pre-init: the ctor value is used
    vkDeviceWaitIdle(m_device);
    vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    if (m_indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    if (m_indexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
    m_vertexBuffer = VK_NULL_HANDLE; m_vertexBufferMemory = VK_NULL_HANDLE;
    m_indexBuffer = VK_NULL_HANDLE;  m_indexBufferMemory = VK_NULL_HANDLE;
    createBuffers();
}

void WaterRenderPipeline::setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                                           VkImageView sceneDepthView, VkSampler sceneDepthSampler) {
    if (m_descriptorSet == VK_NULL_HANDLE) return;
    if (refractionView == VK_NULL_HANDLE || sceneDepthView == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo refr{ refractionSampler, refractionView,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    // Sampled while bound as a READ-ONLY depth attachment in the water pass — hence this layout.
    VkDescriptorImageInfo dep{ sceneDepthSampler, sceneDepthView,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

    std::array<VkWriteDescriptorSet, 2> w{};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = m_descriptorSet; w[0].dstBinding = 0; w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &refr;
    w[1] = w[0]; w[1].dstBinding = 1; w[1].pImageInfo = &dep;
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    m_sceneBound = true;
}

void WaterRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    auto vertShaderCode = readFile(Core::AssetManager::instance().resolveShader("water.vert.spv"));
    auto fragShaderCode = readFile(Core::AssetManager::instance().resolveShader("water.frag.spv"));

    VkShaderModule vertShaderModule, fragShaderModule;
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = vertShaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(vertShaderCode.data());
    vkCreateShaderModule(m_device, &createInfo, nullptr, &vertShaderModule);
    createInfo.codeSize = fragShaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(fragShaderCode.data());
    vkCreateShaderModule(m_device, &createInfo, nullptr, &fragShaderModule);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";
    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(glm::vec3);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // visible from above and below the surface
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // Depth bias: the sea plane sits at exactly Y=seaLevel, so any voxel TOP face at that same height
    // is coplanar with it → the depth test can't decide a winner and the surfaces flicker/swap as the
    // camera moves (z-fighting at the water line). Push the plane slightly back in depth so terrain
    // exactly at sea level consistently wins the tie (the water line reads as land, no flicker). The
    // plane's world height is unchanged. (depthWriteEnable stays false, so this only affects the test.)
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 2.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 1.5f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth-test so terrain occludes the surface (this IS the implicit-ocean trick),
    // but do not write depth — the surface is transparent and must not block later
    // passes (e.g. the mirror pass).
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Standard straight-alpha blending over the already-rendered opaque scene.
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create water graphics pipeline!");

    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);

    createUnderwaterPipeline(renderPass, swapChainExtent);
}

// Fullscreen underwater fog overlay. Same pipeline LAYOUT as the sea plane (set 0 = scene UBO,
// set 1 = refraction/depth/reflection, same push constants), so it needs no descriptors of its
// own — only a different vertex shader (the fullscreen triangle) and different state:
// no vertex buffer, no depth test (it must cover sky and the underside of the surface too).
void WaterRenderPipeline::createUnderwaterPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    auto vertCode = readFile(Core::AssetManager::instance().resolveShader("post_process.vert.spv"));
    auto fragCode = readFile(Core::AssetManager::instance().resolveShader("water_underwater.frag.spv"));

    VkShaderModule vertMod, fragMod;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = vertCode.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    vkCreateShaderModule(m_device, &ci, nullptr, &vertMod);
    ci.codeSize = fragCode.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    vkCreateShaderModule(m_device, &ci, nullptr, &fragMod);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vertMod; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragMod; stages[1].pName = "main";

    // No vertex input: post_process.vert generates the fullscreen triangle from gl_VertexIndex.
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{}; vp.width = (float)swapChainExtent.width; vp.height = (float)swapChainExtent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D sc{}; sc.offset = {0, 0}; sc.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo vps{};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test OFF — the fog covers everything, including sky (no depth) and the water surface
    // above the camera. It never writes depth.
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages;
    gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vps; gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds; gpi.pColorBlendState = &cb;
    gpi.layout = m_pipelineLayout; gpi.renderPass = renderPass; gpi.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpi, nullptr, &m_underwaterPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create underwater overlay pipeline!");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
}

void WaterRenderPipeline::render(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                                 const Camera& camera,
                                 const glm::mat4& projectionMatrix, float seaLevel, float size,
                                 VkExtent2D screenExtent, bool reflectionEnabled) {
    // The shaders sample every binding in set 1 unconditionally; don't draw until they all point
    // at real images. (Refraction/depth via setSceneTextures, reflection via setReflectionTexture,
    // the water-layer levels via recordHydrologyUpload — the sentinel form counts.)
    if (!m_sceneBound || !m_reflectionBound || !m_hydroBound || uboSet == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkDescriptorSet sets[] = { uboSet, m_descriptorSet };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 2, sets, 0, nullptr);

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
    glm::vec3 camPos = camera.getPosition();

    WaterPushConstants pc{};
    pc.viewProj   = projectionMatrix * camera.getViewMatrix();
    pc.camPosTime = glm::vec4(camPos, t);
    // params.y / params3.zw carry the WATER-LAYER grid transform (water-layer P1): hydro origin XZ
    // + inverse cell size (0 = flat-sea mode). params.y previously held the wave radius, which no
    // shader stage read — evicted for a lane that is actually consumed.
    (void)size;
    pc.params     = glm::vec4(seaLevel, m_hydroParams.x, m_waveAmplitude, m_windDirection);
    pc.params2    = glm::vec4(static_cast<float>(screenExtent.width),
                              static_cast<float>(screenExtent.height),
                              reflectionEnabled ? 1.0f : 0.0f, m_waveLength);
    pc.params3    = glm::vec4(SEA_CORE_SPACING, SEA_CORE_HALF, m_hydroParams.y, m_hydroParams.z);

    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(WaterPushConstants), &pc);

    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
}

void WaterRenderPipeline::renderUnderwater(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                                           const Camera& camera, const glm::mat4& projectionMatrix,
                                           float submergence, float depthBelow,
                                           VkExtent2D screenExtent, float turbidity) {
    if (!m_sceneBound || !m_reflectionBound || uboSet == VK_NULL_HANDLE) return;
    if (m_underwaterPipeline == VK_NULL_HANDLE || submergence <= 0.0f) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_underwaterPipeline);
    VkDescriptorSet sets[] = { uboSet, m_descriptorSet };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                            0, 2, sets, 0, nullptr);

    float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
    WaterPushConstants pc{};
    pc.viewProj   = projectionMatrix * camera.getViewMatrix();
    pc.camPosTime = glm::vec4(camera.getPosition(), t);
    pc.params     = glm::vec4(0.0f, 0.0f, submergence, depthBelow);
    // params2.w was the one free slot in this 128-byte block (v4 W2): the turbidity of the body the
    // camera is INSIDE. The surface samples its profile per pixel from the hydrology texture, but a
    // fullscreen overlay has no per-pixel body — without this the same lake reads murky from above
    // and clear from below, and breaking the surface pops.
    pc.params2    = glm::vec4(static_cast<float>(screenExtent.width),
                              static_cast<float>(screenExtent.height), 0.0f, turbidity);
    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(WaterPushConstants), &pc);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);   // fullscreen triangle, no vertex buffer
}

void WaterRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_underwaterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_underwaterPipeline, nullptr);
        m_underwaterPipeline = VK_NULL_HANDLE;
    }
    createPipeline(renderPass, swapChainExtent);   // also recreates the underwater pipeline
}

} // namespace Graphics
} // namespace Phyxel
