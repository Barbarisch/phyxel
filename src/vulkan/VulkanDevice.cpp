#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "core/Types.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>
#include <array>

namespace VulkanCube {
namespace Vulkan {

VulkanDevice::VulkanDevice() {
    // Constructor - initialize everything to default state
}

VulkanDevice::~VulkanDevice() {
    cleanup();
}

bool VulkanDevice::initialize() {
    if (!createInstance()) {
        std::cerr << "Failed to create Vulkan instance!" << std::endl;
        return false;
    }

    if (!setupDebugMessenger()) {
        std::cerr << "Failed to set up debug messenger!" << std::endl;
        return false;
    }

    if (!pickPhysicalDevice()) {
        std::cerr << "Failed to find a suitable GPU!" << std::endl;
        return false;
    }

    if (!createLogicalDevice()) {
        std::cerr << "Failed to create logical device!" << std::endl;
        return false;
    }

    return true;
}

void VulkanDevice::cleanup() {
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
    
    // Cleanup frustum culling buffers
    cleanupFrustumCullingBuffers();
    
    // Cleanup compute descriptors
    cleanupComputeDescriptors();
    
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
        std::cerr << "Validation layers requested, but not available!" << std::endl;
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VulkanCube";
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
        std::cerr << "Failed to create instance! Error: " << result << std::endl;
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
        std::cerr << "Failed to load debug messenger function!" << std::endl;
        return false;
    }
}

bool VulkanDevice::createSurface(void* window) {
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window);
    
    if (glfwCreateWindowSurface(instance, glfwWindow, nullptr, &surface) != VK_SUCCESS) {
        std::cerr << "Failed to create window surface!" << std::endl;
        return false;
    }
    
    std::cout << "Vulkan surface created successfully" << std::endl;
    return true;
}

bool VulkanDevice::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "Failed to find GPUs with Vulkan support!" << std::endl;
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
        std::cerr << "Failed to find a suitable GPU!" << std::endl;
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
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
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

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

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
        std::cerr << "Failed to create logical device! Error: " << result << std::endl;
        return false;
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);

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
    // First preference: IMMEDIATE for uncapped FPS (no V-Sync) to measure true performance
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return availablePresentMode;
        }
    }
    
    // Second preference: MAILBOX for smooth experience with triple buffering
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    
    // Fallback: FIFO (V-Sync) - guaranteed to be available
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
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

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
        std::cerr << "Failed to create swap chain!" << std::endl;
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
            std::cerr << "Failed to create image view!" << std::endl;
            return false;
        }
    }

    std::cout << "Swapchain created successfully" << std::endl;
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
            std::cerr << "Failed to create framebuffer!" << std::endl;
            return false;
        }
    }

    std::cout << "Framebuffers created successfully" << std::endl;
    return true;
}

bool VulkanDevice::createCommandBuffers() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = findQueueFamilies(physicalDevice).graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool!" << std::endl;
        return false;
    }

    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffers!" << std::endl;
        return false;
    }

    std::cout << "Command buffers created successfully" << std::endl;
    return true;
}

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
            std::cerr << "Failed to create synchronization objects for a frame!" << std::endl;
            return false;
        }
    }

    std::cout << "Sync objects created successfully" << std::endl;
    return true;
}

// Command buffer operations
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

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[frameIndex]) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool VulkanDevice::presentFrame(uint32_t imageIndex, uint32_t frameIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores[frameIndex];

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
    
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
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
    redCube.color = {1.0f, 0.0f, 0.0f}; // Red cube
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

bool VulkanDevice::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout!" << std::endl;
        return false;
    }

    return true;
}

