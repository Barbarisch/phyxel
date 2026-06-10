#pragma once

#include "core/GameCallbacks.h"
#include "core/GameplayCameraController.h"

#include <string>

namespace Phyxel {
namespace Scene { class AnimatedVoxelCharacter; }
namespace Core {

class EngineRuntime;

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

private:
    GameplayCameraController cameraController_;
    std::string cameraResolvedScene_;
    bool cameraResolved_ = false;
};

} // namespace Core
} // namespace Phyxel
