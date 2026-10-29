#include "graphics/DepthConvention.h"
#include "graphics/WaterSurfaceRenderPipeline.h"
#include "graphics/Camera.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cstddef>
#include <vector>

namespace Phyxel {
namespace Graphics {

namespace {

// Must match water_surface.vert/frag's push block exactly. 128 bytes (the guaranteed minimum).
struct WaterSurfacePush {
    glm::mat4 viewProj;
    glm::vec4 camPosTime;
    glm::vec4 screen;
    glm::vec4 look0;   // G3: tint.rgb (x < 0 = unset), clarity m (0 = unset)
    glm::vec4 look1;   // G3: turbidity (< 0 unset), roughness (< 0 unset)
};
static_assert(sizeof(WaterSurfacePush) == 128, "push block must stay within the 128-byte guarantee");
// 29 S1: water_surface_pull.vert's block - the shared 128 bytes, then the vertex stage's own (the field's lattice)
struct WaterSurfacePullPush {
    WaterSurfacePush base;
    glm::ivec4 field;    // origin x, origin z (cells), nx, nz
    glm::vec4 field2;    // h, the ripple layer + 1, -, -
};
static_assert(sizeof(WaterSurfacePullPush) == 160, "29 S1: the pull block is 160 bytes");
static_assert(WaterSurfaceRenderPipeline::kVertexArenaAlign % sizeof(Core::Water::WaterSurfaceVertex) == 0 && WaterSurfaceRenderPipeline::kVertexArenaAlign % 256 == 0,
              "26.30: a vertex-arena offset must be a whole number of vertices (vertexOffset = offset / vertex size)");

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open shader: " + filename);
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0); file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & properties) == properties) return i;
    throw std::runtime_error("water-surface: no suitable memory type");
}

}  // namespace

WaterSurfaceRenderPipeline::WaterSurfaceRenderPipeline() : m_startTime(std::chrono::high_resolution_clock::now()) {}
WaterSurfaceRenderPipeline::~WaterSurfaceRenderPipeline() { cleanup(); }

void WaterSurfaceRenderPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    for (uint32_t i = 0; i < kFrames; ++i) {
        if (m_vertexMapped[i]) vkUnmapMemory(m_device, m_vertexMemory[i]);
        if (m_indexMapped[i]) vkUnmapMemory(m_device, m_indexMemory[i]);
        vkDestroyBuffer(m_device, m_vertexBuffer[i], nullptr); vkFreeMemory(m_device, m_vertexMemory[i], nullptr);
        vkDestroyBuffer(m_device, m_indexBuffer[i], nullptr);  vkFreeMemory(m_device, m_indexMemory[i], nullptr);
        m_vertexMapped[i] = m_indexMapped[i] = nullptr;
    }
    destroyTileStore();
    if (m_rippleMapped) vkUnmapMemory(m_device, m_rippleMemory);
    vkDestroyBuffer(m_device, m_rippleBuffer, nullptr); vkFreeMemory(m_device, m_rippleMemory, nullptr);
    m_rippleMapped = nullptr; m_rippleBuffer = VK_NULL_HANDLE; m_rippleMemory = VK_NULL_HANDLE;
    destroyPullPipeline();
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    m_device = VK_NULL_HANDLE;
}

void WaterSurfaceRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                            VkRenderPass renderPass, VkExtent2D swapChainExtent,
                                            VkDescriptorSetLayout uboLayout) {
    m_device = device; m_physicalDevice = physicalDevice; m_uboLayout = uboLayout;
    createBuffers();
    createTileStore();
    createDescriptorSetLayout(uboLayout);
    createDescriptorPool();
    createPipeline(renderPass, swapChainExtent);
    try { createPullPipeline(renderPass, swapChainExtent); }
    catch (const std::exception& e) { LOG_WARN("WaterSurface", "29 S1: the device-surface pipeline is not available ({}) - the host's tiler draws every volume", e.what()); destroyPullPipeline(); }
}

void WaterSurfaceRenderPipeline::setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                                                  VkImageView sceneDepthView, VkSampler sceneDepthSampler) {
    if (m_descriptorSet == VK_NULL_HANDLE || refractionView == VK_NULL_HANDLE || sceneDepthView == VK_NULL_HANDLE) return;
    VkDescriptorImageInfo infos[2]{};
    infos[0].imageView = refractionView; infos[0].sampler = refractionSampler; infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[1].imageView = sceneDepthView; infos[1].sampler = sceneDepthSampler; infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_descriptorSet; writes[i].dstBinding = static_cast<uint32_t>(i);
        writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
    m_texturesBound = true;
}

