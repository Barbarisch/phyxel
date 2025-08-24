#include "ui/ImGuiRenderer.h"
#include "core/Types.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "scene/SceneManager.h"
#include "physics/PhysicsWorld.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <glm/glm.hpp>
#include <iostream>
#include <iomanip>

namespace VulkanCube::UI {

ImGuiRenderer::ImGuiRenderer()
    : m_context(nullptr)
    , m_initialized(false)
    , m_vulkanDevice(nullptr)
    , m_renderPipeline(nullptr)
    , m_window(nullptr) {
}

ImGuiRenderer::~ImGuiRenderer() {
    cleanup();
}

bool ImGuiRenderer::initialize(GLFWwindow* window, Vulkan::VulkanDevice* vulkanDevice, Vulkan::RenderPipeline* renderPipeline) {
    m_window = window;
    m_vulkanDevice = vulkanDevice;
    m_renderPipeline = renderPipeline;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        std::cerr << "Failed to initialize ImGui GLFW backend!" << std::endl;
        return false;
    }

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    if (vkCreateDescriptorPool(vulkanDevice->getDevice(), &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
        std::cerr << "Failed to create ImGui descriptor pool!" << std::endl;
        return false;
    }

    // Initialize ImGui Vulkan backend
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.Instance = vulkanDevice->getInstance();
    init_info.PhysicalDevice = vulkanDevice->getPhysicalDevice();
    init_info.Device = vulkanDevice->getDevice();
    init_info.QueueFamily = vulkanDevice->getGraphicsQueueFamily();
    init_info.Queue = vulkanDevice->getGraphicsQueue();
    init_info.DescriptorPool = imguiPool;
    init_info.RenderPass = renderPipeline->getRenderPass();
    init_info.MinImageCount = vulkanDevice->getSwapChainImageCount();
    init_info.ImageCount = vulkanDevice->getSwapChainImageCount();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.Subpass = 0;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        std::cerr << "Failed to initialize ImGui Vulkan backend!" << std::endl;
        return false;
    }

    // Note: Font upload will be handled automatically by ImGui
    m_initialized = true;
    return true;
}

void ImGuiRenderer::cleanup() {
    if (m_initialized) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        
        if (m_context) {
            ImGui::DestroyContext(m_context);
            m_context = nullptr;
        }
        
        m_initialized = false;
    }
}

void ImGuiRenderer::newFrame() {
    if (!m_initialized) return;
    
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiRenderer::endFrame() {
    if (!m_initialized) return;
    
    ImGui::Render();
}

void ImGuiRenderer::render(uint32_t currentFrame, uint32_t imageIndex) {
    if (!m_initialized) return;
    
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData) {
        ImGui_ImplVulkan_RenderDrawData(drawData, m_vulkanDevice->getCommandBuffer(currentFrame));
    }
}

