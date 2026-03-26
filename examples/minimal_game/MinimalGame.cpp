#include "MinimalGame.h"
#include "core/ChunkManager.h"
#include "core/GameSettings.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "graphics/RenderCoordinator.h"
#include "input/InputManager.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "ui/WindowManager.h"
#include "ui/ImGuiRenderer.h"
#include "ui/GameScreen.h"
#include "ui/GameMenus.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Logger.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Examples {

// ============================================================================
// Game-owned subsystems
// ============================================================================

// The game owns a RenderCoordinator (it has game-specific wiring)
static std::unique_ptr<Phyxel::Graphics::RenderCoordinator> renderCoordinator;

// ============================================================================
// GameCallbacks implementation
// ============================================================================

bool MinimalGame::onInitialize(Phyxel::Core::EngineRuntime& engine) {
    LOG_INFO("MinimalGame", "Initializing minimal example game...");

    auto* chunkMgr = engine.getChunkManager();

    // Build a 32x1x32 stone platform at Y=15
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            chunkMgr->addCube(glm::ivec3(x, 15, z));
        }
    }

    // Build a few pillars
    for (int y = 16; y < 22; ++y) {
        chunkMgr->addCube(glm::ivec3(0,  y, 0));
        chunkMgr->addCube(glm::ivec3(31, y, 0));
        chunkMgr->addCube(glm::ivec3(0,  y, 31));
        chunkMgr->addCube(glm::ivec3(31, y, 31));
    }

    chunkMgr->rebuildAllChunkFaces();
    LOG_INFO("MinimalGame", "Built 32x32 platform with 4 pillars");

    // Position camera above the platform looking down
    auto* cam = engine.getCamera();
    cam->setPosition(glm::vec3(16.0f, 30.0f, 16.0f));
    cam->setYaw(-90.0f);
    cam->setPitch(-35.0f);

    // Sync InputManager so camera movement works
    auto* input = engine.getInputManager();
    input->setCameraPosition(cam->getPosition());
    input->setYawPitch(cam->getYaw(), cam->getPitch());

    // Create a RenderCoordinator for frame rendering
    renderCoordinator = std::make_unique<Phyxel::Graphics::RenderCoordinator>(
        engine.getVulkanDevice(),
        engine.getRenderPipeline(),
        engine.getDynamicRenderPipeline(),
        engine.getImGuiRenderer(),
        engine.getWindowManager(),
        engine.getInputManager(),
        engine.getCamera(),
        engine.getChunkManager(),
        engine.getPerformanceMonitor(),
        engine.getPerformanceProfiler(),
        nullptr,  // No raycast visualizer
        nullptr   // No scripting system
    );

    // Populate inventory with some starter items
    inventory_.addItem("Stone", 32);
    inventory_.addItem("Wood", 16);
    inventory_.addItem("Metal", 8);
    inventory_.addItem("Glass", 4);

    // Start on main menu
    screen_.setState(Phyxel::UI::ScreenState::MainMenu);

    // Load user settings (or keep defaults if file doesn't exist)
    Phyxel::Core::GameSettings::loadFromFile("settings.json", settings_);
    if (settings_.keybindings.empty()) {
        settings_.keybindings = Phyxel::Core::GameSettings::defaultKeybindings();
    }
    applySettings(engine);

    initialized_ = true;
    LOG_INFO("MinimalGame", "Minimal game initialized");
    return true;
}

void MinimalGame::onHandleInput(Phyxel::Core::EngineRuntime& engine) {
    auto* input = engine.getInputManager();
    if (!input) return;

    auto state = screen_.getState();

    // ESC: pause toggle (in gameplay) or resume (in pause) or go back (in settings)
    if (input->isKeyPressed(GLFW_KEY_ESCAPE)) {
        if (state == Phyxel::UI::ScreenState::Playing) {
            screen_.togglePause();
        } else if (state == Phyxel::UI::ScreenState::Paused) {
            screen_.resume();
        } else if (state == Phyxel::UI::ScreenState::Inventory) {
            screen_.resume();
        } else if (state == Phyxel::UI::ScreenState::Settings ||
                   state == Phyxel::UI::ScreenState::KeybindingRebind) {
            screen_.goBack();
        }
    }

    // Tab: toggle inventory (in gameplay or inventory screen)
    if (input->isKeyPressed(GLFW_KEY_TAB)) {
        if (state == Phyxel::UI::ScreenState::Playing ||
            state == Phyxel::UI::ScreenState::Inventory) {
            screen_.toggleInventory();
        }
    }

    // Only process camera/movement input when actually playing
    if (Phyxel::UI::isGameRunning(screen_.getState())) {
        input->processInput(0.016f);
    }
}

void MinimalGame::onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) {
    if (!Phyxel::UI::isGameRunning(screen_.getState())) return;

    elapsed_ += dt;

    // Update physics at fixed step
    auto* physics = engine.getPhysicsWorld();
    if (physics) {
        physics->stepSimulation(dt);
    }
}

