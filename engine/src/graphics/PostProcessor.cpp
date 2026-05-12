#include "graphics/PostProcessor.h"
#include "vulkan/VulkanDevice.h"
#include "utils/FileUtils.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <array>
#include <stdexcept>

namespace Phyxel {
namespace Graphics {

// Helper to create shader module
static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

PostProcessor::PostProcessor(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height)
    : device(device), width(width), height(height) {
}

PostProcessor::~PostProcessor() {
    cleanup();
}

bool PostProcessor::initialize() {
    if (!createOffscreenResources()) return false;
    if (!createBloomResources(width, height)) return false;
    if (!createSceneRenderPass()) return false;
    if (!createBlurRenderPass()) return false;
    if (!createSSAORenderPass()) return false;
    if (!createOITRenderPass()) return false;  // OIT render pass created before framebuffers
    if (!createSceneFramebuffer()) return false;
    if (!createOITResources()) return false;  // OIT framebuffer (uses oitRenderPass + depthImageView from scene)
    if (!createBlurFramebuffers(width, height)) return false;
    if (!createPostProcessRenderPass()) return false;
    
    if (!createDescriptorSetLayout()) return false;
    if (!createBlurDescriptorSetLayout()) return false;
    
    if (!createPipeline()) return false;
    if (!createBlurPipeline()) return false;
    
    if (!createDescriptorPool()) return false;
    if (!createDescriptorSet()) return false;
    if (!createBlurDescriptorSets()) return false;

    // SSAO — after descriptor pool is created
    if (!createSSAODescriptors()) return false;
    if (!createSSAOPipeline()) return false;
    if (!createSSAOBlurDescriptors()) return false;
    if (!createSSAOBlurPipeline()) return false;
    if (!createSSAOResources()) return false;
    if (!createReflectionResources()) return false;
    
    updateDescriptorSet();
    updateBlurDescriptorSets();
    
    return true;
}

void PostProcessor::cleanup() {
    VkDevice vkDevice = device->getDevice();

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (blurDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, blurDescriptorSetLayout, nullptr);
        blurDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (blurPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, blurPipeline, nullptr);
        blurPipeline = VK_NULL_HANDLE;
    }
    if (blurPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, blurPipelineLayout, nullptr);
        blurPipelineLayout = VK_NULL_HANDLE;
    }
    if (postProcessRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkDevice, postProcessRenderPass, nullptr);
        postProcessRenderPass = VK_NULL_HANDLE;
    }
    if (sceneFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(vkDevice, sceneFramebuffer, nullptr);
        sceneFramebuffer = VK_NULL_HANDLE;
    }
    if (sceneRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkDevice, sceneRenderPass, nullptr);
        sceneRenderPass = VK_NULL_HANDLE;
    }
    if (blurRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkDevice, blurRenderPass, nullptr);
        blurRenderPass = VK_NULL_HANDLE;
    }
    
    // Cleanup Bloom Resources
    for (size_t i = 0; i < 2; i++) {
        if (blurFramebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vkDevice, blurFramebuffers[i], nullptr);
            blurFramebuffers[i] = VK_NULL_HANDLE;
        }
        if (blurImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, blurImageViews[i], nullptr);
            blurImageViews[i] = VK_NULL_HANDLE;
        }
        if (blurImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, blurImages[i], nullptr);
            blurImages[i] = VK_NULL_HANDLE;
        }
        if (blurImageMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, blurImageMemory[i], nullptr);
            blurImageMemory[i] = VK_NULL_HANDLE;
        }
    }

    if (offscreenSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vkDevice, offscreenSampler, nullptr);
        offscreenSampler = VK_NULL_HANDLE;
    }
    if (offscreenImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vkDevice, offscreenImageView, nullptr);
        offscreenImageView = VK_NULL_HANDLE;
    }
    if (offscreenImage != VK_NULL_HANDLE) {
        vkDestroyImage(vkDevice, offscreenImage, nullptr);
        offscreenImage = VK_NULL_HANDLE;
    }
    if (offscreenImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, offscreenImageMemory, nullptr);
        offscreenImageMemory = VK_NULL_HANDLE;
    }
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vkDevice, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(vkDevice, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }

    cleanupSSAOResources();
    cleanupOITResources();
    cleanupReflectionResources();
}

