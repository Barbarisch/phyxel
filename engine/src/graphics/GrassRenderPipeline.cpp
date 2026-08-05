#include "graphics/DepthConvention.h"
#include "graphics/GrassRenderPipeline.h"
#include "core/AssetManager.h"
#include "core/Types.h"
#include "utils/Logger.h"

#include <fstream>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Graphics {

// Push constant layout — MUST match grass.vert. Time + camera position come from the UBO;
// the wind-field scalars come from the shared WindSystem via Params::wind.
struct GrassPush {
    glm::vec3 chunkBaseOffset;   // CAMERA-RELATIVE chunk origin (docs/CameraRelativeRendering.md)
    float     bladeHeight;
    float     windStrength;
    float     radius;
    float     fadeRange;
    float     growDuration;
    uint32_t  bladesPerVoxel;
    float     windDirX;
    float     windDirZ;
    float     windBase;
    float     gustAmp;
    float     gustScale;
    float     gustSpeed;
    uint32_t  bladeStyle;
    // ABSOLUTE chunk origin, float-exact (integers < 2^24) — the hash/clump/wind-phase seeds
    // must NOT be camera-relative or blades re-roll as the camera moves. Scalar floats so the
    // std430 offsets append tightly after bladeStyle on both sides.
    float     absBaseX;
    float     absBaseY;
    float     absBaseZ;
    float     pushStrength;   // displacer bend amplitude (0 disables the interaction response)
    float     widthScale;     // density-LOD width compensation (1.0 = full density, near field)
    // World size of ONE shadow-map texel this frame (2*fittedRadius / mapWidth). The shadow
    // pass clamps blade width to a few of these so a blade always rasterizes: MEASURED, a
    // 0.04u blade writes NOTHING at a 0.024u texel while 0.08u casts, and the required width
    // scales with shadow distance — so a constant widening is wrong at every distance but one.
    float     shadowWidthScale;   // shadow proxy width / real blade width (1.0 = identical)
};
static_assert(sizeof(GrassPush) == 88, "GrassPush must match the grass.vert push-constant block");

static std::vector<char> readShaderFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("GrassRenderPipeline: cannot open shader: " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

bool GrassRenderPipeline::s_castShadows = true;
float GrassRenderPipeline::s_shadowWidthScale = 2.0f;

GrassRenderPipeline::GrassRenderPipeline() {}

GrassRenderPipeline::~GrassRenderPipeline() { cleanup(); }

void GrassRenderPipeline::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pipeline       != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipeline       = VK_NULL_HANDLE;
    m_shadowPipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

bool GrassRenderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkRenderPass renderPass, VkExtent2D extent,
                                     VkDescriptorSetLayout uboDescriptorSetLayout) {
    m_device         = device;
    m_physicalDevice = physicalDevice;
    try {
        createPipeline(renderPass, extent, uboDescriptorSetLayout);
    } catch (const std::exception& e) {
        LOG_ERROR("GrassRenderPipeline", "Initialization failed: {}", e.what());
        return false;
    }
    LOG_INFO("GrassRenderPipeline", "Initialized (grass blade layer)");
    return true;
}

void GrassRenderPipeline::recreatePipeline(VkRenderPass renderPass, VkExtent2D extent) {
    (void)renderPass; (void)extent;
    LOG_WARN("GrassRenderPipeline", "recreatePipeline called — use initialize() after resize");
}

void GrassRenderPipeline::createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                                         VkDescriptorSetLayout uboLayout) {
    auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("grass.vert.spv"));
    auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("grass.frag.spv"));

    VkShaderModule vertModule, fragModule;
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = vertCode.size();
    smInfo.pCode    = reinterpret_cast<const uint32_t*>(vertCode.data());
    vkCreateShaderModule(m_device, &smInfo, nullptr, &vertModule);
    smInfo.codeSize = fragCode.size();
    smInfo.pCode    = reinterpret_cast<const uint32_t*>(fragCode.data());
    vkCreateShaderModule(m_device, &smInfo, nullptr, &fragModule);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    // Instance-rate binding for GrassInstanceData (no vertex buffer — gl_VertexIndex builds blades).
    VkVertexInputBindingDescription binding = GrassInstanceData::getBindingDescription();
    auto attrs = GrassInstanceData::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;    // blades are double-sided
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;      // cutout: kept fragments write depth
    depthStencil.depthCompareOp   = Graphics::DepthConvention::sceneDepthCompareOp();

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable = VK_FALSE;           // alpha-tested, not blended → no OIT cost

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttach;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(GrassPush);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &uboLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create GrassRenderPipeline layout");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisample;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create GrassRenderPipeline");
    }

    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