void WaterSurfaceRenderPipeline::createBuffers() {
    for (uint32_t i = 0; i < kFrames; ++i) {
        auto make = [&](VkBuffer& buf, VkDeviceMemory& mem, void*& mapped, VkDeviceSize bytes, VkBufferUsageFlags usage) {
            VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size = bytes; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(m_device, &bi, nullptr, &buf) != VK_SUCCESS) throw std::runtime_error("failed to create water-surface buffer");
            VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, buf, &mr);
            VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = mr.size;
            ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS) throw std::runtime_error("failed to allocate water-surface memory");
            vkBindBufferMemory(m_device, buf, mem, 0);
            vkMapMemory(m_device, mem, 0, bytes, 0, &mapped);   // persistently mapped ring slot
        };
        make(m_vertexBuffer[i], m_vertexMemory[i], m_vertexMapped[i], sizeof(Core::Water::WaterSurfaceVertex) * kMaxVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        make(m_indexBuffer[i], m_indexMemory[i], m_indexMapped[i], sizeof(uint32_t) * kMaxIndices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
    {   // 22: the ripple buffer - kFrames slots of [vec4 fade (L2), vec4 geo[8], ivec4 meta[8], float heights[]], each slot aligned for a dynamic offset
        VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        const VkDeviceSize align = std::max<VkDeviceSize>(props.limits.minStorageBufferOffsetAlignment, 16);
        const VkDeviceSize raw = static_cast<VkDeviceSize>(kRippleHeaderBytes) + kRippleFloats * sizeof(float);
        m_rippleSlotBytes = (raw + align - 1) / align * align;
        VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size = m_rippleSlotBytes * kFrames; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_rippleBuffer) != VK_SUCCESS) throw std::runtime_error("failed to create water-surface ripple buffer");
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, m_rippleBuffer, &mr);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_rippleMemory) != VK_SUCCESS) throw std::runtime_error("failed to allocate water-surface ripple memory");
        vkBindBufferMemory(m_device, m_rippleBuffer, m_rippleMemory, 0);
        vkMapMemory(m_device, m_rippleMemory, 0, bi.size, 0, &m_rippleMapped);
        for (uint32_t i = 0; i < kFrames; ++i) {   // no layers, no fade
            char* base = static_cast<char*>(m_rippleMapped) + i * m_rippleSlotBytes;
            std::memset(base, 0, kRippleHeaderBytes);
            reinterpret_cast<float*>(base)[3] = -1.0f;
        }
    }
}

void WaterSurfaceRenderPipeline::createDescriptorSetLayout(VkDescriptorSetLayout uboLayout) {
    VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(WaterSurfacePush);
    std::array<VkDescriptorSetLayoutBinding, 3> binds{};
    binds[0].binding = 0; binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; binds[0].descriptorCount = 1; binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1] = binds[0]; binds[1].binding = 1;
    binds[2] = binds[0]; binds[2].binding = 2; binds[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;   // 22: ripples
    VkDescriptorSetLayoutCreateInfo li{}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; li.bindingCount = 3; li.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) throw std::runtime_error("water-surface descriptor set layout");
    std::array<VkDescriptorSetLayout, 2> sets = {uboLayout, m_descriptorSetLayout};
    VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pli.setLayoutCount = 2; pli.pSetLayouts = sets.data(); pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS) throw std::runtime_error("water-surface pipeline layout");
}

void WaterSurfaceRenderPipeline::createDescriptorPool() {
    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[0].descriptorCount = 2;
    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC; sizes[1].descriptorCount = 1;   // 22
    VkDescriptorPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pi.poolSizeCount = 2; pi.pPoolSizes = sizes; pi.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool) != VK_SUCCESS) throw std::runtime_error("water-surface descriptor pool");
    VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_descriptorSet) != VK_SUCCESS) throw std::runtime_error("water-surface descriptor set");
    VkDescriptorBufferInfo rb{}; rb.buffer = m_rippleBuffer; rb.offset = 0; rb.range = m_rippleSlotBytes;   // 22: one slot; the dynamic offset picks it
    VkWriteDescriptorSet w{}; w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = m_descriptorSet; w.dstBinding = 2; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC; w.pBufferInfo = &rb;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void WaterSurfaceRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    auto vsCode = readFile(Core::AssetManager::instance().resolveShader("water_surface.vert.spv"));
    auto fsCode = readFile(Core::AssetManager::instance().resolveShader("water_surface.frag.spv"));
    VkShaderModule vs, fs;
    VkShaderModuleCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = vsCode.size(); ci.pCode = reinterpret_cast<const uint32_t*>(vsCode.data()); vkCreateShaderModule(m_device, &ci, nullptr, &vs);
    ci.codeSize = fsCode.size(); ci.pCode = reinterpret_cast<const uint32_t*>(fsCode.data()); vkCreateShaderModule(m_device, &ci, nullptr, &fs);
    VkPipelineShaderStageCreateInfo vss{}, fss{};
    vss.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; vss.stage = VK_SHADER_STAGE_VERTEX_BIT; vss.module = vs; vss.pName = "main";
    fss.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; fss.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fss.module = fs; fss.pName = "main";
    VkPipelineShaderStageCreateInfo stages[] = {vss, fss};

    VkVertexInputBindingDescription bind{}; bind.binding = 0; bind.stride = sizeof(Core::Water::WaterSurfaceVertex); bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[7];
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Core::Water::WaterSurfaceVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Core::Water::WaterSurfaceVertex, depth)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Core::Water::WaterSurfaceVertex, normal)};
    attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Core::Water::WaterSurfaceVertex, side)};
    attrs[4] = {4, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Core::Water::WaterSurfaceVertex, foam)};   // G2
    attrs[5] = {5, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Core::Water::WaterSurfaceVertex, flow)};   // G2
    attrs[6] = {6, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Core::Water::WaterSurfaceVertex, pad)};    // 22: ripple layer + 1 (0 = none)
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind; vi.vertexAttributeDescriptionCount = 7; vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{}; vp.width = (float)swapChainExtent.width; vp.height = (float)swapChainExtent.height; vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D sc{}; sc.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo vps{}; vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{}; dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;   // both sides: the camera can be under the surface
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = Graphics::DepthConvention::sceneDepthCompareOp();
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gpi{}; gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages; gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia; gpi.pViewportState = &vps;
    gpi.pRasterizationState = &rs; gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds; gpi.pColorBlendState = &cb;
    gpi.layout = m_pipelineLayout; gpi.renderPass = renderPass; gpi.subpass = 0; gpi.pDynamicState = &dynState;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpi, nullptr, &m_pipeline) != VK_SUCCESS) throw std::runtime_error("failed to create water-surface graphics pipeline");
    vkDestroyShaderModule(m_device, vs, nullptr); vkDestroyShaderModule(m_device, fs, nullptr);
}