void PostProcessor::resize(uint32_t newWidth, uint32_t newHeight) {
    // Skip resize with zero dimensions (window minimized)
    if (newWidth == 0 || newHeight == 0) {
        return;
    }

    width = newWidth;
    height = newHeight;
    
    // Cleanup size-dependent resources
    VkDevice vkDevice = device->getDevice();
    
    vkDestroyFramebuffer(vkDevice, sceneFramebuffer, nullptr);
    vkDestroyImageView(vkDevice, offscreenImageView, nullptr);
    vkDestroyImage(vkDevice, offscreenImage, nullptr);
    vkFreeMemory(vkDevice, offscreenImageMemory, nullptr);
    vkDestroyImageView(vkDevice, depthImageView, nullptr);
    vkDestroyImage(vkDevice, depthImage, nullptr);
    vkFreeMemory(vkDevice, depthImageMemory, nullptr);
    
    // Cleanup Bloom Resources
    for (size_t i = 0; i < 2; i++) {
        vkDestroyFramebuffer(vkDevice, blurFramebuffers[i], nullptr);
        vkDestroyImageView(vkDevice, blurImageViews[i], nullptr);
        vkDestroyImage(vkDevice, blurImages[i], nullptr);
        vkFreeMemory(vkDevice, blurImageMemory[i], nullptr);
    }
    
    // Recreate
    createOffscreenResources();
    createBloomResources(width, height);
    createSceneFramebuffer();
    createBlurFramebuffers(width, height);
    // Recreate SSAO size-dependent resources
    cleanupSSAOResources();
    createSSAOResources();
    cleanupOITResources();
    createOITRenderPass();   // cleanupOITResources destroys oitRenderPass; must recreate before createOITResources
    createOITResources();
    cleanupReflectionResources();
    createReflectionResources();
    updateDescriptorSet();
    updateBlurDescriptorSets();
}

