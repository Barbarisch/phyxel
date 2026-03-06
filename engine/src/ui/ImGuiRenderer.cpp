#include "ui/ImGuiRenderer.h"
#include "core/Types.h"
#include "core/ForceSystem.h"
#include "scripting/ScriptingSystem.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "utils/GpuProfiler.h"
#include "utils/Logger.h"
#include "physics/PhysicsWorld.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <glm/glm.hpp>
#include <iostream>
#include <iomanip>

namespace Phyxel::UI {

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

bool ImGuiRenderer::initialize(GLFWwindow* window, Vulkan::VulkanDevice* vulkanDevice, VkRenderPass renderPass) {
    m_window = window;
    m_vulkanDevice = vulkanDevice;
    // m_renderPipeline = renderPipeline; // Removed

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
        LOG_ERROR("UI", "Failed to initialize ImGui GLFW backend!");
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
        LOG_ERROR("UI", "Failed to create ImGui descriptor pool!");
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
    init_info.RenderPass = renderPass;
    init_info.MinImageCount = vulkanDevice->getSwapChainImageCount();
    init_info.ImageCount = vulkanDevice->getSwapChainImageCount();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.Subpass = 0;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        LOG_ERROR("UI", "Failed to initialize ImGui Vulkan backend!");
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

static int InputTextCallbackStub(ImGuiInputTextCallbackData* data) {
    ImGuiRenderer* renderer = (ImGuiRenderer*)data->UserData;
    return renderer->handleInputTextCallback(data);
}

int ImGuiRenderer::handleInputTextCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Locate beginning of current word with balanced parenthesis support
        const char* word_end = data->Buf + data->CursorPos;
        const char* word_start = word_end;
        int paren_balance = 0;

        while (word_start > data->Buf) {
            const char c = word_start[-1];
            
            if (c == ')') {
                paren_balance++;
            } else if (c == '(') {
                if (paren_balance > 0) {
                    paren_balance--;
                } else {
                    // Unbalanced opening parenthesis - start of argument list
                    break;
                }
            } else if (paren_balance == 0) {
                // Only break on separators if we are not inside parentheses
                if (c == ' ' || c == '\t' || c == ',' || c == ';')
                    break;
            }
            
            word_start--;
        }

        std::string prefix(word_start, word_end);
        
        // Debug logging for completion
        Utils::Logger::getInstance().log(Utils::LogLevel::Debug, "ImGuiRenderer", "Completion prefix: '" + prefix + "'");
        
        if (m_currentScriptingSystem && !prefix.empty()) {
            m_completions = m_currentScriptingSystem->getCompletions(prefix);
            if (!m_completions.empty()) {
                int start_index = (int)(word_start - data->Buf);
                int count = (int)(word_end - word_start);

                Utils::Logger::getInstance().log(Utils::LogLevel::Debug, "ImGuiRenderer", "Replacing " + std::to_string(count) + " chars at " + std::to_string(start_index) + " with '" + m_completions[0] + "'");

                if (m_completions.size() == 1) {
                    // Single match. Delete the beginning of the word and replace it entirely
                    data->DeleteChars(start_index, count);
                    data->InsertChars(start_index, m_completions[0].c_str());
                } else {
                    // Multiple matches. 
                    // Find common prefix
                    std::string common = m_completions[0];
                    for (size_t i = 1; i < m_completions.size(); i++) {
                        size_t j = 0;
                        while (j < common.length() && j < m_completions[i].length() && common[j] == m_completions[i][j])
                            j++;
                        common = common.substr(0, j);
                    }
                    
                    if (common.length() > prefix.length()) {
                        data->DeleteChars(start_index, count);
                        data->InsertChars(start_index, common.c_str());
                    }
                    
                    m_completionPrefix = prefix; // Store original prefix for filtering if needed
                    m_showCompletionPopup = true;
                    m_selectedCompletionIndex = 0;
                }
            }
        }
    }
    return 0;
}

