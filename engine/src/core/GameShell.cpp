#include "core/GameShell.h"
#include "ui/DialogueSystem.h"
#include "ui/UISystem.h"
#include "graphics/RenderCoordinator.h"
#include "core/ProgressionHud.h"
#include "core/PartyHud.h"
#include "core/JournalHud.h"
#include "core/ObjectiveTracker.h"
#include "input/InputManager.h"
#include "core/Party.h"
#include "core/EntityRegistry.h"
#include "core/CharacterSheet.h"
#include "ui/HudDataContext.h"
#include "core/EngineRuntime.h"
#include "core/SceneManager.h"
#include "graphics/Camera.h"
#include "input/InputManager.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include "graphics/CameraRig.h"
#include "input/ControlScheme.h"
#include "core/ChunkManager.h"
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

void GameShell::startTestApi(EngineRuntime& engine, int port, const std::string& name) {
    gameApi_.runtime          = &engine;
    gameApi_.renderCoordinator = apiRenderCoordinator();
    gameApi_.npcManager       = apiNPCManager();
    gameApi_.triggers         = apiTriggerSystem();
    gameApi_.screen           = apiScreen();
    gameApi_.entityRegistry   = apiEntityRegistry();
    gameApi_.playerProvider   = [this]() { return apiPlayer(); };
    gameApi_.worldHealthProvider = [this]() { return apiWorldHealth(); };
    gameApi_.uiLintProvider = [this]() -> nlohmann::json {
        auto* rc = apiRenderCoordinator();
        auto* ui = rc ? rc->getUISystem() : nullptr;
        return ui ? ui->lintLayout() : nlohmann::json();
    };
    gameApi_.combatDirector   = apiCombatDirector();
    gameApi_.combatAI         = apiCombatAI();
    gameApi_.combatSystem     = apiCombatSystem();
    gameApi_.cameraControl    = [this](bool detach, const glm::vec3& p, float yaw, float pitch) {
        setDetachedCamera(detach, p, yaw, pitch);
    };
    gameApi_.playerTurn       = apiPlayerTurn();
    gameApi_.clickToMove      = apiClickToMove();
    gameApi_.pointerClickProvider = [this](float x, float y, const std::string& b) { return apiPointerClick(x, y, b); };
    gameApi_.playerSheet      = apiPlayerSheet();
    gameApi_.inventory        = apiInventory();
    gameApi_.dialogueSystem   = apiDialogueSystem();
    gameApi_.interactionManager = apiInteractionManager();
    gameApi_.npcHeldItems     = apiNpcHeldItems();
    gameApi_.itemEffects      = apiItemEffects();
    gameApi_.projectName      = name;
    if (gameApi_.start(port))
        LOG_WARN("GameShell", "*** TEST API ENABLED on 127.0.0.1:{} — dev/test build, do NOT ship ***", port);
}

void GameShell::applyMmoBindings(Input::InputManager& input, bool mmo) {
    // WoW: E is strafe-right, so the keyboard interact moves to F (WoW's own F is
    // "assist"; unbound here). B opens the bags. The defaults come back for other
    // schemes so a first-person game keeps E to interact.
    input.bindAction("Interact",        mmo ? GLFW_KEY_F : GLFW_KEY_E);
    input.bindAction("ToggleInventory", mmo ? GLFW_KEY_B : GLFW_KEY_TAB);
    LOG_INFO("GameShell", "control bindings: {} (Interact={}, Inventory={})",
             mmo ? "mmo/wow" : "default", mmo ? "F" : "E", mmo ? "B" : "Tab");
}

