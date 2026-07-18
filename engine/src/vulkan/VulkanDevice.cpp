#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "core/MaterialRegistry.h"
#include "core/Types.h"
#include "utils/Logger.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>
#include <array>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <imgui_impl_vulkan.h>

namespace Phyxel {
namespace Vulkan {

// Log (don't throw) on a non-success VkResult. Used to wrap setup/upload-time calls
// (vkBindBufferMemory, vkAllocateCommandBuffers, vkBegin/EndCommandBuffer) that were
// previously fire-and-forget — a failure there silently corrupted a resource. Preserves
// existing control flow; only adds visibility. NOT for per-frame hot-loop calls.
#define VK_LOG_IF_FAILED(call) do { \
        VkResult vk_res_ = (call); \
        if (vk_res_ != VK_SUCCESS) \
            LOG_ERROR("Vulkan", "{} failed (VkResult {})", #call, static_cast<int>(vk_res_)); \
    } while (0)

VulkanDevice::VulkanDevice() {
    // Constructor - initialize everything to default state
}

VulkanDevice::~VulkanDevice() {
    cleanup();
}

bool VulkanDevice::initialize() {
    if (!createInstance()) {
        LOG_ERROR("Vulkan", "Failed to create Vulkan instance!");
        return false;
    }

    if (!setupDebugMessenger()) {
        LOG_ERROR("Vulkan", "Failed to set up debug messenger!");
        return false;
    }

    if (!pickPhysicalDevice()) {
        LOG_ERROR("Vulkan", "Failed to find a suitable GPU!");
        return false;
    }

    if (!createLogicalDevice()) {
        LOG_ERROR("Vulkan", "Failed to create logical device!");
        return false;
    }

    return true;
}

void VulkanDevice::cleanup() {
    // Guard against double cleanup (destructor calls cleanup())
    if (m_cleanedUp || device == VK_NULL_HANDLE) {
        return;
    }
    m_cleanedUp = true;
    
    // Cleanup rendering resources
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        }
        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
        }
    }
    uniformBuffers.clear();
    uniformBuffersMemory.clear();

    // Cleanup light SSBO buffers
    cleanupLightBuffers();

    // Cleanup atlas UV SSBO buffers
    cleanupAtlasUVBuffers();

    // Cleanup reflection UBO buffers
    cleanupReflectionBuffers();
    
    if (instanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, instanceBuffer, nullptr);
        instanceBuffer = VK_NULL_HANDLE;
    }
    if (instanceBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, instanceBufferMemory, nullptr);
        instanceBufferMemory = VK_NULL_HANDLE;
    }
    
    // Cleanup dynamic subcube buffer
    cleanupDynamicSubcubeBuffer();

    // Cleanup ImGui textures (must be before ImGui shutdown)
    cleanupImGuiTextures();
    
    // Cleanup texture atlas
    cleanupTextureAtlas();
    
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer, nullptr);
        indexBuffer = VK_NULL_HANDLE;
    }
    if (indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexBufferMemory, nullptr);
        indexBufferMemory = VK_NULL_HANDLE;
    }
    
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }

    // Cleanup sync objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        }
        if (renderFinishedSemaphores.size() > i && renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        }
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }
    }

    // Cleanup compute resources
    cleanupComputeResources();

    // Cleanup command pool
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    // Cleanup framebuffers
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapChainFramebuffers.clear();

    // Cleanup depth resources
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }

    // Cleanup image views
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapChainImageViews.clear();

    // Cleanup swapchain
    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, nullptr);
        }
        debugMessenger = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        LOG_ERROR("Vulkan", "Validation layers requested, but not available!");
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phyxel";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("Vulkan", "Failed to create instance! Error: " << result);
        return false;
    }

    return true;
}

bool VulkanDevice::setupDebugMessenger() {
    if (!enableValidationLayers) return true;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        VkResult result = func(instance, &createInfo, nullptr, &debugMessenger);
        return result == VK_SUCCESS;
    } else {
        LOG_ERROR("Vulkan", "Failed to load debug messenger function!");
        return false;
    }
}

bool VulkanDevice::createSurface(void* window) {
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window);
    
    if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create window surface!");
        return false;
    }
    
    LOG_INFO("Vulkan", "Vulkan surface created successfully");
    return true;
}

bool VulkanDevice::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        LOG_ERROR("Vulkan", "Failed to find GPUs with Vulkan support!");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice = device;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        LOG_ERROR("Vulkan", "Failed to find a suitable GPU!");
        return false;
    }

    return true;
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
            // On desktop GPUs the graphics family almost always supports compute too
            if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                indices.computeFamily = i;
            }
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        i++;
    }

    // If graphics family didn't support compute, search for a dedicated compute family
    if (!indices.computeFamily.has_value()) {
        i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                indices.computeFamily = i;
                break;
            }
            i++;
        }
    }

    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

SwapChainSupportDetails VulkanDevice::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

bool VulkanDevice::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    // Cache queue family indices
    graphicsQueueFamily = indices.graphicsFamily.value();
    computeQueueFamily  = indices.computeFamily.has_value()
                            ? indices.computeFamily.value()
                            : indices.graphicsFamily.value(); // fallback

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
        computeQueueFamily
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Query supported features so we only enable what the device actually has.
    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.fillModeNonSolid = VK_TRUE;  // Required for wireframe rendering (VK_POLYGON_MODE_LINE)
    deviceFeatures.wideLines = VK_TRUE;          // Required for line width > 1.0
    deviceFeatures.independentBlend = VK_TRUE;   // Required for OIT: accum and reveal attachments use different blend states
    // BC texture compression (BC7 voxel texture array). Most desktop GPUs support it; if not,
    // the texture path falls back to uncompressed RGBA.
    if (supportedFeatures.textureCompressionBC) {
        deviceFeatures.textureCompressionBC = VK_TRUE;
        bc7Supported_ = true;
    } else {
        LOG_WARN("Vulkan", "Device lacks textureCompressionBC; voxel textures will stay uncompressed RGBA");
    }
    // D0 (docs/RenderDensityPlan.md): pipeline-statistics queries for the overdraw counter.
    if (supportedFeatures.pipelineStatisticsQuery) {
        deviceFeatures.pipelineStatisticsQuery = VK_TRUE;
        pipelineStatsSupported_ = true;
    } else {
        LOG_WARN("Vulkan", "Device lacks pipelineStatisticsQuery; D0 overdraw counter disabled");
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        LOG_ERROR_FMT("Vulkan", "Failed to create logical device! Error: " << result);
        return false;
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    vkGetDeviceQueue(device, computeQueueFamily, 0, &computeQueue);

    LOG_INFO_FMT("Vulkan", "Compute queue family: " << computeQueueFamily
        << (computeSharesGraphicsQueue() ? " (shared with graphics)" : " (dedicated)"));

    return true;
}

std::vector<const char*> VulkanDevice::getRequiredExtensions() {
    std::vector<const char*> extensions;

    // Add GLFW required extensions (would be implemented with actual windowing system)
    extensions.push_back("VK_KHR_surface");
#ifdef __linux__
    extensions.push_back("VK_KHR_xcb_surface");
#elif defined(_WIN32)
    extensions.push_back("VK_KHR_win32_surface");
#endif

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanDevice::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void VulkanDevice::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VkSurfaceFormatKHR VulkanDevice::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanDevice::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // Try the preferred mode first
    for (const auto& mode : availablePresentModes) {
        if (mode == preferredPresentMode_) {
            return mode;
        }
    }
    // Fallback chain: IMMEDIATE → MAILBOX → FIFO
    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) return mode;
    }
    for (const auto& mode : availablePresentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanDevice::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, int windowWidth, int windowHeight) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(windowWidth),
            static_cast<uint32_t>(windowHeight)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