void ImGuiRenderer::renderScriptingConsole(bool showConsole, ScriptingSystem* scriptingSystem) {
    if (!showConsole || !scriptingSystem) return;

    m_currentScriptingSystem = scriptingSystem;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scripting Console", nullptr)) {
        
        if (ImGui::BeginTabBar("ScriptingTabs")) {
            
            // TAB 1: CONSOLE
            if (ImGui::BeginTabItem("Console")) {
                // Quick Actions
                ImGui::Text("Quick Actions:");
                if (ImGui::Button("Reload & Run world_gen.py")) {
                    scriptingSystem->reloadScript("world_gen.py");
                    scriptingSystem->runCommand("run_demo()");
                }
                ImGui::SameLine();
                if (ImGui::Button("Reload startup.py")) {
                    scriptingSystem->reloadScript("startup.py");
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Log")) {
                    scriptingSystem->clearLog();
                }

                ImGui::Separator();

                // Log Output
                ImGui::BeginChild("ScrollingRegion", ImVec2(0, -60), true, ImGuiWindowFlags_HorizontalScrollbar);
                const auto& logs = scriptingSystem->getLogBuffer();
                for (const auto& msg : logs) {
                    ImGui::TextUnformatted(msg.c_str());
                }
                // Auto-scroll if at bottom
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();

                ImGui::Separator();

                // Command Input
                ImGui::Text("Execute Python Command (Tab for completion):");
                bool reclaim_focus = false;
                
                ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion;
                if (ImGui::InputText("##Command", m_scriptInputBuffer, IM_ARRAYSIZE(m_scriptInputBuffer), flags, InputTextCallbackStub, (void*)this)) {
                    scriptingSystem->runCommand(m_scriptInputBuffer);
                    // Clear buffer after execution
                    m_scriptInputBuffer[0] = '\0';
                    reclaim_focus = true;
                }

                // Auto-focus input
                if (reclaim_focus || ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
                
                // Completion Popup
                if (m_showCompletionPopup) {
                    ImGui::OpenPopup("CompletionPopup");
                    m_showCompletionPopup = false; // Only open once
                }
                
                if (ImGui::BeginPopup("CompletionPopup")) {
                    for (int i = 0; i < m_completions.size(); i++) {
                        if (ImGui::Selectable(m_completions[i].c_str())) {
                            // In a real implementation, we would insert this back into the input buffer
                            // But ImGui doesn't make it easy to modify the buffer from outside the callback
                            // For now, just print it to log or copy to clipboard
                            ImGui::SetClipboardText(m_completions[i].c_str());
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::Separator();
                ImGui::TextWrapped("Tip: Use 'phyxel.get_app()' to access engine internals.");
                ImGui::EndTabItem();
            }
            
            // TAB 2: EDITOR
            if (ImGui::BeginTabItem("Editor")) {
                ImGui::Text("Python Script Editor");
                
                if (ImGui::Button("Run Script")) {
                    scriptingSystem->runCommand(m_scriptEditorBuffer);
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear")) {
                    m_scriptEditorBuffer[0] = '\0';
                }
                
                ImGui::InputTextMultiline("##Editor", m_scriptEditorBuffer, IM_ARRAYSIZE(m_scriptEditorBuffer), ImVec2(-FLT_MIN, -FLT_MIN));
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    
    m_currentScriptingSystem = nullptr;
}

void ImGuiRenderer::renderPerformanceOverlay(
    bool showOverlay,
    Timer* timer,
    PerformanceProfiler* performanceProfiler,
    const FrameTiming& frameTiming,
    const std::vector<DetailedFrameTiming>& detailedTimings,
    Physics::PhysicsWorld* physicsWorld,
    const glm::vec3& cameraPos,
    uint64_t frameCount,
    float& renderDistance,
    float& chunkInclusionDistance) {
    
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
        
        // World Statistics
        ImGui::Text("RENDERING STATS");
        ImGui::Separator();
        
        // Calculate total cubes in world vs visible cubes after frustum culling
        int totalWorldCubes = frameTiming.visibleInstances + frameTiming.frustumCulledInstances;
        int visibleAfterFrustum = frameTiming.visibleInstances;
        
        ImGui::Text("Total Cubes: %d", totalWorldCubes);
        ImGui::SameLine();
        ImGui::Text("Vertices: %d", totalWorldCubes * 36); // 36 vertices per cube
        
        ImGui::Text("Draw Calls: %d", frameTiming.drawCalls);
        ImGui::SameLine();
        ImGui::Text("Visible After Frustum: %d", visibleAfterFrustum);
        
        // Frustum culling stats (CPU chunk-level culling)
        ImGui::Text("Frustum Culled: %d", frameTiming.frustumCulledInstances);
        ImGui::SameLine();
        float frustumCullRate = totalWorldCubes > 0 ? 
                               (float)frameTiming.frustumCulledInstances / totalWorldCubes * 100.0f : 0.0f;
        ImGui::Text("Rate: %.1f%%", frustumCullRate);
        
        // Occlusion culling stats
        ImGui::Text("Fully Hidden Cubes: %d", frameTiming.fullyOccludedCubes);
        ImGui::SameLine();
        ImGui::Text("Partially Hidden: %d", frameTiming.partiallyOccludedCubes);
        
        ImGui::Text("Hidden Faces: %d", frameTiming.totalHiddenFaces);
        ImGui::SameLine();
        ImGui::Text("Face Reduction: %d%%", (frameTiming.totalHiddenFaces > 0 && frameTiming.visibleInstances > 0) ? 
                   (frameTiming.totalHiddenFaces * 100) / (frameTiming.visibleInstances * 6) : 0);
        
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
        
        // Render Settings
        ImGui::Text("RENDER SETTINGS");
        ImGui::Separator();
        
        // Two-tier distance system
        ImGui::SliderFloat("Frustum Culling Distance", &renderDistance, 32.0f, 300.0f, "%.0f units");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Actual render distance used for frustum culling.\nControls camera far plane and what you see.");
        }
        
        ImGui::SliderFloat("Chunk Loading Distance", &chunkInclusionDistance, 
                          renderDistance, 500.0f, "%.0f units");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Distance for chunk inclusion (must be >= frustum distance).\nChunks beyond frustum but within this range stay loaded.");
        }
        
        // Auto-adjust if user tries to set chunk distance below render distance
        if (chunkInclusionDistance < renderDistance) {
            chunkInclusionDistance = renderDistance;
        }
        
        ImGui::Spacing();
        
        // Camera Position
        ImGui::Text("CAMERA POSITION");
        ImGui::Separator();
        ImGui::Text("X: %.1f  Y: %.1f  Z: %.1f", cameraPos.x, cameraPos.y, cameraPos.z);
        
        ImGui::Spacing();
        
        // Controls
        ImGui::Text("CONTROLS");
        ImGui::Separator();
        ImGui::Text("WASD: Move  Space/Shift: Up/Down");
        ImGui::Text("Right-Click: Look  ESC: Exit  F1: Toggle overlay");
    }
    ImGui::End();
}

void ImGuiRenderer::renderForceSystemDebug(
    bool showDebug,
    Phyxel::ForceSystem* forceSystem,
    Phyxel::MouseVelocityTracker* mouseVelocityTracker,
    bool hasHoveredCube,
    const glm::vec3& hoveredCubePos,
    float& manualForceValue) {
    
    if (!showDebug || !m_initialized) return;

    // Create a window for the force system debug
    ImGui::SetNextWindowPos(ImVec2(470, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Force System Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        
        // Force Control
        ImGui::Text("FORCE CONTROL");
        ImGui::Separator();
        
        // Manual force value slider
        ImGui::SliderFloat("Breaking Force", &manualForceValue, 0.0f, 2000.0f, "%.1f N");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Drag to adjust the force applied when breaking cubes.\nHigher values = stronger breaking force.");
        }
        
        // Force magnitude indicator with color coding
        float normalizedForce = manualForceValue / 2000.0f; // Normalize to 0-1
        ImVec4 forceColor;
        if (normalizedForce < 0.3f) {
            forceColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Green for low force
        } else if (normalizedForce < 0.7f) {
            forceColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f); // Yellow for medium force
        } else {
            forceColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); // Red for high force
        }
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, forceColor);
        ImGui::ProgressBar(normalizedForce, ImVec2(0.0f, 0.0f), "");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("Force Level");
        
        // Quick preset buttons
        ImGui::Text("Quick Presets:");
        if (ImGui::Button("Light (200N)")) manualForceValue = 200.0f;
        ImGui::SameLine();
        if (ImGui::Button("Medium (500N)")) manualForceValue = 500.0f;
        ImGui::SameLine();
        if (ImGui::Button("Strong (1000N)")) manualForceValue = 1000.0f;
        
        ImGui::Spacing();
        
        // Mouse Velocity Information (for reference)
        ImGui::Text("MOUSE VELOCITY (Reference)");
        ImGui::Separator();
        
        if (mouseVelocityTracker) {
            glm::vec2 velocity = mouseVelocityTracker->getVelocity();
            float speed = mouseVelocityTracker->getSpeed();
            
            ImGui::Text("Velocity X: %.2f px/s", velocity.x);
            ImGui::Text("Velocity Y: %.2f px/s", velocity.y);
            ImGui::Text("Speed: %.2f px/s", speed);
            
            // Visual speed indicator
            float normalizedSpeed = std::min(speed / 1000.0f, 1.0f); // Normalize to 1000 px/s max
            ImGui::ProgressBar(normalizedSpeed, ImVec2(0.0f, 0.0f), "");
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::Text("Speed");
        } else {
            ImGui::Text("MouseVelocityTracker not available");
        }
        
        ImGui::Spacing();
        
        // Force System Parameters
        ImGui::Text("FORCE SYSTEM PARAMETERS");
        ImGui::Separator();
        
        if (forceSystem) {
            // Note: These would need to be added as getters to ForceSystem class
            ImGui::Text("Base Force Magnitude: %.2f", 1000.0f); // Placeholder - would need getter
            ImGui::Text("Force Falloff Rate: %.2f", 0.7f);      // Placeholder - would need getter
            ImGui::Text("Max Propagation Distance: %d", 3);      // Placeholder - would need getter
            ImGui::Text("Bond Strength Threshold: %.2f", 500.0f); // Placeholder - would need getter
        } else {
            ImGui::Text("ForceSystem not available");
        }
        
        ImGui::Spacing();
        
        // Current Target Information
        ImGui::Text("CURRENT TARGET");
        ImGui::Separator();
        
        if (hasHoveredCube) {
            ImGui::Text("Target Position:");
            ImGui::Text("X: %.1f  Y: %.1f  Z: %.1f", hoveredCubePos.x, hoveredCubePos.y, hoveredCubePos.z);
            
            // Real-time force preview using manual force value
            if (forceSystem) {
                ImGui::Text("Applied Force: %.1f N", manualForceValue);
                
                // Estimate number of cubes that might break (simplified calculation)
                int estimatedCubes = static_cast<int>(manualForceValue / 250.0f) + 1; // Rough estimate
                estimatedCubes = std::min(estimatedCubes, 10); // Cap at reasonable number
                ImGui::Text("Estimated Cubes Affected: ~%d", estimatedCubes);
                
                // Force direction based on camera (simplified)
                ImGui::Text("Force Direction: Away from camera");
            }
        } else {
            ImGui::Text("No cube currently hovered");
            ImGui::Text("Hover over a cube to see force preview");
        }
        
        ImGui::Spacing();
        
        // Instructions
        ImGui::Text("INSTRUCTIONS");
        ImGui::Separator();
        ImGui::Text("F3: Toggle this debug window");
        ImGui::Text("Adjust force slider above, then:");
        ImGui::Text("Left-click cube: Break with set force");
        ImGui::Text("Use presets for quick force testing");
        ImGui::Text("Higher force = more cubes affected");
    }
    ImGui::End();
}

