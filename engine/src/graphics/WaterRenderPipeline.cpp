#include "graphics/DepthConvention.h"
#include "graphics/WaterRenderPipeline.h"
#include "graphics/Camera.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Phyxel {
namespace Graphics {

// The underwater overlay's push block (water_underwater.frag reads the first 112 bytes; params3 is the deleted sea sheet's
// lane, kept zero so the range stays what the layout declares).
struct WaterPushConstants {
    glm::mat4 viewProj;    // 64
    glm::vec4 camPosTime;  // 16  (camera xyz, time seconds)
    glm::vec4 params;      // 16  (clarity, unused, submergence, depth below)
    glm::vec4 params2;     // 16  (screen width, screen height, unused, turbidity)
    glm::vec4 params3;     // 16  (unused)
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

WaterRenderPipeline::WaterRenderPipeline() : m_startTime(std::chrono::high_resolution_clock::now()) {}

WaterRenderPipeline::~WaterRenderPipeline() { cleanup(); }

void WaterRenderPipeline::cleanup() {
    if (m_device != VK_NULL_HANDLE) {
        if (m_underwaterPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_device, m_underwaterPipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_underwaterPipeline = VK_NULL_HANDLE; m_pipelineLayout = VK_NULL_HANDLE;
        m_descriptorPool = VK_NULL_HANDLE; m_descriptorSetLayout = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
    }
}

void WaterRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkRenderPass renderPass, VkExtent2D swapChainExtent,
                                     VkDescriptorSetLayout uboLayout) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    createDescriptorSetLayout(uboLayout);
    createDescriptorPool();
    createUnderwaterPipeline(renderPass, swapChainExtent);
}

void WaterRenderPipeline::createDescriptorSetLayout(VkDescriptorSetLayout uboLayout) {
    // Set 1 — the water pipeline's own samplers:
    //   0 = half-res scene colour copy (refraction)
    //   1 = scene depth (seabed distance → absorption thickness + soft shoreline)
    //   2 = planar reflection (dormant; see docs/Water.md Phase 5)
    // (3, the old sea sheet's water-layer level grid, went with the sheet - WaterCore §55.2 W6.1b.)
    std::array<VkDescriptorSetLayoutBinding, 3> binds{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1] = binds[0]; binds[1].binding = 1;
    binds[2] = binds[0]; binds[2].binding = 2;

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
    poolSize.descriptorCount = 3;   // refraction + scene depth + reflection

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

// Fullscreen underwater fog overlay (set 0 = scene UBO, set 1 = refraction/depth/reflection). The sea sheet that shared this
// layout was deleted (WaterCore §55.2 W6.1b); the push block keeps its old shape - water_underwater.frag reads it as is.
// No vertex buffer, no depth test (it must cover sky and the underside of the surface too).
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

    // DYNAMIC viewport/scissor: a baked viewport goes stale the instant the window resizes (the tree-foliage-detach lesson);
    // set once per render pass by PostProcessor's begin*RenderPass. Shadow pipelines keep a static one on purpose.
    VkDynamicState uwDynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo uwDynState{};
    uwDynState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    uwDynState.dynamicStateCount = 2;
    uwDynState.pDynamicStates    = uwDynStates;

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
    gpi.pDynamicState = &uwDynState;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpi, nullptr, &m_underwaterPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create underwater overlay pipeline!");

    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
}

void WaterRenderPipeline::renderUnderwater(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                                           const Camera& camera, const glm::mat4& projectionMatrix,
                                           float submergence, float depthBelow,
                                           VkExtent2D screenExtent, float turbidity, float clarity) {
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
    pc.params     = glm::vec4(clarity, 0.0f, submergence, depthBelow);   // x: G3 clarity (m, 0 = derived) - the lane was unused
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
    if (m_underwaterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_underwaterPipeline, nullptr);
        m_underwaterPipeline = VK_NULL_HANDLE;
    }
    createUnderwaterPipeline(renderPass, swapChainExtent);
}

} // namespace Graphics
} // namespace Phyxel