void WaterSurfaceRenderPipeline::writeRipples(uint32_t slot) {
    // 22: this frame's ripple heights into the slot (header: fade = L2's (viewer xz, near, far); geo = world origin xz + 1/pitch;
    // meta = offset, nx, nz, smooth)
    char* base = static_cast<char*>(m_rippleMapped) + slot * m_rippleSlotBytes;
    std::memset(base, 0, kRippleHeaderBytes);
    std::memcpy(base, &m_rippleFade[0], 16);
    auto* geo = reinterpret_cast<float*>(base + 16); auto* meta = reinterpret_cast<int32_t*>(base + 16 + kRippleIndices * 16);
    auto* heights = reinterpret_cast<float*>(base + kRippleHeaderBytes);
    size_t used = 0;
    if (m_ripples)
        for (size_t li = 0; li < m_ripples->size() && li < static_cast<size_t>(kRippleIndices); ++li) {
            const Core::Water::RippleLayer* L = (*m_ripples)[li];
            if (!L) continue;
            const size_t n = static_cast<size_t>(L->nx()) * L->nz();
            if (used + n > kRippleFloats) break;   // over the slot: the rest draw without ripples (the manager's budget prevents it)
            std::memcpy(heights + used, L->heights().data(), n * sizeof(float));
            geo[li * 4 + 0] = L->origin().x * Core::Water::RippleLayer::kPitch; geo[li * 4 + 1] = L->origin().y * Core::Water::RippleLayer::kPitch;
            geo[li * 4 + 2] = 1.0f / Core::Water::RippleLayer::kPitch;
            meta[li * 4 + 0] = static_cast<int32_t>(used); meta[li * 4 + 1] = L->nx(); meta[li * 4 + 2] = L->nz(); meta[li * 4 + 3] = m_rippleSmooth ? 1 : 0;
            used += n;
        }
}

void WaterSurfaceRenderPipeline::pushLook(VkCommandBuffer commandBuffer, const Camera& camera, const glm::mat4& projectionMatrix, VkExtent2D screenExtent,
                                          const Core::Water::WaterLookPacked* look) {
    WaterSurfacePush pc{};
    pc.viewProj = projectionMatrix * camera.getViewMatrix();   // absolute world space, like the sheet and the cells
    const float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
    pc.camPosTime = glm::vec4(camera.getPosition(), t);
    pc.screen = glm::vec4(static_cast<float>(screenExtent.width), static_cast<float>(screenExtent.height), static_cast<float>(m_debugMode), m_scatterLegacy ? 1.0f : 0.0f);
    const float guardOff = m_horizonGuard ? 0.0f : 1.0f;   // 22.12 B2 rides look1.z (unused by the look: 0 = guard on)
    const Core::Water::WaterLookPacked neutral;
    const Core::Water::WaterLookPacked& lk = look ? *look : neutral;
    pc.look0 = lk.look0; pc.look1 = lk.look1; pc.look1.z = guardOff; pc.look1.w = m_defaultClarity;
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterSurfacePush), &pc);
}

void WaterSurfaceRenderPipeline::render(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet, const Camera& camera,
                                        const glm::mat4& projectionMatrix, const Core::Water::WaterSurfaceMesh& mesh,
                                        VkExtent2D screenExtent, uint32_t frame) {
    m_lastVertices = 0; m_lastTruncated = false;
    if (mesh.vertices.empty() || mesh.indices.empty() || !m_texturesBound || uboSet == VK_NULL_HANDLE) return;
    const uint32_t slot = frame % kFrames;
    size_t nv = mesh.vertices.size(), ni = mesh.indices.size();
    if (nv > kMaxVertices || ni > kMaxIndices) {   // truncate to whole quads, and say so
        m_lastTruncated = true;
        const size_t quads = std::min(kMaxVertices / 4, kMaxIndices / 6);
        nv = quads * 4; ni = quads * 6;
    }
    std::memcpy(m_vertexMapped[slot], mesh.vertices.data(), nv * sizeof(Core::Water::WaterSurfaceVertex));
    std::memcpy(m_indexMapped[slot], mesh.indices.data(), ni * sizeof(uint32_t));
    m_lastVertices = static_cast<uint32_t>(nv);
    writeRipples(slot);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkDescriptorSet sets[] = {uboSet, m_descriptorSet};
    const uint32_t dynOff = static_cast<uint32_t>(slot * m_rippleSlotBytes);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 2, sets, 1, &dynOff);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer[slot], &off);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer[slot], 0, VK_INDEX_TYPE_UINT32);
    // G3: one draw per field so each body's look reaches the shader; a mesh without ranges (older
    // callers) draws once with the neutral look. Ranges past a truncation are clipped.
    if (mesh.ranges.empty()) {
        pushLook(commandBuffer, camera, projectionMatrix, screenExtent, nullptr);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(ni), 1, 0, 0, 0);
    } else {
        for (const auto& rg : mesh.ranges) {
            if (rg.firstIndex >= ni) break;
            const uint32_t cnt = static_cast<uint32_t>(std::min<size_t>(rg.indexCount, ni - rg.firstIndex));
            pushLook(commandBuffer, camera, projectionMatrix, screenExtent, &rg.look);
            vkCmdDrawIndexed(commandBuffer, cnt, 1, rg.firstIndex, 0, 0);
        }
    }
}