bool VulkanDevice::createSwapChain(int windowWidth, int windowHeight) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, windowWidth, windowHeight);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create swap chain!");
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;

    // Create image views
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to create image view!");
            return false;
        }
    }

    LOG_INFO("Vulkan", "Swapchain created successfully");
    return true;
}

bool VulkanDevice::createFramebuffers(VkRenderPass renderPass) {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            swapChainImageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to create framebuffer!");
            return false;
        }
    }

    LOG_INFO("Vulkan", "Framebuffers created successfully");
    return true;
}

bool VulkanDevice::createCommandBuffers() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = findQueueFamilies(physicalDevice).graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create command pool!");
        return false;
    }

    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate command buffers!");
        return false;
    }

    LOG_INFO("Vulkan", "Command buffers created successfully");
    return true;
}

// ============================================================
// COMPUTE INFRASTRUCTURE
// ============================================================

bool VulkanDevice::initComputeResources() {
    // Create a command pool for the compute queue family.
    // If compute shares the graphics family, we still create a separate pool
    // so compute command buffers can be reset independently.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType  = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags  = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = computeQueueFamily;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &computeCommandPool) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create compute command pool!");
        return false;
    }

    computeCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = computeCommandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());

    if (vkAllocateCommandBuffers(device, &allocInfo, computeCommandBuffers.data()) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate compute command buffers!");
        return false;
    }

    LOG_INFO_FMT("Vulkan", "Compute resources initialized (" << MAX_FRAMES_IN_FLIGHT << " command buffers)");
    return true;
}

void VulkanDevice::cleanupComputeResources() {
    if (computeCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, computeCommandPool, nullptr);
        computeCommandPool = VK_NULL_HANDLE;
    }
    computeCommandBuffers.clear();
}

VkCommandBuffer VulkanDevice::getComputeCommandBuffer(uint32_t frameIndex) const {
    if (frameIndex < computeCommandBuffers.size()) {
        return computeCommandBuffers[frameIndex];
    }
    return VK_NULL_HANDLE;
}

void VulkanDevice::createStorageBuffer(VkDeviceSize size, VkBuffer& buffer, VkDeviceMemory& memory,
                                        VkBufferUsageFlags extraUsage) {
    createBuffer(
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | extraUsage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        buffer, memory
    );
}

void VulkanDevice::createPersistentStagingBuffer(VkDeviceSize size, VkBuffer& buffer,
                                                   VkDeviceMemory& memory, void** mappedPtr) {
    createBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        buffer, memory
    );
    if (vkMapMemory(device, memory, 0, size, 0, mappedPtr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map persistent staging buffer memory!");
    }
}

// ============================================================

bool VulkanDevice::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            LOG_ERROR("Vulkan", "Failed to create synchronization objects for a frame!");
            return false;
        }
    }

    LOG_INFO("Vulkan", "Sync objects created successfully");
    return true;
}

// Command buffer operations
void VulkanDevice::deviceWaitIdle() {
    vkDeviceWaitIdle(device);
}

void VulkanDevice::recreateSyncObjects() {
    // Destroy old sync objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        if (renderFinishedSemaphores.size() > i && renderFinishedSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(device, inFlightFences[i], nullptr);
    }
    createSyncObjects();
}

void VulkanDevice::waitForFence(uint32_t frameIndex) {
    vkWaitForFences(device, 1, &inFlightFences[frameIndex], VK_TRUE, UINT64_MAX);
}

void VulkanDevice::resetFence(uint32_t frameIndex) {
    vkResetFences(device, 1, &inFlightFences[frameIndex]);
}

VkResult VulkanDevice::acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex) {
    return vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, 
                                imageAvailableSemaphores[frameIndex], VK_NULL_HANDLE, imageIndex);
}

void VulkanDevice::resetCommandBuffer(uint32_t frameIndex) {
    vkResetCommandBuffer(commandBuffers[frameIndex], 0);
}

void VulkanDevice::beginCommandBuffer(uint32_t frameIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(commandBuffers[frameIndex], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }
}

void VulkanDevice::beginRenderPass(uint32_t frameIndex, uint32_t imageIndex, VkRenderPass renderPass) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffers[frameIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanDevice::endRenderPass(uint32_t frameIndex) {
    vkCmdEndRenderPass(commandBuffers[frameIndex]);
}

void VulkanDevice::endCommandBuffer(uint32_t frameIndex) {
    if (vkEndCommandBuffer(commandBuffers[frameIndex]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
}

bool VulkanDevice::submitCommandBuffer(uint32_t frameIndex) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[frameIndex]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[frameIndex];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[frameIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[frameIndex]);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "vkQueueSubmit failed with VkResult={}", static_cast<int>(result));
        return false;
    }

    return true;
}

VkResult VulkanDevice::presentFrame(uint32_t imageIndex, uint32_t frameIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores[frameIndex];

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

VkCommandBuffer VulkanDevice::getCommandBuffer(uint32_t frameIndex) {
    return commandBuffers[frameIndex];
}

// Depth buffer management
bool VulkanDevice::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    
    createImage(swapChainExtent.width, swapChainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                depthImage, depthImageMemory);
    
    depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

    return true;
}

VkFormat VulkanDevice::findDepthFormat() {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkFormat VulkanDevice::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported format!");
}

void VulkanDevice::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                              VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

VkImageView VulkanDevice::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }

    return imageView;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

bool VulkanDevice::createVertexBuffer() {
    // Define the 8 vertices of a cube (just vertex IDs)
    std::vector<Vertex> vertices = {
        {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}
    };

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t) bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    return true;
}

// D1: 6-index quad draw for the MAIN OPAQUE pass. Default ON — verified pixel-identical (main pass
// back-culls; shadow/reflection/OIT stay 36-index). ~34% off the main Static Geometry pass.
bool VulkanDevice::s_quadDraw = true;

bool VulkanDevice::createIndexBuffer() {
    // Define indices for the 6 faces of the cube (each face has 2 triangles = 6 indices)
    // These match the original main.cpp.bak face definitions
    std::vector<uint16_t> indices = {
        // Front face (+Z)
        4, 6, 5, 6, 7, 5,
        // Back face (-Z)  
        0, 1, 2, 1, 3, 2,
        // Right face (+X)
        1, 5, 3, 3, 5, 7,
        // Left face (-X)
        0, 2, 4, 2, 6, 4,
        // Top face (+Y)
        2, 3, 6, 3, 7, 6,
        // Bottom face (-Y)
        0, 4, 1, 1, 4, 5
    };

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                 stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t) bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

    copyBuffer(stagingBuffer, indexBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    return true;
}

bool VulkanDevice::createInstanceBuffer() {
    // Create a temporary single instance buffer for now - will be updated later with actual scene data
    InstanceData redCube;
    redCube.packedData = 0x3F << 15; // All faces visible (0x3F face mask), position (0,0,0)
    redCube.textureIndex = Phyxel::Core::MaterialRegistry::instance().getPlaceholderIndex();
    std::vector<InstanceData> instances = { redCube };

    // We'll create buffer large enough for full 32x32x32 = 32,768 instances
    VkDeviceSize bufferSize = sizeof(InstanceData) * 35000; // Buffer for up to 35,000 instances (with some margin)

    createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                 instanceBuffer, instanceBufferMemory);

    // Initially populate with the single test instance
    void* data;
    vkMapMemory(device, instanceBufferMemory, 0, sizeof(instances[0]) * instances.size(), 0, &data);
    memcpy(data, instances.data(), sizeof(instances[0]) * instances.size());
    vkUnmapMemory(device, instanceBufferMemory);

    return true;
}