// Density LOD tiers, as fractions of the near-field blade count.
//
// THE FALLOFF HAS TO BE STEEP, and the reason is worth stating because a gentle-looking curve is
// catastrophic here. Grass voxels grow as the AREA of the disc, so the number of instances in an
// annulus grows linearly with r. A band's vertex cost is (blades) x (chunks in band), so holding
// cost flat across bands needs density to fall roughly as 1/r -- and to keep the TOTAL bounded as
// the radius grows it must fall faster still.
//
// Worked, with 18 verts/blade and a worst-case 1024 grass-topped voxels per surface chunk:
//   28 blades @ r=48, no LOD (the old default) ......  3.6M verts/frame
//   98 blades @ r=320, bands 100/55/30/15% .......... 143.4M  <- 40x the baseline; the outermost
//                                                        band alone was 51.9M. A curve that looks
//                                                        like a reasonable falloff still loses to
//                                                        r^2 growth.
//   70 blades @ r=192, the bands below .............. 22.8M, spread 3.3/3.9/4.7/4.6/6.4M
// The last row is the shipped config: no single band dominates, which is the property to preserve
// when retuning. Recompute before changing `radius` or `bladesPerVoxel` -- the total is quadratic
// in the radius and it is very easy to make this 10x more expensive by eye.
uint32_t GrassRenderPipeline::bladesForDistance(uint32_t bladesPerVoxel, float dist, float radius) {
    if (bladesPerVoxel == 0) return 0;
    const float r = (radius > 1e-3f) ? radius : 1e-3f;
    const float t = dist / r;   // 0 at the camera, 1 at the radius edge

    // ⚠️ THIS IS A CONSERVATIVE UPPER BOUND, NOT THE DENSITY.
    // The real density falloff now lives PER BLADE in grass.vert and is continuous in the blade's
    // own world distance. It had to move: deciding density per-chunk made two adjacent chunks in
    // different bands draw different densities, and the boundary showed as a hard seam through
    // open field ("disjointed grass", user 2026-08-01). Nothing per-chunk may influence how a
    // blade looks, or the seam comes straight back.
    //
    // What remains here is only "how many blades is it possible for this chunk to need", so the
    // draw can still be shortened for vertex cost. It MUST NOT be tighter than the shader's own
    // test anywhere in the chunk, so `dist` is the chunk's NEAREST point (the caller passes
    // centre-distance minus the chunk half-diagonal) and the curve below is the shader's
    // densityFrac rounded UP at every distance.
    // MUST MIRROR grass.vert's densityFrac = 1/(1 + kDensityFalloff*t^2), with headroom.
    // If these two drift apart the CPU bound can clip blades the shader wanted and the chunk seam
    // returns — so they are written as the same expression with the same constant.
    const float u = std::max(0.0f, t - kDensityNearBand) / (1.0f - kDensityNearBand);
    float frac = 1.0f / (1.0f + kDensityFalloff * u * u);
    frac *= 1.20f;                                     // headroom for the soft-edge fade band
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 1.0f / 18.0f) frac = 1.0f / 18.0f;

    // Round to WHOLE clumps: grass.vert assigns clumps as `blade / kBladesPerClump`, so a count
    // that splits a clump would draw a partial tuft (some blades of a hashed clump present, the
    // rest missing) — it reads as a torn tuft, not a sparser meadow.
    const uint32_t maxClumps = (bladesPerVoxel + kBladesPerClump - 1) / kBladesPerClump;
    uint32_t clumps = static_cast<uint32_t>(static_cast<float>(maxClumps) * frac + 0.5f);
    if (clumps < 1) clumps = 1;                 // never drop a chunk to bare ground
    if (clumps > maxClumps) clumps = maxClumps;
    return std::min(clumps * kBladesPerClump, bladesPerVoxel);
}