bool VulkanDevice::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor pool!" << std::endl;
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
        std::cerr << "Failed to allocate descriptor sets!" << std::endl;
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

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void VulkanDevice::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VulkanDevice::updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj, uint32_t numInstances) {
    UniformBufferObject ubo{};
    ubo.view = view;
    ubo.proj = proj;
    ubo.numInstances = numInstances;

    // Debug: Log matrix data for the first few frames
    static int debugFrameCount = 0;
    if (debugFrameCount < 3) {
        std::cout << "[DEBUG] Frame " << debugFrameCount << " - Matrix upload:" << std::endl;
        std::cout << "  View[0]: " << view[0][0] << ", " << view[0][1] << ", " << view[0][2] << ", " << view[0][3] << std::endl;
        std::cout << "  Proj[0]: " << proj[0][0] << ", " << proj[0][1] << ", " << proj[0][2] << ", " << proj[0][3] << std::endl;
        std::cout << "  NumInstances: " << numInstances << std::endl;
        debugFrameCount++;
    }

    void* data;
    vkMapMemory(device, uniformBuffersMemory[frameIndex], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(device, uniformBuffersMemory[frameIndex]);
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
    std::cout << "[BUFFER DEBUG] updateDynamicSubcubeBuffer called with " << dynamicSubcubes.size() << " subcubes" << std::endl;
    std::cout << "[BUFFER DEBUG] dynamicSubcubeBuffer handle: " << dynamicSubcubeBuffer << std::endl;
    
    if (dynamicSubcubes.empty() || dynamicSubcubeBuffer == VK_NULL_HANDLE) {
        std::cout << "[BUFFER DEBUG] Early return - empty subcubes or null buffer handle" << std::endl;
        return;
    }
    
    VkDeviceSize bufferSize = sizeof(DynamicSubcubeInstanceData) * std::min(static_cast<uint32_t>(dynamicSubcubes.size()), maxDynamicSubcubes);
    std::cout << "[BUFFER DEBUG] Updating buffer with " << bufferSize << " bytes" << std::endl;
    
    void* data;
    vkMapMemory(device, dynamicSubcubeBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, dynamicSubcubes.data(), (size_t) bufferSize);
    vkUnmapMemory(device, dynamicSubcubeBufferMemory);
    
    std::cout << "[BUFFER DEBUG] Buffer update complete" << std::endl;
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

void VulkanDevice::drawIndexed(uint32_t frameIndex, uint32_t indexCount, uint32_t instanceCount) {
    vkCmdDrawIndexed(commandBuffers[frameIndex], indexCount, instanceCount, 0, 0, 0);
}

void VulkanDevice::pushConstants(uint32_t frameIndex, VkPipelineLayout pipelineLayout, const glm::vec3& chunkBaseOffset) {
    struct PushConstants {
        glm::vec3 chunkBaseOffset;
    } pushData;
    pushData.chunkBaseOffset = chunkBaseOffset;
    
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
    // Push chunk base offset
    pushConstants(frameIndex, pipelineLayout, chunkBaseOffset);
    
    // Bind instance buffer at the correct offset for this chunk
    bindInstanceBufferWithOffset(frameIndex, instanceOffset);
    
    // Draw all cubes in this chunk (36 indices per cube)
    vkCmdDrawIndexed(commandBuffers[frameIndex], 36, instanceCount, 0, 0, 0);
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
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// Frustum culling buffer management
bool VulkanDevice::createFrustumCullingBuffers(uint32_t maxInstances) {
    std::cout << "[DEBUG] Creating frustum culling buffers for " << maxInstances << " instances" << std::endl;
    
    // Create AABB buffer (2 vec4s per instance: min+max.x, max.yz+unused)
    VkDeviceSize aabbBufferSize = sizeof(glm::vec4) * 2 * maxInstances;
    
    createBuffer(aabbBufferSize, 
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
                 aabbBuffer, aabbBufferMemory);
    
    std::cout << "[DEBUG] Created AABB buffer: " << aabbBufferSize << " bytes" << std::endl;
    
    // Create visibility buffer (1 uint per instance)
    VkDeviceSize visibilityBufferSize = sizeof(uint32_t) * maxInstances;
    
    createBuffer(visibilityBufferSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 visibilityBuffer, visibilityBufferMemory);
    
    std::cout << "[DEBUG] Created visibility buffer: " << visibilityBufferSize << " bytes" << std::endl;
    std::cout << "Frustum culling buffers created successfully" << std::endl;
    return true;
}

void VulkanDevice::updateAABBBuffer(const std::vector<glm::vec3>& positions) {
    if (positions.empty()) {
        std::cout << "[DEBUG] updateAABBBuffer: No positions provided" << std::endl;
        return;
    }
    
    std::cout << "[DEBUG] updateAABBBuffer: Updating " << positions.size() << " positions" << std::endl;
    
    // Debug: Log first few cube positions and their AABBs
    for (size_t i = 0; i < std::min(positions.size(), size_t(5)); ++i) {
        const auto& pos = positions[i];
        glm::vec3 minCorner = pos - 0.5f;
        glm::vec3 maxCorner = pos + 0.5f;
        std::cout << "[DEBUG] Cube " << i << ": pos(" << pos.x << "," << pos.y << "," << pos.z 
                  << ") -> AABB min(" << minCorner.x << "," << minCorner.y << "," << minCorner.z
                  << ") max(" << maxCorner.x << "," << maxCorner.y << "," << maxCorner.z << ")" << std::endl;
    }
    
    // Generate AABBs for unit cubes at each position
    std::vector<glm::vec4> aabbData;
    aabbData.reserve(positions.size() * 2);
    
    for (const auto& pos : positions) {
        // AABB for unit cube: min = pos - 0.5, max = pos + 0.5
        glm::vec3 minCorner = pos - 0.5f;
        glm::vec3 maxCorner = pos + 0.5f;
        
        // Pack as vec4(min.x, min.y, min.z, max.x), vec4(max.y, max.z, unused, unused)
        aabbData.emplace_back(minCorner.x, minCorner.y, minCorner.z, maxCorner.x);
        aabbData.emplace_back(maxCorner.y, maxCorner.z, 0.0f, 0.0f);
    }
    
    // Upload to GPU using staging buffer
    VkDeviceSize bufferSize = sizeof(glm::vec4) * aabbData.size();
    std::cout << "[DEBUG] Uploading " << bufferSize << " bytes of AABB data" << std::endl;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);
    
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, aabbData.data(), (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);
    
    copyBuffer(stagingBuffer, aabbBuffer, bufferSize);
    
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    std::cout << "[DEBUG] AABB buffer update complete" << std::endl;
}

void VulkanDevice::cleanupFrustumCullingBuffers() {
    if (aabbBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, aabbBuffer, nullptr);
        aabbBuffer = VK_NULL_HANDLE;
    }
    if (aabbBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, aabbBufferMemory, nullptr);
        aabbBufferMemory = VK_NULL_HANDLE;
    }
    
    if (visibilityBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, visibilityBuffer, nullptr);
        visibilityBuffer = VK_NULL_HANDLE;
    }
    if (visibilityBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, visibilityBufferMemory, nullptr);
        visibilityBufferMemory = VK_NULL_HANDLE;
    }
}