bool VulkanDevice::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
                     uniformBuffers[i], uniformBuffersMemory[i]);
    }

    return true;
}

bool VulkanDevice::createLightBuffers() {
    VkDeviceSize bufferSize = sizeof(Graphics::LightBufferGPU);

    lightBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    lightBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     lightBuffers[i], lightBuffersMemory[i]);
    }

    // Initialize with empty data
    Graphics::LightBufferGPU emptyData{};
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        void* data;
        vkMapMemory(device, lightBuffersMemory[i], 0, bufferSize, 0, &data);
        memcpy(data, &emptyData, sizeof(emptyData));
        vkUnmapMemory(device, lightBuffersMemory[i]);
    }

    LOG_INFO("Vulkan", "Created light SSBO buffers ({} bytes each)", bufferSize);
    return true;
}

void VulkanDevice::updateLightBuffer(uint32_t frameIndex, const Graphics::LightBufferGPU& lightData) {
    void* data;
    vkMapMemory(device, lightBuffersMemory[frameIndex], 0, sizeof(lightData), 0, &data);
    memcpy(data, &lightData, sizeof(lightData));
    vkUnmapMemory(device, lightBuffersMemory[frameIndex]);
}

void VulkanDevice::cleanupLightBuffers() {
    for (size_t i = 0; i < lightBuffers.size(); i++) {
        if (lightBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, lightBuffers[i], nullptr);
        }
        if (lightBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, lightBuffersMemory[i], nullptr);
        }
    }
    lightBuffers.clear();
    lightBuffersMemory.clear();
}

bool VulkanDevice::createAtlasUVBuffers() {
    // Allocate enough for header (16 bytes) + 256 materials * 6 faces * sizeof(vec4)
    // = 16 + 6144 = 6160 bytes. Round up for safety.
    VkDeviceSize bufferSize = 16 + 256 * 6 * sizeof(glm::vec4);

    atlasUVBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    atlasUVBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     atlasUVBuffers[i], atlasUVBuffersMemory[i]);
    }

    // Initialize with zeros
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        void* data;
        vkMapMemory(device, atlasUVBuffersMemory[i], 0, bufferSize, 0, &data);
        memset(data, 0, bufferSize);
        vkUnmapMemory(device, atlasUVBuffersMemory[i]);
    }

    LOG_INFO("Vulkan", "Created atlas UV SSBO buffers ({} bytes each)", bufferSize);
    return true;
}

void VulkanDevice::updateAtlasUVBuffer(const std::vector<glm::vec4>& uvs, uint32_t fallbackIndex,
                                       uint32_t count512, uint32_t count1024) {
    // SSBO header carries per-class layer counts + fallback for the mixed-res split:
    //   count512 = layers in the 512 array, count1024 = layers in the 1024 array.
    // (textureUVs[] is retained for layout compatibility but is no longer sampled.)
    struct AtlasUVHeader {
        uint32_t count512;
        uint32_t fallbackIndex;
        uint32_t count1024;
        uint32_t _pad1;
    };

    size_t dataSize = sizeof(AtlasUVHeader) + std::max<size_t>(1, uvs.size()) * sizeof(glm::vec4);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        void* mapped;
        vkMapMemory(device, atlasUVBuffersMemory[i], 0, dataSize, 0, &mapped);

        AtlasUVHeader header;
        header.count512 = count512;
        header.fallbackIndex = fallbackIndex;
        header.count1024 = count1024;
        header._pad1 = 0;

        memcpy(mapped, &header, sizeof(header));
        if (!uvs.empty())
            memcpy(static_cast<char*>(mapped) + sizeof(header), uvs.data(), uvs.size() * sizeof(glm::vec4));

        vkUnmapMemory(device, atlasUVBuffersMemory[i]);
    }

    LOG_INFO("Vulkan", "Updated atlas SSBO (count512={}, count1024={}, fallback={})",
             count512, count1024, fallbackIndex);
}

void VulkanDevice::cleanupAtlasUVBuffers() {
    for (size_t i = 0; i < atlasUVBuffers.size(); i++) {
        if (atlasUVBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, atlasUVBuffers[i], nullptr);
        }
        if (atlasUVBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, atlasUVBuffersMemory[i], nullptr);
        }
    }
    atlasUVBuffers.clear();
    atlasUVBuffersMemory.clear();
}

bool VulkanDevice::createDescriptorSetLayout() {
    // UBO binding (binding 0)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT; // Visible to both stages
    uboLayoutBinding.pImmutableSamplers = nullptr;

    // Texture atlas sampler binding (binding 1)
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Shadow map sampler binding (binding 2)
    VkDescriptorSetLayoutBinding shadowMapLayoutBinding{};
    shadowMapLayoutBinding.binding = 2;
    shadowMapLayoutBinding.descriptorCount = 1;
    shadowMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapLayoutBinding.pImmutableSamplers = nullptr;
    shadowMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Light SSBO binding (binding 3)
    VkDescriptorSetLayoutBinding lightBufferBinding{};
    lightBufferBinding.binding = 3;
    lightBufferBinding.descriptorCount = 1;
    lightBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightBufferBinding.pImmutableSamplers = nullptr;
    lightBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Atlas UV SSBO binding (binding 4)
    VkDescriptorSetLayoutBinding atlasUVBinding{};
    atlasUVBinding.binding = 4;
    atlasUVBinding.descriptorCount = 1;
    atlasUVBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    atlasUVBinding.pImmutableSamplers = nullptr;
    atlasUVBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Hi-res (1024) texture array sampler binding (binding 5) — mixed-res split
    VkDescriptorSetLayoutBinding samplerHiLayoutBinding{};
    samplerHiLayoutBinding.binding = 5;
    samplerHiLayoutBinding.descriptorCount = 1;
    samplerHiLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerHiLayoutBinding.pImmutableSamplers = nullptr;
    samplerHiLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Normal+roughness array samplers: binding 6 (512), binding 7 (1024) — PBR
    VkDescriptorSetLayoutBinding normal512Binding{};
    normal512Binding.binding = 6;
    normal512Binding.descriptorCount = 1;
    normal512Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normal512Binding.pImmutableSamplers = nullptr;
    normal512Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normal1024Binding{};
    normal1024Binding.binding = 7;
    normal1024Binding.descriptorCount = 1;
    normal1024Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normal1024Binding.pImmutableSamplers = nullptr;
    normal1024Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 8> bindings = {uboLayoutBinding, samplerLayoutBinding, shadowMapLayoutBinding, lightBufferBinding, atlasUVBinding, samplerHiLayoutBinding, normal512Binding, normal1024Binding};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create descriptor set layout!");
        return false;
    }

    return true;
}

bool VulkanDevice::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 5; // albedo 512/1024 + normal 512/1024 + Shadow
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; // Light SSBO + Atlas UV SSBO

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create descriptor pool!");
        return false;
    }

    return true;
}

bool VulkanDevice::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate descriptor sets!");
        return false;
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    return true;
}

void VulkanDevice::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    VK_LOG_IF_FAILED(vkBindBufferMemory(device, buffer, bufferMemory, 0));
}

