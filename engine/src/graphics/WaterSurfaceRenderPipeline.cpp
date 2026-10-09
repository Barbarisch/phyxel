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

namespace Phyxel {
namespace Graphics {

namespace {

// Must match water_surface.vert/frag's push block exactly. 96 bytes.
struct WaterSurfacePush {
    glm::mat4 viewProj;
    glm::vec4 camPosTime;
    glm::vec4 screen;
};

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
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    m_device = VK_NULL_HANDLE;
}

void WaterSurfaceRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                            VkRenderPass renderPass, VkExtent2D swapChainExtent,
                                            VkDescriptorSetLayout uboLayout) {
    m_device = device; m_physicalDevice = physicalDevice;
    createBuffers();
    createDescriptorSetLayout(uboLayout);
    createDescriptorPool();
    createPipeline(renderPass, swapChainExtent);
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
}

void WaterSurfaceRenderPipeline::createDescriptorSetLayout(VkDescriptorSetLayout uboLayout) {
    VkPushConstantRange pc{}; pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; pc.offset = 0; pc.size = sizeof(WaterSurfacePush);
    std::array<VkDescriptorSetLayoutBinding, 2> binds{};
    binds[0].binding = 0; binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; binds[0].descriptorCount = 1; binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1] = binds[0]; binds[1].binding = 1;
    VkDescriptorSetLayoutCreateInfo li{}; li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; li.bindingCount = 2; li.pBindings = binds.data();
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) throw std::runtime_error("water-surface descriptor set layout");
    std::array<VkDescriptorSetLayout, 2> sets = {uboLayout, m_descriptorSetLayout};
    VkPipelineLayoutCreateInfo pli{}; pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; pli.setLayoutCount = 2; pli.pSetLayouts = sets.data(); pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
    if (vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pipelineLayout) != VK_SUCCESS) throw std::runtime_error("water-surface pipeline layout");
}

void WaterSurfaceRenderPipeline::createDescriptorPool() {
    VkDescriptorPoolSize size{}; size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; size.descriptorCount = 2;
    VkDescriptorPoolCreateInfo pi{}; pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pi.poolSizeCount = 1; pi.pPoolSizes = &size; pi.maxSets = 1;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool) != VK_SUCCESS) throw std::runtime_error("water-surface descriptor pool");
    VkDescriptorSetAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; ai.descriptorPool = m_descriptorPool; ai.descriptorSetCount = 1; ai.pSetLayouts = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_descriptorSet) != VK_SUCCESS) throw std::runtime_error("water-surface descriptor set");
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
    VkVertexInputAttributeDescription attrs[4];
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Core::Water::WaterSurfaceVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Core::Water::WaterSurfaceVertex, depth)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Core::Water::WaterSurfaceVertex, normal)};
    attrs[3] = {3, 0, VK_FORMAT_R32_SFLOAT,       offsetof(Core::Water::WaterSurfaceVertex, side)};
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind; vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;
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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkDescriptorSet sets[] = {uboSet, m_descriptorSet};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 2, sets, 0, nullptr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer[slot], &off);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer[slot], 0, VK_INDEX_TYPE_UINT32);
    WaterSurfacePush pc{};
    pc.viewProj = projectionMatrix * camera.getViewMatrix();   // absolute world space, like the sheet and the cells
    const float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_startTime).count();
    pc.camPosTime = glm::vec4(camera.getPosition(), t);
    pc.screen = glm::vec4(static_cast<float>(screenExtent.width), static_cast<float>(screenExtent.height), static_cast<float>(m_debugMode), 0.0f);
    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(WaterSurfacePush), &pc);
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(ni), 1, 0, 0, 0);
}

void WaterSurfaceRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent) {
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    createPipeline(renderPass, swapChainExtent);
}

} // namespace Graphics
} // namespace Phyxel
