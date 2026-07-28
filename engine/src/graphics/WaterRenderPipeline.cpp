#include "graphics/WaterRenderPipeline.h"
#include "graphics/Camera.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <array>
#include <fstream>
#include <stdexcept>

namespace Phyxel {
namespace Graphics {

// ── SEA MESH (WaterSystemV3 Phase 2) ──────────────────────────────────────────────────────────
// The sea used to be a SINGLE quad locked to sea level — which is why it could never read as an
// ocean: a perfectly flat plane has no shape to catch light, no matter what the fragment shader
// does. It is now a camera-centred RADIAL grid that the vertex shader displaces with Gerstner
// waves.
//
// Why radial rather than a uniform grid: the sheet spans ~2x the render distance, so a uniform grid
// would spend most of its vertices on the horizon (where a wave is sub-pixel) and starve the water
// near the viewer (where the shape actually reads). Rings at radius (r/R)^2 put density where the
// camera is, which is the cheap approximation of a projected grid.
//
// ⚑GROUND — these two numbers are MEASURED, not guessed. Release, WaterLab, a vantage where the
// sea fills the frame (the worst case), 60 samples of total frame time per configuration:
//
//     128 tris (1 ring, coverage-matched control) .... 1.469 ms
//   4,608 tris (48 x 96)  .......................... 1.679 ms   (+0.21 ms)
//  24,320 tris (96 x 128) .......................... 1.912 ms   (+0.44 ms)
//
// 48 x 96 keeps 95% of the wave structure of the 5x denser mesh (row-to-row luminance change 2.010
// vs 2.112 on an identical capture) for less than half the cost, so that is the default. The denser
// mesh is NOT free: it was +30% of frame time in a sea-filling view. If a scene ever needs the cost
// back, these are the knob — and re-measure rather than assuming.
static constexpr int SEA_RINGS = 48;
static constexpr int SEA_SECTORS = 96;

// Unit-radius disc: xz in [-1,1], y unused. The shader scales by the sheet's half-size.
static void buildSeaMesh(std::vector<glm::vec3>& verts, std::vector<uint32_t>& indices) {
    verts.clear();
    indices.clear();
    verts.emplace_back(0.0f, 0.0f, 0.0f);   // centre vertex (under the camera)
    for (int r = 1; r <= SEA_RINGS; ++r) {
        // Quadratic radius growth => dense near the viewer, coarse toward the horizon.
        const float t = static_cast<float>(r) / static_cast<float>(SEA_RINGS);
        const float radius = t * t;
        for (int s = 0; s < SEA_SECTORS; ++s) {
            const float a = 6.28318530718f * static_cast<float>(s) / static_cast<float>(SEA_SECTORS);
            verts.emplace_back(radius * std::cos(a), 0.0f, radius * std::sin(a));
        }
    }
    auto ringVert = [](int ring, int sector) -> uint32_t {
        return 1u + static_cast<uint32_t>((ring - 1) * SEA_SECTORS + (sector % SEA_SECTORS));
    };
    for (int s = 0; s < SEA_SECTORS; ++s) {           // centre fan
        indices.push_back(0);
        indices.push_back(ringVert(1, s));
        indices.push_back(ringVert(1, s + 1));
    }
    for (int r = 1; r < SEA_RINGS; ++r)               // ring quads
        for (int s = 0; s < SEA_SECTORS; ++s) {
            const uint32_t a = ringVert(r, s),     b = ringVert(r, s + 1);
            const uint32_t c = ringVert(r + 1, s), d = ringVert(r + 1, s + 1);
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(b); indices.push_back(c); indices.push_back(d);
        }
}

// Push constants shared by both stages. 112 bytes — under the 128-byte guaranteed min.
struct WaterPushConstants {
    glm::mat4 viewProj;    // 64
    glm::vec4 camPosTime;  // 16  (camera xyz, time seconds)
    glm::vec4 params;      // 16  (seaLevel, sheet size, wave amplitude, wind direction radians)
    glm::vec4 params2;     // 16  (screen width, screen height, reflectionEnabled, wave length)
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
    std::vector<glm::vec3> verts;
    std::vector<uint32_t>  indices;
    buildSeaMesh(verts, indices);
    m_indexCount = static_cast<uint32_t>(indices.size());

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
    // The fragment shader samples every binding in set 1 unconditionally; don't draw until they
    // point at real images. (Reflection is written at init, refraction/depth by setSceneTextures.)
    if (!m_sceneBound || !m_reflectionBound || uboSet == VK_NULL_HANDLE) return;

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
    pc.params     = glm::vec4(seaLevel, size, m_waveAmplitude, m_windDirection);
    pc.params2    = glm::vec4(static_cast<float>(screenExtent.width),
                              static_cast<float>(screenExtent.height),
                              reflectionEnabled ? 1.0f : 0.0f, m_waveLength);

    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(WaterPushConstants), &pc);

    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);
}

void WaterRenderPipeline::renderUnderwater(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet,
                                           const Camera& camera, const glm::mat4& projectionMatrix,
                                           float submergence, float depthBelow,
                                           VkExtent2D screenExtent) {
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
    pc.params2    = glm::vec4(static_cast<float>(screenExtent.width),
                              static_cast<float>(screenExtent.height), 0.0f, 0.0f);
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