void VulkanDevice::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_LOG_IF_FAILED(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_LOG_IF_FAILED(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    VK_LOG_IF_FAILED(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// Vulkan clip [-1,1] XY -> shadow-map UV [0,1]; must stay identical to the biasMat
// the shaders used to build inline (column-major, same as GLSL mat4 constructor).
static const glm::mat4 kShadowBiasMat(
    0.5f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.5f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.5f, 0.5f, 0.0f, 1.0f);

void VulkanDevice::updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, const glm::vec3& sunDirection, const glm::vec3& sunColor, uint32_t numInstances, float ambientLight, float emissiveMultiplier, const glm::vec3& cameraPosition, float elapsedTime) {
    UniformBufferObject ubo{};
    ubo.view = view;
    ubo.proj = proj;
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    ubo.sunDirection = sunDirection;
    ubo.sunColor = sunColor;
    ubo.numInstances = numInstances;
    ubo.ambientLight = ambientLight;
    ubo.emissiveMultiplier = emissiveMultiplier;
    // Camera-relative rendering (docs/CameraRelativeRendering.md): `view` arrives as the
    // eye-at-origin rotation-only matrix and every GPU position is (world - camera), so the
    // shading-space camera sits at the origin. cameraWorld keeps the true position for
    // absolute-hash reconstruction in shaders.
    ubo.cameraPosition = glm::vec3(0.0f);
    ubo.cameraWorld = cameraPosition;
    ubo.elapsedTime = elapsedTime;
    ubo.viewProj = proj * view;
    ubo.biasedLightSpace = kShadowBiasMat * lightSpaceMatrix;

    // Debug: Log matrix data for the first few frames
    static int debugFrameCount = 0;
    if (debugFrameCount < 3) {
        LOG_DEBUG_FMT("Vulkan", "Frame " << debugFrameCount << " - Matrix upload:");
        LOG_DEBUG_FMT("Vulkan", "  View[0]: " << view[0][0] << ", " << view[0][1] << ", " << view[0][2] << ", " << view[0][3]);
        LOG_DEBUG_FMT("Vulkan", "  Proj[0]: " << proj[0][0] << ", " << proj[0][1] << ", " << proj[0][2] << ", " << proj[0][3]);
        LOG_DEBUG_FMT("Vulkan", "  NumInstances: " << numInstances);
        debugFrameCount++;
    }

    void* data;
    vkMapMemory(device, uniformBuffersMemory[frameIndex], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniformBuffersMemory[frameIndex]);
}

void VulkanDevice::setReflectedViewProj(uint32_t frameIndex, const glm::mat4& reflectedVP) {
    // Patch just the reflectedViewProj field in the existing UBO buffer.
    // The field is at offsetof(UniformBufferObject, reflectedViewProj).
    const size_t offset = offsetof(UniformBufferObject, reflectedViewProj);
    void* data;
    vkMapMemory(device, uniformBuffersMemory[frameIndex], offset, sizeof(glm::mat4), 0, &data);
    memcpy(data, &reflectedVP, sizeof(glm::mat4));
    vkUnmapMemory(device, uniformBuffersMemory[frameIndex]);
}

bool VulkanDevice::createReflectionBuffers() {
    // Create per-frame UBO buffers for the reflected camera pass
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    reflectionUniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    reflectionUniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            reflectionUniformBuffers[i], reflectionUniformBuffersMemory[i]);
    }

    // Separate descriptor pool for reflection sets (avoids resizing existing pool)
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; // atlas + shadow
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; // lights + atlas UVs

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &reflectionDescriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create reflection descriptor pool!");
        return false;
    }

    // Allocate reflection descriptor sets using main layout
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = reflectionDescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    reflectionDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, reflectionDescriptorSets.data()) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate reflection descriptor sets!");
        return false;
    }

    // Write all 5 bindings: reflection UBO at 0, shared resources at 1-4
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = reflectionUniformBuffers[i];
        uboInfo.offset = 0;
        uboInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo atlasInfo{};
        atlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        atlasInfo.imageView = textureAtlasImageView;
        atlasInfo.sampler = textureAtlasSampler;

        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMapImageView;
        shadowInfo.sampler = shadowMapSampler;

        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = lightBuffers[i];
        lightInfo.offset = 0;
        lightInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo atlasUVInfo{};
        atlasUVInfo.buffer = atlasUVBuffers[i];
        atlasUVInfo.offset = 0;
        atlasUVInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 5> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = reflectionDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &uboInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = reflectionDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &atlasInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = reflectionDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &shadowInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = reflectionDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &lightInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = reflectionDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &atlasUVInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    LOG_INFO("Vulkan", "Created reflection UBO buffers and descriptor sets");
    return true;
}

void VulkanDevice::updateReflectionUniformBuffer(uint32_t frameIndex, const glm::mat4& reflectedView, const glm::mat4& proj, const glm::mat4& lightSpaceMatrix, const glm::vec3& sunDirection, const glm::vec3& sunColor, uint32_t numInstances, float ambientLight, float emissiveMultiplier, const glm::vec3& cameraPosition) {
    UniformBufferObject ubo{};
    ubo.view = reflectedView;
    ubo.proj = proj;
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    ubo.sunDirection = sunDirection;
    ubo.sunColor = sunColor;
    ubo.numInstances = numInstances;
    ubo.ambientLight = ambientLight;
    ubo.emissiveMultiplier = emissiveMultiplier;
    ubo.cameraPosition = cameraPosition;
    ubo.reflectedViewProj = glm::mat4(1.0f); // Not used during reflection rendering
    ubo.viewProj = proj * reflectedView;
    ubo.biasedLightSpace = kShadowBiasMat * lightSpaceMatrix;
    void* data;
    vkMapMemory(device, reflectionUniformBuffersMemory[frameIndex], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, reflectionUniformBuffersMemory[frameIndex]);
}

void VulkanDevice::bindReflectionDescriptorSets(uint32_t frameIndex, VkPipelineLayout layout) {
    vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
        layout, 0, 1, &reflectionDescriptorSets[frameIndex], 0, nullptr);
}

void VulkanDevice::cleanupReflectionBuffers() {
    if (reflectionDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, reflectionDescriptorPool, nullptr);
        reflectionDescriptorPool = VK_NULL_HANDLE;
        reflectionDescriptorSets.clear();
    }
    for (size_t i = 0; i < reflectionUniformBuffers.size(); i++) {
        if (reflectionUniformBuffers[i] != VK_NULL_HANDLE)
            vkDestroyBuffer(device, reflectionUniformBuffers[i], nullptr);
        if (reflectionUniformBuffersMemory[i] != VK_NULL_HANDLE)
            vkFreeMemory(device, reflectionUniformBuffersMemory[i], nullptr);
    }
    reflectionUniformBuffers.clear();
    reflectionUniformBuffersMemory.clear();
}

void VulkanDevice::updateInstanceBuffer(const std::vector<InstanceData>& instances) {
    VkDeviceSize bufferSize = sizeof(instances[0]) * instances.size();
    
    void* data;
    vkMapMemory(device, instanceBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, instances.data(), (size_t) bufferSize);
    vkUnmapMemory(device, instanceBufferMemory);
}

bool VulkanDevice::createDynamicSubcubeBuffer(uint32_t maxSubcubes) {
    maxDynamicSubcubes = maxSubcubes;
    VkDeviceSize bufferSize = sizeof(DynamicSubcubeInstanceData) * maxDynamicSubcubes;

    createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                dynamicSubcubeBuffer, dynamicSubcubeBufferMemory);
    
    return true;
}