// ---- 26.30 the tile store ----------------------------------------------------------------------------------------------------

bool WaterSurfaceRenderPipeline::Arena::alloc(VkDeviceSize bytes, VkDeviceSize& off) {
    bytes = (bytes + align - 1) / align * align;   // every block a multiple of align, so every offset is (the free list starts at 0)
    for (auto it = freeList.begin(); it != freeList.end(); ++it) {
        if (it->second < bytes) continue;
        off = it->first;
        const VkDeviceSize rest = it->second - bytes;
        freeList.erase(it);
        if (rest > 0) freeList[off + bytes] = rest;
        used += bytes;
        return true;
    }
    return false;
}

void WaterSurfaceRenderPipeline::Arena::release(VkDeviceSize off, VkDeviceSize bytes) {
    bytes = (bytes + align - 1) / align * align;
    used -= bytes;
    auto it = freeList.emplace(off, bytes).first;
    auto next = std::next(it);
    if (next != freeList.end() && it->first + it->second == next->first) { it->second += next->second; freeList.erase(next); }
    if (it != freeList.begin()) {
        auto prev = std::prev(it);
        if (prev->first + prev->second == it->first) { prev->second += it->second; freeList.erase(it); }
    }
}

void WaterSurfaceRenderPipeline::Arena::grow(VkDeviceSize newSize) {
    // size = the buffer's bytes; the free list ends at whole multiples of align (as createTileStore's)
    const VkDeviceSize end = size / align * align, newEnd = newSize / align * align;
    if (newEnd <= end) return;
    auto tail = freeList.empty() ? freeList.end() : std::prev(freeList.end());
    if (tail != freeList.end() && tail->first + tail->second == end) tail->second += newEnd - end;   // a free tail extends
    else freeList[end] = newEnd - end;
    size = newSize;
}

static constexpr VkBufferUsageFlags kTileVertexUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
static constexpr VkBufferUsageFlags kTileIndexUsage  = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;   // 27.27: SRC - copied into a larger successor

void WaterSurfaceRenderPipeline::makeDeviceBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
    VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bi.size = bytes; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &buf) != VK_SUCCESS) throw std::runtime_error("failed to create water-surface tile buffer");
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, buf, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, props);
    if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, buf, nullptr); buf = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate water-surface tile memory");
    }
    vkBindBufferMemory(m_device, buf, mem, 0);
}

bool WaterSurfaceRenderPipeline::growArena(VkCommandBuffer cmd, Arena& a, VkDeviceSize need, VkDeviceSize maxBytes, VkBufferUsageFlags usage) {
    // 27.27: a full arena doubles (at least room for `need` at its end) - a new buffer holding the old one's bytes at the same
    // offsets (copied on the device), so every tile drawn and every held-back free stays valid; the old buffer is freed once no
    // frame in flight can read it
    if (s_arenaNoGrowForTest) return false;
    const VkDeviceSize newSize = std::min(std::max(a.size * 2, a.size + need + a.align), maxBytes);
    if (newSize < a.size + need + a.align) return false;
    VkBuffer nb = VK_NULL_HANDLE; VkDeviceMemory nm = VK_NULL_HANDLE;
    try { makeDeviceBuffer(nb, nm, newSize, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); }
    catch (const std::exception& e) { LOG_WARN("WaterSurface", "tile arena could not grow to {} MB: {}", newSize >> 20, e.what()); return false; }
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);   // earlier uploads land first
    const VkBufferCopy all{0, 0, a.size};
    vkCmdCopyBuffer(cmd, a.buffer, nb, 1, &all);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);   // before this frame's uploads into it
    m_retiredBuffers.push_back({m_frameCounter, a.buffer, a.memory});
    LOG_INFO("WaterSurface", "tile arena grew {} -> {} MB ({} MB in use)", a.size >> 20, newSize >> 20, a.used >> 20);
    a.buffer = nb; a.memory = nm;
    a.grow(newSize);
    ++m_arenaGrows; ++m_tileStats.grows;
    return true;
}