// Compute descriptor management
bool VulkanDevice::createComputeDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * 2); // AABB + Visibility buffers
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    
    VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &computeDescriptorPool);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create compute descriptor pool! Error: " << result << std::endl;
        return false;
    }
    
    std::cout << "Compute descriptor pool created successfully" << std::endl;
    return true;
}

bool VulkanDevice::createComputeDescriptorSets(RenderPipeline* renderPipeline) {
    if (!renderPipeline) {
        std::cerr << "RenderPipeline is null!" << std::endl;
        return false;
    }
    
    VkDescriptorSetLayout computeLayout = renderPipeline->getComputeDescriptorSetLayout();
    if (computeLayout == VK_NULL_HANDLE) {
        std::cerr << "Compute descriptor set layout is null!" << std::endl;
        return false;
    }
    
    // Ensure required buffers exist
    if (aabbBuffer == VK_NULL_HANDLE || visibilityBuffer == VK_NULL_HANDLE) {
        std::cerr << "AABB or visibility buffer not created yet!" << std::endl;
        return false;
    }
    
    if (uniformBuffers.empty()) {
        std::cerr << "Uniform buffers not created yet!" << std::endl;
        return false;
    }
    
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, computeLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = computeDescriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();
    
    computeDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, computeDescriptorSets.data());
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to allocate compute descriptor sets! Error: " << result << std::endl;
        return false;
    }
    
    // Update descriptor sets to bind our storage buffers
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        
        // Binding 0: AABB Buffer
        VkDescriptorBufferInfo aabbBufferInfo{};
        aabbBufferInfo.buffer = aabbBuffer;
        aabbBufferInfo.offset = 0;
        aabbBufferInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = computeDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &aabbBufferInfo;
        
        // Binding 1: Visibility Buffer
        VkDescriptorBufferInfo visibilityBufferInfo{};
        visibilityBufferInfo.buffer = visibilityBuffer;
        visibilityBufferInfo.offset = 0;
        visibilityBufferInfo.range = VK_WHOLE_SIZE;
        
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = computeDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &visibilityBufferInfo;
        
        // Binding 2: Uniform Buffer
        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = uniformBuffers[i];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(UniformBufferObject);
        
        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = computeDescriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &uniformBufferInfo;
        
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                              descriptorWrites.data(), 0, nullptr);
    }
    
    std::cout << "Compute descriptor sets created and updated successfully" << std::endl;
    return true;
}