void VulkanDevice::updateDynamicSubcubeBuffer(const std::vector<DynamicSubcubeInstanceData>& dynamicSubcubes) {
    // std::cout << "[BUFFER DEBUG] updateDynamicSubcubeBuffer called with " << dynamicSubcubes.size() << " subcubes" << std::endl;
    // std::cout << "[BUFFER DEBUG] dynamicSubcubeBuffer handle: " << dynamicSubcubeBuffer << std::endl;
    
    if (dynamicSubcubes.empty() || dynamicSubcubeBuffer == VK_NULL_HANDLE) {
        //std::cout << "[BUFFER DEBUG] Early return - empty subcubes or null buffer handle" << std::endl;
        return;
    }
    
    VkDeviceSize bufferSize = sizeof(DynamicSubcubeInstanceData) * std::min(static_cast<uint32_t>(dynamicSubcubes.size()), maxDynamicSubcubes);
    //std::cout << "[BUFFER DEBUG] Updating buffer with " << bufferSize << " bytes" << std::endl;
    
    void* data;
    vkMapMemory(device, dynamicSubcubeBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, dynamicSubcubes.data(), (size_t) bufferSize);
    vkUnmapMemory(device, dynamicSubcubeBufferMemory);
    
    //std::cout << "[BUFFER DEBUG] Buffer update complete" << std::endl;
}

void VulkanDevice::bindDynamicSubcubeBuffer(uint32_t frameIndex) {
    VkBuffer vertexBuffers[] = {vertexBuffer, dynamicSubcubeBuffer};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffers[frameIndex], 0, 2, vertexBuffers, offsets);
}

void VulkanDevice::cleanupDynamicSubcubeBuffer() {
    if (dynamicSubcubeBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, dynamicSubcubeBuffer, nullptr);
        dynamicSubcubeBuffer = VK_NULL_HANDLE;
    }
    if (dynamicSubcubeBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, dynamicSubcubeBufferMemory, nullptr);
        dynamicSubcubeBufferMemory = VK_NULL_HANDLE;
    }
}

bool VulkanDevice::createCharacterInstanceBuffer(uint32_t maxInstances) {
    maxCharacterInstances = maxInstances;
    VkDeviceSize bufferSize = sizeof(CharacterInstanceData) * maxCharacterInstances;

    createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                characterInstanceBuffer, characterInstanceBufferMemory);

    // Persistently map the buffer once. The memory is HOST_COHERENT, so a plain
    // memcpy into the mapped pointer is visible to the GPU without an explicit
    // flush — this avoids a vkMapMemory/vkUnmapMemory pair every single frame
    // (which, on short GPU-light frames, was a measurable per-frame cost).
    if (characterInstanceBufferMemory != VK_NULL_HANDLE) {
        vkMapMemory(device, characterInstanceBufferMemory, 0, bufferSize, 0,
                    &characterInstanceMapped);
    }

    return true;
}

void VulkanDevice::updateCharacterInstanceBuffer(const std::vector<CharacterInstanceData>& instances) {
    if (instances.empty() || characterInstanceMapped == nullptr) {
        return;
    }

    VkDeviceSize bufferSize = sizeof(CharacterInstanceData) * std::min(static_cast<uint32_t>(instances.size()), maxCharacterInstances);
    memcpy(characterInstanceMapped, instances.data(), (size_t) bufferSize);
}

void VulkanDevice::bindCharacterInstanceBuffer(VkCommandBuffer commandBuffer) {
    VkBuffer vertexBuffers[] = {characterInstanceBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
}

void VulkanDevice::cleanupCharacterInstanceBuffer() {
    if (characterInstanceBufferMemory != VK_NULL_HANDLE && characterInstanceMapped != nullptr) {
        vkUnmapMemory(device, characterInstanceBufferMemory);
        characterInstanceMapped = nullptr;
    }
    if (characterInstanceBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, characterInstanceBuffer, nullptr);
        characterInstanceBuffer = VK_NULL_HANDLE;
    }
    if (characterInstanceBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, characterInstanceBufferMemory, nullptr);
        characterInstanceBufferMemory = VK_NULL_HANDLE;
    }
}

void VulkanDevice::bindVertexBuffers(uint32_t frameIndex) {
    VkBuffer vertexBuffers[] = {vertexBuffer, instanceBuffer};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffers[frameIndex], 0, 2, vertexBuffers, offsets);
}

void VulkanDevice::bindIndexBuffer(uint32_t frameIndex) {
    vkCmdBindIndexBuffer(commandBuffers[frameIndex], indexBuffer, 0, VK_INDEX_TYPE_UINT16);
}

void VulkanDevice::bindDescriptorSets(uint32_t frameIndex, VkPipelineLayout pipelineLayout) {
    vkCmdBindDescriptorSets(commandBuffers[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);
}

void VulkanDevice::drawIndexed(uint32_t frameIndex, uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstInstance) {
    vkCmdDrawIndexed(commandBuffers[frameIndex], indexCount, instanceCount, 0, 0, firstInstance);
}

void VulkanDevice::pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, const glm::vec3& chunkBaseAbs) {
    pushConstants(frameIndex, pipelineLayout, chunkBaseOffset, 0u, chunkBaseAbs);
}

void VulkanDevice::pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset, uint32_t debugMode, const glm::vec3& chunkBaseAbs) {
    struct PushConstants {
        glm::vec3 chunkBaseOffset;  // camera-relative (positions)
        uint32_t debugMode;
        glm::vec3 chunkBaseAbs;     // exact absolute origin (varied-hash seed)
    } pushData;
    pushData.chunkBaseOffset = chunkBaseOffset;
    pushData.debugMode = debugMode;
    pushData.chunkBaseAbs = chunkBaseAbs;

    vkCmdPushConstants(commandBuffers[frameIndex], pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushData);
}

void VulkanDevice::bindInstanceBufferWithOffset(uint32_t frameIndex, VkDeviceSize offset) {
    VkBuffer vertexBuffers[] = {vertexBuffer, instanceBuffer};
    VkDeviceSize offsets[] = {0, offset};
    vkCmdBindVertexBuffers(commandBuffers[frameIndex], 0, 2, vertexBuffers, offsets);
}

void VulkanDevice::drawChunk(uint32_t frameIndex, VkPipelineLayout pipelineLayout,
                            const glm::vec3& chunkBaseOffset, VkDeviceSize instanceOffset, uint32_t instanceCount) {
    // Push chunk base offset (dead legacy path: absolute == relative near origin)
    pushConstants(frameIndex, pipelineLayout, chunkBaseOffset, chunkBaseOffset);
    
    // Bind instance buffer at the correct offset for this chunk
    bindInstanceBufferWithOffset(frameIndex, instanceOffset);
    
    // Draw all cubes in this chunk (36-index cube, or 6-index quad when s_quadDraw — D1)
    vkCmdDrawIndexed(commandBuffers[frameIndex], chunkIndexCount(), instanceCount, 0, 0, 0);
}

void VulkanDevice::drawChunks(uint32_t frameIndex, VkPipelineLayout pipelineLayout, 
                             const std::vector<ChunkRenderData>& chunks) {
    for (const auto& chunk : chunks) {
        drawChunk(frameIndex, pipelineLayout, chunk.worldPosition, 
                 chunk.instanceOffset, chunk.instanceCount);
    }
}

uint32_t VulkanDevice::getGraphicsQueueFamily() const {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    return indices.graphicsFamily.value();
}

VkCommandBuffer VulkanDevice::getCommandBuffer(uint32_t frameIndex) const {
    return commandBuffers[frameIndex];
}