void WaterSurfaceRenderPipeline::createTileStore() {
    makeDeviceBuffer(m_vArena.buffer, m_vArena.memory, kArenaVertexBytes, kTileVertexUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    makeDeviceBuffer(m_iArena.buffer, m_iArena.memory, kArenaIndexBytes, kTileIndexUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_vArena.size = kArenaVertexBytes; m_vArena.align = kVertexArenaAlign; m_vArena.freeList = {{0, kArenaVertexBytes / kVertexArenaAlign * kVertexArenaAlign}};
    m_iArena.size = kArenaIndexBytes;  m_iArena.freeList = {{0, kArenaIndexBytes}};
    for (uint32_t i = 0; i < kFrames; ++i) {
        makeDeviceBuffer(m_staging[i], m_stagingMemory[i], kStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(m_device, m_stagingMemory[i], 0, kStagingBytes, 0, &m_stagingMapped[i]);
    }
}

void WaterSurfaceRenderPipeline::destroyTileStore() {
    for (uint32_t i = 0; i < kFrames; ++i) {
        if (m_stagingMapped[i]) vkUnmapMemory(m_device, m_stagingMemory[i]);
        vkDestroyBuffer(m_device, m_staging[i], nullptr); vkFreeMemory(m_device, m_stagingMemory[i], nullptr);
        m_staging[i] = VK_NULL_HANDLE; m_stagingMemory[i] = VK_NULL_HANDLE; m_stagingMapped[i] = nullptr;
    }
    for (Arena* a : {&m_vArena, &m_iArena}) { vkDestroyBuffer(m_device, a->buffer, nullptr); vkFreeMemory(m_device, a->memory, nullptr); *a = Arena{}; }
    for (const auto& r : m_retiredBuffers) { vkDestroyBuffer(m_device, r.buffer, nullptr); vkFreeMemory(m_device, r.memory, nullptr); }
    m_retiredBuffers.clear();
    m_gpuTiles.clear(); m_pendingFree.clear(); m_waitingSince.clear();
}

void WaterSurfaceRenderPipeline::uploadTiles(VkCommandBuffer cmd, const Core::Water::WaterSurfaceTiler& tiler, uint32_t frame) {
    using Core::Water::WaterSurfaceVertex;
    ++m_frameCounter;
    m_tileStats = TileStats{};
    // frees whose frames are done: a frame in flight may still draw from a region for kFrames frames after it was replaced
    for (auto it = m_pendingFree.begin(); it != m_pendingFree.end();) {
        if (it->frame + kFrames + 1 <= m_frameCounter) { m_vArena.release(it->vOff, it->vBytes); m_iArena.release(it->iOff, it->iBytes); it = m_pendingFree.erase(it); }
        else ++it;
    }
    for (auto it = m_retiredBuffers.begin(); it != m_retiredBuffers.end();) {   // 27.27: outgrown arenas no frame in flight reads
        if (it->frame + kFrames + 1 <= m_frameCounter) { vkDestroyBuffer(m_device, it->buffer, nullptr); vkFreeMemory(m_device, it->memory, nullptr); it = m_retiredBuffers.erase(it); }
        else ++it;
    }
    auto retire = [&](const GpuTile& g) { if (g.vBytes || g.iBytes) m_pendingFree.push_back({m_frameCounter, g.vOff, g.vBytes, g.iOff, g.iBytes}); };
    for (auto it = m_gpuTiles.begin(); it != m_gpuTiles.end();) {   // tiles that are gone
        if (!tiler.tiles().count(it->first)) { retire(it->second); it = m_gpuTiles.erase(it); } else ++it;
    }
    const uint32_t slot = frame % kFrames;
    char* staging = static_cast<char*>(m_stagingMapped[slot]);
    VkDeviceSize stagingUsed = 0;
    std::vector<VkBufferCopy> vCopies, iCopies;
    // 27.27: the tiles to upload, in a fair order - HOLES first (drawing nothing on the device where the new mesh has water), then
    // the longest-waiting; in key order (the old order) the same front tiles won the budget every frame and the back ones starved
    struct Want { const Core::Water::SurfaceTileKey* key; const Core::Water::SurfaceTile* t; bool hole; uint64_t since; };
    std::vector<Want> want;
    for (const auto& [key, t] : tiler.tiles()) {
        ++m_tileStats.tiles;
        auto found = m_gpuTiles.find(key);
        if (found != m_gpuTiles.end() && found->second.generation == t.generation) { m_waitingSince.erase(key); continue; }
        const bool hole = !t.mesh.indices.empty() && (found == m_gpuTiles.end() || found->second.indexCount == 0);
        const uint64_t since = m_waitingSince.try_emplace(key, m_frameCounter).first->second;
        want.push_back({&key, &t, hole, since});
    }
    for (auto it = m_waitingSince.begin(); it != m_waitingSince.end();) { if (!tiler.tiles().count(it->first)) it = m_waitingSince.erase(it); else ++it; }
    if (!s_uploadKeyOrderForTest)
        std::stable_sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.hole != b.hole ? a.hole : a.since < b.since; });
    for (const Want& wt : want) {
        const auto& key = *wt.key; const auto& t = *wt.t;
        auto found = m_gpuTiles.find(key);
        const VkDeviceSize vb = t.mesh.vertices.size() * sizeof(WaterSurfaceVertex), ib = t.mesh.indices.size() * sizeof(uint32_t);
        if (stagingUsed + vb + ib > kStagingBytes) {   // next frame; the old version (if any) still draws
            ++m_tileStats.deferred; m_tileStats.holesDeferred += wt.hole;
            m_tileStats.oldestWaitFrames = std::max<long>(m_tileStats.oldestWaitFrames, static_cast<long>(m_frameCounter - wt.since));
            continue;
        }
        GpuTile g; g.generation = t.generation; g.indexCount = static_cast<uint32_t>(t.mesh.indices.size());
        if (vb > 0) {
            VkDeviceSize vo = 0, io = 0;
            // 27.27: a full arena grows (a skipped tile was a dry square on screen); only past the cap is a tile skipped - and said so
            const bool okV = m_vArena.alloc(vb, vo) || (growArena(cmd, m_vArena, vb, kArenaVertexMaxBytes, kTileVertexUsage) && m_vArena.alloc(vb, vo));
            const bool okI = okV && (m_iArena.alloc(ib, io) || (growArena(cmd, m_iArena, ib, kArenaIndexMaxBytes, kTileIndexUsage) && m_iArena.alloc(ib, io)));
            if (!okI) {
                if (okV) m_vArena.release(vo, vb);
                ++m_tileStats.overArena;
                if (!m_overArenaLogged && !s_arenaNoGrowForTest) {
                    m_overArenaLogged = true;
                    LOG_WARN("WaterSurface", "tile arenas full at their caps ({} / {} MB): water tiles are NOT drawn", m_vArena.size >> 20, m_iArena.size >> 20);
                }
                continue;
            }
            g.vOff = vo; g.vBytes = vb; g.iOff = io; g.iBytes = ib;
            std::memcpy(staging + stagingUsed, t.mesh.vertices.data(), vb); vCopies.push_back({stagingUsed, vo, vb}); stagingUsed += vb;
            std::memcpy(staging + stagingUsed, t.mesh.indices.data(), ib);  iCopies.push_back({stagingUsed, io, ib}); stagingUsed += ib;
        }
        if (found != m_gpuTiles.end()) retire(found->second);
        m_gpuTiles[key] = g;
        m_waitingSince.erase(key);
        ++m_tileStats.uploaded;
    }
    m_tileStats.uploadBytes = stagingUsed;
    m_tileStats.arenaVertexBytes = m_vArena.used; m_tileStats.arenaIndexBytes = m_iArena.used;
    m_tileStats.arenaVertexSize = m_vArena.size; m_tileStats.arenaIndexSize = m_iArena.size; m_tileStats.growsTotal = m_arenaGrows;
    if (vCopies.empty()) return;
    vkCmdCopyBuffer(cmd, m_staging[slot], m_vArena.buffer, static_cast<uint32_t>(vCopies.size()), vCopies.data());
    vkCmdCopyBuffer(cmd, m_staging[slot], m_iArena.buffer, static_cast<uint32_t>(iCopies.size()), iCopies.data());
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void WaterSurfaceRenderPipeline::renderTiles(VkCommandBuffer cmd, VkDescriptorSet uboSet, const Camera& camera, const glm::mat4& projectionMatrix,
                                             const Core::Water::WaterSurfaceTiler& tiler, const Core::Water::WaterSurfaceMesh& extra,
                                             VkExtent2D screenExtent, uint32_t frame) {
    m_lastVertices = 0; m_lastTruncated = false;
    if (!m_texturesBound || uboSet == VK_NULL_HANDLE) return;
    const uint32_t slot = frame % kFrames;
    writeRipples(slot);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkDescriptorSet sets[] = {uboSet, m_descriptorSet};
    const uint32_t dynOff = static_cast<uint32_t>(slot * m_rippleSlotBytes);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 2, sets, 1, &dynOff);
    // the view's four side planes (row 3 +- rows 0 and 1 of viewProj) do not depend on the depth convention (reverse-Z); a tile
    // wholly outside one is not drawn. Near and far are not tested.
    const glm::mat4 vp = projectionMatrix * camera.getViewMatrix();
    auto row = [&](int r) { return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]); };
    const glm::vec4 planes[4] = {row(3) + row(0), row(3) - row(0), row(3) + row(1), row(3) - row(1)};
    auto visible = [&](const glm::vec3& lo, const glm::vec3& hi) {
        for (const glm::vec4& p : planes) {
            const glm::vec3 c(p.x >= 0 ? hi.x : lo.x, p.y >= 0 ? hi.y : lo.y, p.z >= 0 ? hi.z : lo.z);   // the corner furthest along the normal
            if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < 0.0f) return false;
        }
        return true;
    };
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vArena.buffer, &off);
    vkCmdBindIndexBuffer(cmd, m_iArena.buffer, 0, VK_INDEX_TYPE_UINT32);
    for (const auto& [key, t] : tiler.tiles()) {
        auto g = m_gpuTiles.find(key);
        if (g == m_gpuTiles.end() || g->second.indexCount == 0) continue;
        if (m_cull && !visible(t.lo, t.hi)) { ++m_tileStats.culled; continue; }
        pushLook(cmd, camera, projectionMatrix, screenExtent, &t.look);
        vkCmdDrawIndexed(cmd, g->second.indexCount, 1, static_cast<uint32_t>(g->second.iOff / sizeof(uint32_t)),
                         static_cast<int32_t>(g->second.vOff / sizeof(Core::Water::WaterSurfaceVertex)), 0);
        ++m_tileStats.drawn; m_tileStats.drawnVertices += static_cast<uint32_t>(g->second.vBytes / sizeof(Core::Water::WaterSurfaceVertex));
    }
    m_lastVertices = m_tileStats.drawnVertices;
    if (!extra.vertices.empty() && !extra.indices.empty()) {   // the droplets: small, every frame, through the ring
        const size_t nv = std::min(extra.vertices.size(), kMaxVertices), ni = std::min(extra.indices.size(), kMaxIndices);
        std::memcpy(m_vertexMapped[slot], extra.vertices.data(), nv * sizeof(Core::Water::WaterSurfaceVertex));
        std::memcpy(m_indexMapped[slot], extra.indices.data(), ni * sizeof(uint32_t));
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer[slot], &off);
        vkCmdBindIndexBuffer(cmd, m_indexBuffer[slot], 0, VK_INDEX_TYPE_UINT32);
        for (const auto& rg : extra.ranges) {
            if (rg.firstIndex >= ni) break;
            pushLook(cmd, camera, projectionMatrix, screenExtent, &rg.look);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(std::min<size_t>(rg.indexCount, ni - rg.firstIndex)), 1, rg.firstIndex, 0, 0);
        }
        m_lastVertices += static_cast<uint32_t>(nv);
    }
}

void WaterSurfaceRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    createPipeline(renderPass, swapChainExtent);
    if (m_pullPipeline != VK_NULL_HANDLE) {   // 29 S1: the pipeline only (its layout and sets stand)
        vkDestroyPipeline(m_device, m_pullPipeline, nullptr); m_pullPipeline = VK_NULL_HANDLE;
        try { createPullPipeline(renderPass, swapChainExtent); } catch (const std::exception& e) { LOG_WARN("WaterSurface", "29 S1: pull pipeline: {}", e.what()); }
    }
}

// ---- 29 S1: the surface drawn from the device -----------------------------------------------------------------------------------

void WaterSurfaceRenderPipeline::createPullPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    if (props.limits.maxPushConstantsSize < sizeof(WaterSurfacePullPush)) throw std::runtime_error("push constants under 160 bytes");
    if (m_pullLayout == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayoutBinding, 4> binds{};
        for (uint32_t i = 0; i < 4; ++i) { binds[i].binding = i; binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; binds[i].descriptorCount = 1; binds[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT; }
        VkDescriptorSetLayoutCreateInfo li{}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; li.bindingCount = 4; li.pBindings = binds.data();
        if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_pullSetLayout) != VK_SUCCESS) throw std::runtime_error("pull set layout");
        VkPushConstantRange pcs[2]{};
        pcs[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pcs[0].offset = 0; pcs[0].size = sizeof(WaterSurfacePush);
        pcs[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT; pcs[1].offset = sizeof(WaterSurfacePush); pcs[1].size = sizeof(WaterSurfacePullPush) - sizeof(WaterSurfacePush);
        // set 0 (the scene UBO) is the scene's layout - the same one the plain pipeline's layout was made with
        VkDescriptorSetLayout uboLayout = m_uboLayout;
        std::array<VkDescriptorSetLayout, 3> sets = {uboLayout, m_descriptorSetLayout, m_pullSetLayout};
        VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pli.setLayoutCount = 3; pli.pSetLayouts = sets.data(); pli.pushConstantRangeCount = 2; pli.pPushConstantRanges = pcs;
        if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pullLayout) != VK_SUCCESS) throw std::runtime_error("pull pipeline layout");
        VkDescriptorPoolSize size{}; size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; size.descriptorCount = 4 * 64;
        VkDescriptorPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.poolSizeCount = 1; pi.pPoolSizes = &size; pi.maxSets = 64;
        if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pullPool) != VK_SUCCESS) throw std::runtime_error("pull descriptor pool");
    }
    auto vsCode = readFile(Core::AssetManager::instance().resolveShader("water_surface_pull.vert.spv"));
    auto fsCode = readFile(Core::AssetManager::instance().resolveShader("water_surface.frag.spv"));
    VkShaderModule vs, fs;
    VkShaderModuleCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = vsCode.size(); ci.pCode = reinterpret_cast<const uint32_t*>(vsCode.data()); vkCreateShaderModule(m_device, &ci, nullptr, &vs);
    ci.codeSize = fsCode.size(); ci.pCode = reinterpret_cast<const uint32_t*>(fsCode.data()); vkCreateShaderModule(m_device, &ci, nullptr, &fs);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;   // no vertex input: pulled
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{}; vp.width = (float)swapChainExtent.width; vp.height = (float)swapChainExtent.height; vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    VkRect2D sc{}; sc.extent = swapChainExtent;
    VkPipelineViewportStateCreateInfo vps{}; vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{}; dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; dynState.dynamicStateCount = 2; dynState.pDynamicStates = dynStates;
    // the plain pipeline's states exactly (both sides, depth test without write, alpha blend)
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = Graphics::DepthConvention::sceneDepthCompareOp();
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE; cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; cb.attachmentCount = 1; cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gpi{}; gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.stageCount = 2; gpi.pStages = stages; gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia; gpi.pViewportState = &vps;
    gpi.pRasterizationState = &rs; gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds; gpi.pColorBlendState = &cb;
    gpi.layout = m_pullLayout; gpi.renderPass = renderPass; gpi.subpass = 0; gpi.pDynamicState = &dynState;
    const VkResult res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpi, nullptr, &m_pullPipeline);
    vkDestroyShaderModule(m_device, vs, nullptr); vkDestroyShaderModule(m_device, fs, nullptr);
    if (res != VK_SUCCESS) { m_pullPipeline = VK_NULL_HANDLE; throw std::runtime_error("pull graphics pipeline"); }
}