float GrassRenderPipeline::widthCompensation(uint32_t bladesDrawn, uint32_t bladesPerVoxel) {
    if (bladesDrawn == 0 || bladesPerVoxel == 0) return 1.0f;
    const float frac = static_cast<float>(bladesDrawn) / static_cast<float>(bladesPerVoxel);
    if (frac >= 1.0f) return 1.0f;
    // Area conservation: n blades of width w cover ~n*w, so w scales as 1/frac to hold coverage.
    // sqrt() deliberately UNDER-compensates — full 1/frac at the far tier would make individual
    // blades read as fat ribbons against the horizon. Capped so a thin blade never becomes a slab.
    return std::min(1.0f / std::sqrt(frac), 2.6f);
}

bool GrassRenderPipeline::initializeShadow(VkRenderPass shadowRenderPass, VkExtent2D shadowExtent) {
    if (m_device == VK_NULL_HANDLE || m_pipelineLayout == VK_NULL_HANDLE) {
        LOG_ERROR("GrassRenderPipeline", "initializeShadow called before initialize()");
        return false;
    }
    try {
        auto vertCode = readShaderFile(Core::AssetManager::instance().resolveShader("grass_shadow.vert.spv"));
        auto fragCode = readShaderFile(Core::AssetManager::instance().resolveShader("grass_shadow.frag.spv"));

        VkShaderModule vertModule, fragModule;
        VkShaderModuleCreateInfo smInfo{};
        smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smInfo.codeSize = vertCode.size();
        smInfo.pCode    = reinterpret_cast<const uint32_t*>(vertCode.data());
        vkCreateShaderModule(m_device, &smInfo, nullptr, &vertModule);
        smInfo.codeSize = fragCode.size();
        smInfo.pCode    = reinterpret_cast<const uint32_t*>(fragCode.data());
        vkCreateShaderModule(m_device, &smInfo, nullptr, &fragModule);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName  = "main";

        VkVertexInputBindingDescription binding = GrassInstanceData::getBindingDescription();
        auto attrs = GrassInstanceData::getAttributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount   = 1;
        vertexInput.pVertexBindingDescriptions      = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions    = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(shadowExtent.width),
                            static_cast<float>(shadowExtent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, shadowExtent};
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports    = &viewport;
        viewportState.scissorCount  = 1;
        viewportState.pScissors     = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth   = 1.0f;
        rasterizer.cullMode    = VK_CULL_MODE_NONE;   // double-sided blades cast from any sun angle
        rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        // FORWARD-Z: the shadow pass clears to 1.0 and compares LESS (ShadowMap::createPipeline),
        // even though the SCENE pass is reverse-Z. Using the scene op here would silently
        // reject every fragment — the exact bug that kept foliage out of the shadow map.
        depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendStateCreateInfo colorBlend{};   // depth-only: no color attachments
        colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 0;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = 2;
        pipelineInfo.pStages             = stages;
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisample;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlend;
        pipelineInfo.layout              = m_pipelineLayout;   // same set layout + push range
        pipelineInfo.renderPass          = shadowRenderPass;
        pipelineInfo.subpass             = 0;
        VkResult res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                 nullptr, &m_shadowPipeline);
        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
        if (res != VK_SUCCESS) throw std::runtime_error("failed to create grass shadow pipeline");
    } catch (const std::exception& e) {
        LOG_ERROR("GrassRenderPipeline", "Shadow pipeline init failed ({}); grass casts no shadows",
                  e.what());
        m_shadowPipeline = VK_NULL_HANDLE;
        return false;
    }
    LOG_INFO("GrassRenderPipeline", "Shadow caster initialized (grass casts shadows)");
    return true;
}