VkCommandBuffer VulkanDevice::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_LOG_IF_FAILED(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_LOG_IF_FAILED(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    return commandBuffer;
}

void VulkanDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    VK_LOG_IF_FAILED(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// Texture atlas management methods
bool VulkanDevice::loadTextureAtlas(const std::string& atlasPath) {
    LOG_DEBUG_FMT("Vulkan", "Loading texture atlas: " << atlasPath);
    
    // Load image using stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(atlasPath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    std::vector<uint8_t> fallbackPixels; // For fallback texture
    bool usingFallback = false;
    
    if (!pixels) {
        LOG_ERROR_FMT("Vulkan", "Failed to load texture atlas: " << atlasPath);
        LOG_ERROR_FMT("Vulkan", "stb_image error: " << stbi_failure_reason());
        
        // Create fallback checkerboard texture as backup
        LOG_WARN("Vulkan", "Creating fallback checkerboard texture...");
        texWidth = 128;
        texHeight = 128;
        texChannels = 4;
        const VkDeviceSize fallbackSize = texWidth * texHeight * texChannels;
        
        fallbackPixels.resize(fallbackSize);
        for (int y = 0; y < texHeight; y++) {
            for (int x = 0; x < texWidth; x++) {
                const int index = (y * texWidth + x) * texChannels;
                const bool checker = ((x / 8) + (y / 8)) % 2;
                fallbackPixels[index + 0] = checker ? 255 : 64;  // R
                fallbackPixels[index + 1] = checker ? 64 : 255;  // G
                fallbackPixels[index + 2] = 64;                  // B
                fallbackPixels[index + 3] = 255;                 // A
            }
        }
        pixels = fallbackPixels.data();
        usingFallback = true;
    } else {
        LOG_DEBUG_FMT("Vulkan", "Successfully loaded texture atlas: " << texWidth << "x" << texHeight << " channels=" << texChannels);
    }
    
    const VkDeviceSize imageSize = texWidth * texHeight * 4; // Always use 4 channels
    
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);
    
    // Copy pixel data to staging buffer
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);
    
    // Free the loaded image data (only if not using fallback)
    if (!usingFallback) {
        stbi_image_free(pixels);
    }
    
    // Create texture image
    createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureAtlasImage, textureAtlasImageMemory);
    
    // Transition image layout and copy buffer to image
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_LOG_IF_FAILED(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_LOG_IF_FAILED(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    // Transition to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureAtlasImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, textureAtlasImage, 
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read-only
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VK_LOG_IF_FAILED(vkEndCommandBuffer(commandBuffer));

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    
    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    // Create image view
    textureAtlasImageView = createImageView(textureAtlasImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    
    LOG_DEBUG("Vulkan", "Texture atlas loaded successfully");
    return true;
}

bool VulkanDevice::uploadTextureAtlasPixels(const uint8_t* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) return false;

    // Wait for GPU idle before modifying the texture
    vkDeviceWaitIdle(device);

    // Destroy old image resources
    if (textureAtlasImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, textureAtlasImageView, nullptr);
        textureAtlasImageView = VK_NULL_HANDLE;
    }
    if (textureAtlasImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, textureAtlasImage, nullptr);
        textureAtlasImage = VK_NULL_HANDLE;
    }
    if (textureAtlasImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, textureAtlasImageMemory, nullptr);
        textureAtlasImageMemory = VK_NULL_HANDLE;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // Create new texture image
    createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureAtlasImage, textureAtlasImageMemory);

    // Transition + copy via one-shot command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_LOG_IF_FAILED(vkAllocateCommandBuffers(device, &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_LOG_IF_FAILED(vkBeginCommandBuffer(cmd, &beginInfo));

    // Barrier: undefined → transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureAtlasImage;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer → image
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, textureAtlasImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Barrier: transfer dst → shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VK_LOG_IF_FAILED(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // Recreate image view
    textureAtlasImageView = createImageView(textureAtlasImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

    // Update descriptor sets so they point to the new image view
    // Only if sampler exists (during hot-reload). During initial setup,
    // the caller creates the sampler and updates descriptors afterwards.
    if (textureAtlasSampler != VK_NULL_HANDLE) {
        updateDescriptorSetsWithTexture();
    }

    LOG_INFO("Vulkan", "Texture atlas re-uploaded ({}x{})", width, height);
    return true;
}

bool VulkanDevice::uploadTextureArray(int target, const uint8_t* pixels, int texSize, int layerCount) {
    if (!pixels || texSize <= 0 || layerCount <= 0) return false;

    // target: 0=albedo512(binding1) 1=albedo1024(binding5) 2=normal512(binding6) 3=normal1024(binding7)
    VkImage* imgP; VkDeviceMemory* memP; VkImageView* viewP;
    switch (target) {
        case 1:  imgP=&textureArrayHiImage;    memP=&textureArrayHiImageMemory;    viewP=&textureArrayHiImageView;    break;
        case 2:  imgP=&textureNormal512Image;  memP=&textureNormal512ImageMemory;  viewP=&textureNormal512ImageView;  break;
        case 3:  imgP=&textureNormal1024Image; memP=&textureNormal1024ImageMemory; viewP=&textureNormal1024ImageView; break;
        default: imgP=&textureAtlasImage;      memP=&textureAtlasImageMemory;      viewP=&textureAtlasImageView;      break;
    }
    VkImage& img = *imgP; VkDeviceMemory& mem = *memP; VkImageView& view = *viewP;

    // Normal+roughness maps are linear data; albedo is sRGB.
    const VkFormat format = (target >= 2) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;

    // Decide mip count — only enable mip generation if the format supports linear blit.
    VkFormatProperties fmtProps{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &fmtProps);
    const bool canMip =
        (fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
        (fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) &&
        (fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    uint32_t mipLevels = 1;
    if (canMip) {
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(texSize))) + 1u;
    } else {
        LOG_WARN("Vulkan", "Texture array format lacks linear-blit support; skipping mipmaps");
    }

    vkDeviceWaitIdle(device);

    // Destroy old image resources
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (img != VK_NULL_HANDLE) {
        vkDestroyImage(device, img, nullptr);
        img = VK_NULL_HANDLE;
    }
    if (mem != VK_NULL_HANDLE) {
        vkFreeMemory(device, mem, nullptr);
        mem = VK_NULL_HANDLE;
    }

    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(texSize) * texSize * 4 * layerCount;

    // Staging buffer holds all layers (mip 0) contiguously, layer-major.
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // Create the 2D array image with a full mip chain.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { static_cast<uint32_t>(texSize), static_cast<uint32_t>(texSize), 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = static_cast<uint32_t>(layerCount);
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &imageInfo, nullptr, &img) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create texture array image");
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, img, &memReq);
    VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.allocationSize = memReq.size;
    memAlloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &memAlloc, nullptr, &mem) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate texture array memory");
        vkDestroyImage(device, img, nullptr); img = VK_NULL_HANDLE;
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return false;
    }
    vkBindImageMemory(device, img, mem, 0);

    // One-shot command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_LOG_IF_FAILED(vkAllocateCommandBuffers(device, &allocInfo, &cmd));
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_LOG_IF_FAILED(vkBeginCommandBuffer(cmd, &beginInfo));

    const uint32_t nLayers = static_cast<uint32_t>(layerCount);

    // Transition ALL mips + layers UNDEFINED -> TRANSFER_DST
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, nLayers };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy staging -> mip 0 (all layers in one copy; buffer is layer-major)
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, nLayers };
    region.imageExtent = { static_cast<uint32_t>(texSize), static_cast<uint32_t>(texSize), 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, img,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Generate mip chain by successive linear blits (across all layers each level).
    int32_t mipW = texSize, mipH = texSize;
    for (uint32_t i = 1; i < mipLevels; i++) {
        // level i-1: TRANSFER_DST -> TRANSFER_SRC
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, nLayers };
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipW, mipH, 1 };
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, nLayers };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipW > 1 ? mipW / 2 : 1, mipH > 1 ? mipH / 2 : 1, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, nLayers };
        vkCmdBlitImage(cmd,
            img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // level i-1: TRANSFER_SRC -> SHADER_READ
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        if (mipW > 1) mipW /= 2;
        if (mipH > 1) mipH /= 2;
    }

    // Last mip level: TRANSFER_DST -> SHADER_READ
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, nLayers };
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VK_LOG_IF_FAILED(vkEndCommandBuffer(cmd));
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // Create a 2D_ARRAY view spanning all mips + layers.
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = img;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, nLayers };
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create texture array image view");
        return false;
    }

    // Refresh descriptors if the sampler already exists (hot-reload path). At first-time
    // startup the sampler is created afterwards and descriptors are written by the caller.
    if (textureAtlasSampler != VK_NULL_HANDLE) {
        updateDescriptorSetsWithTexture();
    }

    LOG_INFO("Vulkan", "Texture array uploaded (class {}, {} layers @ {}x{}, {} mips)",
             target, layerCount, texSize, texSize, mipLevels);
    return true;
}