void WaterSurfaceRenderPipeline::destroyPullPipeline() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pullPipeline) vkDestroyPipeline(m_device, m_pullPipeline, nullptr);
    if (m_pullPool) vkDestroyDescriptorPool(m_device, m_pullPool, nullptr);   // frees the sets
    if (m_pullLayout) vkDestroyPipelineLayout(m_device, m_pullLayout, nullptr);
    if (m_pullSetLayout) vkDestroyDescriptorSetLayout(m_device, m_pullSetLayout, nullptr);
    m_pullPipeline = VK_NULL_HANDLE; m_pullPool = VK_NULL_HANDLE; m_pullLayout = VK_NULL_HANDLE; m_pullSetLayout = VK_NULL_HANDLE;
    m_pullSets.clear();
}

void WaterSurfaceRenderPipeline::gpuSurfaceBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void WaterSurfaceRenderPipeline::renderGpuSurfaces(VkCommandBuffer cmd, VkDescriptorSet uboSet, const Camera& camera, const glm::mat4& projectionMatrix,
                                                   const std::vector<Core::Water::GpuSurfaceDraw>& draws, VkExtent2D screenExtent, uint32_t frame) {
    m_lastGpuSurfaceDraws = 0;
    ++m_pullFrame;
    if (m_pullPipeline == VK_NULL_HANDLE || draws.empty() || !m_texturesBound || uboSet == VK_NULL_HANDLE) return;
    // sets of volumes not drawn for longer than the frames in flight are freed (a destroyed volume's: destroyVolume waited idle)
    for (auto it = m_pullSets.begin(); it != m_pullSets.end();) {
        if (it->lastFrame + kFrames + 1 < m_pullFrame) { vkFreeDescriptorSets(m_device, m_pullPool, 1, &it->set); it = m_pullSets.erase(it); }
        else ++it;
    }
    const uint32_t slot = frame % kFrames;
    writeRipples(slot);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pullPipeline);
    VkDescriptorSet sets[] = {uboSet, m_descriptorSet};
    const uint32_t dynOff = static_cast<uint32_t>(slot * m_rippleSlotBytes);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pullLayout, 0, 2, sets, 1, &dynOff);
    for (const auto& d : draws) {
        if (d.nx <= 0 || d.nz <= 0 || d.surf == VK_NULL_HANDLE) continue;
        PullSet* ps = nullptr;
        for (auto& s : m_pullSets) if (s.serial == d.serial) ps = &s;
        if (!ps) {
            VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = m_pullPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_pullSetLayout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(m_device, &ai, &set) != VK_SUCCESS) continue;   // the pool is full (64 volumes drawn at once): not drawn this frame
            VkDescriptorBufferInfo bi[4] = {{d.surf, 0, d.surfBytes}, {d.corners, 0, d.cornersBytes}, {d.meta, 0, d.metaBytes}, {d.list, 0, d.listBytes}};
            VkWriteDescriptorSet w[4]{};
            for (uint32_t i = 0; i < 4; ++i) { w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = set; w[i].dstBinding = i; w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[i].pBufferInfo = &bi[i]; }
            vkUpdateDescriptorSets(m_device, 4, w, 0, nullptr);
            m_pullSets.push_back({d.serial, set, m_pullFrame});
            ps = &m_pullSets.back();
        }
        ps->lastFrame = m_pullFrame;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pullLayout, 2, 1, &ps->set, 0, nullptr);
        WaterSurfacePullPush pc{};
        pc.base.viewProj = projectionMatrix * camera.getViewMatrix();
        const float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
        pc.base.camPosTime = glm::vec4(camera.getPosition(), t);
        pc.base.screen = glm::vec4(static_cast<float>(screenExtent.width), static_cast<float>(screenExtent.height), static_cast<float>(m_debugMode), m_scatterLegacy ? 1.0f : 0.0f);
        pc.base.look0 = d.look.look0; pc.base.look1 = d.look.look1; pc.base.look1.z = m_horizonGuard ? 0.0f : 1.0f; pc.base.look1.w = m_defaultClarity;   // as pushLook
        pc.field = glm::ivec4(d.originX, d.originZ, d.nx, d.nz);
        pc.field2 = glm::vec4(d.h, static_cast<float>(d.rippleLayer + 1), 0.0f, 0.0f);
        vkCmdPushConstants(cmd, m_pullLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterSurfacePush), &pc.base);
        vkCmdPushConstants(cmd, m_pullLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(WaterSurfacePush), sizeof(WaterSurfacePullPush) - sizeof(WaterSurfacePush), &pc.field);
        vkCmdDraw(cmd, 6u * static_cast<uint32_t>(d.nx) * static_cast<uint32_t>(d.nz), 1, 0, 0);   // every column's first-run top
        vkCmdDrawIndirect(cmd, d.args, 0, 1, 16);                                                   // the extras (firstInstance 1)
        ++m_lastGpuSurfaceDraws;
    }
}

} // namespace Graphics
} // namespace Phyxel