void GrassRenderPipeline::renderShadow(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                       const std::vector<ChunkDraw>& chunks) {
    if (!s_castShadows) return;
    if (!m_params.enabled || m_shadowPipeline == VK_NULL_HANDLE || chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                            &uboSet, 0, nullptr);

    const uint32_t vertsPerBlade = 18;   // must match grass.vert (SEGMENTS*6)

    GrassPush pc{};
    pc.bladeHeight    = m_params.bladeHeight;
    pc.windStrength   = m_params.windStrength;
    pc.radius         = m_params.radius;
    pc.fadeRange      = m_params.fadeRange;
    pc.growDuration   = m_params.growDuration;
    pc.bladesPerVoxel = m_params.bladesPerVoxel;
    pc.windDirX       = m_params.wind.dir.x;
    pc.windDirZ       = m_params.wind.dir.y;
    pc.windBase       = m_params.wind.base;
    pc.gustAmp        = m_params.wind.gustAmp;
    pc.gustScale      = m_params.wind.gustScale;
    pc.gustSpeed      = m_params.wind.gustSpeed;
    pc.bladeStyle     = m_params.bladeStyle;
    pc.pushStrength   = m_params.pushStrength;
    pc.shadowWidthScale = s_shadowWidthScale;   // shadow pass widens the proxy only

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        pc.chunkBaseOffset = glm::vec3(glm::dvec3(c.origin) - glm::dvec3(m_cameraWorld));
        pc.absBaseX = c.origin.x;
        pc.absBaseY = c.origin.y;
        pc.absBaseZ = c.origin.z;

        // SAME density-LOD tier as the visible pass: a blade that isn't drawn must not cast,
        // and a blade that IS drawn must cast from the identical clump math (the shader
        // re-rolls survivors if bladesPerVoxel changes — see render()).
        const uint32_t bladesDrawn = bladesForDistance(m_params.bladesPerVoxel, c.centerDist,
                                                       m_params.radius);
        if (bladesDrawn == 0) continue;
        // widthCompensation is retained for the CPU-side conservative path; the SHADER
        // reads widthScale purely as the runtime blade-width knob (Params::bladeWidthScale).
        pc.widthScale = m_params.bladeWidthScale;

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPush), &pc);
        VkDeviceSize offset = c.bindOffset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &offset);
        vkCmdDraw(cmd, vertsPerBlade * bladesDrawn, c.count, 0, 0);
    }
}

void GrassRenderPipeline::render(VkCommandBuffer cmd, VkDescriptorSet uboSet,
                                 const std::vector<ChunkDraw>& chunks) {
    if (!m_params.enabled || m_pipeline == VK_NULL_HANDLE || chunks.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &uboSet, 0, nullptr);

    const uint32_t vertsPerBlade = 18;  // 3 stacked segments/blade (must match SEGMENTS*6 in grass.vert)

    GrassPush pc{};
    pc.bladeHeight    = m_params.bladeHeight;
    pc.windStrength   = m_params.windStrength;
    pc.radius         = m_params.radius;
    pc.fadeRange      = m_params.fadeRange;
    pc.growDuration   = m_params.growDuration;
    pc.bladesPerVoxel = m_params.bladesPerVoxel;
    pc.windDirX       = m_params.wind.dir.x;
    pc.windDirZ       = m_params.wind.dir.y;
    pc.windBase       = m_params.wind.base;
    pc.gustAmp        = m_params.wind.gustAmp;
    pc.gustScale      = m_params.wind.gustScale;
    pc.gustSpeed      = m_params.wind.gustSpeed;
    pc.bladeStyle     = m_params.bladeStyle;
    pc.pushStrength   = m_params.pushStrength;
    pc.shadowWidthScale = s_shadowWidthScale;   // shadow pass widens the proxy only

    for (const auto& c : chunks) {
        if (c.buffer == VK_NULL_HANDLE || c.count == 0) continue;
        // camera-relative position; exact ABSOLUTE origin for the hash seeds
        pc.chunkBaseOffset = glm::vec3(glm::dvec3(c.origin) - glm::dvec3(m_cameraWorld));
        pc.absBaseX = c.origin.x;
        pc.absBaseY = c.origin.y;
        pc.absBaseZ = c.origin.z;

        // Density LOD. NOTE pc.bladesPerVoxel stays at the FULL count — the shader's clump math
        // must not change with the tier, or survivors re-roll (see bladesForDistance).
        const uint32_t bladesDrawn = bladesForDistance(m_params.bladesPerVoxel, c.centerDist,
                                                       m_params.radius);
        if (bladesDrawn == 0) continue;
        // widthCompensation is retained for the CPU-side conservative path; the SHADER
        // reads widthScale purely as the runtime blade-width knob (Params::bladeWidthScale).
        pc.widthScale = m_params.bladeWidthScale;

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPush), &pc);
        VkDeviceSize offset = c.bindOffset;  // 4.3 A2: arena span offset (0 legacy)
        vkCmdBindVertexBuffers(cmd, 0, 1, &c.buffer, &offset);
        vkCmdDraw(cmd, vertsPerBlade * bladesDrawn, c.count, 0, 0);
    }
}

} // namespace Graphics
} // namespace Phyxel