bool VulkanDevice::uploadTextureArrayBC7(int target, const uint8_t* data, size_t dataSize,
                                         const std::vector<size_t>& levelByteOffsets,
                                         int baseSize, int layerCount, int mipLevels) {
    if (!data || dataSize == 0 || baseSize <= 0 || layerCount <= 0 || mipLevels <= 0) return false;
    if (static_cast<int>(levelByteOffsets.size()) != mipLevels) return false;

    // target: 0=albedo512(binding1) 1=albedo1024(binding5) 2=normal512(binding6) 3=normal1024(binding7)
    VkImage* imgP; VkDeviceMemory* memP; VkImageView* viewP;
    switch (target) {
        case 1:  imgP=&textureArrayHiImage;    memP=&textureArrayHiImageMemory;    viewP=&textureArrayHiImageView;    break;
        case 2:  imgP=&textureNormal512Image;  memP=&textureNormal512ImageMemory;  viewP=&textureNormal512ImageView;  break;
        case 3:  imgP=&textureNormal1024Image; memP=&textureNormal1024ImageMemory; viewP=&textureNormal1024ImageView; break;
        default: imgP=&textureAtlasImage;      memP=&textureAtlasImageMemory;      viewP=&textureAtlasImageView;      break;
    }
    VkImage& img = *imgP; VkDeviceMemory& mem = *memP; VkImageView& view = *viewP;

    // Normal+roughness maps are linear data; albedo is sRGB.
    const VkFormat format = (target >= 2) ? VK_FORMAT_BC7_UNORM_BLOCK : VK_FORMAT_BC7_SRGB_BLOCK;
    const uint32_t nLayers = static_cast<uint32_t>(layerCount);
    const uint32_t nMips = static_cast<uint32_t>(mipLevels);

    vkDeviceWaitIdle(device);

    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr); view = VK_NULL_HANDLE;
    }
    if (img != VK_NULL_HANDLE) {
        vkDestroyImage(device, img, nullptr); img = VK_NULL_HANDLE;
    }
    if (mem != VK_NULL_HANDLE) {
        vkFreeMemory(device, mem, nullptr); mem = VK_NULL_HANDLE;
    }

    // Staging buffer with the entire compressed blob.
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer, stagingBufferMemory);
    void* mapped;
    vkMapMemory(device, stagingBufferMemory, 0, dataSize, 0, &mapped);
    memcpy(mapped, data, dataSize);
    vkUnmapMemory(device, stagingBufferMemory);

    // Create the BC7 2D array image (mips precomputed — no blit).
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { static_cast<uint32_t>(baseSize), static_cast<uint32_t>(baseSize), 1 };
    imageInfo.mipLevels = nMips;
    imageInfo.arrayLayers = nLayers;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &imageInfo, nullptr, &img) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create BC7 texture array image");
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return false;
    }
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, img, &memReq);
    VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.allocationSize = memReq.size;
    memAlloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &memAlloc, nullptr, &mem) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to allocate BC7 texture array memory");
        vkDestroyImage(device, img, nullptr); img = VK_NULL_HANDLE;
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        return false;
    }
    vkBindImageMemory(device, img, mem, 0);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_LOG_IF_FAILED(vkAllocateCommandBuffers(device, &allocInfo, &cmd));
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_LOG_IF_FAILED(vkBeginCommandBuffer(cmd, &beginInfo));

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, nMips, 0, nLayers };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // One copy region per mip level; all layers contiguous within a level.
    std::vector<VkBufferImageCopy> regions(nMips);
    for (uint32_t i = 0; i < nMips; i++) {
        uint32_t dim = std::max(1u, static_cast<uint32_t>(baseSize) >> i);
        VkBufferImageCopy& r = regions[i];
        r = {};
        r.bufferOffset = levelByteOffsets[i];
        r.bufferRowLength = 0;    // tightly packed
        r.bufferImageHeight = 0;
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, nLayers };
        r.imageExtent = { dim, dim, 1 };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, img,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          static_cast<uint32_t>(regions.size()), regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VK_LOG_IF_FAILED(vkEndCommandBuffer(cmd));
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = img;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, nMips, 0, nLayers };
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create BC7 texture array image view");
        return false;
    }

    if (textureAtlasSampler != VK_NULL_HANDLE) {
        updateDescriptorSetsWithTexture();
    }

    LOG_INFO("Vulkan", "BC7 texture array uploaded (class {}, {} layers @ {}x{}, {} mips, {} MB)",
             target, layerCount, baseSize, baseSize, mipLevels,
             static_cast<int>(dataSize / (1024 * 1024)));
    return true;
}

bool VulkanDevice::createTextureAtlasSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Texture-array path: trilinear filtering + full mip chain (anisotropy not enabled as
    // a device feature, so left off). REPEAT wrap handles greedy-merged faces whose UVs
    // run 0..sizeU; each array layer is a full tile so there is no atlas bleed to clamp.
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureAtlasSampler) != VK_SUCCESS) {
        LOG_ERROR("Vulkan", "Failed to create texture sampler!");
        return false;
    }

    LOG_DEBUG("Vulkan", "Texture atlas sampler created successfully");
    return true;
}