void GameShell::publishProgressionHud() {
    auto* rc = apiRenderCoordinator();
    if (!rc) return;
    auto& hud = rc->hudData();

    // Captured as `this`, not as a sheet pointer: a game may swap sheets on load or on a new
    // character, and a captured pointer would quietly keep drawing the old one.
    hud.setFloat("player.xpPercent", [this]() -> float {
        auto* sheet = apiPlayerSheet();
        return sheet ? progressionHud(*sheet).percent : 0.0f;
    });
    hud.setFloat("player.level", [this]() -> float {
        auto* sheet = apiPlayerSheet();
        return sheet ? static_cast<float>(progressionHud(*sheet).level) : 1.0f;
    });
    hud.setFloat("player.xp", [this]() -> float {
        auto* sheet = apiPlayerSheet();
        return sheet ? static_cast<float>(progressionHud(*sheet).xpTotal) : 0.0f;
    });
    hud.setText("player.xpText", [this]() -> std::string {
        auto* sheet = apiPlayerSheet();
        return sheet ? progressionHud(*sheet).text : std::string("Lv 1  -  0 XP");
    });
    // Lets a HUD hide the bar entirely for a game with no progression authored, rather than
    // showing every such game a permanently empty bar.
    hud.setFloat("player.hasProgression", [this]() -> float {
        auto* sheet = apiPlayerSheet();
        return (sheet && !sheet->classes.empty()) ? 1.0f : 0.0f;
    });

    LOG_INFO("GameShell", "progression HUD published (player.xpPercent/level/xp/xpText)");
}

void GameShell::publishPartyHud() {
    auto* rc = apiRenderCoordinator();
    if (!rc) return;

    rc->hudData().setList("party", [this]() -> std::vector<UI::HudRecord> {
        auto* party = apiParty();
        if (!party) return {};
        // Health comes from the live entity, not from the party record, so a member who is
        // hurt in this scene shows it. An entity the registry does not have is ABSENT - the
        // member is simply in another scene - and partyHudRecords reports that as "Away".
        auto* reg = apiEntityRegistry();
        return partyHudRecords(*party, [reg](const std::string& id, float& hp, float& maxHp) {
            if (!reg) return false;
            auto* e = reg->getEntity(id);
            auto* hc = e ? e->getHealthComponent() : nullptr;
            if (!hc) return false;
            hp = hc->getHealth();
            maxHp = hc->getMaxHealth();
            return true;
        });
    });
    rc->hudData().setFloat("party.any", [this]() -> float {
        auto* party = apiParty();
        return (party && !party->getMembers().empty()) ? 1.0f : 0.0f;
    });

    LOG_INFO("GameShell", "party HUD published (party list + party.any)");
}

void GameShell::publishJournalHud() {
    auto* rc = apiRenderCoordinator();
    if (!rc) return;
    // Three sections, each its own list, so the panel can give every section a static heading
    // and an empty section collapses to nothing (a repeater reports its content height).
    auto sections = [this]() -> JournalSections {
        auto* tracker = apiObjectiveTracker();
        return tracker ? journalHudSections(*tracker) : JournalSections{};
    };
    auto& hud = rc->hudData();
    hud.setList("journal.main",      [sections]() { return sections().main; });
    hud.setList("journal.side",      [sections]() { return sections().side; });
    hud.setList("journal.completed", [sections]() { return sections().completed; });
    hud.setFloat("journal.mainAny",      [sections]() { return sections().main.empty() ? 0.0f : 1.0f; });
    hud.setFloat("journal.sideAny",      [sections]() { return sections().side.empty() ? 0.0f : 1.0f; });
    hud.setFloat("journal.completedAny", [sections]() { return sections().completed.empty() ? 0.0f : 1.0f; });
    hud.setFloat("journal.visible", [this]() -> float { return journalOpen_ ? 1.0f : 0.0f; });
    LOG_INFO("GameShell", "journal HUD published (journal.main/side/completed + journal.visible)");
}

void GameShell::updateJournalToggle(Input::InputManager& input) {
    const bool down = input.isActionPressed("ToggleJournal");
    const bool edge = down && !journalKeyDown_;
    journalKeyDown_ = down;
    // G-183: not over a conversation - two modal overlays (journal + dialogue box) would sit on
    // top of each other. The layout gate cites this as the reason journal.visible and
    // dialogue.active/aiActive are exclusive.
    auto* dlg = apiDialogueSystem();
    if (edge && !(dlg && dlg->isActive())) {
        journalOpen_ = !journalOpen_;
        LOG_INFO("GameShell", "Journal {}", journalOpen_ ? "opened" : "closed");
    }
}

void GameShell::pumpTestApi() { gameApi_.pump(); }
void GameShell::stopTestApi() { gameApi_.stop(); }