void ImGuiRenderer::renderPerformanceOverlay(
    bool showOverlay,
    Timer* timer,
    PerformanceProfiler* performanceProfiler,
    const FrameTiming& frameTiming,
    const std::vector<DetailedFrameTiming>& detailedTimings,
    Scene::SceneManager* sceneManager,
    Physics::PhysicsWorld* physicsWorld,
    const glm::vec3& cameraPos,
    uint64_t frameCount) {
    
    if (!showOverlay || !m_initialized) return;

    // Create a window for the performance overlay
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Performance Overlay", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        
        // Frame Performance
        float fps = timer->getFPS();
        ImGui::Text("FRAME PERFORMANCE");
        ImGui::Separator();
        ImGui::Text("FPS: %.1f fps", fps);
        ImGui::SameLine();
        ImGui::Text("Frame Time: %.2f ms", 1000.0f / fps);
        
        ImGui::Spacing();
        
        // Rendering Stats
        ImGui::Text("RENDERING STATS");
        ImGui::Separator();
        ImGui::Text("Cubes: %zu", sceneManager->getCubeCount());
        ImGui::SameLine();
        ImGui::Text("Vertices: %d", frameTiming.vertexCount);
        
        ImGui::Text("Draw Calls: %d", frameTiming.drawCalls);
        ImGui::SameLine();
        ImGui::Text("Visible Instances: %d", frameTiming.visibleInstances);
        
        // Frustum culling stats (CPU chunk-level culling)
        ImGui::Text("Frustum Culled: %d", frameTiming.frustumCulledInstances);
        ImGui::SameLine();
        float frustumCullRate = frameTiming.visibleInstances > 0 ? 
                               (float)frameTiming.frustumCulledInstances / (frameTiming.visibleInstances + frameTiming.frustumCulledInstances) * 100.0f : 0.0f;
        ImGui::Text("Rate: %.1f%%", frustumCullRate);
        
        // Modern chunk-based occlusion culling stats (CPU neighbor detection)
        ImGui::Text("Fully Hidden Cubes: %d", frameTiming.fullyOccludedCubes);
        ImGui::SameLine();
        ImGui::Text("Partially Hidden: %d", frameTiming.partiallyOccludedCubes);
        
        ImGui::Text("Hidden Faces: %d", frameTiming.totalHiddenFaces);
        ImGui::SameLine();
        ImGui::Text("Face Reduction: %d%%", frameTiming.totalHiddenFaces > 0 ? 
                   (frameTiming.totalHiddenFaces * 100) / (frameTiming.visibleInstances * 6) : 0);
        
        // Note about culling methods
        ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.8f, 1.0f), "Frustum: GPU compute shader (instance-level)");
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Occlusion: CPU neighbor detection (face-level)");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1.0f), "Final: GPU vertex shader applies both");
        
        // Show efficiency
        int totalCubes = frameTiming.visibleInstances;
        if (totalCubes > 0) {
            float occlusionEfficiency = (float)frameTiming.fullyOccludedCubes / totalCubes * 100.0f;
            ImGui::Text("Full Occlusion Rate: %.1f%%", occlusionEfficiency);
            
            float partialEfficiency = (float)frameTiming.partiallyOccludedCubes / totalCubes * 100.0f;
            ImGui::Text("Partial Occlusion Rate: %.1f%%", partialEfficiency);
        } else {
            ImGui::Text("Full Occlusion Rate: 0.0%%");
            ImGui::Text("Partial Occlusion Rate: 0.0%%");
        }
        
        ImGui::Text("Physics Bodies: %d", physicsWorld->getRigidBodyCount());
        
        ImGui::Spacing();
        
        // Memory Stats
        ImGui::Text("MEMORY BANDWIDTH");
        ImGui::Separator();
        auto frameStats = performanceProfiler->getCurrentFrameStats();
        ImGui::Text("Bandwidth: %.1f MB/s", frameStats.memoryBandwidthMBps);
        ImGui::SameLine();
        ImGui::Text("Frame %llu", frameCount);
        
        ImGui::Spacing();
        
        // Frame Breakdown
        if (!detailedTimings.empty()) {
            ImGui::Text("FRAME BREAKDOWN");
            ImGui::Separator();
            
            const auto& latest = detailedTimings.back();
            ImGui::Text("Instance Update: %.2f ms (%.1f%%)", 
                       latest.instanceUpdateTime, 
                       (latest.instanceUpdateTime / latest.totalFrameTime) * 100.0);
            
            ImGui::Text("Frustum Culling: %.2f ms (%.1f%%)", 
                       latest.frustumCullingTime, 
                       (latest.frustumCullingTime / latest.totalFrameTime) * 100.0);
            
            ImGui::Text("Occlusion Culling: %.2f ms (%.1f%%)", 
                       latest.occlusionCullingTime, 
                       (latest.occlusionCullingTime / latest.totalFrameTime) * 100.0);
            
            ImGui::Text("Command Record: %.2f ms (%.1f%%)", 
                       latest.commandRecordTime, 
                       (latest.commandRecordTime / latest.totalFrameTime) * 100.0);
            
            ImGui::Text("GPU Submit: %.2f ms (%.1f%%)", 
                       latest.gpuSubmitTime, 
                       (latest.gpuSubmitTime / latest.totalFrameTime) * 100.0);
            
            ImGui::Text("Present: %.2f ms (%.1f%%)", 
                       latest.presentTime, 
                       (latest.presentTime / latest.totalFrameTime) * 100.0);
        }
        
        ImGui::Spacing();
        
        // Camera Position
        ImGui::Text("CAMERA POSITION");
        ImGui::Separator();
        ImGui::Text("X: %.1f  Y: %.1f  Z: %.1f", cameraPos.x, cameraPos.y, cameraPos.z);
        
        ImGui::Spacing();
        
        // Culling Systems Debug
        ImGui::Text("CULLING SYSTEMS DEBUG");
        ImGui::Separator();
        ImGui::Text("Frustum: GPU compute shader, instance-level");
        ImGui::Text("Occlusion: CPU neighbor detection, face-level");
        ImGui::Text("Chunks: Multi-chunk system (32³ cubes each)");
        ImGui::Text("Method: Cross-chunk neighbor detection");
        ImGui::Text("Final: GPU vertex shader applies both");
        ImGui::Text("Face bits: 6-bit mask in packed instance data");
        ImGui::Text("Efficiency: ~85-95%% face reduction expected");
        
        ImGui::Spacing();
        
        // Controls
        ImGui::Text("CONTROLS");
        ImGui::Separator();
        ImGui::Text("WASD: Move  Space/Shift: Up/Down");
        ImGui::Text("Right-Click: Look  ESC: Exit  F1: Toggle overlay");
    }
    ImGui::End();
}

} // namespace VulkanCube::UI