void VulkanDevice::updateDescriptorSetsWithTexture() {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // UBO descriptor (binding 0)
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        // Texture descriptor (binding 1)
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textureAtlasImageView;
        imageInfo.sampler = textureAtlasSampler;

        // Shadow map descriptor (binding 2)
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMapImageView ? shadowMapImageView : textureAtlasImageView;
        shadowInfo.sampler = shadowMapSampler ? shadowMapSampler : textureAtlasSampler;

        // Light SSBO descriptor (binding 3)
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = lightBuffers[i];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(Graphics::LightBufferGPU);

        // Atlas UV SSBO descriptor (binding 4)
        VkDescriptorBufferInfo atlasUVBufferInfo{};
        atlasUVBufferInfo.buffer = atlasUVBuffers[i];
        atlasUVBufferInfo.offset = 0;
        atlasUVBufferInfo.range = VK_WHOLE_SIZE;

        // Hi-res (1024) texture array descriptor (binding 5). Falls back to the 512 view if the
        // 1024 class is empty so the binding is always valid.
        VkDescriptorImageInfo imageHiInfo{};
        imageHiInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageHiInfo.imageView = textureArrayHiImageView ? textureArrayHiImageView : textureAtlasImageView;
        imageHiInfo.sampler = textureAtlasSampler;

        // Normal+roughness descriptors (bindings 6/7), each falling back to its albedo view.
        VkDescriptorImageInfo normal512Info{};
        normal512Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normal512Info.imageView = textureNormal512ImageView ? textureNormal512ImageView : textureAtlasImageView;
        normal512Info.sampler = textureAtlasSampler;

        VkDescriptorImageInfo normal1024Info{};
        normal1024Info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normal1024Info.imageView = textureNormal1024ImageView ? textureNormal1024ImageView :
                                   (textureArrayHiImageView ? textureArrayHiImageView : textureAtlasImageView);
        normal1024Info.sampler = textureAtlasSampler;

        std::array<VkWriteDescriptorSet, 8> descriptorWrites{};

        // UBO write
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        // Texture write
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        // Shadow map write
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &shadowInfo;

        // Light SSBO write
        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &lightBufferInfo;

        // Atlas UV SSBO write
        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &atlasUVBufferInfo;

        // Hi-res texture array write (binding 5)
        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = descriptorSets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pImageInfo = &imageHiInfo;

        // Normal+roughness writes (bindings 6/7)
        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = descriptorSets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &normal512Info;

        descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[7].dstSet = descriptorSets[i];
        descriptorWrites[7].dstBinding = 7;
        descriptorWrites[7].dstArrayElement = 0;
        descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[7].descriptorCount = 1;
        descriptorWrites[7].pImageInfo = &normal1024Info;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    
    LOG_DEBUG("Vulkan", "Descriptor sets updated with texture atlas and light SSBO");
}

void VulkanDevice::cleanupTextureAtlas() {
    if (textureAtlasSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, textureAtlasSampler, nullptr);
        textureAtlasSampler = VK_NULL_HANDLE;
    }
    if (textureAtlasImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, textureAtlasImageView, nullptr);
        textureAtlasImageView = VK_NULL_HANDLE;
    }
    if (textureAtlasImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, textureAtlasImage, nullptr);
        textureAtlasImage = VK_NULL_HANDLE;
    }
    if (textureAtlasImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, textureAtlasImageMemory, nullptr);
        textureAtlasImageMemory = VK_NULL_HANDLE;
    }
    // Hi-res (1024) class resources
    if (textureArrayHiImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, textureArrayHiImageView, nullptr);
        textureArrayHiImageView = VK_NULL_HANDLE;
    }
    if (textureArrayHiImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, textureArrayHiImage, nullptr);
        textureArrayHiImage = VK_NULL_HANDLE;
    }
    if (textureArrayHiImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, textureArrayHiImageMemory, nullptr);
        textureArrayHiImageMemory = VK_NULL_HANDLE;
    }
    // Normal+roughness resources
    VkImageView nrViews[2] = { textureNormal512ImageView, textureNormal1024ImageView };
    VkImage nrImages[2] = { textureNormal512Image, textureNormal1024Image };
    VkDeviceMemory nrMems[2] = { textureNormal512ImageMemory, textureNormal1024ImageMemory };
    for (int k = 0; k < 2; k++) {
        if (nrViews[k] != VK_NULL_HANDLE) vkDestroyImageView(device, nrViews[k], nullptr);
        if (nrImages[k] != VK_NULL_HANDLE) vkDestroyImage(device, nrImages[k], nullptr);
        if (nrMems[k] != VK_NULL_HANDLE) vkFreeMemory(device, nrMems[k], nullptr);
    }
    textureNormal512ImageView = textureNormal1024ImageView = VK_NULL_HANDLE;
    textureNormal512Image = textureNormal1024Image = VK_NULL_HANDLE;
    textureNormal512ImageMemory = textureNormal1024ImageMemory = VK_NULL_HANDLE;
}

void* VulkanDevice::loadImGuiTexture(const std::string& path) {
    // Return cached handle if already loaded
    auto it = imguiTextureCache_.find(path);
    if (it != imguiTextureCache_.end()) {
        return reinterpret_cast<void*>(it->second.descriptorSet);
    }

    // Load pixels via stb_image
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_WARN("Vulkan", "loadImGuiTexture: failed to load '{}'", path);
        return nullptr;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    // Upload via staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingMemory);
    stbi_image_free(pixels);

    ImGuiTextureEntry entry;
    createImage(w, h, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, entry.image, entry.memory);

    // Transition + copy using a one-shot command buffer
    {
        VkCommandBuffer cmd = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entry.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, entry.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(cmd);
    }

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    entry.view = createImageView(entry.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(device, &samplerInfo, nullptr, &entry.sampler);

    // Register with ImGui — returns a VkDescriptorSet usable as ImTextureID
    entry.descriptorSet = ImGui_ImplVulkan_AddTexture(
        entry.sampler, entry.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    imguiTextureCache_[path] = entry;
    LOG_INFO("Vulkan", "Loaded ImGui texture '{}' ({}x{})", path, w, h);
    return reinterpret_cast<void*>(entry.descriptorSet);
}

void VulkanDevice::cleanupImGuiTextures() {
    vkDeviceWaitIdle(device);
    for (auto& [path, entry] : imguiTextureCache_) {
        if (entry.descriptorSet != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(entry.descriptorSet);
        if (entry.sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, entry.sampler, nullptr);
        if (entry.view != VK_NULL_HANDLE)
            vkDestroyImageView(device, entry.view, nullptr);
        if (entry.image != VK_NULL_HANDLE)
            vkDestroyImage(device, entry.image, nullptr);
        if (entry.memory != VK_NULL_HANDLE)
            vkFreeMemory(device, entry.memory, nullptr);
    }
    imguiTextureCache_.clear();
}

bool VulkanDevice::recreateSwapChain(int windowWidth, int windowHeight, VkRenderPass renderPass) {
    LOG_INFO("Vulkan", "Attempting to recreate swapchain: {}x{}", windowWidth, windowHeight);

    // Handle minimization - wait until window is visible again
    if (windowWidth == 0 || windowHeight == 0) {
        LOG_INFO("Vulkan", "Window minimized, skipping swapchain recreation");
        return false; // Signal to try again later
    }

    // Wait for device to be idle
    vkDeviceWaitIdle(device);

    // Clean up existing swapchain resources
    cleanupSwapChain();

    // Recreate swapchain and its dependencies (createSwapChain includes image view creation)
    if (!createSwapChain(windowWidth, windowHeight)) {
        LOG_ERROR("Vulkan", "Failed to recreate swapchain!");
        return false;
    }

    if (!createDepthResources()) {
        LOG_ERROR("Vulkan", "Failed to recreate depth resources!");
        return false;
    }

    if (!createFramebuffers(renderPass)) {
        LOG_ERROR("Vulkan", "Failed to recreate framebuffers!");
        return false;
    }

    // Reset the resize flag
    framebufferResized = false;

    LOG_INFO_FMT("Vulkan", "Swapchain recreated successfully for size " << windowWidth << "x" << windowHeight);
    return true;
}

void VulkanDevice::cleanupSwapChain() {
    // Cleanup depth resources
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }

    // Cleanup framebuffers
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapChainFramebuffers.clear();

    // Cleanup image views
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    swapChainImageViews.clear();

    // Cleanup swapchain
    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    LOG_WARN_FMT("Vulkan", "Validation layer: " << pCallbackData->pMessage);
    return VK_FALSE;
}

} // namespace Vulkan
} // namespace Phyxel