void GameShell::updateGameplayCamera(EngineRuntime& engine, float dt,
                                     Scene::AnimatedVoxelCharacter* character,
                                     bool driveCharacter) {
    auto* input = engine.getInputManager();
    auto* cam   = engine.getCamera();
    if (!input || !cam) return;

    // DETACHED camera (harness / spectator): a fixed pose owns the camera and
    // the rig is skipped entirely. Without this there was no way to look at
    // anything except over the player's shoulder — a 40-character battle could
    // only be photographed by standing the player next to it, and a screenshot
    // of an empty sky was indistinguishable from a battle that never rendered.
    if (cameraDetached_) {
        cam->setPosition(detachedCamPos_);
        cam->setYaw(detachedCamYaw_);
        cam->setPitch(detachedCamPitch_);
        if (character) character->update(dt);   // world keeps running
        return;
    }

    // Resolve the rig + scheme from the active scene's camera block — once, and
    // again whenever the active scene changes (each scene may author its own
    // camera.mode / camera.controlScheme).
    auto* sm = engine.getSceneManager();
    const auto* active = sm ? sm->getActiveScene() : nullptr;
    const std::string sceneId = active ? active->id : std::string();
    if (!cameraResolved_ || sceneId != cameraResolvedScene_) {
        std::string rigName = defaultRigName();
        std::string schemeName;
        if (active && active->definition.contains("camera")) {
            const auto& camDef = active->definition["camera"];
            rigName = camDef.value("mode", rigName);
            schemeName = camDef.value("controlScheme", "");
        }
        // No authored controlScheme: the rig decides (third_person -> WoW-style MMO
        // controls, first_person -> fps). Before 2026-09-15 this fell back to "fps"
        // for every rig, which gave Ravenmere's third-person town an always-on
        // mouse-look with a grabbed cursor (the system the user rejected).
        if (schemeName.empty()) schemeName = schemeForRig(rigName);
        // Unknown names (incl. mode "free") fall back to the shell defaults.
        if (!cameraController_.setRigByName(rigName))
            cameraController_.setRigByName(defaultRigName());
        if (!cameraController_.setSchemeByName(schemeName))
            cameraController_.setSchemeByName(defaultSchemeName());
        if (auto* rig = cameraController_.rig()) {
            // Eye height: the rig ships a generic 0.5 (≈ knee height on a humanoid),
            // which left first-person looking out of the character's shins. Derive it
            // from THIS character's controller height so the eye sits near the top of
            // the body (feet + ~1.7 for the default ~1.9-tall humanoid) and scales with
            // the model. worldPosition (the rig target) is the capsule BOTTOM/feet, and
            // the controller half-height is half the full height, so full height =
            // 2×halfHeight; 1.8×halfHeight ≈ 90% of full height. An explicit game.json
            // camera.eyeHeight still wins.
            if (character)
                rig->eyeHeight = character->getControllerHalfHeight() * 1.8f;
            if (active && active->definition.contains("camera") &&
                active->definition["camera"].contains("eyeHeight"))
                rig->eyeHeight = active->definition["camera"].value("eyeHeight", rig->eyeHeight);
            onCameraRigResolved(*rig);
        }
        cameraResolved_ = true;
        cameraResolvedScene_ = sceneId;
    }

    // Rig-specific wiring that must follow EVERY rig swap (the combat rig and back
    // included): the MMO rig's wall collision reads the chunk world.
    if (auto* rig = cameraController_.rig(); rig != configuredRig_) {
        if (auto* mmo = dynamic_cast<Graphics::MmoRig*>(rig)) {
            if (auto* cm = engine.getChunkManager())
                mmo->solidAt = [cm](const glm::ivec3& c) { return cm->hasVoxelAt(c); };
        }
        configuredRig_ = rig;
    }
    if (auto* scheme = cameraController_.scheme(); scheme != configuredScheme_) {
        applyMmoBindings(*input, dynamic_cast<Input::MmoScheme*>(scheme) != nullptr);
        configuredScheme_ = scheme;
    }

    cameraController_.update(dt, *input, character, *cam,
                             /*advanceCharacter=*/true, driveCharacter);

    // Update-LOD: publish the viewer position so AnimatedVoxelCharacter can tick
    // distant characters at a reduced rate. This is the standalone-game analog of
    // the editor's per-frame setViewerPosition call, and is essential for crowds
    // (100s of characters) where most are far from the camera at any moment.
    Scene::AnimatedVoxelCharacter::setViewerPosition(cam->getPosition());
}

} // namespace Core
} // namespace Phyxel