void MinimalGame::onRender(Phyxel::Core::EngineRuntime& engine) {
    auto* window = engine.getWindowManager();
    if (window && window->isMinimized()) return;

    auto* imgui = engine.getImGuiRenderer();
    if (imgui) {
        imgui->newFrame();

        auto state = screen_.getState();

        switch (state) {
        case Phyxel::UI::ScreenState::MainMenu:
            Phyxel::UI::renderMainMenu("Phyxel", {
                [this]() { screen_.startGame(); },
                [this]() { screen_.toggleSettings(); },
                [&engine]() {
                    auto* w = engine.getWindowManager();
                    if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
                }
            });
            break;

        case Phyxel::UI::ScreenState::Playing:
            Phyxel::UI::renderGameHUD(&health_, &inventory_);
            break;

        case Phyxel::UI::ScreenState::Paused:
            Phyxel::UI::renderGameHUD(&health_, &inventory_);
            Phyxel::UI::renderPauseMenu({
                [this]() { screen_.resume(); },
                [this]() { screen_.toggleSettings(); },
                [this]() { screen_.returnToMainMenu(); },
                [&engine]() {
                    auto* w = engine.getWindowManager();
                    if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
                }
            });
            break;

        case Phyxel::UI::ScreenState::Inventory:
            Phyxel::UI::renderGameHUD(&health_, &inventory_);
            Phyxel::UI::renderInventoryScreen(&inventory_,
                [this]() { screen_.resume(); });
            break;

        case Phyxel::UI::ScreenState::Settings:
            Phyxel::UI::renderSettingsScreen(settings_, {
                [this, &engine](int w, int h) {
                    settings_.resolutionWidth = w;
                    settings_.resolutionHeight = h;
                    auto* win = engine.getWindowManager();
                    if (win) win->setSize(w, h);
                },
                [this, &engine](bool fs) {
                    settings_.fullscreen = fs;
                    auto* win = engine.getWindowManager();
                    if (win) win->setFullscreen(fs);
                },
                [this, &engine](int mode) {
                    settings_.vsync = static_cast<Phyxel::Core::VSyncMode>(mode);
                    auto* dev = engine.getVulkanDevice();
                    if (dev) {
                        VkPresentModeKHR pm = VK_PRESENT_MODE_IMMEDIATE_KHR;
                        if (mode == 1) pm = VK_PRESENT_MODE_FIFO_KHR;
                        else if (mode == 2) pm = VK_PRESENT_MODE_MAILBOX_KHR;
                        dev->setPreferredPresentMode(pm);
                        // Takes effect on next swapchain recreation (e.g. window resize)
                    }
                },
                [this, &engine](float fov) {
                    settings_.fov = fov;
                    auto* cam = engine.getCamera();
                    if (cam) cam->setZoom(fov);
                },
                [this, &engine](float sens) {
                    settings_.mouseSensitivity = sens;
                    auto* cam = engine.getCamera();
                    if (cam) cam->setMouseSensitivity(sens);
                },
                [this](float v) { settings_.masterVolume = v; },
                [this](float v) { settings_.musicVolume = v; },
                [this](float v) { settings_.sfxVolume = v; },
                [this]() { screen_.enterKeybindingRebind(); },
                [this]() { screen_.goBack(); },
                [this]() { settings_.saveToFile("settings.json"); },
                [this](const std::string& provider, const std::string& model, const std::string& apiKey) {
                    // AI settings changed — update LLM config if service exists
                    (void)provider; (void)model; (void)apiKey;
                }
            });
            break;

        case Phyxel::UI::ScreenState::KeybindingRebind:
            Phyxel::UI::renderKeybindingScreen(
                settings_.keybindings, rebindState_,
                [this]() { screen_.goBack(); });
            break;
        }

        imgui->endFrame();
    }

    // Render the world
    if (renderCoordinator) {
        renderCoordinator->render();
    }
}

void MinimalGame::onShutdown() {
    LOG_INFO("MinimalGame", "Shutting down minimal game...");
    settings_.saveToFile("settings.json");
    renderCoordinator.reset();
    initialized_ = false;
}

void MinimalGame::applySettings(Phyxel::Core::EngineRuntime& engine) {
    auto* win = engine.getWindowManager();
    if (win) {
        win->setSize(settings_.resolutionWidth, settings_.resolutionHeight);
        win->setFullscreen(settings_.fullscreen);
    }

    auto* cam = engine.getCamera();
    if (cam) {
        cam->setZoom(settings_.fov);
        cam->setMouseSensitivity(settings_.mouseSensitivity);
    }

    auto* dev = engine.getVulkanDevice();
    if (dev) {
        VkPresentModeKHR pm = VK_PRESENT_MODE_IMMEDIATE_KHR;
        if (settings_.vsync == Phyxel::Core::VSyncMode::On)
            pm = VK_PRESENT_MODE_FIFO_KHR;
        else if (settings_.vsync == Phyxel::Core::VSyncMode::Adaptive)
            pm = VK_PRESENT_MODE_MAILBOX_KHR;
        dev->setPreferredPresentMode(pm);
    }
}

} // namespace Examples