bool PostProcessor::createOffscreenResources() {
    // Color Attachment (HDR)
    VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    device->createImage(width, height, colorFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, offscreenImage, offscreenImageMemory);
        
    offscreenImageView = device->createImageView(offscreenImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);

    // Depth Attachment (SAMPLED_BIT so SSAO pass can read it)
    VkFormat depthFormat = device->findDepthFormat();
    device->createImage(width, height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
        
    depthImageView = device->createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Sampler
    if (offscreenSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        if (vkCreateSampler(device->getDevice(), &samplerInfo, nullptr, &offscreenSampler) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

bool PostProcessor::createSceneRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = device->findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // Stored for SSAO pass
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    
    // Dependencies
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &sceneRenderPass) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createSceneFramebuffer() {
    std::array<VkImageView, 2> attachments = {
        offscreenImageView,
        depthImageView
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = sceneRenderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr, &sceneFramebuffer) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createPostProcessRenderPass() {
    // This render pass targets the swapchain
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = device->getSwapChainImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // We still need a depth attachment because VulkanDevice::createFramebuffers attaches one
    // But we won't use it for depth testing in the post-process quad
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = device->findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &postProcessRenderPass) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bloomSamplerLayoutBinding{};
    bloomSamplerLayoutBinding.binding = 1;
    bloomSamplerLayoutBinding.descriptorCount = 1;
    bloomSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bloomSamplerLayoutBinding.pImmutableSamplers = nullptr;
    bloomSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoLayoutBinding{};
    ssaoLayoutBinding.binding = 2;
    ssaoLayoutBinding.descriptorCount = 1;
    ssaoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoLayoutBinding.pImmutableSamplers = nullptr;
    ssaoLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding oitAccumLayoutBinding{};
    oitAccumLayoutBinding.binding = 3;
    oitAccumLayoutBinding.descriptorCount = 1;
    oitAccumLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    oitAccumLayoutBinding.pImmutableSamplers = nullptr;
    oitAccumLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding oitRevealLayoutBinding{};
    oitRevealLayoutBinding.binding = 4;
    oitRevealLayoutBinding.descriptorCount = 1;
    oitRevealLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    oitRevealLayoutBinding.pImmutableSamplers = nullptr;
    oitRevealLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
        samplerLayoutBinding, bloomSamplerLayoutBinding, ssaoLayoutBinding,
        oitAccumLayoutBinding, oitRevealLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createPipeline() {
    auto vertShaderCode = Utils::readFile(Core::AssetManager::instance().resolveShader("post_process.vert.spv"));
    auto fragShaderCode = Utils::readFile(Core::AssetManager::instance().resolveShader("post_process.frag.spv"));

    VkShaderModule vertShaderModule = createShaderModule(device->getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(device->getDevice(), fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }

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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = postProcessRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        return false;
    }

    vkDestroyShaderModule(device->getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragShaderModule, nullptr);

    return true;
}

bool PostProcessor::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 10;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 10;

    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        return false;
    }
    return true;
}

void PostProcessor::updateDescriptorSet() {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = offscreenImageView;
    imageInfo.sampler = offscreenSampler;

    VkDescriptorImageInfo bloomImageInfo{};
    bloomImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Use the first blur image as the result (assuming even number of passes ending in 0)
    bloomImageInfo.imageView = blurImageViews[0];
    bloomImageInfo.sampler = offscreenSampler;

    // SSAO blurred result (binding 2) — use a white 1x1 fallback if not yet created
    VkDescriptorImageInfo ssaoImageInfo{};
    ssaoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoImageInfo.imageView = (ssaoBlurImageView != VK_NULL_HANDLE) ? ssaoBlurImageView : blurImageViews[0];
    ssaoImageInfo.sampler = (ssaoSampler != VK_NULL_HANDLE) ? ssaoSampler : offscreenSampler;

    // OIT accum (binding 3) and reveal (binding 4)
    VkDescriptorImageInfo oitAccumInfo{};
    oitAccumInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    oitAccumInfo.imageView = (oitAccumImageView != VK_NULL_HANDLE) ? oitAccumImageView : blurImageViews[0];
    oitAccumInfo.sampler   = (oitSampler != VK_NULL_HANDLE) ? oitSampler : offscreenSampler;

    VkDescriptorImageInfo oitRevealInfo{};
    oitRevealInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    oitRevealInfo.imageView = (oitRevealImageView != VK_NULL_HANDLE) ? oitRevealImageView : blurImageViews[0];
    oitRevealInfo.sampler   = (oitSampler != VK_NULL_HANDLE) ? oitSampler : offscreenSampler;

    std::array<VkWriteDescriptorSet, 5> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pImageInfo = &imageInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &bloomImageInfo;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &ssaoImageInfo;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = descriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pImageInfo = &oitAccumInfo;

    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = descriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pImageInfo = &oitRevealInfo;

    vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void PostProcessor::beginSceneRenderPass(VkCommandBuffer commandBuffer) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = sceneRenderPass;
    renderPassInfo.framebuffer = sceneFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void PostProcessor::endSceneRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

void PostProcessor::beginPostProcessRenderPass(VkCommandBuffer commandBuffer, VkFramebuffer swapchainFramebuffer) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = postProcessRenderPass;
    renderPassInfo.framebuffer = swapchainFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {width, height};

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void PostProcessor::drawQuad(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

void PostProcessor::endPostProcessRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

void PostProcessor::draw(VkCommandBuffer commandBuffer, VkFramebuffer swapchainFramebuffer) {
    renderBloom(commandBuffer);
    beginPostProcessRenderPass(commandBuffer, swapchainFramebuffer);
    drawQuad(commandBuffer);
    endPostProcessRenderPass(commandBuffer);
}

} // namespace Graphics
} // namespace Phyxel

// =============================================================================
// SSAO Implementation (continues after main Phyxel namespace)
// =============================================================================

namespace Phyxel {
namespace Graphics {

static void insertImageMemoryBarrier(
    VkCommandBuffer cmdbuffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldImageLayout,
    VkImageLayout newImageLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcAccessMask = srcAccessMask;
    imageMemoryBarrier.dstAccessMask = dstAccessMask;
    imageMemoryBarrier.oldLayout = oldImageLayout;
    imageMemoryBarrier.newLayout = newImageLayout;
    imageMemoryBarrier.image = image;
    imageMemoryBarrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(
        cmdbuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &imageMemoryBarrier);
}

bool PostProcessor::createBloomResources(uint32_t width, uint32_t height) {
    for (int i = 0; i < 2; i++) {
        device->createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blurImages[i], blurImageMemory[i]);
        blurImageViews[i] = device->createImageView(blurImages[i], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    return true;
}

bool PostProcessor::createBlurRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &blurRenderPass) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createBlurFramebuffers(uint32_t width, uint32_t height) {
    for (int i = 0; i < 2; i++) {
        VkImageView attachments[] = {
            blurImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = blurRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = width;
        framebufferInfo.height = height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr, &blurFramebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool PostProcessor::createBlurDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &blurDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }
    return true;
}

bool PostProcessor::createBlurPipeline() {
    auto vertShaderCode = Utils::readFile(Core::AssetManager::instance().resolveShader("post_process.vert.spv"));
    auto fragShaderCode = Utils::readFile(Core::AssetManager::instance().resolveShader("blur.frag.spv"));

    VkShaderModule vertShaderModule = createShaderModule(device->getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(device->getDevice(), fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(int);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &blurDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &blurPipelineLayout) != VK_SUCCESS) {
        return false;
    }

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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = blurPipelineLayout;
    pipelineInfo.renderPass = blurRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &blurPipeline) != VK_SUCCESS) {
        return false;
    }

    vkDestroyShaderModule(device->getDevice(), vertShaderModule, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragShaderModule, nullptr);

    return true;
}

bool PostProcessor::createBlurDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(2, blurDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, blurDescriptorSets.data()) != VK_SUCCESS) {
        return false;
    }
    return true;
}

void PostProcessor::updateBlurDescriptorSets() {
    for (int i = 0; i < 2; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Set 0 reads from Blur 1, Set 1 reads from Blur 0
        imageInfo.imageView = blurImageViews[(i + 1) % 2];
        imageInfo.sampler = offscreenSampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = blurDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void PostProcessor::renderBloom(VkCommandBuffer commandBuffer) {
    // 1. Blit offscreen to blurImages[0]
    insertImageMemoryBarrier(commandBuffer, blurImages[0], 
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
        
    insertImageMemoryBarrier(commandBuffer, offscreenImage,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
        
    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    
    vkCmdBlitImage(commandBuffer, offscreenImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blurImages[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    
    insertImageMemoryBarrier(commandBuffer, offscreenImage,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
        
    insertImageMemoryBarrier(commandBuffer, blurImages[0],
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
        
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
    
    bool horizontal = true;
    int amount = 10;
    
    for (int i = 0; i < amount; i++) {
        int inputIndex = horizontal ? 0 : 1; // Read from 0, Write to 1 (Horizontal)
        int outputIndex = horizontal ? 1 : 0;
        
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = blurRenderPass;
        renderPassInfo.framebuffer = blurFramebuffers[outputIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {width, height};
        
        VkClearValue clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)width;
        viewport.height = (float)height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {width, height};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        
        // Bind descriptor set that reads from inputIndex
        // Set 0 reads 1, Set 1 reads 0.
        // If inputIndex is 0, we want Set 1.
        // If inputIndex is 1, we want Set 0.
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout, 0, 1, &blurDescriptorSets[(inputIndex + 1) % 2], 0, nullptr);
        
        int h = horizontal ? 1 : 0;
        vkCmdPushConstants(commandBuffer, blurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &h);
        
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        
        vkCmdEndRenderPass(commandBuffer);
        
        horizontal = !horizontal;
    }
}

// =============================================================================
// SSAO Resources / Pipeline / Render
// =============================================================================

bool PostProcessor::createSSAOResources() {
    VkDevice vkDevice = device->getDevice();

    // R8_UNORM occlusion image (SSAO output)
    device->createImage(width, height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ssaoImage, ssaoImageMemory);
    ssaoImageView = device->createImageView(ssaoImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // R8_UNORM blur output image
    device->createImage(width, height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ssaoBlurImage, ssaoBlurImageMemory);
    ssaoBlurImageView = device->createImageView(ssaoBlurImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // Nearest sampler for SSAO and depth
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f; si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.maxLod = 1.0f;
    if (vkCreateSampler(vkDevice, &si, nullptr, &ssaoSampler) != VK_SUCCESS) return false;
    if (vkCreateSampler(vkDevice, &si, nullptr, &depthSampler) != VK_SUCCESS) return false;

    // Framebuffers
    auto makeFB = [&](VkImageView view, VkFramebuffer& fb) -> bool {
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = ssaoRenderPass;
        fbi.attachmentCount = 1; fbi.pAttachments = &view;
        fbi.width = width; fbi.height = height; fbi.layers = 1;
        return vkCreateFramebuffer(vkDevice, &fbi, nullptr, &fb) == VK_SUCCESS;
    };
    if (!makeFB(ssaoImageView, ssaoFramebuffer)) return false;
    if (!makeFB(ssaoBlurImageView, ssaoBlurFramebuffer)) return false;

    // Now that samplers exist, write the deferred SSAO descriptors
    updateSSAODescriptors();
    return true;
}

bool PostProcessor::createSSAORenderPass() {
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0] = { VK_SUBPASS_EXTERNAL, 0,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_DEPENDENCY_BY_REGION_BIT };
    deps[1] = { 0, VK_SUBPASS_EXTERNAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT };

    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1; rpi.pAttachments = &att;
    rpi.subpassCount = 1;   rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2; rpi.pDependencies = deps.data();
    return vkCreateRenderPass(device->getDevice(), &rpi, nullptr, &ssaoRenderPass) == VK_SUCCESS;
}

static bool makeFullscreenPipeline(VkDevice vkDevice,
    VkRenderPass renderPass, VkDescriptorSetLayout descLayout,
    VkShaderModule vertMod, VkShaderModule fragMod,
    uint32_t pushConstantSize,
    VkPipelineLayout& outLayout, VkPipeline& outPipeline)
{
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = pushConstantSize;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1; pli.pSetLayouts = &descLayout;
    if (pushConstantSize > 0) { pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr; }
    if (vkCreatePipelineLayout(vkDevice, &pli, nullptr, &outLayout) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertMod, "main", nullptr };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragMod, "main", nullptr };

    VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1; vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cba;
    std::vector<VkDynamicState> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynState.dynamicStateCount = static_cast<uint32_t>(dyn.size()); dynState.pDynamicStates = dyn.data();

    VkGraphicsPipelineCreateInfo gpi{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpi.stageCount = 2; gpi.pStages = stages;
    gpi.pVertexInputState = &vi; gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vps; gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms; gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb; gpi.pDynamicState = &dynState;
    gpi.layout = outLayout; gpi.renderPass = renderPass;
    return vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, 1, &gpi, nullptr, &outPipeline) == VK_SUCCESS;
}

bool PostProcessor::createSSAOPipeline() {
    auto vertCode = Utils::readFile(Core::AssetManager::instance().resolveShader("post_process.vert.spv"));
    auto fragCode = Utils::readFile(Core::AssetManager::instance().resolveShader("ssao.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) { LOG_ERROR("PostProcessor", "Failed to load SSAO shaders"); return false; }
    VkShaderModule vertMod = createShaderModule(device->getDevice(), vertCode);
    VkShaderModule fragMod = createShaderModule(device->getDevice(), fragCode);
    // proj(64) + invProj(64) + noiseScale(8) + radius(4) + bias(4) = 144 bytes
    bool ok = makeFullscreenPipeline(device->getDevice(), ssaoRenderPass, ssaoDescriptorSetLayout,
        vertMod, fragMod, 144, ssaoPipelineLayout, ssaoPipeline);
    vkDestroyShaderModule(device->getDevice(), vertMod, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragMod, nullptr);
    return ok;
}

bool PostProcessor::createSSAOBlurPipeline() {
    auto vertCode = Utils::readFile(Core::AssetManager::instance().resolveShader("post_process.vert.spv"));
    auto fragCode = Utils::readFile(Core::AssetManager::instance().resolveShader("ssao_blur.frag.spv"));
    if (vertCode.empty() || fragCode.empty()) { LOG_ERROR("PostProcessor", "Failed to load SSAO blur shaders"); return false; }
    VkShaderModule vertMod = createShaderModule(device->getDevice(), vertCode);
    VkShaderModule fragMod = createShaderModule(device->getDevice(), fragCode);
    bool ok = makeFullscreenPipeline(device->getDevice(), ssaoRenderPass, ssaoBlurDescriptorSetLayout,
        vertMod, fragMod, 0, ssaoBlurPipelineLayout, ssaoBlurPipeline);
    vkDestroyShaderModule(device->getDevice(), vertMod, nullptr);
    vkDestroyShaderModule(device->getDevice(), fragMod, nullptr);
    return ok;
}

bool PostProcessor::createSSAODescriptors() {
    VkDevice vkDevice = device->getDevice();
    VkDescriptorSetLayoutBinding b{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo dli{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &b };
    if (vkCreateDescriptorSetLayout(vkDevice, &dli, nullptr, &ssaoDescriptorSetLayout) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &ssaoDescriptorSetLayout };
    if (vkAllocateDescriptorSets(vkDevice, &alloc, &ssaoDescriptorSet) != VK_SUCCESS) return false;
    // Descriptor write deferred to updateSSAODescriptors() after samplers are created
    return true;
}

bool PostProcessor::createSSAOBlurDescriptors() {
    VkDevice vkDevice = device->getDevice();
    VkDescriptorSetLayoutBinding b{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo dli{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &b };
    if (vkCreateDescriptorSetLayout(vkDevice, &dli, nullptr, &ssaoBlurDescriptorSetLayout) != VK_SUCCESS) return false;

    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &ssaoBlurDescriptorSetLayout };
    if (vkAllocateDescriptorSets(vkDevice, &alloc, &ssaoBlurDescriptorSet) != VK_SUCCESS) return false;
    // Descriptor write deferred to updateSSAODescriptors() after samplers are created
    return true;
}

void PostProcessor::updateSSAODescriptors() {
    VkDevice vkDevice = device->getDevice();
    if (ssaoDescriptorSet != VK_NULL_HANDLE && depthSampler != VK_NULL_HANDLE && depthImageView != VK_NULL_HANDLE) {
        VkDescriptorImageInfo di{ depthSampler, depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di, nullptr, nullptr };
        vkUpdateDescriptorSets(vkDevice, 1, &w, 0, nullptr);
    }
    if (ssaoBlurDescriptorSet != VK_NULL_HANDLE && ssaoSampler != VK_NULL_HANDLE && ssaoImageView != VK_NULL_HANDLE) {
        VkDescriptorImageInfo di{ ssaoSampler, ssaoImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoBlurDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di, nullptr, nullptr };
        vkUpdateDescriptorSets(vkDevice, 1, &w, 0, nullptr);
    }
}

void PostProcessor::cleanupSSAOResources() {
    VkDevice vkDevice = device->getDevice();
    auto destroy = [&](auto& h, auto fn) { if (h != VK_NULL_HANDLE) { fn(vkDevice, h, nullptr); h = VK_NULL_HANDLE; } };
    destroy(ssaoBlurPipeline,          vkDestroyPipeline);
    destroy(ssaoBlurPipelineLayout,    vkDestroyPipelineLayout);
    destroy(ssaoBlurDescriptorSetLayout, vkDestroyDescriptorSetLayout);
    destroy(ssaoPipeline,              vkDestroyPipeline);
    destroy(ssaoPipelineLayout,        vkDestroyPipelineLayout);
    destroy(ssaoDescriptorSetLayout,   vkDestroyDescriptorSetLayout);
    destroy(ssaoRenderPass,            vkDestroyRenderPass);
    destroy(ssaoBlurFramebuffer,       vkDestroyFramebuffer);
    destroy(ssaoFramebuffer,           vkDestroyFramebuffer);
    destroy(ssaoBlurImageView,         vkDestroyImageView);
    if (ssaoBlurImage != VK_NULL_HANDLE) { vkDestroyImage(vkDevice, ssaoBlurImage, nullptr); ssaoBlurImage = VK_NULL_HANDLE; }
    if (ssaoBlurImageMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, ssaoBlurImageMemory, nullptr); ssaoBlurImageMemory = VK_NULL_HANDLE; }
    destroy(ssaoImageView,             vkDestroyImageView);
    if (ssaoImage != VK_NULL_HANDLE) { vkDestroyImage(vkDevice, ssaoImage, nullptr); ssaoImage = VK_NULL_HANDLE; }
    if (ssaoImageMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, ssaoImageMemory, nullptr); ssaoImageMemory = VK_NULL_HANDLE; }
    destroy(ssaoSampler,               vkDestroySampler);
    destroy(depthSampler,              vkDestroySampler);
}

void PostProcessor::renderSSAO(VkCommandBuffer commandBuffer, const glm::mat4& proj) {
    if (!ssaoEnabled || ssaoPipeline == VK_NULL_HANDLE) return;

    struct SSAOPushConst {
        glm::mat4 proj;
        glm::mat4 invProj;
        glm::vec2 noiseScale;
        float radius;
        float bias;
    } pc;
    pc.proj = proj;
    pc.invProj = glm::inverse(proj);
    pc.noiseScale = glm::vec2(float(width) / 4.0f, float(height) / 4.0f);
    pc.radius = 1.5f;
    pc.bias   = 0.025f;

    VkClearValue clearVal; clearVal.color = {{1.0f, 0.0f, 0.0f, 0.0f}};
    VkViewport vp{0, 0, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D sc{{0,0},{width,height}};

    auto runPass = [&](VkFramebuffer fb, VkPipeline pip, VkPipelineLayout layout,
                        VkDescriptorSet ds, bool pushPC) {
        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = ssaoRenderPass;
        rpi.framebuffer = fb;
        rpi.renderArea.extent = {width, height};
        rpi.clearValueCount = 1; rpi.pClearValues = &clearVal;
        vkCmdBeginRenderPass(commandBuffer, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pip);
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &ds, 0, nullptr);
        if (pushPC) vkCmdPushConstants(commandBuffer, layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    };

    // Pass 1: SSAO → ssaoImage
    runPass(ssaoFramebuffer, ssaoPipeline, ssaoPipelineLayout, ssaoDescriptorSet, true);
    // Pass 2: blur → ssaoBlurImage (which post_process.frag samples)
    runPass(ssaoBlurFramebuffer, ssaoBlurPipeline, ssaoBlurPipelineLayout, ssaoBlurDescriptorSet, false);
}

// =============================================================================
// OIT (Weighted Blended Order-Independent Transparency) Resources
// =============================================================================

bool PostProcessor::createOITRenderPass() {
    // Attachment 0: accum (RGBA16F), cleared to (0,0,0,0), additive blend
    VkAttachmentDescription accumAtt{};
    accumAtt.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    accumAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    accumAtt.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    accumAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    accumAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    accumAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // initialLayout=UNDEFINED: we always CLEAR these attachments at render pass start
    // (loadOp=CLEAR), so prior content is irrelevant. Using UNDEFINED avoids any
    // layout-mismatch validation error and lets the driver do the transition correctly
    // from whatever state the image is in (UNDEFINED on frame 0, SHADER_READ_ONLY on N+1).
    // finalLayout=SHADER_READ_ONLY_OPTIMAL so post_process.frag can sample them directly.
    accumAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    accumAtt.finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 1: reveal (R8_UNORM), cleared to 1.0, multiplicative blend
    VkAttachmentDescription revealAtt = accumAtt;
    revealAtt.format = VK_FORMAT_R8_UNORM;

    // Attachment 2: depth (read-only from scene pass)
    VkAttachmentDescription depthAtt{};
    depthAtt.format = device->findDepthFormat();  // must match createOffscreenResources depth image format
    depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;   // keep scene depth
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthAtt.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference accumRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference revealRef{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

    std::array<VkAttachmentReference, 2> colorRefs = { accumRef, revealRef };

    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 2; sp.pColorAttachments = colorRefs.data();
    sp.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0] = { VK_SUBPASS_EXTERNAL, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        VK_DEPENDENCY_BY_REGION_BIT };
    deps[1] = { 0, VK_SUBPASS_EXTERNAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT };

    std::array<VkAttachmentDescription, 3> atts = { accumAtt, revealAtt, depthAtt };
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 3; rpi.pAttachments = atts.data();
    rpi.subpassCount = 1;    rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2; rpi.pDependencies = deps.data();
    return vkCreateRenderPass(device->getDevice(), &rpi, nullptr, &oitRenderPass) == VK_SUCCESS;
}

bool PostProcessor::createOITResources() {
    VkDevice vkDevice = device->getDevice();

    // Accum image: RGBA16F
    device->createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, oitAccumImage, oitAccumImageMemory);
    oitAccumImageView = device->createImageView(oitAccumImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Reveal image: R8_UNORM
    device->createImage(width, height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, oitRevealImage, oitRevealImageMemory);
    oitRevealImageView = device->createImageView(oitRevealImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // Nearest sampler
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f; si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.maxLod = 1.0f;
    if (vkCreateSampler(vkDevice, &si, nullptr, &oitSampler) != VK_SUCCESS) return false;

    // Framebuffer: [oitAccumView, oitRevealView, depthImageView]
    std::array<VkImageView, 3> views = { oitAccumImageView, oitRevealImageView, depthImageView };
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = oitRenderPass;
    fbi.attachmentCount = 3; fbi.pAttachments = views.data();
    fbi.width = width; fbi.height = height; fbi.layers = 1;
    if (vkCreateFramebuffer(vkDevice, &fbi, nullptr, &oitFramebuffer) != VK_SUCCESS) {
        LOG_ERROR("PostProcessor", "createOITResources: vkCreateFramebuffer FAILED");
        return false;
    }
    // No seed barrier needed: initialLayout=UNDEFINED in createOITRenderPass() means
    // the driver discards prior content and clears (loadOp=CLEAR) on every frame.
    return true;
}

void PostProcessor::cleanupOITResources() {
    VkDevice vkDevice = device->getDevice();
    auto destroyFn = [&](auto& h, auto fn) { if (h != VK_NULL_HANDLE) { fn(vkDevice, h, nullptr); h = VK_NULL_HANDLE; } };
    destroyFn(oitFramebuffer,    vkDestroyFramebuffer);
    destroyFn(oitRenderPass,     vkDestroyRenderPass);
    destroyFn(oitRevealImageView, vkDestroyImageView);
    if (oitRevealImage != VK_NULL_HANDLE) { vkDestroyImage(vkDevice, oitRevealImage, nullptr); oitRevealImage = VK_NULL_HANDLE; }
    if (oitRevealImageMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, oitRevealImageMemory, nullptr); oitRevealImageMemory = VK_NULL_HANDLE; }
    destroyFn(oitAccumImageView,  vkDestroyImageView);
    if (oitAccumImage != VK_NULL_HANDLE) { vkDestroyImage(vkDevice, oitAccumImage, nullptr); oitAccumImage = VK_NULL_HANDLE; }
    if (oitAccumImageMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, oitAccumImageMemory, nullptr); oitAccumImageMemory = VK_NULL_HANDLE; }
    destroyFn(oitSampler,        vkDestroySampler);
}

void PostProcessor::beginOITRenderPass(VkCommandBuffer commandBuffer) {
    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};  // accum: zero
    clearValues[1].color = {{ 1.0f, 0.0f, 0.0f, 0.0f }};  // reveal: 1.0 (nothing transparent yet)
    clearValues[2].depthStencil = { 1.0f, 0 };              // depth (not cleared, but value required)

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass  = oitRenderPass;
    rpi.framebuffer = oitFramebuffer;
    rpi.renderArea.extent = { width, height };
    rpi.clearValueCount = 3;
    rpi.pClearValues    = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0, 0, (float)width, (float)height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, {width, height} };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void PostProcessor::endOITRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

bool PostProcessor::createReflectionResources() {
    VkDevice vkDevice = device->getDevice();
    VkFormat depthFormat = device->findDepthFormat();

    // RGBA16F color image for reflected scene
    device->createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reflectionImage, reflectionImageMemory);
    reflectionImageView = device->createImageView(reflectionImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Depth image for reflection pass (needs SAMPLED_BIT so sceneRenderPass's READ_ONLY_OPTIMAL final layout is valid)
    device->createImage(width, height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reflectionDepthImage, reflectionDepthMemory);
    reflectionDepthView = device->createImageView(reflectionDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Sampler for the reflection image (linear filter for smooth sampling)
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxAnisotropy = 1.0f;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    si.maxLod = 1.0f;
    if (vkCreateSampler(vkDevice, &si, nullptr, &reflectionSampler) != VK_SUCCESS) return false;

    // Framebuffer using the existing sceneRenderPass (same format as scene pass)
    std::array<VkImageView, 2> views = { reflectionImageView, reflectionDepthView };
    VkFramebufferCreateInfo fbi{};
    fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbi.renderPass = sceneRenderPass;
    fbi.attachmentCount = static_cast<uint32_t>(views.size());
    fbi.pAttachments = views.data();
    fbi.width = width;
    fbi.height = height;
    fbi.layers = 1;
    if (vkCreateFramebuffer(vkDevice, &fbi, nullptr, &reflectionFramebuffer) != VK_SUCCESS) return false;

    // Transition reflection color image to SHADER_READ_ONLY_OPTIMAL so it can be sampled
    // safely even before the first reflection render pass runs
    {
        VkCommandBuffer cmd = device->beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = reflectionImage;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        device->endSingleTimeCommands(cmd);
    }
    return true;
}

void PostProcessor::cleanupReflectionResources() {
    VkDevice vkDevice = device->getDevice();
    auto d = [&](auto& h, auto fn) { if (h != VK_NULL_HANDLE) { fn(vkDevice, h, nullptr); h = VK_NULL_HANDLE; } };
    d(reflectionFramebuffer, vkDestroyFramebuffer);
    d(reflectionSampler,     vkDestroySampler);
    d(reflectionDepthView,   vkDestroyImageView);
    if (reflectionDepthImage  != VK_NULL_HANDLE) { vkDestroyImage(vkDevice, reflectionDepthImage, nullptr);  reflectionDepthImage  = VK_NULL_HANDLE; }
    if (reflectionDepthMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, reflectionDepthMemory, nullptr);   reflectionDepthMemory = VK_NULL_HANDLE; }
    d(reflectionImageView,   vkDestroyImageView);
    if (reflectionImage       != VK_NULL_HANDLE) { vkDestroyImage(vkDevice, reflectionImage, nullptr);       reflectionImage       = VK_NULL_HANDLE; }
    if (reflectionImageMemory != VK_NULL_HANDLE) { vkFreeMemory(vkDevice, reflectionImageMemory, nullptr);   reflectionImageMemory = VK_NULL_HANDLE; }
}

void PostProcessor::beginReflectionRenderPass(VkCommandBuffer commandBuffer) {
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass  = sceneRenderPass;
    rpi.framebuffer = reflectionFramebuffer;
    rpi.renderArea.extent = { width, height };
    rpi.clearValueCount = 2;
    rpi.pClearValues    = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{ 0, 0, (float)width, (float)height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, {width, height} };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void PostProcessor::endReflectionRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

} // namespace Graphics
} // namespace Phyxel