void VulkanDevice::dispatchFrustumCulling(uint32_t frameIndex, RenderPipeline* renderPipeline, uint32_t numInstances) {
    if (!renderPipeline || computeDescriptorSets.empty()) {
        std::cout << "[DEBUG] Skipping compute dispatch - not initialized" << std::endl;
        return; // Skip if not properly initialized
    }
    
    VkCommandBuffer commandBuffer = commandBuffers[frameIndex];
    
    // Bind compute pipeline
    renderPipeline->bindComputePipeline(commandBuffer);
    
    // Bind compute descriptor sets
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           renderPipeline->getComputeLayout(), 0, 1,
                           &computeDescriptorSets[frameIndex], 0, nullptr);
    
    // Calculate workgroup count (64 threads per workgroup as defined in shader)
    uint32_t workgroupCount = (numInstances + 63) / 64; // Round up division
    
    // Dispatch compute shader
    vkCmdDispatch(commandBuffer, workgroupCount, 1, 1);
    
    // Add memory barrier to ensure compute writes are visible to subsequent operations
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
}

std::vector<uint32_t> VulkanDevice::downloadVisibilityResults(uint32_t numInstances) {
    std::vector<uint32_t> results(numInstances, 0);
    
    if (visibilityBuffer == VK_NULL_HANDLE || numInstances == 0) {
        std::cout << "[DEBUG] Cannot download visibility results - buffer not ready" << std::endl;
        return results;
    }
    
    // Create staging buffer for download
    VkDeviceSize bufferSize = sizeof(uint32_t) * numInstances;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);
    
    // Copy from device-local visibility buffer to host-visible staging buffer
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, visibilityBuffer, stagingBuffer, 1, &copyRegion);
    
    endSingleTimeCommands(commandBuffer);
    
    // Map and read the data
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(results.data(), data, (size_t)bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);
    
    // Cleanup staging buffer
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    // Count visible/culled
    uint32_t visibleCount = 0;
    for (uint32_t result : results) {
        if (result) visibleCount++;
    }
    
    return results;
}

void VulkanDevice::cleanupComputeDescriptors() {
    if (computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, computeDescriptorPool, nullptr);
        computeDescriptorPool = VK_NULL_HANDLE;
    }
    computeDescriptorSets.clear();
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

} // namespace Vulkan
} // namespace VulkanCube
