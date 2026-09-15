#pragma once
#include <nlohmann/json.hpp>

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
class CombatSystem;
class PlayerTurnController;
class ClickToMove;
class CharacterSheet;
class Inventory;

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
    /// The control scheme a rig implies when game.json authors only `camera.mode`
    /// (third_person -> WoW-style "wow", first_person -> "fps", else `fallback`).
    static std::string defaultSchemeForRig(const std::string& rigName, const std::string& fallback) {
        if (rigName == "third_person" || rigName == "ThirdPerson" || rigName == "third" ||
            rigName == "wow" || rigName == "mmo" || rigName == "chase") return "wow";
        if (rigName == "first_person" || rigName == "FirstPerson" || rigName == "first") return "fps";
        return fallback;
    }
    GameplayCameraController& gameplayCamera() { return cameraController_; }

protected:
    // Per-frame gameplay camera + character control — call from onUpdate while
    // the game is in its playing state. Resolves the camera rig + control
    // scheme from the active scene's "camera" block ("mode" /
    // "controlScheme") on first use and re-resolves after every scene
    // transition, then runs the shared controller: samples input via the
    // scheme, drives the character (movement, facing, jump/attack/crouch,
    // advances its update), and frames the camera via the rig.
    // driveCharacter=false: frame the character without steering it (turn-based
    // combat — the TurnActor owns movement, the camera observes tactically).
    void updateGameplayCamera(EngineRuntime& engine, float dt,
                              Scene::AnimatedVoxelCharacter* character,
                              bool driveCharacter = true);

    /// Park the camera at a fixed pose, bypassing the rig entirely (spectator /
    /// harness framing). Pass detach=false to hand control back to the rig.
    void setDetachedCamera(bool detach, const glm::vec3& pos = {},
                           float yaw = 0.0f, float pitch = 0.0f) {
        cameraDetached_  = detach;
        detachedCamPos_  = pos;
        detachedCamYaw_  = yaw;
        detachedCamPitch_ = pitch;
    }
    bool cameraDetached() const { return cameraDetached_; }

    // Defaults used when the scene's camera block doesn't name one.
    virtual std::string defaultRigName() const { return "first_person"; }
    virtual std::string defaultSchemeName() const { return "fps"; }
    /// The scheme a rig implies when game.json authors only `camera.mode`: a third-person
    /// rig gets the WoW-style MMO controls, first person gets fps, else defaultSchemeName().
    std::string schemeForRig(const std::string& rigName) const { return defaultSchemeForRig(rigName, defaultSchemeName()); }

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
    /// Latest WorldHealth report (null when none) - see WorldHealth.h.
    virtual nlohmann::json               apiWorldHealth()       { return nlohmann::json(); }
    virtual UI::GameScreen*              apiScreen()            { return nullptr; }
    virtual EntityRegistry*              apiEntityRegistry()    { return nullptr; }
    virtual Scene::AnimatedVoxelCharacter* apiPlayer()          { return nullptr; }
    // Turn-based combat trio (see GameApiService) — override all three or none.
    virtual CombatDirector*       apiCombatDirector() { return nullptr; }
    virtual CombatAISystem*       apiCombatAI()       { return nullptr; }
    virtual CombatSystem*         apiCombatSystem()   { return nullptr; }  // damage funnel
    virtual PlayerTurnController* apiPlayerTurn()     { return nullptr; }
    virtual ClickToMove*          apiClickToMove()    { return nullptr; }  // walk_to (G-75)
    /// The host's left click at viewport pixels (pointer_click). Default: not available.
    virtual nlohmann::json        apiPointerClick(float, float, const std::string&) { return nlohmann::json{{"error", "no pointer click handler"}}; }
    virtual CharacterSheet*       apiPlayerSheet()    { return nullptr; }  // progression
    virtual Inventory*            apiInventory()      { return nullptr; }  // loot/persistence
    virtual UI::DialogueSystem*   apiDialogueSystem() { return nullptr; }  // dialogue_state (G-47)

private:
    GameplayCameraController cameraController_;
    std::string cameraResolvedScene_;
    bool cameraResolved_ = false;
    // Detached spectator camera (see setDetachedCamera).
    bool      cameraDetached_   = false;
    glm::vec3 detachedCamPos_{0.0f};
    float     detachedCamYaw_   = 0.0f;
    float     detachedCamPitch_ = 0.0f;
    GameApiService gameApi_;
    const Graphics::CameraRig*  configuredRig_ = nullptr;     // last rig given its world hooks
    const Input::ControlScheme* configuredScheme_ = nullptr;  // last scheme given its bindings
    /// WoW scheme: Q/E strafe, so Interact leaves E for F; bags on B. Restored otherwise.
    void applyMmoBindings(Input::InputManager& input, bool mmo);
};

} // namespace Core
} // namespace Phyxel