void ImGuiRenderer::renderLightingControls(
    bool showControls,
    glm::vec3& sunDirection,
    glm::vec3& sunColor,
    float& ambientStrength,
    float& emissiveMultiplier
) {
    if (!showControls) return;

    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Lighting Controls", &showControls)) {
        ImGui::Text("Sun Direction");
        // Use a direction widget or simple sliders for XYZ
        // Normalize after editing to keep it a direction vector
        if (ImGui::SliderFloat3("Direction", &sunDirection.x, -1.0f, 1.0f)) {
            if (glm::length(sunDirection) > 0.001f) {
                sunDirection = glm::normalize(sunDirection);
            }
        }

        ImGui::Separator();
        ImGui::Text("Sun Color");
        ImGui::ColorEdit3("Color", &sunColor.x);

        ImGui::Separator();
        ImGui::Text("Ambient Light");
        ImGui::SliderFloat("Strength", &ambientStrength, 0.0f, 2.0f);

        ImGui::Separator();
        ImGui::Text("Emissive Glow");
        ImGui::SliderFloat("Multiplier", &emissiveMultiplier, 1.0f, 10.0f);
    }
    ImGui::End();
}

void ImGuiRenderer::renderProfilerWindow(bool show, PerformanceProfiler* cpuProfiler, GpuProfiler* gpuProfiler) {
    if (!show || !cpuProfiler) return;

    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Performance Profiler", &show)) {
        
        if (ImGui::BeginTabBar("ProfilerTabs")) {
            
            // CPU Tab
            if (ImGui::BeginTabItem("CPU Timeline")) {
                const auto* root = cpuProfiler->getLastFrameRoot();
                if (root) {
                    std::function<void(const PerformanceProfiler::ProfilerNode*)> drawNode;
                    drawNode = [&](const PerformanceProfiler::ProfilerNode* node) {
                        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;
                        if (node->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
                        
                        bool open = ImGui::TreeNodeEx(node, flags, "%s: %.3f ms", node->name.c_str(), node->durationMs);
                        if (open) {
                            for (const auto& child : node->children) {
                                drawNode(child.get());
                            }
                            ImGui::TreePop();
                        }
                    };
                    drawNode(root);
                } else {
                    ImGui::Text("No CPU data available.");
                }
                ImGui::EndTabItem();
            }

            // GPU Tab
            if (ImGui::BeginTabItem("GPU Timeline")) {
                if (gpuProfiler) {
                    const auto& results = gpuProfiler->getResults();
                    if (results.empty()) {
                        ImGui::Text("No GPU data available (or waiting for first frame).");
                    } else {
                        // Simple list for now, as GPU results are flat list with depth
                        for (const auto& res : results) {
                            ImGui::Indent(res.depth * 10.0f);
                            ImGui::Text("%s: %.3f ms", res.name.c_str(), res.durationMs);
                            ImGui::Unindent(res.depth * 10.0f);
                        }
                    }
                } else {
                    ImGui::Text("GPU Profiler not initialized.");
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace Phyxel::UI
