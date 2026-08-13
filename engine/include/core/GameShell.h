#pragma once

#include "core/GameCallbacks.h"
#include "core/GameplayCameraController.h"
#include "core/GameApiService.h"

#include <string>

namespace Phyxel {
namespace Graphics { class RenderCoordinator; }
namespace Scene { class AnimatedVoxelCharacter; }
namespace UI { class GameScreen; }
namespace Core {

class EngineRuntime;
class NPCManager;
class TriggerSystem;
class EntityRegistry;
class CombatDirector;
class CombatAISystem;
class PlayerTurnController;

// Engine-side base for standalone game hosts. Scaffolded games subclass THIS
// instead of GameCallbacks, so shell behavior lives in the engine and fixes
// propagate to every game on rebuild instead of rotting in per-project copies
// (see the game-shell roadmap in docs/AgentContext.md).
//
// First responsibility: the gameplay camera + character control loop
// (docs/CameraControlSystem.md). More of the scaffold shell (screen state,
// menu renderer wiring, triggers) migrates here over time.
class GameShell : public GameCallbacks {
public:
    GameplayCameraController& gameplayCamera() { return cameraController_; }

protected:
    // Per-frame gameplay camera + character control — call from onUpdate while
    // the game is in its playing state. Resolves the camera rig + control
    // scheme from the active scene's "camera" block ("mode" /
    // "controlScheme") on first use and re-resolves after every scene
    // transition, then runs the shared controller: samples input via the
    // scheme, drives the character (movement, facing, jump/attack/crouch,
    // advances its update), and frames the camera via the rig.
    void updateGameplayCamera(EngineRuntime& engine, float dt,
                              Scene::AnimatedVoxelCharacter* character);

    // Defaults used when the scene's camera block doesn't name one.
    virtual std::string defaultRigName() const { return "first_person"; }
    virtual std::string defaultSchemeName() const { return "fps"; }

    // Hook to tweak a freshly resolved rig's knobs (distance, fov, eyeHeight,
    // orthoScale, pitch clamps) before it takes effect.
    virtual void onCameraRigResolved(Graphics::CameraRig& rig) {}

    // --- Opt-in standalone test API (GameApiService) -------------------------
    // Lets an automated harness drive/observe the REAL packaged game (not the
    // editor proxy). DEV/TEST ONLY — the generated main.cpp calls startTestApi
    // only when `--test`/`config.testApiEnabled` is set. A game exposes its own
    // subsystems to the API by overriding the api* hooks below (default null →
    // that endpoint reports "not available"). Call the three lifecycle methods
    // from onInitialize / onUpdate / onShutdown respectively.
    void startTestApi(EngineRuntime& engine, int port, const std::string& name);
    void pumpTestApi();     // drain queued commands — call once per frame in onUpdate
    void stopTestApi();
    bool testApiRunning() const { return gameApi_.isRunning(); }

    virtual Graphics::RenderCoordinator* apiRenderCoordinator() { return nullptr; }
    virtual NPCManager*                  apiNPCManager()        { return nullptr; }
    virtual TriggerSystem*               apiTriggerSystem()     { return nullptr; }
    virtual UI::GameScreen*              apiScreen()            { return nullptr; }
    virtual EntityRegistry*              apiEntityRegistry()    { return nullptr; }
    virtual Scene::AnimatedVoxelCharacter* apiPlayer()          { return nullptr; }
    // Turn-based combat trio (see GameApiService) — override all three or none.
    virtual CombatDirector*       apiCombatDirector() { return nullptr; }
    virtual CombatAISystem*       apiCombatAI()       { return nullptr; }
    virtual PlayerTurnController* apiPlayerTurn()     { return nullptr; }

private:
    GameplayCameraController cameraController_;
    std::string cameraResolvedScene_;
    bool cameraResolved_ = false;
    GameApiService gameApi_;
};

} // namespace Core
} // namespace Phyxel
