#!/usr/bin/env python3
"""
create_project.py — Scaffold a new Phyxel game project.

Creates a standalone game directory with:
  - CMakeLists.txt linking against phyxel_core
  - A GameCallbacks implementation (MyGame.h / MyGame.cpp)
  - main.cpp entry point
  - engine.json default config

When given a --game-definition, generates C++ that loads the full game
(world from pre-baked SQLite, NPCs/dialogue/story from game.json at runtime).

Usage:
  python tools/create_project.py MyAwesomeGame
  python tools/create_project.py MyAwesomeGame --game-definition samples/game_definitions/mountains_rpg.json
  python tools/create_project.py MyAwesomeGame --output ~/projects/MyAwesomeGame
"""

import argparse
import json
import os
import sys
import textwrap
from pathlib import Path


def create_project(
    name: str,
    output_dir: Path,
    phyxel_root: Path,
    game_definition: dict | None = None,
) -> None:
    """Generate all project files."""
    output_dir.mkdir(parents=True, exist_ok=True)

    class_name = name.replace("-", "").replace("_", "")
    name_lower = name.lower().replace(" ", "_")
    header_guard = name.upper().replace("-", "_").replace(" ", "_") + "_H"

    # Compute path to Phyxel root (relative if same drive, absolute otherwise)
    is_relative_path = True
    try:
        phyxel_path = os.path.relpath(phyxel_root, output_dir).replace(os.sep, '/')
    except ValueError:
        # Different drives on Windows — use absolute path
        phyxel_path = str(phyxel_root).replace(os.sep, '/')
        is_relative_path = False

    # Determine if this game has NPCs/dialogue/story from its definition
    has_npcs = False
    has_story = False
    has_player = False
    camera_cfg = None

    if game_definition:
        has_npcs = bool(game_definition.get("npcs"))
        has_story = bool(game_definition.get("story"))
        has_player = bool(game_definition.get("player"))
        camera_cfg = game_definition.get("camera")

    files = {}

    # ── CMakeLists.txt ──────────────────────────────────────────────────
    files["CMakeLists.txt"] = textwrap.dedent(f"""\
        cmake_minimum_required(VERSION 3.15)
        project({name} LANGUAGES CXX)
        set(CMAKE_CXX_STANDARD 17)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)

        # ── Find Phyxel engine ──────────────────────────────────
        set(PHYXEL_ROOT "{f'${{CMAKE_CURRENT_SOURCE_DIR}}/{phyxel_path}' if is_relative_path else phyxel_path}"
            CACHE PATH "Path to Phyxel repository root")
        # Debug info in Release too (no codegen change: /Zi only emits symbols; the
        # exe links /DEBUG so a .pdb ships beside it). Core::CrashHandler symbolizes
        # crash stacks from it — without it a crash is "module unknown, offset 0".
        if(MSVC)
            set(CMAKE_CXX_FLAGS_RELEASE "${{CMAKE_CXX_FLAGS_RELEASE}} /Zi")
            set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${{CMAKE_EXE_LINKER_FLAGS_RELEASE}} /DEBUG /OPT:REF /OPT:ICF")
        endif()
        add_subdirectory(${{PHYXEL_ROOT}} phyxel_build EXCLUDE_FROM_ALL)

        # ── Game executable ─────────────────────────────────────
        add_executable(${{PROJECT_NAME}}
            main.cpp
            {class_name}.cpp
            {class_name}.h
        )

        target_link_libraries(${{PROJECT_NAME}} PRIVATE phyxel_core)
        target_include_directories(${{PROJECT_NAME}} PRIVATE ${{CMAKE_CURRENT_SOURCE_DIR}})

        if(MSVC)
            set_property(TARGET ${{PROJECT_NAME}} PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
            set_target_properties(${{PROJECT_NAME}} PROPERTIES
                LINK_FLAGS "/NODEFAULTLIB:python313t.lib")
        endif()

        # ── Copy runtime assets to build output ─────────────────
        # Seed the engine's default shaders + resources FIRST, then layer the
        # project's own dirs on top so project files win. Without the engine
        # defaults a freshly scaffolded game ships an EMPTY shaders/ (no *.spv →
        # the app exits right after "Framebuffers created successfully") and a
        # resources/ missing humanoid.anim / default_hud.json / fonts / textures /
        # materials.json — i.e. instant crash or an empty, checkerboard world.
        # (game-dev feedback round 5 — UIShowcase.)
        add_custom_command(TARGET ${{PROJECT_NAME}} POST_BUILD
            COMMENT "Copying runtime assets to output directory"
            COMMAND ${{CMAKE_COMMAND}} -E copy_directory
                "${{PHYXEL_ROOT}}/shaders" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/shaders"
            COMMAND ${{CMAKE_COMMAND}} -E copy_directory
                "${{PHYXEL_ROOT}}/resources" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/resources"
            COMMAND ${{CMAKE_COMMAND}} -E copy_directory
                "${{CMAKE_CURRENT_SOURCE_DIR}}/shaders" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/shaders"
            COMMAND ${{CMAKE_COMMAND}} -E copy_directory
                "${{CMAKE_CURRENT_SOURCE_DIR}}/resources" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/resources"
            COMMAND ${{CMAKE_COMMAND}} -E copy_directory
                "${{CMAKE_CURRENT_SOURCE_DIR}}/worlds" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/worlds"
            COMMAND ${{CMAKE_COMMAND}} -E copy_if_different
                "${{CMAKE_CURRENT_SOURCE_DIR}}/engine.json" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/engine.json"
            COMMAND ${{CMAKE_COMMAND}} -E copy_if_different
                "${{CMAKE_CURRENT_SOURCE_DIR}}/game.json" "$<TARGET_FILE_DIR:${{PROJECT_NAME}}>/game.json"
        )
    """)

    # ── Header ──────────────────────────────────────────────────────────
    # Determine what includes and members the game needs
    extra_includes = []
    extra_members = []
    extra_fwd_decls = []

    extra_includes.append('#include "graphics/RenderCoordinator.h"')
    extra_includes.append('#include "scene/AnimatedVoxelCharacter.h"')
    extra_includes.append('#include "ui/GameScreen.h"')
    extra_includes.append('#include "ui/GameMenus.h"')
    extra_includes.append('#include "core/TriggerSystem.h"')
    extra_includes.append('#include "core/WorldHealth.h"')
    extra_includes.append('#include <filesystem>')
    extra_includes.append('#include <fstream>')
    extra_includes.append('#include "core/LocationRegistry.h"')
    extra_members.append("    std::unique_ptr<Phyxel::Graphics::RenderCoordinator> renderCoordinator_;")
    extra_members.append("    Phyxel::Scene::AnimatedVoxelCharacter* playerCharacter_ = nullptr;")
    extra_members.append("    std::vector<std::unique_ptr<Phyxel::Scene::Entity>> entities_;")
    extra_members.append("    Phyxel::UI::GameScreen screen_;")
    extra_members.append("    Phyxel::Core::TriggerSystem triggers_;  // declarative when/then win conditions (game.json \"triggers\")")
    extra_members.append("    nlohmann::json lastWorldHealth_;  // WorldHealth::check on every world scene ready (layer B self-check)")
    extra_members.append("    std::string lastLintSignature_;    // visible-screen set the HUD layout lint last ran on")
    extra_members.append("    int lintArmFrames_ = 0;             // frames until the deferred HUD lint runs")
    extra_members.append("    glm::uvec2 lastUiWindow_{0u, 0u};    // last window size the HUD was placed for (G-132)")
    extra_members.append("    Phyxel::Core::Currency wallet_;         // the player's money (never an inventory item)")
    extra_members.append("    Phyxel::Core::ClickToMove clickToMove_;  // click the ground -> walk the NavGraph route (G-75)")
    extra_members.append("    bool walkLmbHeld_ = false;              // edge guard for out-of-combat clicks")
    extra_members.append("    bool pointerMode_ = false;              // turn-based ruleset: free cursor, LMB is a pointer click")
    extra_members.append("    Phyxel::Core::ClickToMove::Result lastWalkResult_ = Phyxel::Core::ClickToMove::Result::Idle;")
    extra_members.append("    nlohmann::json pointerClick(Phyxel::Core::EngineRuntime& engine, const glm::vec2& click, bool rightButton);")
    extra_members.append("    nlohmann::json apiPointerClick(float x, float y, const std::string& button) override { return engine_ ? pointerClick(*engine_, {x, y}, button == \"right\") : nlohmann::json{{\"error\", \"no engine\"}}; }")
    extra_members.append("    bool clickToMoveEnabled_ = false;       // game.json controls.clickToMove (WoW default: off)")
    extra_members.append("    bool pointerLmbHeld_ = false, pointerRmbHeld_ = false;   // click-vs-drag tracking")
    extra_members.append("    glm::vec2 pointerLmbPress_{0.0f}, pointerRmbPress_{0.0f};")
    extra_members.append("    bool pointerDragging_ = false;          // a held button moved past the click threshold")
    extra_members.append("    bool interactKeyDown_ = false;          // edge guard for the Interact action")
    extra_members.append("    bool characterKeyDown_ = false;         // edge guard for the ToggleCharacter action (G-133)")
    extra_members.append("    std::optional<std::string> characterSheetToken(const std::string& key) const;  // {{sheet.*}} for character_screen.json")
    extra_members.append("    std::unique_ptr<Phyxel::RaycastVisualizer> raycastVisualizer_;   // world-space line pass (target rings)")
    extra_includes.append('#include "core/GameDefinitionLoader.h"')
    extra_members.append("    Phyxel::Core::GameSubsystems gameSubsystems_;  // persistent: the SceneManager keeps a pointer to it")
    # Editor-parity gameplay state (docs/game-production/StandaloneParityGaps.md §1):
    # objectives + persistence exist in the SHIPPED game, not just the editor host.
    extra_includes.append('#include "core/ObjectiveTracker.h"')
    extra_includes.append('#include "core/PlayerProfile.h"')
    extra_includes.append('#include "core/ActionBar.h"')
    # Turn-based combat in the SHIPPED game (StandaloneParityGaps.md §1, CombatDirector
    # row): the same director/AI/player-turn stack the editor wires (Application.cpp
    # ~554-589 + 1847-1859), minus editor-only cast visuals. game.json "combat.mode"
    # selects the ruleset; a "start_combat" trigger action begins authored encounters.
    extra_includes.append('#include "core/CombatDirector.h"')
    extra_includes.append('#include "core/CombatAISystem.h"')
    extra_includes.append('#include "core/PlayerTurnController.h"')
    extra_includes.append('#include "core/CombatSystem.h"')
    # Spellcasting (shipped-game parity with the editor's cast path)
    extra_includes.append('#include "core/VfxDirector.h"')
    extra_includes.append('#include "core/SpellVfxMapper.h"')
    extra_includes.append('#include "core/SpellDefinition.h"')
    extra_includes.append('#include "core/SpellAnimMapper.h"')
    extra_includes.append('#include "core/Party.h"')
    extra_includes.append('#include "core/DiceSystem.h"')
    extra_includes.append('#include "scene/CharacterTurnBody.h"')
    extra_includes.append('#include "scene/NPCEntity.h"')
    extra_members.append("    Phyxel::Core::CombatDirector combatDirector_;        // initiative + turn order (single source of combat truth)")
    extra_members.append("    Phyxel::Core::CombatAISystem combatAI_;              // runs enemy turns through TurnActor")
    extra_members.append("    Phyxel::Core::PlayerTurnController playerTurn_;      // player intents -> the same TurnActor path")
    extra_members.append("    Phyxel::Core::Party rpgParty_;")
    extra_members.append("    std::unique_ptr<Phyxel::Core::CombatSystem> combatSystem_;  // the applyDamage funnel")
    extra_members.append("    std::unordered_map<Phyxel::Scene::AnimatedVoxelCharacter*, std::unique_ptr<Phyxel::Scene::CharacterTurnBody>> turnBodies_;")
    # Progression: kill/quest XP -> CharacterProgression::awardXP -> level-up.
    # Authored via game.json "progression" {class, race, kill_xp, objective_xp}.
    extra_includes.append('#include "core/CharacterSheet.h"')
    extra_includes.append('#include "core/CharacterProgression.h"')
    extra_includes.append('#include "core/ClassDefinition.h"')
    extra_includes.append('#include "core/RaceDefinition.h"')
    extra_includes.append('#include "core/ProficiencySystem.h"')
    extra_members.append("    Phyxel::Core::CharacterSheet playerSheet_;  // progression: XP/level/classes")
    extra_members.append("    int killXp_ = 0;       // XP per enemy killed (game.json progression.kill_xp)")
    extra_members.append("    int objectiveXp_ = 0;  // XP per objective completed (progression.objective_xp)")
    extra_members.append("    bool profileRestored_ = false;  // profile restore runs ONCE per session, at the first world-scene load (a menu-start game has no world DB open at boot)")
    # Inventory: loot via the give_item trigger action; persists in the profile blob.
    extra_includes.append('#include "core/Inventory.h"')
    extra_members.append("    Phyxel::Core::Inventory inventory_;  // player inventory (loot; persisted via PlayerProfile)")
    extra_members.append("    bool endTurnKeyDown_ = false;  // Space = End Turn edge detector (turn-based combat)")
    extra_members.append("    bool dlgKeyDown_[6] = {};      // edge detectors for E, Enter, 1-4 (dialogue keys)")
    extra_members.append("    bool barKeyDown_[13] = {};     // G-150: edge detectors for the 12 bar keys + the spellbook toggle")
    extra_members.append("    bool spellbookOpen_ = false;   // G-150: the abilities panel you drag from")
    extra_members.append("    bool victoryEmitted_ = false;  // combat_victory fired once per encounter (kill path OR escape path)")
    extra_members.append("    bool loseAutoGameOver_ = true; // player death -> game-over screen (game.json lose.auto_game_over)")
    # BG3-style tactical camera: swap to an overhead/isometric rig while an
    # encounter runs, restore the scene's rig after (combat.camera in game.json).
    extra_members.append('    std::string combatCameraRig_ = "overhead";  // rig while in combat (game.json combat.camera)')
    extra_members.append("    std::string preCombatRig_;      // rig to restore when the encounter ends")
    extra_members.append("    bool wasInCombat_ = false;      // combat camera edge detection")
    # menuWorld: menu scenes with an authored world get a looping CameraPath
    # orbit behind their UI (PresentationPolish.md §3 Tier 1).
    extra_includes.append('#include "graphics/CameraManager.h"')
    extra_includes.append('#include "graphics/CameraRig.h"')  # TacticalRig focus framing
    extra_members.append("    Phyxel::Graphics::CameraPath menuCamPath_;  // drives the menuWorld orbit while a menu scene is up")
    extra_members.append("    bool combatLmbHeld_ = false;    // click-to-act edge detection (BG3 mouse combat)")
    # Spell hotbar (BG3 casting UI): authored spells -> combat spellbar; click a
    # slot to ARM, click an enemy to CAST (routes through castSpell instead of
    # the melee pick). Ground click with a spell armed = cancel.
    extra_includes.append('#include "core/SpellcasterComponent.h"')
    extra_includes.append('#include "core/CombatLog.h"')
    # Behavior-tree action vocabulary registered by this game (registerBehaviorActions)
    extra_includes.append('#include "ai/BTActionRegistry.h"')
    extra_includes.append('#include "ai/TacticalSpace.h"')   # LOS for the combat verbs
    extra_includes.append('#include "ai/CommandStructure.h"')
    extra_members.append("    Phyxel::AI::CommandStructure command_;  // squads + orders (game.json squad/rank)")
    extra_includes.append('#include "core/KinematicVoxelManager.h"')
    extra_includes.append('#include "core/ItemRegistry.h"')
    extra_includes.append('#include "core/ItemPropManager.h"')
    extra_includes.append('#include "scene/behaviors/CombatBehavior.h"')
    extra_includes.append('#include <glm/gtx/euler_angles.hpp>')   # eulerAngleYXZ for grip rotation
    extra_members.append("    std::unique_ptr<Phyxel::Core::KinematicVoxelManager> kinematicVoxelManager_;  // holds NPC weapon meshes")
    extra_members.append("    std::unique_ptr<Phyxel::ObjectTemplateManager> weaponTemplates_;  // loads the weapon .voxel models on demand")
    extra_members.append("    std::unique_ptr<Phyxel::ObjectTemplateManager> templates_;        // the whole template library (game.json templates, transition markers)")
    extra_members.append("    std::unique_ptr<Phyxel::Core::PlacedObjectManager> placedObjects_;  // placed-object registry the loader places into")
    extra_members.append("    struct HeldWeapon { std::string kinId; int anchorId = -1; std::string itemId; };")
    extra_members.append("    std::unordered_map<std::string, HeldWeapon> npcHeld_;  // entityId -> attached weapon")
    extra_includes.append('#include "ai/ActionSystem.h"')
    extra_members.append('    Phyxel::Core::SpellcasterComponent playerCaster_;  // slots/known spells; bound to playerTurn_')
    extra_members.append('    std::unordered_map<std::string, Phyxel::Core::SpellcasterComponent> npcCasters_;  // game.json "casters": enemy/companion spell lists for CombatAI')
    extra_members.append('    std::unordered_map<std::string, Phyxel::Core::CombatTactics> npcTactics_;  // game.json "combat_ai": per-NPC tactical profile')
    extra_members.append('    std::vector<std::string> playerSpells_;  // game.json progression.spells (SpellRegistry ids)')
    extra_members.append('    std::string armedSpell_;        // spellbar-armed spell; empty = melee/move clicks')
    extra_members.append('    Phyxel::Core::EncounterInitiator encounterInitiator_;   // G-137: fights from clicks, sight and opening spells')
    extra_members.append('    std::string pendingOpeningCast_;                        // spell armed out of combat; cast on the first turn')
    extra_members.append('    std::vector<std::string> encounterHostiles_;            // the enemy side of the running encounter')
    extra_members.append('    float aggroClock_ = 0.0f;                               // throttle for the sight scan')
    extra_members.append('    std::unordered_set<std::string> authoredHostiles_;      // enemy-side ids of every authored start_combat (G-137)')
    extra_members.append('    void beginEncounterWith(std::vector<Phyxel::Core::CombatDirector::Combatant> combatants, const std::string& reason);')
    extra_members.append('    bool startEncounterAgainst(const std::string& npcId, const std::string& reason);')
    extra_members.append('    std::string hoveredTarget_;     // combatant under the cursor (nameplate + targeting readout)')
    extra_members.append('    int nameplateDiagFrame_ = 0;    // throttle for the nameplate diagnostic')
    extra_members.append("    Phyxel::Core::ObjectiveTracker objectiveTracker_;  // quest-log spine; game.json \"objectives\" load here")
    extra_members.append("    nlohmann::json initialObjectives_ = nlohmann::json::array();  // game.json objectives, re-added on New Game")
    extra_members.append("    void newGame();   // restart from the start scene with a fresh run (G-129)")
    extra_members.append("    Phyxel::Core::PlayerProfile playerProfile_;        // persisted to the active scene's world DB (player_state table)")
    extra_members.append("    std::string loadingSceneName_;  // destination scene shown on the loading screen")
    extra_members.append("    float loadingHold_ = -1.0f;         // >= 0: loading screen held after the load until the world settles (G-115)")

    # Menu-scene support: a JSON-driven menu renderer for sceneType:"menu" scenes.
    # When the loaded game uses menu scenes, the SceneManager drives the flow and
    # this renders the active menu — so the built-in ScreenState shell does NOT
    # double up on top of it (see docs/GameCreationGuide.md, "Menus").
    extra_includes.append('#include "ui/GameMenuRenderer.h"')
    # Data-driven UISystem HUD + menus (custom-Vulkan, no ImGui) — see docs/HudSystem.md.
    extra_includes.append('#include "ui/MenuDefinition.h"')   # loadHudInto / loadMenuInto / MenuActions
    extra_includes.append('#include "ui/UISystem.h"')
    extra_includes.append('#include "ui/HudDataContext.h"')
    extra_includes.append('#include "graphics/RaycastVisualizer.h"')   # target rings (CombatUiBg3 increment 3)
    extra_includes.append('#include "core/CurrencySystem.h"')   # wallet (CombatUiBg3 increment 4)
    extra_includes.append('#include "core/ClickToMove.h"')   # click-to-move (CombatUiBg3 increment 5, G-75)
    extra_includes.append('#include "core/HealthComponent.h"')
    extra_includes.append('#include "core/RpgItem.h"')
    extra_includes.append('#include "core/EncounterInitiator.h"')   # fights that start without a trigger (G-137)
    extra_includes.append('#include <unordered_set>')
    extra_includes.append('#include "utils/VoxelRayMarch.h"')
    extra_includes.append('#include "utils/CapsuleOcclusion.h"')   # ring dots hidden behind bodies (G-138)   # combat_ai "weapon" -> damage dice
    extra_members.append("    std::unique_ptr<Phyxel::UI::GameMenuRenderer> gameMenuRenderer_;")
    extra_members.append("    bool menuSceneActive_ = false;  // a sceneType:\"menu\" scene is currently shown")
    extra_members.append("    std::string activeDataScreen_;  // which data-driven overlay is loaded: pause/intro/victory/credits (replaces ImGui ScreenState screens)")
    extra_members.append("    bool escPrev_ = false;  // ESC edge-trigger (held isKeyPressed would toggle pause every frame)")
    extra_members.append("    float lastDt_ = 0.0f;           // last frame dt, for menu animations in onRender")
    extra_members.append("    bool authoredCameraMode_ = false;  // game.json camera block carries an explicit \"mode\"")

    if has_npcs or game_definition:
        extra_includes.append('#include "core/EntityRegistry.h"')
        extra_includes.append('#include "core/NPCManager.h"')
        extra_members.append("    std::unique_ptr<Phyxel::Core::EntityRegistry> entityRegistry_;")
        extra_members.append("    std::unique_ptr<Phyxel::Core::NPCManager> npcManager_;")

    # The dialogue/speech members are declared UNCONDITIONALLY: the GameShell override
    # `apiDialogueSystem()` that returns `dialogueSystem_` is always emitted, so a shell
    # regenerated without a game definition (`--force` alone, 2026-09-11) did not compile
    # ("'dialogueSystem_': undeclared identifier"). Construction stays conditional; the
    # override then returns null, which the API reports honestly.
    extra_includes.append('#include "ui/DialogueSystem.h"')
    extra_includes.append('#include "ui/SpeechBubbleManager.h"')
    extra_members.append("    std::unique_ptr<Phyxel::UI::DialogueSystem> dialogueSystem_;")
    extra_members.append("    std::unique_ptr<Phyxel::UI::SpeechBubbleManager> speechBubbleManager_;")
    if has_npcs or game_definition:
        pass

    if has_story or game_definition:
        extra_includes.append('#include "story/StoryEngine.h"')
        extra_members.append("    std::unique_ptr<Phyxel::Story::StoryEngine> storyEngine_;")

    # Interaction manager for E-key NPC interaction
    if has_npcs or game_definition:
        extra_includes.append('#include "core/InteractionManager.h"')
        extra_members.append("    std::unique_ptr<Phyxel::Core::InteractionManager> interactionManager_;")

    # AI conversation service (always available — uses API key from settings/env)
    if has_npcs or game_definition:
        extra_includes.append('#include "ai/AIConversationService.h"')
        extra_includes.append('#include "core/GameSettings.h"')
        extra_members.append("    std::unique_ptr<Phyxel::AI::AIConversationService> aiConversationService_;")
        extra_members.append("    Phyxel::Core::GameSettings settings_;")

    extra_includes.append('#include "core/ObjectTemplateManager.h"')
    extra_includes.append('#include "core/PlacedObjectManager.h"')

    includes_str = "\n".join(sorted(set(extra_includes)))
    members_str = "\n".join(extra_members)

    # Build header file (no textwrap.dedent — control indentation directly)
    header_lines = [
        "#pragma once",
        '#include "core/GameShell.h"',
        '#include "core/EngineRuntime.h"',
        '#include "scene/Entity.h"',
        *sorted(set(extra_includes)),
        "#include <memory>",
        "#include <vector>",
        "#include <unordered_map>",
        "",
        "// GameShell is the engine-side base for standalone games: it owns the",
        "// gameplay camera + character control loop (rig/scheme resolved from each",
        "// scene's camera block) so this scaffold stays thin and engine fixes",
        "// propagate on rebuild. See docs/CameraControlSystem.md.",
        f"class {class_name} : public Phyxel::Core::GameShell {{",
        "public:",
        f"    bool onInitialize(Phyxel::Core::EngineRuntime& engine) override;",
        f"    void onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) override;",
        f"    void onRender(Phyxel::Core::EngineRuntime& engine) override;",
        f"    void onHandleInput(Phyxel::Core::EngineRuntime& engine) override;",
        f"    void onShutdown() override;",
        "",
        "    // Expose this game's subsystems to the opt-in standalone test API",
        "    // (GameShell::startTestApi, dev/test only). Null hooks stay 'not available'.",
        "    Phyxel::Graphics::RenderCoordinator* apiRenderCoordinator() override { return renderCoordinator_.get(); }",
        "    Phyxel::UI::GameScreen* apiScreen() override { return &screen_; }",
        "    Phyxel::Core::TriggerSystem* apiTriggerSystem() override { return &triggers_; }",
        "    nlohmann::json apiWorldHealth() override { return lastWorldHealth_; }",
        "    Phyxel::Scene::AnimatedVoxelCharacter* apiPlayer() override { return playerCharacter_; }",
        "    Phyxel::Core::CombatDirector*       apiCombatDirector() override { return &combatDirector_; }",
        "    Phyxel::Core::CombatAISystem*       apiCombatAI() override       { return &combatAI_; }",
        "    Phyxel::Core::CombatSystem*         apiCombatSystem() override   { return combatSystem_.get(); }",
        "    Phyxel::Core::PlayerTurnController* apiPlayerTurn() override     { return &playerTurn_; }",
        "    Phyxel::Core::ClickToMove*          apiClickToMove() override    { return &clickToMove_; }",
        "    Phyxel::Core::CharacterSheet*       apiPlayerSheet() override    { return &playerSheet_; }",
        "    Phyxel::Core::Inventory*            apiInventory() override      { return &inventory_; }",
        "    Phyxel::UI::DialogueSystem*         apiDialogueSystem() override { return dialogueSystem_.get(); }",
        *([
            "    Phyxel::Core::EntityRegistry* apiEntityRegistry() override { return entityRegistry_.get(); }",
            "    Phyxel::Core::NPCManager* apiNPCManager() override { return npcManager_.get(); }",
        ] if (has_npcs or game_definition) else []),
        "",
        "private:",
        f"    bool loadGameDefinition(Phyxel::Core::EngineRuntime& engine);",
        f"    Phyxel::Scene::Entity* spawnEntity(const std::string& type, const glm::vec3& pos, const std::string& animFile);",
        f"    void updateCursorMode(Phyxel::Core::EngineRuntime& engine);",
        f"    void savePlayerProfile();   // camera+health -> active scene world DB",
        f"    bool loadPlayerProfile();   // world DB -> camera+health (boot / save points)",
        f"    void applyAudioSettings();  // settings_ volumes -> EngineRuntime AudioSystem",
        f"    void grantXP(int xp, const char* why);  // awardXP + level-up log/event",
        f"    void faceToward(Phyxel::Scene::AnimatedVoxelCharacter* ch, const glm::vec3& at);",
        f"    void playCastVisual(const std::string& spellId, const glm::vec3& targetPos, std::function<void()> onRelease);",
        f"    void playCastVisualFor(Phyxel::Scene::AnimatedVoxelCharacter* caster, const std::string& spellId, const glm::vec3& targetPos, std::function<void()> onRelease);",
        f"    void setArmedSpell(const std::string& id);  // spellbar arm/disarm + button highlight",
        f"    void registerBehaviorActions();  // this game's BT action verbs (BTActionRegistry)",
        f"    void installDoctrine();          // how this game's officers decide",
        f"    void equipNpcWeapons();          // attach game.json \"weapon\" models to grip bones",
        f"    void updateHeldWeapons();        // follow the grip bone each frame",
        f"    void updateCommand(float dt);    // per-frame squad situation + orders",
        f"    void refreshSpellbar();  // repaint labels/slot counts/enabled state from live caster state",
        f"    bool fireActionSlot(int slot);   // G-150: what a bar click or keybind 1-9,0,-,= does",
        f"    void ensureDefaultActionBar();   // G-150: first-run layout; never overwrites the player's",
        f"    void onActionBarDrop(const Phyxel::UI::HudRecord& r);  // G-150: a drag landed on the bar",
        f"    Phyxel::Scene::AnimatedVoxelCharacter* characterOf(const std::string& entityId);",
        f"    void faceCombatants();  // everyone faces the nearest opposing-side combatant",
        "",
        "    float elapsed_ = 0.0f;",
        f"    Phyxel::Core::EngineRuntime* engine_ = nullptr;",
        *extra_members,
        "};",
        "",
    ]
    files[f"{class_name}.h"] = "\n".join(header_lines)

    # ── Implementation ──────────────────────────────────────────────────
    files[f"{class_name}.cpp"] = _generate_game_cpp(class_name, game_definition)

    # ── main.cpp ────────────────────────────────────────────────────────
    files["main.cpp"] = textwrap.dedent(f"""\
        #include "{class_name}.h"
        #include "core/EngineRuntime.h"
        #include "core/EngineConfig.h"
        #include "utils/Logger.h"

        #include <cstdlib>
        #include <string>

        int main(int argc, char* argv[]) {{
            // Write a log next to the exe so a packaged game that exits early is
            // diagnosable (boot errors land in {name_lower}.log instead of nowhere).
            Phyxel::Utils::Logger::enableFileOutput(true, "{name_lower}.log");
            LOG_INFO("main", "{name} starting");

            Phyxel::Core::EngineConfig config;
            Phyxel::Core::EngineConfig::loadFromFile("engine.json", config);

            // DEV/TEST ONLY: `--test` (or `--api`) [port] enables the in-game HTTP
            // API so an automated harness (game-production validators / adversarial
            // playtest) can drive + observe THIS real build. Off by default — never
            // pass it in a build a player runs. Binds to localhost.
            for (int i = 1; i < argc; ++i) {{
                std::string a = argv[i];
                if (a == "--test" || a == "--api") {{
                    config.testApiEnabled = true;
                    if (i + 1 < argc && argv[i + 1][0] != '-') config.apiPort = std::atoi(argv[++i]);
                }}
            }}

            Phyxel::Core::EngineRuntime engine;
            if (!engine.initialize(config)) {{
                LOG_ERROR("main", "Failed to initialize engine");
                return 1;
            }}

            {class_name} game;
            engine.run(game);
            return 0;
        }}
    """)

    # ── engine.json ─────────────────────────────────────────────────────
    # NOTE: per-project API/MCP port + the Claude workflow files (.mcp.json,
    # .phyxel/config.json, CLAUDE.md) are written by `phyxel link` below — the single
    # canonical, path-free wiring (see docs/GameDevWorkflow.md). Do NOT duplicate that
    # here (a parallel scheme collides with `phyxel link`).
    engine_cfg = {
        "window": {"width": 1280, "height": 720, "title": name},
        "rendering": {
            "max_chunk_render_distance": 96.0,
            "chunk_inclusion_distance": 128.0,
        },
    }
    files["engine.json"] = json.dumps(engine_cfg, indent=2) + "\n"

    # Write all files
    for filename, content in files.items():
        filepath = output_dir / filename
        filepath.write_text(content, encoding="utf-8")

    # Copy game.json if provided
    if game_definition:
        game_json_path = output_dir / "game.json"
        game_json_path.write_text(json.dumps(game_definition, indent=2), encoding="utf-8")

    # Create placeholder directories
    (output_dir / "worlds").mkdir(exist_ok=True)
    (output_dir / "shaders").mkdir(exist_ok=True)
    (output_dir / "resources" / "textures").mkdir(parents=True, exist_ok=True)

    # Wire the Claude game-dev workflow (per-project port + .mcp.json + CLAUDE.md) via
    # the canonical `phyxel link` — single source of truth, path-free/portable (see
    # docs/GameDevWorkflow.md). Degrades to an instruction if the CLI isn't installed.
    import shutil, subprocess
    linked = False
    phyxel_exe = shutil.which("phyxel")
    if phyxel_exe:
        try:
            r = subprocess.run([phyxel_exe, "link", str(output_dir)],
                               capture_output=True, text=True, timeout=30)
            if r.returncode == 0:
                linked = True
                if r.stdout.strip():
                    print(r.stdout.strip())
            else:
                print(f"  (phyxel link failed: {r.stderr.strip()})")
        except Exception as e:
            print(f"  (phyxel link error: {e})")

    # For multi-scene definitions, list expected scene databases
    if game_definition and "scenes" in game_definition:
        scenes = game_definition["scenes"]
        print(f"  Multi-scene game detected with {len(scenes)} scene(s).")
        world_scenes = [s for s in scenes if s.get("sceneType", "world") != "menu"]
        print(f"  Each world scene needs a pre-baked world database in worlds/:")
        for scene in world_scenes:
            db_name = scene.get("worldDatabase", f"worlds/{scene['id']}.db")
            db_file = Path(db_name).name
            print(f"    - worlds/{db_file}")

        # Menu-scene pattern guidance — the generated standalone renders menu scenes
        # via GameMenuRenderer instead of the built-in ScreenState shell, but the
        # author still needs to pick ONE pattern (see docs/GameCreationGuide.md).
        menu_scenes = [s for s in scenes if s.get("sceneType") == "menu"]
        start_scene = game_definition.get("startScene", "")
        start_def = next((s for s in scenes if s.get("id") == start_scene), None)
        if menu_scenes:
            print()
            print(f"  NOTE: {len(menu_scenes)} menu scene(s) detected "
                  f"({', '.join(s.get('id', '?') for s in menu_scenes)}).")
            print( "        The standalone renders the active menu scene and hides the")
            print( "        built-in Intro/MainMenu shell so they don't double up.")
            if start_def is not None and start_def.get("sceneType") != "menu":
                print( "        startScene is a WORLD scene -> the built-in shell drives;")
                print( "        end the game with show_victory/show_credits triggers, OR")
                print( "        set startScene to a menu scene to use your menu scenes.")
            print( "        See docs/GameCreationGuide.md -> 'Menus & Win/Lose Screens'.")

    print(f"Created project '{name}' in {output_dir}")
    print()
    if linked:
        print("  Claude game-dev workflow wired (own engine port + .mcp.json + CLAUDE.md via")
        print("  `phyxel link`). Open a Claude session in this folder and it auto-targets this")
        print("  project's engine instance (no collision with other sessions).")
    else:
        print("  Game-dev workflow NOT wired — the `phyxel` CLI isn't installed. Enable it once")
        print("  per machine (see docs/GameDevWorkflow.md 'Per-machine setup'):")
        print("    pip install -e <engine>/tools/phyxel-cli && phyxel init --home <engine>")
        print(f"  then:  phyxel link \"{output_dir}\"")
    print()
    print("Next steps:")
    print(f"  1. Copy required assets (shaders, textures) from the Phyxel engine")
    print(f"  2. Pre-bake the world:  python tools/package_game.py {name} --prebake-world")
    print(f"  3. Build:")
    print(f"       cd {output_dir}")
    print(f"       cmake -B build -S .")
    print(f"       cmake --build build --config Debug")
    print(f"       .\\build\\Debug\\{name}.exe")


def _generate_game_cpp(class_name: str, game_def: dict | None) -> str:
    """Generate the game implementation C++ file."""
    # Tagline for the intro/credits screens: the game definition's description,
    # escaped for use inside a C++ string literal.
    game_tagline = ((game_def or {}).get("description", "") or "A Phyxel game")
    game_tagline = game_tagline.replace("\\", "\\\\").replace('"', '\\"')

    # ── Data-driven HUD wiring (UISystem, no ImGui) ─────────────────────
    # Built as a NORMAL string (not inside the cpp f-string) so its C++ braces need
    # no doubling, then injected via {hud_setup}. A DialogueSystem exists whenever a
    # game definition is loaded (see create_project: member added when game_definition),
    # so dialogue providers are emitted iff game_def is present. player health is
    # always available (playerCharacter_ is an unconditional member).
    has_dialogue = bool(game_def)
    dialogue_providers = ""
    if has_dialogue:
        dialogue_providers = """
                hud.setFloat("dialogue.active", [this]() {
                    return (dialogueSystem_ && dialogueSystem_->isActive() && !dialogueSystem_->isAIConversation()) ? 1.0f : 0.0f;
                });
                hud.setFloat("dialogue.waiting", [this]() {
                    return (dialogueSystem_ && dialogueSystem_->getState() == Phyxel::UI::DialogueState::WaitingForInput) ? 1.0f : 0.0f;
                });
                hud.setText("dialogue.speaker", [this]() {
                    return dialogueSystem_ ? dialogueSystem_->getCurrentSpeaker() : std::string();
                });
                hud.setText("dialogue.text", [this]() {
                    return dialogueSystem_ ? dialogueSystem_->getRevealedText() : std::string();
                });
                hud.setList("dialogue.choices", [this]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    if (!dialogueSystem_ || dialogueSystem_->getState() != Phyxel::UI::DialogueState::ChoiceSelection) return rows;
                    const auto& ch = dialogueSystem_->getAvailableChoices();
                    for (size_t i = 0; i < ch.size(); ++i) {
                        Phyxel::UI::HudRecord r;
                        r.texts["label"] = "[" + std::to_string(i + 1) + "] " + ch[i].text;
                        rows.push_back(std::move(r));
                    }
                    return rows;
                });"""
    hud_setup = """
            // Data-driven HUD on the UISystem (custom-Vulkan, no ImGui): init the
            // UISystem and register the data providers this game supplies. The HUD
            // PANELS are loaded later in loadGameDefinition(), once game.json is
            // parsed, so a game's own "hud" block can override the engine default
            // (resources/ui/default_hud.json). See docs/HudSystem.md.
            renderCoordinator_->initUISystem();
            {
                auto& hud = renderCoordinator_->hudData();
                hud.setText("player.goldText", [this]() {{ return wallet_.gold > 0 || wallet_.silver > 0 || wallet_.copper > 0 ? wallet_.toString() : std::string("0 gp"); }});
                // Compass (G-117): heading from the camera (0 = north = +z, 90 = east = +x); the
                // engine's camera front is (cos yaw, ., sin yaw), so the bearing is 90 - yaw.
                hud.setFloat("compass.heading", [this]() {
                    auto* cam = engine_ ? engine_->getCamera() : nullptr;
                    float b = cam ? 90.0f - cam->getYaw() : 0.0f;
                    b = std::fmod(b, 360.0f); if (b < 0.0f) b += 360.0f;
                    return b;
                });
                // Points of interest: every scene exit (named after the scene it leads to) and
                // every authored location, as bearings from the player. No quest markers.
                hud.setList("compass.poi", [this]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    if (!playerCharacter_) return rows;
                    const glm::vec3 me = playerCharacter_->getPosition();
                    auto bearingTo = [&](const glm::vec3& p) {
                        float b = glm::degrees(std::atan2(p.x - me.x, p.z - me.z));
                        if (b < 0.0f) b += 360.0f; return b;
                    };
                    auto* smgr = engine_ ? engine_->getSceneManager() : nullptr;
                    for (const auto& ex : triggers_.exitRegions()) {
                        const auto& r = ex.region;
                        if (!r.contains("from") || !r.contains("to")) continue;
                        const glm::vec3 c((r["from"].value("x", 0.0f) + r["to"].value("x", 0.0f)) * 0.5f, me.y,
                                          (r["from"].value("z", 0.0f) + r["to"].value("z", 0.0f)) * 0.5f);
                        std::string label = ex.targetScene;
                        if (smgr) if (const auto* sc = smgr->findScene(ex.targetScene)) if (!sc->name.empty()) label = sc->name;
                        Phyxel::UI::HudRecord rec; rec.texts["label"] = label; rec.floats["bearing"] = bearingTo(c);
                        rows.push_back(std::move(rec));
                    }
                    if (auto* locs = engine_ ? engine_->getLocationRegistry() : nullptr)
                        for (const auto& [lid, loc] : locs->getAllLocations()) {
                            Phyxel::UI::HudRecord rec; rec.texts["label"] = loc.name.empty() ? lid : loc.name;
                            rec.floats["bearing"] = bearingTo(loc.position); rows.push_back(std::move(rec));
                        }
                    return rows;
                });
                hud.setFloat("player.health", [this]() {
                    auto* hc = playerCharacter_ ? playerCharacter_->getHealthComponent() : nullptr;
                    return hc ? hc->getHealth() : 100.0f;
                });
                hud.setFloat("player.maxHealth", [this]() {
                    auto* hc = playerCharacter_ ? playerCharacter_->getHealthComponent() : nullptr;
                    return hc ? hc->getMaxHealth() : 100.0f;
                });

                // Combat panels (initiative order, turn label, action budget, hit
                // chance) — the same providers the editor registers, wired to the
                // standalone's own combat stack. Hidden until combat.inCombat flips.
                hud.setFloat("combat.inCombat",         [this]{ return combatDirector_.inCombat() ? 1.0f : 0.0f; });
                hud.setFloat("combat.playerTurnActive", [this]{ return playerTurn_.isPlayerTurnActive() ? 1.0f : 0.0f; });
                // The action bar also shows OUT of combat (spells only): arm one, click a
                // hostile, and the fight opens with it (G-137).
                hud.setFloat("actionbar.visible", [this]{
                    if (playerTurn_.isPlayerTurnActive()) return 1.0f;
                    const bool talking = dialogueSystem_ && dialogueSystem_->isActive();
                    return (!combatDirector_.inCombat() && !talking && !playerSpells_.empty()) ? 1.0f : 0.0f;
                });
                hud.setText ("combat.roundText", [this]{
                    char buf[32];
                    snprintf(buf, sizeof(buf), "COMBAT - Round %d", combatDirector_.currentRound());
                    return std::string(buf);
                });
                hud.setText ("combat.turnLabel", [this]{
                    if (combatDirector_.isPlayerTurn()) return std::string("YOUR TURN");
                    std::string id = combatDirector_.currentEntityId();
                    return id.empty() ? std::string("") : (id + "'s Turn");
                });
                hud.setText ("combat.budgetText", [this]{
                    const auto* b = playerTurn_.budget();
                    if (!b) return std::string("");
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Action:%s  Bonus:%s  Move:%.1f",
                             b->action ? "Y" : "-", b->bonusAction ? "Y" : "-",
                             playerTurn_.movementRemainingUnits());
                    return std::string(buf);
                });
                // One line that tells the player what to do (the manual test of 2026-09-11:
                // "I have no idea how to actually do an attack").
                hud.setText ("combat.hintText", [this]() -> std::string {{
                    if (!combatDirector_.inCombat())
                        return armedSpell_.empty() ? std::string("Arm a spell, then click a foe to open the fight with it")
                                                   : "Cast " + armedSpell_ + ": click a foe to start the fight (click elsewhere to cancel)";
                    if (!playerTurn_.isPlayerTurnActive()) return "Enemy turn...";
                    if (!playerTurn_.approachTarget().empty()) return "Closing in on " + playerTurn_.approachTarget().substr(playerTurn_.approachTarget().rfind("npc_", 0) == 0 ? 4 : 0) + "...";
                    if (!armedSpell_.empty()) return "Cast " + armedSpell_ + ": click a target (click elsewhere to cancel)";
                    if (!hoveredTarget_.empty()) return "Click to attack " + (hoveredTarget_.rfind("npc_", 0) == 0 ? hoveredTarget_.substr(4) : hoveredTarget_);
                    if (!playerTurn_.selectedTarget().empty()) return "Target: " + playerTurn_.selectedTarget().substr(playerTurn_.selectedTarget().rfind("npc_", 0) == 0 ? 4 : 0) + "  -  Attack, or click the ground to move";
                    return "Your turn: click an enemy to attack, the ground to move";
                }});
                hud.setText ("combat.hitChanceText", [this]{
                    const std::string& tgt = playerTurn_.selectedTarget();
                    if (tgt.empty()) return std::string("");
                    char buf[96];
                    snprintf(buf, sizeof(buf), "%s: %.0f%% to hit (AC %d)%s",
                             tgt.c_str(), playerTurn_.hitChanceVs(tgt) * 100.0f, playerTurn_.targetAC(tgt),
                             playerTurn_.inReachOf(tgt) ? "" : "  [out of reach]");
                    return std::string(buf);
                });
                hud.setList("combat.turn_order", [this]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    const auto& tracker = combatDirector_.initiative();
                    if (!tracker.isCombatActive()) return rows;
                    const std::string& cur = combatDirector_.currentEntityId();
                    for (const auto& p : tracker.turnOrder()) {
                        Phyxel::UI::HudRecord r;
                        float hp = 0.0f, maxHp = 1.0f;
                        if (entityRegistry_) {
                            if (auto* e = entityRegistry_->getEntity(p.entityId)) {
                                if (auto* hc = e->getHealthComponent()) { hp = hc->getHealth(); maxHp = hc->getMaxHealth(); }
                            }
                        }
                        bool active = (p.entityId == cur);
                        r.floats["hp"] = hp;
                        r.floats["maxHp"] = maxHp;
                        r.floats["hpFrac"] = (maxHp > 0.0f) ? hp / maxHp : 0.0f;
                        r.floats["active"] = active ? 1.0f : 0.0f;
                        r.texts["name"] = p.entityId;
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%s%s [%d]  %d/%d",
                                 active ? "> " : "  ", p.entityId.c_str(), p.initiativeRoll,
                                 (int)(hp + 0.5f), (int)(maxHp + 0.5f));
                        r.texts["label"] = buf;
                        rows.push_back(std::move(r));
                    }
                    return rows;
                });

                // Objectives (quest log) — [x] when complete; .any gates visibility.
                hud.setFloat("objectives.any", [this] {
                    for (const auto* o : objectiveTracker_.getAllObjectives())
                        if (o && !o->hidden && o->status != Phyxel::Core::Objective::Status::Failed) return 1.0f;
                    return 0.0f;
                });
                hud.setList("objectives", [this]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    for (const auto* o : objectiveTracker_.getAllObjectives()) {
                        if (!o || o->hidden || o->status == Phyxel::Core::Objective::Status::Failed) continue;
                        bool done = (o->status == Phyxel::Core::Objective::Status::Completed);
                        Phyxel::UI::HudRecord r;
                        r.texts["label"] = std::string(done ? "[x] " : "[ ] ") + o->title;
                        r.floats["complete"] = done ? 1.0f : 0.0f;
                        rows.push_back(std::move(r));
                    }
                    return rows;
                });

                // Hotbar — first 9 slots, icon from the item's top-face texture.
                hud.setFloat("hotbar.any", [this] { return inventory_.size() > 0 ? 1.0f : 0.0f; });
                hud.setFloat("hotbar.creative", [] { return 0.0f; });   // a shipped RPG has no Minecraft hotbar
                // BG3 ACTION BAR (CombatUiBg3 increment 4): one row per thing you can DO on
                // your turn - Attack, each spell (armed = ember, depleted = disabled), End Turn.
                // Clicks route through the "actionbar.use" handler with the row's record.
                // ── UNIFIED ACTION BAR (G-150) ───────────────────────────────────
                // TWELVE stored slots, not a list derived from whatever the player
                // happens to know. Empty slots are real and stay put, so the bar does not
                // change shape when a fight starts, and an arrangement the player made
                // survives a relaunch (PlayerProfile::actionBar).
                auto wrapInto = [](const std::string& text, size_t cols, std::string& out) {
                    size_t lineLen = 0;
                    for (size_t i = 0; i < text.size(); ) {
                        size_t j = text.find(' ', i);
                        if (j == std::string::npos) j = text.size();
                        const size_t wlen = j - i;
                        if (lineLen > 0 && lineLen + 1 + wlen > cols) { out += "\\n"; lineLen = 0; }
                        else if (lineLen > 0) { out += " "; lineLen += 1; }
                        out.append(text, i, wlen);
                        lineLen += wlen;
                        i = j + 1;
                    }
                };
                // One assignment -> the row that draws it. SHARED by the bar and the
                // spellbook, so a spell reads identically in both and the tooltip text
                // is written once.
                auto describe = [this, wrapInto](const Phyxel::Core::ActionSlot& sl,
                                                 Phyxel::UI::HudRecord& r) {
                    using K = Phyxel::Core::ActionSlot::Kind;
                    const bool myTurn = playerTurn_.isPlayerTurnActive();
                    const bool actionLeft = myTurn && playerTurn_.budget() && playerTurn_.budget()->action;
                    const bool outOfCombat = !combatDirector_.inCombat();
                    r.floats["enabled"] = 0.0f;
                    r.floats["armed"] = 0.0f;
                    if (sl.empty()) return;
                    if (sl.kind == K::Action && sl.id == "attack") {
                        r.texts["label"] = "Attack";
                        r.texts["icon"] = "resources/ui/icons/spells/attack.png";
                        r.texts["tooltip"] = std::string("Attack\\nWeapon attack against the selected target.")
                                           + (actionLeft ? "" : "\\nUnavailable: no action left this turn");
                        r.floats["enabled"] = actionLeft ? 1.0f : 0.0f;
                        return;
                    }
                    if (sl.kind == K::Action && sl.id == "end_turn") {
                        r.texts["label"] = "End Turn";
                        r.texts["icon"] = "resources/ui/icons/spells/end_turn.png";
                        r.texts["tooltip"] = std::string("End Turn\\nPass the turn to the next combatant.")
                                           + (myTurn ? "" : "\\nUnavailable: it is not your turn");
                        r.floats["enabled"] = myTurn ? 1.0f : 0.0f;
                        return;
                    }
                    if (sl.kind == K::Spell) {
                        const std::string& sid = sl.id;
                        const auto* sd = Phyxel::Core::SpellRegistry::instance().getSpell(sid);
                        const std::string why = playerTurn_.castBlockedReason(sid);
                        const bool depleted = (why == "no slots" || why == "not prepared");
                        std::string label = sd ? sd->name : sid;
                        if (sd && !sd->isCantrip()) {
                            const int lvl = sd->level;
                            const int left = (lvl >= 1 && lvl <= Phyxel::Core::SpellSlots::MAX_SPELL_LEVEL)
                                                 ? playerCaster_.slots().remaining[lvl - 1] : 0;
                            label += " (" + std::to_string(left) + ")";
                        }
                        r.texts["label"] = label;
                        // Icons are generated from resources/spells/*.json by
                        // tools/gen_spell_icons.py; a spell with no PNG falls back to
                        // drawing its label rather than an empty square.
                        r.texts["icon"] = "resources/ui/icons/spells/" + sid + ".png";
                        std::string tip = label;
                        if (sd) {
                            tip += "\\n";
                            tip += sd->isCantrip() ? std::string("Cantrip")
                                                   : ("Level " + std::to_string(sd->level));
                            tip += std::string(" - ") + Phyxel::Core::spellSchoolName(sd->school);
                            if (sd->isSelf)               tip += " - Self";
                            else if (sd->isTouch)         tip += " - Touch";
                            else if (sd->rangeInFeet > 0) tip += " - " + std::to_string(sd->rangeInFeet) + " ft";
                            if (sd->hasDamage())
                                tip += "\\n" + sd->baseDamage.toString() + " "
                                     + Phyxel::Core::damageTypeToString(sd->damageType);
                            if (sd->hasHeal()) tip += "\\nHeals " + sd->healDice.toString();
                            if (sd->requiresConcentration) tip += "\\nConcentration";
                            if (!sd->description.empty()) { tip += "\\n"; wrapInto(sd->description, 38, tip); }
                        }
                        const bool rowEnabled = ((outOfCombat || actionLeft) && !depleted);
                        if (!rowEnabled && !why.empty()) tip += "\\nUnavailable: " + why;
                        r.texts["tooltip"] = tip;
                        r.floats["enabled"] = rowEnabled ? 1.0f : 0.0f;
                        r.floats["armed"] = (armedSpell_ == sid) ? 1.0f : 0.0f;
                        return;
                    }
                    // An ITEM can be assigned, dragged and saved, but nothing can fire it
                    // yet - this game has no use-item path at all. Say so in the tooltip
                    // rather than shipping a slot that silently does nothing.
                    r.texts["label"] = sl.id;
                    r.texts["icon"] = "resources/ui/icons/items/" + sl.id + ".png";
                    r.texts["tooltip"] = sl.id + "\\nItem\\nUnavailable: items cannot be used from the bar yet";
                };
                // The keybind label for each slot: 1-9, then 0, -, = (the WoW row).
                auto slotKeyLabel = [](int i) -> std::string {
                    if (i >= 0 && i <= 8) return std::string(1, static_cast<char>('1' + i));
                    if (i == 9)  return "0";
                    if (i == 10) return "-";
                    if (i == 11) return "=";
                    return "";
                };
                hud.setList("actionbar", [this, describe, slotKeyLabel]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    const auto& bar = playerProfile_.actionBar;
                    for (int i = 0; i < Phyxel::Core::ActionBar::SLOT_COUNT; ++i) {
                        const auto& sl = bar.at(i);
                        Phyxel::UI::HudRecord r;
                        r.floats["slot"] = static_cast<float>(i);
                        describe(sl, r);
                        // Only a FILLED slot can be picked up; an empty one is a drop
                        // target and nothing else.
                        if (!sl.empty()) r.texts["payload"] = "slot:" + std::to_string(i);
                        const std::string key = slotKeyLabel(i);
                        if (!sl.empty() && !key.empty()) {
                            r.texts["tooltip"] = "[" + key + "] " + r.texts["tooltip"];
                        } else if (sl.empty()) {
                            r.texts["tooltip"] = "[" + key + "] Empty slot\\nDrag an ability here from the "
                                                 "abilities panel (P).";
                        }
                        rows.push_back(std::move(r));
                    }
                    return rows;
                });
                // Everything that CAN go on the bar: the two built-in actions plus every
                // spell the player knows. Dragging from here copies - this is a
                // catalogue, not an inventory - which is also how an action dragged off
                // the bar can be put back.
                hud.setFloat("spellbook.visible", [this] { return spellbookOpen_ ? 1.0f : 0.0f; });
                hud.setList("spellbook", [this, describe]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    using K = Phyxel::Core::ActionSlot::Kind;
                    auto add = [&](K kind, const std::string& id) {
                        Phyxel::UI::HudRecord r;
                        describe(Phyxel::Core::ActionSlot{kind, id}, r);
                        r.texts["payload"] = std::string(Phyxel::Core::ActionSlot::kindName(kind)) + ":" + id;
                        // The catalogue shows what a thing IS, never whether you can use
                        // it this instant - it is not the bar.
                        r.floats["enabled"] = 1.0f;
                        rows.push_back(std::move(r));
                    };
                    add(K::Action, "attack");
                    for (const auto& sid : playerSpells_) add(K::Spell, sid);
                    add(K::Action, "end_turn");
                    return rows;
                });
                hud.setAction("actionbar.drop", [this](const Phyxel::UI::HudRecord& r) {
                    onActionBarDrop(r);
                });
                hud.setAction("actionbar.use", [this](const Phyxel::UI::HudRecord& r) {
                    const auto it = r.floats.find("slot");
                    if (it != r.floats.end()) fireActionSlot(static_cast<int>(it->second));
                });
                hud.setList("hotbar", [this]() {
                    std::vector<Phyxel::UI::HudRecord> rows;
                    int n = std::min(inventory_.size(), 9);
                    int sel = inventory_.getSelectedSlot();
                    for (int i = 0; i < n; ++i) {
                        Phyxel::UI::HudRecord r;
                        auto slot = inventory_.getSlot(i);
                        if (slot && !slot->itemId.empty()) {
                            std::string lower;
                            lower.reserve(slot->itemId.size());
                            for (char c : slot->itemId) lower += static_cast<char>(std::tolower((unsigned char)c));
                            r.texts["icon"] = "resources/textures/source/" + lower + "_top.png";
                            r.texts["count"] = (slot->count > 1) ? std::to_string(slot->count) : std::string();
                        } else {
                            r.texts["icon"] = "";
                            r.texts["count"] = "";
                        }
                        r.floats["selected"] = (i == sel) ? 1.0f : 0.0f;
                        rows.push_back(std::move(r));
                    }
                    return rows;
                });

                // Timer-trigger countdown -> hud_countdown panel (replaces ImGui
                // renderCountdownHud). Shows the first active countdown's label + time.
                hud.setFloat("countdown.active", [this]() {
                    return triggers_.getActiveCountdowns().empty() ? 0.0f : 1.0f;
                });
                hud.setText("countdown.text", [this]() -> std::string {
                    auto cds = triggers_.getActiveCountdowns();
                    if (cds.empty()) return std::string();
                    const auto& c = cds.front();
                    int mins = static_cast<int>(c.remaining / 60.0f);
                    float secs = c.remaining - static_cast<float>(mins) * 60.0f;
                    char buf[160];
                    if (!c.label.empty()) snprintf(buf, sizeof(buf), "%s  %d:%04.1f", c.label.c_str(), mins, secs);
                    else                  snprintf(buf, sizeof(buf), "%d:%04.1f", mins, secs);
                    return std::string(buf);
                });""" + dialogue_providers + """
            }"""

    return textwrap.dedent(f"""\
        #include "{class_name}.h"
        #include "core/ChunkManager.h"
        #include "core/GameDefinitionLoader.h"
        #include "core/SceneManager.h"
        #include "graphics/Camera.h"
        #include "graphics/CameraManager.h"
        #include "graphics/RenderCoordinator.h"
        #include "input/InputManager.h"
        #include "physics/PhysicsWorld.h"
        #include "scene/AnimatedVoxelCharacter.h"
        #include "vulkan/VulkanDevice.h"
        #include "vulkan/RenderPipeline.h"
        #include "ui/WindowManager.h"
        #include "ui/ImGuiRenderer.h"
        #include "ui/GameScreen.h"
        #include "ui/GameMenus.h"
        #include "ui/GameMenuRenderer.h"
        #include "core/ChunkStreamingManager.h"
        #include "core/WorldStorage.h"
        #include "core/InteractionManager.h"
        #include "core/AudioSystem.h"
        #include "utils/PerformanceProfiler.h"
        #include "utils/PerformanceMonitor.h"
        #include "utils/Logger.h"
        #include <nlohmann/json.hpp>
        #include <glm/glm.hpp>
        #include <GLFW/glfw3.h>
        #include <fstream>
        #include <filesystem>
        #include <algorithm>
        #include <cctype>
        #include <optional>
        #include <variant>

        // ====================================================================
        // Entity Spawning
        // ====================================================================

        Phyxel::Scene::Entity* {class_name}::spawnEntity(
                const std::string& type, const glm::vec3& pos, const std::string& animFile) {{
            if (type == "animated") {{
                auto ptr = std::make_unique<Phyxel::Scene::AnimatedVoxelCharacter>(
                    engine_->getPhysicsWorld(), pos);
                auto* raw = ptr.get();
                std::string file = animFile.empty() ? "resources/animated_characters/humanoid.anim" : animFile;
                if (raw->loadModel(file)) {{
                    raw->playAnimation("idle");
                }} else {{
                    LOG_ERROR("{class_name}", "Failed to load animated character: {{}}", file);
                }}
                entities_.push_back(std::move(ptr));
                return raw;
            }} else if (type == "physics" || type == "spider") {{
                // The Bullet-era PhysicsCharacter/SpiderCharacter are deprecated and no
                // longer ship with the engine — fall back to the animated character.
                LOG_WARN("{class_name}", "Entity type '{{}}' is deprecated; spawning 'animated' instead", type);
                return spawnEntity("animated", pos, animFile);
            }}
            LOG_WARN("{class_name}", "Unknown entity type: {{}}", type);
            return nullptr;
        }}

        // ====================================================================
        // Cursor mode helper
        // ====================================================================

        void {class_name}::updateCursorMode(Phyxel::Core::EngineRuntime& engine) {{
            auto* window = engine.getWindowManager();
            if (!window) return;
            // Test-API mode (--test): an automated harness drives the game — no
            // human is at the window. NEVER grab the OS cursor: a background
            // test run must not capture the user's mouse, and a hard-killed
            // process whose window held a GLFW_CURSOR_DISABLED grab can leave
            // the cursor locked/confined until the desktop refocuses.
            // (config.testApiEnabled is set in main() BEFORE init, unlike
            // testApiRunning(), which only turns true on the first onUpdate.)
            if (engine.getConfig().testApiEnabled) {{
                window->setCursorVisible(true);
                return;
            }}
            bool inDialogue = dialogueSystem_ && dialogueSystem_->isActive();
            // A menu scene always wants a free cursor (its buttons are clickable);
            // so does turn-based combat (BG3-style click-targeting under the
            // tactical camera).
            bool shouldCapture = !menuSceneActive_ && !pointerMode_ &&
                                 !Phyxel::UI::isMouseFree(screen_.getState()) && !inDialogue &&
                                 !combatDirector_.inCombat();
            // Pointer-driven games: the cursor is free, and disappears only while a
            // mouse-button DRAG turns the view (WoW hides it for the drag and puts it
            // back where it was — GLFW restores the position on re-enable).
            window->setCursorVisible(!shouldCapture && !(pointerMode_ && pointerDragging_));
        }}

        // ====================================================================
        // Initialization
        // ====================================================================

        bool {class_name}::onInitialize(Phyxel::Core::EngineRuntime& engine) {{
            LOG_INFO("{class_name}", "Initializing...");
            engine_ = &engine;

            // Create game subsystems
            entityRegistry_ = std::make_unique<Phyxel::Core::EntityRegistry>();
            npcManager_ = std::make_unique<Phyxel::Core::NPCManager>();
            npcManager_->setPhysicsWorld(engine.getPhysicsWorld());
            npcManager_->setChunkManager(engine.getChunkManager());  // NavGrid needs a chunk source (pathfinding + test-API reachability)
            npcManager_->setEntityRegistry(entityRegistry_.get());

            // ── VISIBLE WEAPONS ────────────────────────────────────────────
            // An NPC's game.json "weapon" only ever set a damage/animation
            // profile string: soldiers fought a whole battle bare-handed while
            // the axes and swords existed as assets nobody attached. The engine
            // has the grip metadata (ItemDefinition::held: bone, offset, euler,
            // scale) and the character can resolve the bone, but the wire
            // between them lived ONLY in the editor — another standalone parity
            // gap. This is that wire, ported.
            // Its own template manager: a generated game has none, and the
            // weapon models must come from somewhere. Templates load on demand
            // (one .voxel per distinct weapon) rather than scanning the whole
            // library at boot.
            weaponTemplates_ = std::make_unique<Phyxel::ObjectTemplateManager>(
                engine.getChunkManager(), nullptr);
            // The full template library + a placed-object registry: without them the
            // GameDefinitionLoader silently skipped every game.json template, item prop
            // and transition marker in a SHIPPED build (Ravenmere G-65 / G-87 root - the
            // editor had them, the standalone never did).
            templates_ = std::make_unique<Phyxel::ObjectTemplateManager>(engine.getChunkManager(), nullptr);
            templates_->loadTemplates("resources/templates");
            placedObjects_ = std::make_unique<Phyxel::Core::PlacedObjectManager>(
                engine.getChunkManager(), templates_.get(), nullptr);
            kinematicVoxelManager_ = std::make_unique<Phyxel::Core::KinematicVoxelManager>();
            kinematicVoxelManager_->setPhysicsWorld(engine.getPhysicsWorld());
            // NOTE: the render hookup CANNOT happen here — renderCoordinator_ is
            // not constructed until later in this function. Doing it here left
            // `if (renderCoordinator_)` false, the call never ran, and weapons
            // attached correctly to hands and were simply never drawn. See the
            // hookup after the RenderCoordinator is created.
            // items.json was loaded only by the editor, so a shipped game had an
            // EMPTY item registry and every weapon lookup missed silently.
            const int loadedItems =
                Phyxel::Core::ItemRegistry::instance().loadFromFile("resources/items.json");
            if (loadedItems <= 0)
                LOG_WARN("{class_name}", "items.json loaded {{}} items - NPC weapons will be invisible",
                         loadedItems);
            else
                LOG_INFO("{class_name}", "item registry: {{}} items", loadedItems);
            registerBehaviorActions();
            installDoctrine();
            // Real-time casters (RangedCasterBehavior) route their spells
            // through the SAME cast visual + damage funnel as everyone else.
            // Without this they fall back to raw takeDamage: no VFX, no death
            // events, no logs — a 20v20 where combatants died of nothing.
            npcManager_->setCasterCastHook(
                [this](const std::string& casterId, const std::string& spellId,
                       const std::string& targetId, const glm::vec3& targetPos, float damage) {{
                    auto* target = entityRegistry_ ? entityRegistry_->getEntity(targetId) : nullptr;
                    playCastVisualFor(characterOf(casterId), spellId, targetPos,
                        [this, target, targetId, casterId, damage]() {{
                            if (!target) return;
                            if (combatSystem_)
                                combatSystem_->applyDamage(target, targetId, damage, casterId,
                                                           Phyxel::Core::DamageType::Fire);
                            else if (auto* hc = target->getHealthComponent())
                                hc->takeDamage(damage);
                        }});
                }});

            dialogueSystem_ = std::make_unique<Phyxel::UI::DialogueSystem>();
            speechBubbleManager_ = std::make_unique<Phyxel::UI::SpeechBubbleManager>();
            speechBubbleManager_->setEntityRegistry(entityRegistry_.get());
            npcManager_->setSpeechBubbleManager(speechBubbleManager_.get());

            storyEngine_ = std::make_unique<Phyxel::Story::StoryEngine>();

            // Declarative dialogue hooks: node-reach events feed triggers, node
            // actions share the trigger "then" vocabulary, choice conditions
            // read story variables. (See docs/GameCreationGuide.md.)
            dialogueSystem_->setEventSink([this](const std::string& type, const nlohmann::json& data) {{
                triggers_.onEvent(type, data);
            }});
            dialogueSystem_->setActionExecutor([this](const nlohmann::json& a) {{
                triggers_.executeHostAction(a, "dialogue");
            }});
            dialogueSystem_->setVariableResolver(
                [this](const std::string& name) -> std::optional<nlohmann::json> {{
                    if (!storyEngine_) return std::nullopt;
                    const auto* var = storyEngine_->getWorldState().getVariable(name);
                    if (!var) return std::nullopt;
                    return std::visit([](const auto& v) {{ return nlohmann::json(v); }}, var->value);
                }});
            // Skill-check choices ("skillCheck": {{"skill":"Persuasion","dc":15}}) roll
            // against the PLAYER's sheet: skill bonus (proficiency-aware) or raw ability
            // modifier. Reputation gates have no faction ledger in the shell yet -> 0.
            // (Ravenmere gap G-03: DialogueSkillCheck existed but nothing wired it.)
            dialogueSystem_->setSkillBonusResolver([this](const Phyxel::Core::DialogueSkillCheck& c) -> int {{
                switch (c.type) {{
                    case Phyxel::Core::DialogueCheckType::SkillCheck:   return playerSheet_.skillBonus(c.skill);
                    case Phyxel::Core::DialogueCheckType::AbilityCheck: return playerSheet_.attributes.modifier(c.ability);
                    case Phyxel::Core::DialogueCheckType::ReputationGate: return 0;
                }}
                return 0;
            }});

            // Interaction manager — detects player proximity to NPCs, handles E-key
            interactionManager_ = std::make_unique<Phyxel::Core::InteractionManager>();
            interactionManager_->setEntityRegistry(entityRegistry_.get());

            // Load user settings (AI config, display prefs, etc.)
            Phyxel::Core::GameSettings::loadFromFile("settings.json", settings_);

            // Push keybindings into the InputManager action map (single source of
            // truth for action->key, so rebinds take effect at runtime). The
            // InputManager already seeds engine defaults, so an empty list just
            // keeps those; a non-empty list (user rebinds) overrides per action.
            if (settings_.keybindings.empty())
                settings_.keybindings = Phyxel::Core::GameSettings::defaultKeybindings();
            if (auto* in = engine.getInputManager()) {{
                for (const auto& kb : settings_.keybindings)
                    in->bindAction(kb.action, kb.key, kb.modifiers);
                in->setInvertY(settings_.invertY);
            }}

            // Apply persisted volume settings to the engine's AudioSystem at boot.
            // Without this the sliders only mutate settings_ fields and the mixer
            // never hears about them (StandaloneParityGaps.md §1, AudioSystem row).
            applyAudioSettings();

            // AI conversation service — enables LLM-driven NPC dialogue
            aiConversationService_ = std::make_unique<Phyxel::AI::AIConversationService>(
                storyEngine_.get(), entityRegistry_.get(), dialogueSystem_.get());

            auto* worldStorage = engine.getChunkManager()->m_streamingManager.getWorldStorage();
            if (worldStorage) {{
                Phyxel::AI::LLMConfig llmConfig;
                llmConfig.provider = settings_.aiProvider;
                llmConfig.model    = settings_.aiModel;
                llmConfig.apiKey   = settings_.aiApiKey;
                if (aiConversationService_->initialize(worldStorage->getDb(), llmConfig)) {{
                    LOG_INFO("{class_name}", "AI Conversation Service ready (configured={{}})",
                             aiConversationService_->isConfigured() ? "yes" : "no");
                }}
            }}

            // World-space line pass (target rings, movement paths - CombatUiBg3 increment 3).
            // Only the editor used to create one; a shipped game had none, so every ring
            // queued by the shell went nowhere (probe L4 2026-09-11).
            raycastVisualizer_ = std::make_unique<Phyxel::RaycastVisualizer>();
            raycastVisualizer_->initialize(engine.getVulkanDevice());
            // Create the render coordinator
            renderCoordinator_ = std::make_unique<Phyxel::Graphics::RenderCoordinator>(
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
                raycastVisualizer_.get(), nullptr
            );
            renderCoordinator_->setNPCManager(npcManager_.get());
            // The player character lives in entities_, NOT the NPCManager. Without
            // this the RenderCoordinator's entities pointer stays null and
            // renderEntities()/renderInstancedCharacters() skip the player entirely
            // — the world renders as empty terrain with an invisible (but loaded +
            // camera-followed) body. (game-dev feedback round 5 — UIShowcase.)
            renderCoordinator_->setEntities(&entities_);
            // Held NPC weapons are kinematic voxel objects; without this the
            // renderer has no idea they exist. They still attach to grip bones
            // and track them correctly — telemetry showed weapon and owner
            // positions matching to within a unit — they are simply never drawn.
            // An earlier version of this hookup sat ~100 lines above, BEFORE the
            // RenderCoordinator was constructed, where `if (renderCoordinator_)`
            // was quietly false.
            if (kinematicVoxelManager_)
                renderCoordinator_->setKinematicVoxelManager(kinematicVoxelManager_.get());
{hud_setup}

            // JSON-driven menu renderer for sceneType:"menu" scenes. MUST be
            // constructed BEFORE loadGameDefinition(): loading a multi-scene game
            // whose startScene is a menu fires cb.onMenuSceneLoaded during the load,
            // and that callback needs the renderer or the start menu silently
            // fails (the built-in shell renders instead — the double-shell bug).
            // Foreground mode so it draws over everything, matching the editor.
            gameMenuRenderer_ = std::make_unique<Phyxel::UI::GameMenuRenderer>();
            gameMenuRenderer_->setRenderToForeground(true);
            gameMenuRenderer_->onTransitionScene = [this](const std::string& sceneId) {{
                auto* sm = engine_ ? engine_->getSceneManager() : nullptr;
                if (sm && sm->hasManifest()) sm->transitionTo(sceneId);
            }};
            gameMenuRenderer_->onQuit = [this]() {{
                auto* w = engine_ ? engine_->getWindowManager() : nullptr;
                if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
            }};
            // {{{{token}}}} interpolation in menu labels: {{{{playtime}}}} shows the
            // gameplay clock (speedrun time on credits), {{{{story.<var>}}}} reads a
            // StoryEngine world variable set by triggers/dialogue.
            gameMenuRenderer_->onResolveVariable =
                [this](const std::string& token) -> std::optional<std::string> {{
                if (token == "playtime") {{
                    const int mins = static_cast<int>(elapsed_ / 60.0f);
                    const float secs = elapsed_ - static_cast<float>(mins) * 60.0f;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d:%04.1f", mins, secs);
                    return std::string(buf);
                }}
                if (token.rfind("story.", 0) == 0 && storyEngine_) {{
                    const auto* var = storyEngine_->getWorldState().getVariable(token.substr(6));
                    if (!var) return std::nullopt;
                    return std::visit([](const auto& v) -> std::string {{
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::string>) return v;
                        else if constexpr (std::is_same_v<T, bool>)   return v ? "true" : "false";
                        else if constexpr (std::is_same_v<T, float>) {{
                            char b[32]; snprintf(b, sizeof(b), "%.2f", v); return std::string(b);
                        }}
                        else return std::to_string(v);
                    }}, var->value);
                }}
                return std::nullopt;
            }};

            // Load game definition (NPCs, story, camera — world is pre-baked in SQLite)
            if (!loadGameDefinition(engine)) {{
                LOG_WARN("{class_name}", "No game.json found — starting with empty world");
            }}

            // Wire NPC interaction callback — priority: AI conversation > tree dialogue
            interactionManager_->setInteractCallback([this](Phyxel::Scene::NPCEntity* npc) {{
                if (!dialogueSystem_ || !npc) return;
                // Speakers face each other for the conversation (the camera-coupled
                // player facing is suppressed while dialogue is active, so the snap
                // holds — see the updateGameplayCamera call).
                if (playerCharacter_) {{
                    faceToward(playerCharacter_, npc->getPosition());
                    if (auto* npcCh = npc->getAnimatedCharacter())
                        faceToward(npcCh, playerCharacter_->getPosition());
                }}
                auto* provider = npc->getDialogueProvider();

                // AI conversation via direct LLM
                if (provider && provider->isAIMode() &&
                    aiConversationService_ && aiConversationService_->isConfigured()) {{
                    std::string npcId = entityRegistry_ ? entityRegistry_->getEntityId(npc) : npc->getName();
                    if (aiConversationService_->startConversation(npc, npcId, npc->getName())) {{
                        LOG_INFO("{class_name}", "Started AI conversation with '{{}}'", npc->getName());
                        if (engine_) updateCursorMode(*engine_);
                        return;
                    }}
                }}

                // Fallback: tree-based dialogue
                const Phyxel::UI::DialogueTree* tree = provider ? provider->getDialogueTree() : nullptr;
                if (tree) {{
                    dialogueSystem_->startConversation(npc->getName(), tree);
                    if (engine_) updateCursorMode(*engine_);
                }} else {{
                    LOG_INFO("{class_name}", "NPC '{{}}' has no dialogue", npc->getName());
                }}
            }});

            // If no player was spawned, create a default one — but ONLY for
            // single-scene games. In a multi-scene game whose startScene is a menu,
            // no player exists yet BY DESIGN: each world scene spawns its own
            // definition player later. A fallback here would squat the "player"
            // entity ID ("Entity ID already taken") and leave a stray duplicate
            // character standing in the world.
            bool multiScene = engine.getSceneManager() && engine.getSceneManager()->hasManifest();
            if (!playerCharacter_ && !multiScene) {{
                auto* entity = spawnEntity("animated", glm::vec3(16.0f, 25.0f, 16.0f), "");
                if (entity) {{
                    playerCharacter_ = dynamic_cast<Phyxel::Scene::AnimatedVoxelCharacter*>(entity);
                    if (entityRegistry_ && playerCharacter_) {{
                        entityRegistry_->registerEntity(playerCharacter_, "player", "animated");
                    }}
                }}
            }}

            // Default to a third-person camera following the player — but only when
            // the game definition didn't author a camera "mode" itself (a maze
            // crawler authored as first_person must START first-person).
            if (playerCharacter_) {{
                auto* cam = engine.getCamera();
                if (!authoredCameraMode_) {{
                    cam->setMode(Phyxel::Graphics::CameraMode::ThirdPerson);
                }}
                cam->setDistanceFromTarget(4.0f);
            }}

            // Completing an objective (from any caller: trigger action, gameplay
            // code) feeds the "objective_complete" event back into the trigger
            // system, so quest chains compose declaratively:
            //   {{when: {{event: "objective_complete", id: "main_quest"}}, then: [...]}}
            // Mirrors the editor host's ObjectiveTracker wiring.
            objectiveTracker_.onCompleted = [this](const std::string& id) {{
                triggers_.onEvent("objective_complete", {{{{"id", id}}}});
                if (objectiveXp_ > 0) grantXP(objectiveXp_, id.c_str());
            }};

            // Turn-based combat stack — the same director/AI/player-turn wiring the
            // editor host has (docs/TurnBasedCombat.md; StandaloneParityGaps.md §1).
            // All of it no-ops until an encounter begins (start_combat trigger action
            // or POST /api/rpg/combat/start on the test API).
            combatSystem_ = std::make_unique<Phyxel::Core::CombatSystem>();
            // Combat audio: impacts/grunts/death screams at the target from
            // applyDamage, swings/battle cries from CombatBehavior — shipped
            // games sound like the editor (StandaloneParityGaps discipline).
            combatSystem_->setSoundRegistry(engine.getSoundRegistry());
            combatSystem_->setInvulnerabilityQuery([](const Phyxel::Scene::Entity* e) -> bool {{
                if (const auto* a = dynamic_cast<const Phyxel::Scene::AnimatedVoxelCharacter*>(e))
                    return a->isDodgeInvulnerable();
                return false;
            }});
            // Damage reactions + DECLARATIVE death hooks. Every kill emits
            // "entity_died" {{id}} into the trigger system (gate quests on a
            // specific kill), and the encounter resolves itself: the dead
            // combatant leaves the initiative; when no enemy-side combatant
            // remains, combat ends and "combat_victory" fires (the player's
            // death fires "player_died" for game-over wiring instead).
            combatSystem_->setOnDamage([this](const Phyxel::Core::DamageEvent& ev) {{
                if (!entityRegistry_) return;
                Phyxel::Scene::Entity* tgt = entityRegistry_->getEntity(ev.targetId);
                if (tgt) {{
                    Phyxel::Scene::AnimatedVoxelCharacter* hitChar = nullptr;
                    if (auto* npcE = dynamic_cast<Phyxel::Scene::NPCEntity*>(tgt)) hitChar = npcE->getAnimatedCharacter();
                    else hitChar = dynamic_cast<Phyxel::Scene::AnimatedVoxelCharacter*>(tgt);
                    if (hitChar) {{
                        if (ev.killed) hitChar->die();
                        else           hitChar->hitReact(ev.actualDamage >= 11.0f);
                    }}
                }}
                if (!ev.killed) {{
                    // A surviving defender snaps to face its attacker (hit react
                    // plays toward the threat, and the counter-attack lines up).
                    if (!ev.attackerId.empty())
                        if (auto* atk = characterOf(ev.attackerId))
                            faceToward(characterOf(ev.targetId), atk->getPosition());
                    return;
                }}
                // An officer's death costs his squad its orders — the engine
                // keeps the last one but stops issuing new ones, so a
                // decapitated squad fights on without coordination.
                command_.notifyDeath(ev.targetId);
                triggers_.onEvent("entity_died", {{{{"id", ev.targetId}}}});
                if (ev.targetId == "player") {{
                    // The player is DEAD. Until 2026-09-08 this only emitted an event
                    // nobody listened to: the encounter ran on with a corpse taking
                    // turns and the screen stayed "playing" (Ravenmere gap G-38).
                    // Now: the encounter ends, and unless game.json opts out with
                    // "lose": {{"auto_game_over": false}} (to script its own
                    // player_died trigger) the game-over screen comes up.
                    if (combatDirector_.inCombat()) combatDirector_.endEncounter();
                    triggers_.onEvent("player_died", {{{{"id", ev.targetId}}}});
                    if (loseAutoGameOver_) {{
                        LOG_INFO("{class_name}", "Player died — game over");
                        screen_.showGameOver();
                        if (engine_) updateCursorMode(*engine_);
                    }}
                    return;
                }}
                // A fallen COMPANION stays fallen: mark the party ledger so the next scene
                // load does not quietly resurrect them (Ravenmere run 6: Bram died to a
                // wolf on the farm and "rejoined at the player's side" in the barrow —
                // gap G-41). A long rest brings the party back (see long_rest).
                if (auto* member = rpgParty_.getMember(ev.targetId)) {{
                    rpgParty_.setAlive(ev.targetId, false);
                    LOG_INFO("{class_name}", "Companion '{{}}' has fallen — they return after a long rest", member->name);
                    triggers_.onEvent("companion_died", {{{{"id", ev.targetId}}, {{"name", member->name}}}});
                    if (combatDirector_.inCombat()) combatDirector_.removeCombatant(ev.targetId);
                    return;   // no kill XP for losing a friend
                }}
                if (killXp_ > 0) grantXP(killXp_, ev.targetId.c_str());
                if (combatDirector_.inCombat()) {{
                    combatDirector_.removeCombatant(ev.targetId);
                    bool enemyRemains = false;
                    for (const auto& p : combatDirector_.initiative().turnOrder())
                        if (!p.isPlayer) {{ enemyRemains = true; break; }}
                    if (!enemyRemains) {{
                        combatDirector_.endEncounter();
                        LOG_INFO("{class_name}", "Encounter won — last enemy fell ('{{}}')", ev.targetId);
                        victoryEmitted_ = true;
                        triggers_.onEvent("combat_victory", {{{{"last_kill", ev.targetId}}, {{"reason", "last_enemy_killed"}}}});
                    }}
                }}
            }});
            if (npcManager_) npcManager_->setCombatSystem(combatSystem_.get());
            auto bodyProvider = [this](Phyxel::Scene::Entity* e) -> Phyxel::Core::ITurnActorBody* {{
                Phyxel::Scene::AnimatedVoxelCharacter* ch = nullptr;
                if (auto* npc = dynamic_cast<Phyxel::Scene::NPCEntity*>(e)) ch = npc->getAnimatedCharacter();
                else ch = dynamic_cast<Phyxel::Scene::AnimatedVoxelCharacter*>(e);
                if (!ch) return nullptr;
                auto& slot = turnBodies_[ch];
                if (!slot) slot = std::make_unique<Phyxel::Scene::CharacterTurnBody>(ch);
                return slot.get();
            }};
            combatAI_.setCombatDirector(&combatDirector_);
            combatAI_.setParty(&rpgParty_);
            combatAI_.setEntityRegistry(entityRegistry_.get());
            combatAI_.setBodyProvider(bodyProvider);
            combatAI_.setCombatSystem(combatSystem_.get());
            // NPC casters (enemies AND companions): game.json "casters" maps an
            // entity id to {{class, level, spells}}; the AI casts from range and
            // spends real slots. Non-casters return null and behave as before.
            combatAI_.setCasterProvider([this](const std::string& id) -> Phyxel::Core::SpellcasterComponent* {{
                auto it = npcCasters_.find(id);
                return it == npcCasters_.end() ? nullptr : &it->second;
            }});
            combatAI_.setCastExecutor([this](const std::string& casterId, const std::string& spellId,
                                             const glm::vec3& targetPos, std::function<void()> onRelease) {{
                playCastVisualFor(characterOf(casterId), spellId, targetPos, std::move(onRelease));
            }});
            // Tactical profiles (game.json "combat_ai"): how each NPC FIGHTS —
            // target priority, kiting range, morale, healer thresholds.
            combatAI_.setTacticsProvider([this](const std::string& id) -> const Phyxel::Core::CombatTactics* {{
                auto it = npcTactics_.find(id);
                return it == npcTactics_.end() ? nullptr : &it->second;
            }});
            // The player's ARMOR CLASS is their sheet's — not the HP-based pseudo-AC that
            // made the wounded easier to hit (G-44). Companions/NPCs: 0 = let the AI use the
            // stat block or the fallback.
            combatAI_.setACProvider([this](const std::string& id) -> int {{
                return id == "player" ? playerSheet_.armorClass : 0;
            }});
            playerTurn_.setCombatDirector(&combatDirector_);
            playerTurn_.setEntityRegistry(entityRegistry_.get());
            playerTurn_.setBodyProvider(bodyProvider);
            playerTurn_.setCombatSystem(combatSystem_.get());
            // Click-to-move (G-75): the player walks the SAME NavGraph route the NPCs
            // walk, through the same body adapter the TurnActor drives in combat.
            clickToMove_.setBodyProvider([this, bodyProvider]() -> Phyxel::Core::ITurnActorBody* {{
                return playerCharacter_ ? bodyProvider(playerCharacter_) : nullptr;
            }});
            clickToMove_.setGraphProvider([this]() -> const Phyxel::Core::NavGraph* {{
                return npcManager_ ? npcManager_->getNavGraph() : nullptr;
            }});
            // Combat moves walk the SAME route (G-136: a straight line into a fence or a
            // tree stalled short of the click), and the ground pick lands on the voxel
            // surface under the cursor rather than a plane at the player's feet.
            playerTurn_.setPathProvider([this](const glm::vec3& from, const glm::vec3& to) {{
                return clickToMove_.planPath(from, to);
            }});
            playerTurn_.setSolidProvider([this](const glm::ivec3& c) {{
                auto* cm = engine_ ? engine_->getChunkManager() : nullptr;
                return cm && cm->hasVoxelAt(c);
            }});
            // G-139: a combat move must not walk the player out of the fight. The scene-exit
            // regions are off limits for the whole turn -- a region trigger fires on ENTRY, so
            // the controller checks every waypoint, not just where the move ends. Out of combat
            // this filter is never consulted, so walking out normally is unaffected.
            // G-147: zoomed fully in the camera sits at the character's eyes, and a wall can
            // push the boom into the torso at any zoom. Either way the body would clip through the
            // near plane, so the renderer drops it from the main pass while the camera is inside
            // it. The shadow stays.
            if (renderCoordinator_)
                renderCoordinator_->setCameraOwnerProvider(
                    [this]() -> Phyxel::Scene::RagdollCharacter* {{ return playerCharacter_; }});

            playerTurn_.setDestinationFilter([this](const glm::vec3& p) {{
                for (const auto& ex : triggers_.exitRegions()) {{
                    const auto& r = ex.region;
                    if (!r.contains("from") || !r.contains("to")) continue;
                    const float x0 = std::min(r["from"].value("x", 0.0f), r["to"].value("x", 0.0f));
                    const float x1 = std::max(r["from"].value("x", 0.0f), r["to"].value("x", 0.0f));
                    const float y0 = std::min(r["from"].value("y", 0.0f), r["to"].value("y", 0.0f));
                    const float y1 = std::max(r["from"].value("y", 0.0f), r["to"].value("y", 0.0f));
                    const float z0 = std::min(r["from"].value("z", 0.0f), r["to"].value("z", 0.0f));
                    const float z1 = std::max(r["from"].value("z", 0.0f), r["to"].value("z", 0.0f));
                    // A shade of margin, because the mover stops within an arrival radius of the
                    // waypoint rather than exactly on it.
                    const float m = 0.5f;
                    if (p.x >= x0 - m && p.x <= x1 + m &&
                        p.y >= y0 - 2.0f && p.y <= y1 + 2.0f &&
                        p.z >= z0 - m && p.z <= z1 + m) return false;
                }}
                return true;
            }});
            // G-139: no scene change while an encounter is running. This is the engine's own
            // chokepoint, so it holds for region triggers, menu buttons and the test API alike --
            // not just for the combat move that surfaced it. The fight has to end first; the
            // region will fire again on the next entry.
            if (auto* smg = engine_ ? engine_->getSceneManager() : nullptr) {{
                smg->setTransitionGuard([this](const std::string& scene, std::string& reason) {{
                    (void)scene;
                    if (combatDirector_.inCombat()) {{
                        reason = "an encounter is running";
                        return false;
                    }}
                    return true;
                }});
            }}
            // Spellcasting in the SHIPPED game: the registry only auto-loaded in
            // the editor before, so every cast failed "Unknown spell" here.
            if (Phyxel::Core::SpellRegistry::instance().count() == 0)
                Phyxel::Core::SpellRegistry::instance().loadFromDirectory("resources/spells");
            // Cast visual: animation via SpellAnimMapper + VFX via the
            // VfxDirector, damage applied at the release frame — the same
            // playCastVisual flow the editor host runs (Application.cpp ~5558).
            playerTurn_.setCastExecutor(
                [this](const std::string& spellId, const std::string&,
                       const glm::vec3& targetPos, std::function<void()> onRelease) {{
                    playCastVisual(spellId, targetPos, std::move(onRelease));
                }});

            // Declarative trigger actions (game.json "triggers"): wire to the shell.
            // Conditions like {{when: {{event: "player_jumped"}}}} can drive
            // show_victory / show_credits / transition_scene / quit_game with no code.
            // Vocabulary is EDITOR-PARITY (StandaloneParityGaps.md §3): a game.json
            // authored + tested in the editor must not change behavior when packaged.
            triggers_.setActionExecutor([this](const nlohmann::json& a, const std::string& tid) {{
                const std::string type = a.value("type", "");
                if (type == "transition_scene") {{
                    auto* sm = engine_ ? engine_->getSceneManager() : nullptr;
                    const std::string target = a.value("target", a.value("scene_id", ""));
                    if (sm && sm->hasManifest() && !target.empty()) sm->transitionTo(target);
                }} else if (type == "quit_game") {{
                    auto* w = engine_ ? engine_->getWindowManager() : nullptr;
                    if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
                }} else if (type == "show_victory") {{
                    screen_.showVictory();
                    if (engine_) updateCursorMode(*engine_);
                }} else if (type == "show_credits") {{
                    screen_.showCredits();
                    if (engine_) updateCursorMode(*engine_);
                }} else if (type == "show_game_over") {{
                    // Authorable lose condition (a timer ran out, an ally you had to
                    // protect died, …) — the fail twin of show_victory (G-38).
                    if (combatDirector_.inCombat()) combatDirector_.endEncounter();
                    screen_.showGameOver();
                    if (engine_) updateCursorMode(*engine_);
                }} else if (type == "set_story_variable") {{
                    // {{"type":"set_story_variable","name":"flag","value":true}}
                    const std::string name = a.value("name", "");
                    if (storyEngine_ && !name.empty() && a.contains("value")) {{
                        const auto& val = a["value"];
                        auto& ws = storyEngine_->getWorldState();
                        if      (val.is_boolean())        ws.setVariable(name, val.get<bool>());
                        else if (val.is_number_integer()) ws.setVariable(name, val.get<int>());
                        else if (val.is_number_float())   ws.setVariable(name, val.get<float>());
                        else if (val.is_string())         ws.setVariable(name, val.get<std::string>());
                    }}
                }} else if (type == "complete_objective") {{
                    // Editor-parity: {{"type":"complete_objective","id":"main_quest"}}
                    const std::string id = a.value("id", "");
                    if (!objectiveTracker_.completeObjective(id))
                        LOG_WARN("{class_name}", "complete_objective: unknown objective '{{}}' (trigger '{{}}')", id, tid);
                }} else if (type == "fail_objective") {{
                    const std::string id = a.value("id", "");
                    if (!objectiveTracker_.failObjective(id))
                        LOG_WARN("{class_name}", "fail_objective: unknown objective '{{}}' (trigger '{{}}')", id, tid);
                }} else if (type == "reveal_objective" || type == "hide_objective") {{
                    // Quest chains: author later steps "hidden": true and surface them
                    // when the previous step completes (Ravenmere gap G-06).
                    //   {{"type":"reveal_objective","id":"main_2"}}
                    const std::string id = a.value("id", "");
                    if (!objectiveTracker_.setHidden(id, type == "hide_objective"))
                        LOG_WARN("{class_name}", "{{}}: unknown objective '{{}}' (trigger '{{}}')", type, id, tid);
                    else
                        LOG_INFO("{class_name}", "Objective '{{}}' {{}} (trigger '{{}}')", id,
                                 type == "hide_objective" ? "hidden" : "revealed", tid);
                }} else if (type == "add_objective") {{
                    //   {{"type":"add_objective","id":"x","title":"...","description":"...","category":"side","priority":0,"hidden":false}}
                    const std::string id = a.value("id", "");
                    if (id.empty() || !objectiveTracker_.addObjective(id, a.value("title", ""), a.value("description", ""),
                                                                      a.value("category", "main"), a.value("priority", 0),
                                                                      a.value("hidden", false)))
                        LOG_WARN("{class_name}", "add_objective: rejected '{{}}' (trigger '{{}}')", id, tid);
                }} else if (type == "save_game") {{
                    // Authorable save point: {{"type":"save_game"}} persists the
                    // player profile to the active scene's world DB.
                    savePlayerProfile();
                }} else if (type == "long_rest") {{
                    // Authorable rest: {{"type":"long_rest"}} restores all spell
                    // slots (an inn bed, a campfire, a chapter break).
                    playerCaster_.onLongRest();
                    refreshSpellbar();
                    LOG_INFO("{class_name}", "Long rest: {{}} slot(s) restored",
                             playerCaster_.slots().totalRemaining());
                    // House rule for a short game: a long rest also brings fallen
                    // companions back (they respawn at the player's side on the next
                    // scene load). Heals the player to full as well.
                    for (const auto& m : rpgParty_.getMembers()) {{
                        if (!m.isAlive) {{
                            rpgParty_.setAlive(m.entityId, true);
                            LOG_INFO("{class_name}", "Long rest: '{{}}' recovers and will rejoin at the next area", m.name);
                        }}
                    }}
                    if (playerCharacter_)
                        if (auto* hc = playerCharacter_->getHealthComponent()) hc->setHealth(hc->getMaxHealth());
                    triggers_.onEvent("long_rest", nlohmann::json::object());
                }} else if (type == "give_item") {{
                    // Authorable loot: {{"type":"give_item","id":"moonpetal_remedy","count":1}}
                    const std::string id = a.value("id", "");
                    const int count = a.value("count", 1);
                    if (!id.empty() && (id == "gold" || id == "gp" || id == "gold_crown" || id == "gold_crowns" || id == "coins")) {{
                        // Money is money (CurrencySystem), not a 64-stack in a hotbar slot.
                        wallet_.addCoins(0, 0, 0, count, 0);
                        LOG_INFO("{class_name}", "Gold received: {{}} gp (trigger '{{}}') -> {{}}", count, tid, wallet_.toString());
                        triggers_.onEvent("item_received", {{{{"id", id}}, {{"count", count}}}});
                    }} else if (!id.empty()) {{
                        const int leftover = inventory_.addItem(id, count);
                        LOG_INFO("{class_name}", "Item received: {{}} x{{}} (trigger '{{}}')", id, count - leftover, tid);
                        triggers_.onEvent("item_received", {{{{"id", id}}, {{"count", count - leftover}}}});
                    }}
                }} else if (type == "remove_item") {{
                    const std::string id = a.value("id", "");
                    if (!id.empty()) inventory_.removeItem(id, a.value("count", 1));
                }} else if (type == "join_party") {{
                    // Recruit a companion (usually from a dialogue node action):
                    //   {{"type":"join_party","entity_id":"npc_Bram","name":"Bram"}}
                    // The member persists in rpgParty_; on every world-scene load
                    // the shell respawns absent members near the player, and
                    // start_combat auto-enlists them on the player's side.
                    const std::string eid  = a.value("entity_id", "");
                    const std::string name = a.value("name", "");
                    if (!eid.empty() && !name.empty()) {{
                        rpgParty_.addMember(eid, name, playerSheet_.totalLevel() > 0 ? playerSheet_.totalLevel() : 1);
                        LOG_INFO("{class_name}", "'{{}}' joined the party ({{}})", name, eid);
                        triggers_.onEvent("party_joined", {{{{"id", eid}}, {{"name", name}}}});
                    }}
                }} else if (type == "start_combat") {{
                    // Authored encounter: {{"type":"start_combat","participants":
                    //   [{{"entity_id":"player","player_side":true}},
                    //    {{"entity_id":"npc_Rat","initiative_bonus":2}}]}}
                    std::vector<Phyxel::Core::CombatDirector::Combatant> combatants;
                    if (a.contains("participants") && a["participants"].is_array()) {{
                        for (const auto& p : a["participants"]) {{
                            const std::string eid = p.value("entity_id", "");
                            if (eid.empty()) continue;
                            Phyxel::Core::CombatDirector::Combatant c;
                            c.entityId        = eid;
                            c.isPlayerSide    = p.value("player_side", false);
                            c.initiativeBonus = p.value("initiative_bonus", 0);
                            c.speed           = p.value("speed", 30);
                            combatants.push_back(c);
                        }}
                    }}
                    // Party members auto-enlist on the player's side (skip any the
                    // author already listed explicitly).
                    for (const auto& m : rpgParty_.getMembers()) {{
                        if (!m.isAlive) continue;
                        bool listed = false;
                        for (const auto& c : combatants)
                            if (c.entityId == m.entityId) {{ listed = true; break; }}
                        if (listed || !entityRegistry_ || !entityRegistry_->getEntity(m.entityId)) continue;
                        Phyxel::Core::CombatDirector::Combatant c;
                        c.entityId        = m.entityId;
                        c.isPlayerSide    = true;
                        c.initiativeBonus = 1;
                        combatants.push_back(c);
                    }}
                    // A region that re-fires over a battlefield lists CORPSES (probe L4
                    // 2026-09-17: the antechamber restarted with three dead skeletons):
                    // drop the dead and the missing; no living enemy = no fight.
                    int enemiesAlive = 0;
                    combatants.erase(std::remove_if(combatants.begin(), combatants.end(),
                        [&](const Phyxel::Core::CombatDirector::Combatant& c) {{
                            auto* e = entityRegistry_ ? entityRegistry_->getEntity(c.entityId) : nullptr;
                            const auto* hc = e ? e->getHealthComponent() : nullptr;
                            const bool alive = e && (!hc || hc->isAlive());
                            if (alive && !c.isPlayerSide) ++enemiesAlive;
                            return !alive;
                        }}), combatants.end());
                    if (combatants.empty() || enemiesAlive == 0) {{
                        LOG_WARN("{class_name}", "start_combat (trigger '{{}}'): no living enemy participants - skipped", tid);
                    }} else if (combatDirector_.inCombat()) {{
                        // The region fired while a fight (a sighting, an opening spell) is
                        // already running: never restart it (G-137).
                        LOG_INFO("{class_name}", "start_combat (trigger '{{}}') skipped: already in combat", tid);
                    }} else {{
                        beginEncounterWith(std::move(combatants), "trigger '" + tid + "'");
                    }}
                }} else {{
                    LOG_WARN("{class_name}", "Unhandled trigger action '{{}}' (trigger '{{}}')", type, tid);
                }}
            }});

            // Start on the intro/splash screen (continues to the main menu), cursor free.
            // If the game's START scene is a menu scene, the SceneManager menu drives
            // instead (menuSceneActive_ is set in loadGameDefinition's callbacks) and
            // this shell stays hidden underneath — no double menus. If the start scene
            // is a WORLD scene (direct boot), onSceneReady already set Playing during
            // the load above — don't stomp it back to Intro.
            if (!Phyxel::UI::isGameRunning(screen_.getState())) {{
                screen_.setState(Phyxel::UI::ScreenState::Intro);
            }}
            updateCursorMode(engine);

            LOG_INFO("{class_name}", "Game initialized");
            return true;
        }}

        bool {class_name}::loadGameDefinition(Phyxel::Core::EngineRuntime& engine) {{
            if (!std::filesystem::exists("game.json")) return false;

            std::ifstream f("game.json");
            if (!f.is_open()) return false;

            try {{
                nlohmann::json gameDef = nlohmann::json::parse(f);

                // Did the author pick a camera mode anywhere? If so, never stomp it
                // with the default ThirdPerson below — GameDefinitionLoader applies
                // the authored mode (first_person/third_person/free) at each load.
                auto cameraHasMode = [](const nlohmann::json& def) {{
                    return def.contains("camera") && def["camera"].contains("mode");
                }};
                authoredCameraMode_ = cameraHasMode(gameDef);
                if (gameDef.contains("scenes")) {{
                    for (const auto& sc : gameDef["scenes"]) {{
                        if (cameraHasMode(sc.value("definition", nlohmann::json::object())))
                            authoredCameraMode_ = true;
                    }}
                }}

                // Top-level "objectives" array -> the quest log. Authorable in
                // game.json (the editor only gets objectives via MCP at runtime;
                // a shipped game needs them in the definition):
                //   "objectives": [{{"id":"main_quest","title":"...","description":"...",
                //                   "category":"main","priority":0,"hidden":false}}]
                if (gameDef.contains("objectives") && gameDef["objectives"].is_array()) {{
                    initialObjectives_ = gameDef["objectives"];
                    for (const auto& o : gameDef["objectives"]) {{
                        objectiveTracker_.addObjective(
                            o.value("id", ""), o.value("title", ""),
                            o.value("description", ""), o.value("category", "main"),
                            o.value("priority", 0), o.value("hidden", false));
                    }}
                    LOG_INFO("{class_name}", "Loaded {{}} objective(s) from game.json",
                             gameDef["objectives"].size());
                }}

                // "lose": {{"auto_game_over": true}} — player death shows the game-over
                // screen (default). Set false to script the lose path yourself with a
                // player_died trigger (respawn, cutscene, …). (G-38)
                if (gameDef.contains("lose") && gameDef["lose"].is_object())
                    loseAutoGameOver_ = gameDef["lose"].value("auto_game_over", true);

                // "controls": {{"clickToMove": bool}} — WoW leaves click-to-move OFF by default
                // (left click selects, right click interacts); BG3-style games turn it on.
                if (gameDef.contains("controls") && gameDef["controls"].is_object()) {{
                    clickToMoveEnabled_ = gameDef["controls"].value("clickToMove", false);
                    LOG_INFO("{class_name}", "Controls: clickToMove={{}}", clickToMoveEnabled_ ? "on" : "off");
                }}
                // "combat": {{"mode": "turn_based"|"real_time"}} — the per-game
                // ruleset (mirrors the editor's combat.mode application).
                if (gameDef.contains("combat") && gameDef["combat"].is_object()) {{
                    const std::string mode = gameDef["combat"].value("mode", "real_time");
                    combatDirector_.setMode(Phyxel::Core::combatModeFromString(mode));
                    // "camera": the rig used while an encounter runs (BG3-style
                    // tactical view). Any registered rig name: overhead (straight-
                    // down birds-eye), isometric (angled ortho), third_person...
                    combatCameraRig_ = gameDef["combat"].value("camera", "overhead");
                    // A turn-based ruleset is POINTER-driven (BG3): the cursor stays free
                    // during play, LMB clicks the world (move / interact / target) and
                    // never swings; the camera orbits on RMB-drag as before.
                    pointerMode_ = (mode == "turn_based");
                    LOG_INFO("{class_name}", "Combat mode: {{}} (camera: {{}})", mode, combatCameraRig_);
                    // AI decision log -> its own JSONL file, separate from the
                    // engine log. Off with {{"combat": {{"decision_log": false}}}}.
                    if (gameDef["combat"].value("decision_log", true)) {{
                        Phyxel::Core::CombatLog::instance().setFile("combat_log.jsonl");
                        LOG_INFO("{class_name}", "AI decision log: combat_log.jsonl "
                                 "(also GET-able via POST /api/rpg/combat/log)");
                    }} else {{
                        Phyxel::Core::CombatLog::instance().setEnabled(false);
                    }}
                }}

                // "progression": {{"class","race","kill_xp","objective_xp"}} — the
                // player levels a real CharacterSheet through
                // CharacterProgression::awardXP. Class data comes from the SAME
                // resources/classes/*.json the editor uses (packaged with the game).
                if (gameDef.contains("progression") && gameDef["progression"].is_object()) {{
                    const auto& prog = gameDef["progression"];
                    namespace fs = std::filesystem;
                    if (fs::exists("resources/classes"))
                        for (const auto& f : fs::directory_iterator("resources/classes"))
                            if (f.path().extension() == ".json")
                                Phyxel::Core::ClassRegistry::instance().loadFromFile(f.path().string());
                    playerSheet_.name   = "player";
                    playerSheet_.raceId = prog.value("race", "human");
                    Phyxel::Core::ClassLevel cl;
                    cl.classId = prog.value("class", "fighter");
                    cl.level   = 1;
                    playerSheet_.classes.clear();
                    playerSheet_.classes.push_back(cl);
                    playerSheet_.experiencePoints = 0;
                    // "abilities": {{"str":14,"dex":10,"con":14,"int":8,"wis":16,"cha":12}} — a
                    // pre-made needs real scores (all-10s made every roll +0, G-30);
                    // "armor_class": 18 — armor is not modelled on the sheet yet, so the
                    // authored AC stands in for chain mail + shield (G-43/G-44).
                    if (prog.contains("abilities") && prog["abilities"].is_object()) {{
                        const auto& ab = prog["abilities"];
                        playerSheet_.attributes.setAll(ab.value("str", 10), ab.value("dex", 10), ab.value("con", 10),
                                                       ab.value("int", 10), ab.value("wis", 10), ab.value("cha", 10));
                    }}
                    playerSheet_.recalculate();
                    if (prog.contains("armor_class") && prog["armor_class"].is_number_integer())
                        playerSheet_.armorClass = prog["armor_class"].get<int>();
                    LOG_INFO("{class_name}", "Player sheet: AC {{}}, STR {{}} DEX {{}} CON {{}} INT {{}} WIS {{}} CHA {{}}",
                             playerSheet_.armorClass,
                             playerSheet_.attributes.get(Phyxel::Core::AbilityType::Strength).total(),
                             playerSheet_.attributes.get(Phyxel::Core::AbilityType::Dexterity).total(),
                             playerSheet_.attributes.get(Phyxel::Core::AbilityType::Constitution).total(),
                             playerSheet_.attributes.get(Phyxel::Core::AbilityType::Intelligence).total(),
                             playerSheet_.attributes.get(Phyxel::Core::AbilityType::Wisdom).total(),
                             playerSheet_.attributes.get(Phyxel::Core::AbilityType::Charisma).total());
                    killXp_      = prog.value("kill_xp", 0);
                    objectiveXp_ = prog.value("objective_xp", 0);
                    // "spells": authored castable list (SpellRegistry ids) — the
                    // combat spellbar builds from this. Unknown ids are dropped
                    // LOUDLY here rather than rendering a dead button. The
                    // registry load is lazy/idempotent and MUST also happen here:
                    // this parse runs before the combat wiring's load, and an
                    // empty registry silently dropped every authored spell once.
                    if (Phyxel::Core::SpellRegistry::instance().count() == 0)
                        Phyxel::Core::SpellRegistry::instance().loadFromDirectory("resources/spells");
                    playerSpells_.clear();
                    if (prog.contains("spells") && prog["spells"].is_array()) {{
                        for (const auto& s : prog["spells"]) {{
                            const std::string id = s.get<std::string>();
                            if (Phyxel::Core::SpellRegistry::instance().getSpell(id))
                                playerSpells_.push_back(id);
                            else
                                LOG_WARN("{class_name}", "progression.spells: unknown spell '{{}}' dropped", id);
                        }}
                    }}
                    // Real spellcasting state: slot table from the class's
                    // casting type + level (PHB tables in SpellSlotTable), save
                    // DC / attack bonus from its casting ability. Authored
                    // spells are LEARNED — cantrips free, leveled ones prepared
                    // — so castSpell can enforce prepared-ness and spend slots.
                    if (!playerSpells_.empty()) {{
                        const auto* cdef = Phyxel::Core::ClassRegistry::instance().getClass(cl.classId);
                        const std::string abilStr = cdef && !cdef->spellcastingAbility.empty()
                                                        ? cdef->spellcastingAbility : "WIS";
                        const std::string castType = cdef && !cdef->spellcastingType.empty()
                                                        ? cdef->spellcastingType : "full";
                        playerCaster_.initialize(cl.classId,
                                                 Phyxel::Core::abilityFromString(abilStr.c_str()),
                                                 cl.level, castType);
                        for (const auto& sid : playerSpells_) {{
                            const auto* sd = Phyxel::Core::SpellRegistry::instance().getSpell(sid);
                            if (!sd) continue;
                            if (sd->isCantrip()) playerCaster_.learnCantrip(sid);
                            else                 playerCaster_.learnSpell(sid, /*prepared=*/true);
                        }}
                        playerTurn_.setSpellcaster(&playerCaster_, &playerSheet_);
                        LOG_INFO("{class_name}", "Spellcaster: {{}} ({{}}, {{}}) DC {{}} atk +{{}}, {{}} slot(s)",
                                 cl.classId, abilStr, castType,
                                 playerTurn_.effectiveSaveDC(), playerTurn_.effectiveSpellAttackBonus(),
                                 playerCaster_.slots().totalRemaining());
                    }}
                    LOG_INFO("{class_name}", "Progression: {{}} {{}} (kill_xp={{}}, objective_xp={{}}, spells={{}})",
                             playerSheet_.raceId, cl.classId, killXp_, objectiveXp_, playerSpells_.size());
                }}

                // "casters": NPC spellcasters (enemies AND companions) the
                // CombatAI casts with —
                //   {{"npc_Rat": {{"class":"wizard","level":1,
                //                 "spells":["fire_bolt","magic_missile"]}}}}
                // Same slot machinery as the player: leveled spells are prepared
                // and really spend slots, cantrips are free.
                if (gameDef.contains("casters") && gameDef["casters"].is_object()) {{
                    if (Phyxel::Core::SpellRegistry::instance().count() == 0)
                        Phyxel::Core::SpellRegistry::instance().loadFromDirectory("resources/spells");
                    for (auto it = gameDef["casters"].begin(); it != gameDef["casters"].end(); ++it) {{
                        const auto& cj = it.value();
                        const std::string classId = cj.value("class", "wizard");
                        const int lvl = std::max(1, cj.value("level", 1));
                        const auto* cdef = Phyxel::Core::ClassRegistry::instance().getClass(classId);
                        const std::string abilStr = cdef && !cdef->spellcastingAbility.empty()
                                                        ? cdef->spellcastingAbility : "INT";
                        const std::string castType = cdef && !cdef->spellcastingType.empty()
                                                        ? cdef->spellcastingType : "full";
                        Phyxel::Core::SpellcasterComponent sc(it.key());
                        sc.initialize(classId, Phyxel::Core::abilityFromString(abilStr.c_str()),
                                      lvl, castType);
                        int learned = 0;
                        if (cj.contains("spells") && cj["spells"].is_array())
                            for (const auto& s : cj["spells"]) {{
                                const std::string sid = s.get<std::string>();
                                const auto* sd = Phyxel::Core::SpellRegistry::instance().getSpell(sid);
                                if (!sd) {{ LOG_WARN("{class_name}", "casters['{{}}']: unknown spell '{{}}' dropped", it.key(), sid); continue; }}
                                if (sd->isCantrip()) sc.learnCantrip(sid);
                                else                 sc.learnSpell(sid, /*prepared=*/true);
                                ++learned;
                            }}
                        LOG_INFO("{class_name}", "NPC caster '{{}}': {{}} {{}} lvl {{}}, {{}} spell(s), {{}} slot(s)",
                                 it.key(), classId, abilStr, lvl, learned, sc.slots().totalRemaining());
                        npcCasters_.emplace(it.key(), std::move(sc));
                    }}
                }}

                // HOSTILES (G-137): who fights the player on sight / when engaged. The
                // authored encounters already say so — every enemy-side participant of a
                // start_combat action, in any scene. (A combat-AI profile is NOT the
                // signal: companions carry one too, and the first build marked Bram,
                // Oswin and Wren hostile.) "hostile": true on an NPC also counts (loader).
                {{
                    auto harvest = [this](const nlohmann::json& triggers) {{
                        if (!triggers.is_array()) return;
                        for (const auto& t : triggers)
                            for (const auto& a : t.value("then", nlohmann::json::array()))
                                if (a.value("type", "") == "start_combat")
                                    for (const auto& pt : a.value("participants", nlohmann::json::array()))
                                        if (!pt.value("player_side", false) && pt.contains("entity_id"))
                                            authoredHostiles_.insert(pt["entity_id"].get<std::string>());
                    }};
                    if (gameDef.contains("triggers")) harvest(gameDef["triggers"]);
                    if (gameDef.contains("scenes") && gameDef["scenes"].is_array())
                        for (const auto& sc : gameDef["scenes"]) {{
                            if (sc.contains("triggers")) harvest(sc["triggers"]);
                            if (sc.contains("definition") && sc["definition"].contains("triggers")) harvest(sc["definition"]["triggers"]);
                        }}
                    LOG_INFO("{class_name}", "authored hostiles: {{}}", authoredHostiles_.size());
                }}

                // "combat_ai": per-NPC tactical profile —
                //   {{"npc_Archer": {{"target":"weakest","preferred_range":25,
                //                    "flee_below_hp":0.3}},
                //    "npc_Acolyte": {{"heal_ally_below":0.6}}}}
                // Absent entities keep the default brute profile (nearest foe,
                // no kiting, fights to the death).
                if (gameDef.contains("combat_ai") && gameDef["combat_ai"].is_object()) {{
                    for (auto it = gameDef["combat_ai"].begin(); it != gameDef["combat_ai"].end(); ++it) {{
                        const auto& tj = it.value();
                        Phyxel::Core::CombatTactics t;
                        const std::string pri = tj.value("target", "nearest");
                        if      (pri == "weakest") t.priority = Phyxel::Core::CombatTactics::Priority::Weakest;
                        else if (pri == "casters") t.priority = Phyxel::Core::CombatTactics::Priority::Casters;
                        else if (pri == "focus")   t.priority = Phyxel::Core::CombatTactics::Priority::Focus;
                        else if (pri != "nearest")
                            LOG_WARN("{class_name}", "combat_ai['{{}}']: unknown target '{{}}' — using nearest", it.key(), pri);
                        t.preferredRangeFeet = tj.value("preferred_range", 0.0f);
                        t.fleeBelowHpFrac    = tj.value("flee_below_hp", 0.0f);
                        t.healAllyBelowFrac  = tj.value("heal_ally_below", 0.0f);
                        // "weapon": an rpg_items id ("mace", "shortsword") -> damage dice from the
                        // item; "attack_bonus": to-hit. Companions fight with real weapons (G-45).
                        t.attackBonus = tj.value("attack_bonus", -1);
                        if (tj.contains("damage_dice") && tj["damage_dice"].is_string())
                            t.damageDice = tj["damage_dice"].get<std::string>();
                        if (tj.contains("weapon") && tj["weapon"].is_string()) {{
                            auto& items = Phyxel::Core::RpgItemRegistry::instance();
                            if (items.count() == 0) items.loadFromDirectory("resources/rpg_items");
                            const std::string wid = tj["weapon"].get<std::string>();
                            if (const auto* item = items.getItem(wid); item && item->hasWeaponDamage())
                                t.damageDice = item->damageDice.toString();
                            else
                                LOG_WARN("{class_name}", "combat_ai['{{}}']: unknown weapon '{{}}' (resources/rpg_items)", it.key(), wid);
                        }}
                        LOG_INFO("{class_name}", "Tactics '{{}}': target={{}} range={{}}ft flee<{{}} heal<{{}}",
                                 it.key(), pri, t.preferredRangeFeet, t.fleeBelowHpFrac, t.healAllyBelowFrac);
                        npcTactics_[it.key()] = t;
                    }}
                }}

                // PERSISTENT member, not a local: the SceneManager keeps a pointer to
                // this for every later scene transition (menu buttons, triggers).
                auto& subsystems = gameSubsystems_;
                subsystems.chunkManager    = engine.getChunkManager();
                subsystems.npcManager      = npcManager_.get();
                subsystems.entityRegistry  = entityRegistry_.get();
                subsystems.dialogueSystem  = dialogueSystem_.get();
                subsystems.storyEngine     = storyEngine_.get();
                subsystems.camera          = engine.getCamera();
                subsystems.triggerSystem   = &triggers_;  // game.json "triggers" load here
                subsystems.commandStructure = &command_;  // game.json "squad"/"rank" load here
                subsystems.templateManager = templates_.get();
                subsystems.placedObjectManager = placedObjects_.get();

                // Wire up entity spawner so the loader can create the player
                subsystems.entitySpawner = [this](const std::string& type,
                        const glm::vec3& pos, const std::string& animFile)
                        -> Phyxel::Scene::Entity* {{
                    auto* entity = spawnEntity(type, pos, animFile);
                    if (entity && type == "animated") {{
                        playerCharacter_ = dynamic_cast<Phyxel::Scene::AnimatedVoxelCharacter*>(entity);
                    }}
                    return entity;
                }};

                // Load HUD panels now that game.json is parsed: a game's own top-level
                // "hud" array overrides the engine default HUD (resources/ui/
                // default_hud.json) — reposition/restyle the health bar, add custom
                // labels, etc. Mirrors the editor's setupGameHud(). Must run BEFORE the
                // multi-scene transition below so the HUD screens already exist when a
                // menu start-scene's loadMenuInto hides them. (game-dev feedback round 5.)
                if (auto* hudUi = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr) {{
                    Phyxel::UI::loadHudInto(*hudUi,
                        gameDef.contains("hud") ? &gameDef["hud"] : nullptr);
                    // Wire the data-driven AI conversation box (hud_ai_dialogue panel):
                    // providers + the text field's submit. Replaces the ImGui dialogue
                    // box for AI conversations. (docs/HudSystem.md §11a.)
                    if (dialogueSystem_)
                        Phyxel::UI::setupAIDialogue(*hudUi, renderCoordinator_->hudData(), dialogueSystem_.get());
                    // The default HUD ships an "End Turn" button (hud_combat_action /
                    // combat_end_turn) that only the EDITOR used to bind — in a shipped
                    // game it rendered and did nothing, so a player who did not want to
                    // attack could never hand the turn over (Ravenmere gap G-02). Mirror
                    // Application::bindCombatHudButtons.
                    for (const auto& [screenName, vis] : hudUi->getScreenList()) {{
                        auto* scr = hudUi->getScreen(screenName);
                        if (!scr) continue;
                        // Attack button: swings at the selected (or hovered) foe; without one it
                        // leaves the hint to explain. Same path a click on the foe takes.
                        if (auto* w = scr->findChild("combat_attack"); w && w->type() == Phyxel::UI::WidgetType::Button) {{
                            static_cast<Phyxel::UI::UIButton*>(w)->onClick = [this] {{
                                std::string tgt = playerTurn_.selectedTarget();
                                if (tgt.empty()) tgt = hoveredTarget_;
                                if (tgt.empty()) {{ LOG_INFO("{class_name}", "Attack button: no target selected"); return; }}
                                playerTurn_.setSelectedTarget(tgt);
                                const bool ok = playerTurn_.requestAttack(tgt);
                                LOG_INFO("{class_name}", "Attack button -> '{{}}' ({{}})", tgt, ok ? "ok" : "refused");
                            }};
                        }}
                        if (auto* w = scr->findChild("combat_end_turn"); w && w->type() == Phyxel::UI::WidgetType::Button) {{
                            static_cast<Phyxel::UI::UIButton*>(w)->onClick = [this] {{
                                if (combatDirector_.inCombat() && playerTurn_.isPlayerTurnActive()) {{
                                    LOG_INFO("{class_name}", "End Turn (button)");
                                    playerTurn_.endTurn();
                                }}
                            }};
                        }}
                    }}
                }}

                // Multi-scene: delegate to SceneManager
                if (Phyxel::Core::GameDefinitionLoader::isMultiScene(gameDef)) {{
                    auto* sm = engine.getSceneManager();
                    if (sm) {{
                        auto manifest = Phyxel::Core::GameDefinitionLoader::parseManifest(gameDef);
                        sm->setSubsystems(&subsystems);

                        // Menu scenes are rendered by gameMenuRenderer_ and drive the
                        // flow themselves; the built-in shell must not double up. These
                        // callbacks track which kind of scene is active.
                        Phyxel::Core::SceneCallbacks cb = {{}};

                        // Unload cleanup — without these, each scene transition leaks
                        // the previous scene's player/NPCs (stray duplicate characters).
                        cb.clearEntities = [this]() {{
                            if (entityRegistry_) entityRegistry_->clear();
                            entities_.clear();
                            playerCharacter_ = nullptr;
                            // Turn bodies wrap per-scene characters — clear them with
                            // the entities or the map dangles across transitions; end
                            // any encounter still running against the old scene.
                            if (combatDirector_.inCombat()) combatDirector_.endEncounter();
                            turnBodies_.clear();
                            hoveredTarget_.clear();
                            playerTurn_.setSelectedTarget("");
                            if (interactionManager_) interactionManager_->clearCache();   // G-130
                        }};
                        cb.clearNPCs = [this]() {{
                            if (interactionManager_) interactionManager_->clearCache();   // G-130: the cached NPC dies with the scene
                            if (!npcManager_) return;
                            for (const auto& name : npcManager_->getAllNPCNames())
                                npcManager_->removeNPC(name);
                        }};
                        cb.endDialogue = [this]() {{
                            if (dialogueSystem_ && dialogueSystem_->isActive())
                                dialogueSystem_->endConversation();
                        }};
                        // Loading screen for transitionStyle:"loading_screen" (the
                        // DEFAULT style — without this callback the SceneManager's
                        // setLoadingScreen calls silently no-op and transitions show
                        // a frozen frame; StandaloneParityGaps.md §2). ScreenState::
                        // Loading drives the data-driven "loading:*" overlay in
                        // onRender. onSceneReady (below) or the hide call returns
                        // the shell to Playing, whichever the SceneManager fires.
                        cb.setLoadingScreen = [this](bool show, const std::string& sceneName) {{
                            if (show) {{
                                loadingSceneName_ = sceneName;
                                // G-115: a transition OUT of a menu scene showed nothing - the
                                // menu owned the screen until the world was ready. The menu is
                                // being left: drop it now and put the loading screen up.
                                if (menuSceneActive_) {{
                                    menuSceneActive_ = false;
                                    if (auto* ui = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr)
                                        Phyxel::UI::unloadMenuFrom(*ui);
                                }}
                                if (screen_.getState() != Phyxel::UI::ScreenState::Loading) {{
                                    LOG_INFO("{class_name}", "Loading screen shown (-> '{{}}')", sceneName);
                                    screen_.setState(Phyxel::UI::ScreenState::Loading);
                                }}
                                loadingHold_ = -1.0f;
                            }} else if (screen_.getState() == Phyxel::UI::ScreenState::Loading && loadingHold_ < 0.0f) {{
                                // Loaded, but the world is still meshing/lighting (the "black
                                // first seconds", G-49): hold the loading screen until the dirty
                                // chunk backlog drains (min 0.5 s, max 4 s), see onUpdate.
                                loadingHold_ = 0.0f;
                            }}
                            if (engine_) updateCursorMode(*engine_);
                        }};
                        cb.onMenuSceneLoaded = [this](const Phyxel::Core::SceneDefinition& scene) {{
                            // Menu scenes render via the UISystem (custom-Vulkan, no ImGui).
                            auto* ui = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr;
                            if (ui && !scene.menuLayout.is_null()) {{
                                Phyxel::UI::MenuActions acts;
                                acts.onTransitionScene = [this](const std::string& sceneId) {{
                                    auto* sm = engine_ ? engine_->getSceneManager() : nullptr;
                                    if (sm && sm->hasManifest()) sm->transitionTo(sceneId);
                                }};
                                acts.onQuit = [this]() {{
                                    auto* w = engine_ ? engine_->getWindowManager() : nullptr;
                                    if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE);
                                }};
                                // Reuse the same {{{{token}}}} resolver the persistent menu
                                // renderer uses. Without it, menu scenes leave every
                                // {{{{playtime}}}}/{{{{story.<var>}}}} literal on screen.
                                // (game-dev feedback round 5 — UIShowcase.)
                                if (gameMenuRenderer_)
                                    acts.onResolveVariable = gameMenuRenderer_->onResolveVariable;
                                Phyxel::UI::loadMenuInto(*ui, scene.menuLayout, acts);
                                menuSceneActive_ = true;
                                if (engine_) updateCursorMode(*engine_);
                            }}
                            // menuWorld camera orbit: "cameraPath" in the menu
                            // scene's definition — waypoints [{{position,yaw,
                            // pitch,dwell}}], "loop" (default true). The world
                            // itself was loaded by the SceneManager.
                            menuCamPath_.stop();
                            menuCamPath_.clearWaypoints();
                            const auto& sdef = scene.definition;
                            if (sdef.contains("cameraPath") && sdef["cameraPath"].is_object()) {{
                                const auto& cp = sdef["cameraPath"];
                                if (cp.contains("waypoints") && cp["waypoints"].is_array()) {{
                                    for (const auto& wj : cp["waypoints"]) {{
                                        Phyxel::Graphics::CameraWaypoint wp;
                                        const auto& p = wj.value("position", nlohmann::json::object());
                                        wp.position = {{p.value("x", 0.0f), p.value("y", 0.0f), p.value("z", 0.0f)}};
                                        wp.yaw       = wj.value("yaw", 0.0f);
                                        wp.pitch     = wj.value("pitch", 0.0f);
                                        wp.dwellTime = wj.value("dwell", 0.0f);
                                        menuCamPath_.addWaypoint(wp);
                                    }}
                                }}
                                if (menuCamPath_.waypointCount() >= 2) {{
                                    menuCamPath_.setLooping(cp.value("loop", true));
                                    menuCamPath_.play();
                                    LOG_INFO("{class_name}", "menuWorld camera path playing ({{}} waypoints)",
                                             menuCamPath_.waypointCount());
                                }}
                            }}
                        }};
                        cb.onSceneReady = [this](const std::string& sceneId) {{
                            auto* smgr = engine_ ? engine_->getSceneManager() : nullptr;
                            const auto* active = smgr ? smgr->getActiveScene() : nullptr;
                            bool isMenu = active && active->sceneType == Phyxel::Core::SceneType::Menu;
                            if (isMenu) return;  // keep the menu visible
                            // The NavGrid describes ONE world. It was built once at boot (on the
                            // menu scene's 3x3 title world: 96x96 cells, all walkable) and never
                            // again, so in the town the spawn was "not in grid" and every path
                            // went straight through the buildings — NPCs and the harness walked
                            // into walls (Ravenmere gap G-53). Rebuild it for each world scene
                            // from the chunks this scene actually loaded.
                            if (npcManager_) {{
                                npcManager_->buildNavGrid();
                                LOG_INFO("{class_name}", "navigation: NavGrid rebuilt for scene (pathService={{}})",
                                         npcManager_->getPathService() ? "running" : "NULL");
                                // HOSTILES (G-137): every authored enemy (start_combat participant
                                // on the enemy side, harvested at load) fights the player on sight /
                                // when engaged, not only inside its trigger region. "hostile": true
                                // in the definition also counts (set by the loader).
                                int hostiles = 0;
                                npcManager_->forEachNPC([&](Phyxel::Scene::NPCEntity& n) {{
                                    if (authoredHostiles_.count("npc_" + n.getName()) || authoredHostiles_.count(n.getName()))
                                        n.setHostile(true);
                                    if (n.isHostile()) ++hostiles;
                                }});
                                encounterInitiator_.clearFled();
                                LOG_INFO("{class_name}", "hostiles in scene: {{}}", hostiles);
                            }}
                            // WORLD HEALTH (WalkabilityGateAndPlaytestLoop layer B): from the
                            // spawn, path to every NPC, trigger region and location on the graph
                            // just built, and check there is terrain under the spawn at all. A
                            // failure is a generator defect that escaped the settlement gate -
                            // or an empty world (G-84). Logged here, served as world_health.
                            if (engine_ && playerCharacter_) {{
                                std::vector<Phyxel::Core::WorldHealthAnchor> anchors;
                                if (npcManager_)
                                    npcManager_->forEachNPC([&](Phyxel::Scene::NPCEntity& npc) {{
                                        anchors.push_back({{npc.getName(), "npc", npc.getPosition(), false, 0}});
                                    }});
                                for (const auto& [tid, region] : triggers_.regionTriggers()) {{
                                    const auto& a = region["from"]; const auto& b = region["to"];
                                    glm::vec3 c((a.value("x", 0.0f) + b.value("x", 0.0f)) * 0.5f,
                                                std::min(a.value("y", 0.0f), b.value("y", 0.0f)) + 1.0f,
                                                (a.value("z", 0.0f) + b.value("z", 0.0f)) * 0.5f);
                                    anchors.push_back({{tid, "trigger", c, false, 0}});
                                }}
                                if (auto* locs = engine_->getLocationRegistry())
                                    for (const auto& [lid, loc] : locs->getAllLocations())
                                        anchors.push_back({{lid, "location", loc.position, false, 0}});
                                auto* cm = engine_->getChunkManager();
                                const auto rep = Phyxel::Core::WorldHealth::check(
                                    npcManager_ ? npcManager_->getNavGraph() : nullptr,
                                    [cm](const glm::ivec3& cube) {{ return cm && cm->hasVoxelAt(cube); }},
                                    playerCharacter_->getPosition(), std::move(anchors));
                                lastWorldHealth_ = rep.toJson();
                                if (rep.ok()) LOG_INFO("{class_name}", "{{}}", rep.summary());
                                else          LOG_WARN("{class_name}", "{{}}", rep.summary());
                                // Layer C: every failure is a defect record (playtest/defects.jsonl
                                // next to the executable) - the fix loop's input, one line each.
                                if (!rep.ok()) {{
                                    std::filesystem::create_directories("playtest");
                                    std::ofstream df("playtest/defects.jsonl", std::ios::app);
                                    const std::string when = Phyxel::Core::WorldHealth::timestamp();
                                    auto line = [&](const std::string& invariant, const glm::vec3& pos,
                                                    const std::string& route, const std::string& evidence) {{
                                        nlohmann::json j = {{{{"when", when}}, {{"source", "selfcheck"}}, {{"scene", sceneId}},
                                                            {{"pos", {{pos.x, pos.y, pos.z}}}}, {{"route_label", route}},
                                                            {{"invariant", invariant}}, {{"evidence", evidence}}}};
                                        df << j.dump() << "\\n";
                                    }};
                                    if (!rep.terrainUnderSpawn) line("terrain_under_spawn", rep.spawn, "spawn", "no voxel within 8 cubes below the spawn");
                                    if (!rep.graphAvailable) line("navgraph_available", rep.spawn, "spawn", "no NavGraph at scene ready");
                                    for (const auto& an : rep.anchors)
                                        if (!an.reachable) line("anchor_reachable_from_spawn", an.pos, "spawn -> " + an.kind + " " + an.id, "WorldHealth: no path");
                                }}
                            }}
                            // A world scene is ready: drop any menu and hand control to
                            // gameplay. Setting Playing must NOT depend on a menu having
                            // been shown — a game whose startScene is a world scene
                            // (direct boot) otherwise stays on Intro and gameplay/camera
                            // never initialize (renders garbage).
                            if (menuSceneActive_) {{
                                menuSceneActive_ = false;
                                if (auto* ui = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr)
                                    Phyxel::UI::unloadMenuFrom(*ui);
                            }}
                            if (screen_.getState() == Phyxel::UI::ScreenState::Loading) {{
                                if (loadingHold_ < 0.0f) loadingHold_ = 0.0f;   // settle hold owns the hand-off (G-115)
                            }} else if (!Phyxel::UI::isGameRunning(screen_.getState())) {{
                                screen_.setState(Phyxel::UI::ScreenState::Playing);
                            }}
                            // First WORLD scene of the session: the scene's DB is
                            // now open and the player spawned — restore the saved
                            // profile. ONE attempt per session: re-entering a scene
                            // mid-run must never rewind live progress to an older
                            // save in that scene's DB.
                            if (!profileRestored_) {{
                                profileRestored_ = true;
                                if (loadPlayerProfile())
                                    LOG_INFO("{class_name}", "Restored saved player profile");
                                ensureDefaultActionBar();   // G-150: no-op once arranged
                            }}
                            // Companions travel WITH the player: scene loads clear
                            // all entities, so respawn absent party members near
                            // the player on every world scene, in FOLLOW mode —
                            // they walk with the player in exploration (BG3-style)
                            // and start_combat auto-enlists them on the player's
                            // side when an encounter begins.
                            if (npcManager_ && entityRegistry_ && playerCharacter_) {{
                                for (const auto& m : rpgParty_.getMembers()) {{
                                    if (!m.isAlive || entityRegistry_->getEntity(m.entityId)) continue;
                                    const glm::vec3 at = playerCharacter_->getPosition() + glm::vec3(1.5f, 0.0f, 1.5f);
                                    // Explicit anim file: spawnNPC does NOT default an empty
                                    // path (unlike the game.json loader) — a companion spawned
                                    // with "" has no rig, so the combat TurnActor can never
                                    // bind its body and its turn stalls the whole encounter.
                                    if (npcManager_->spawnNPC(m.name, "resources/animated_characters/humanoid.anim",
                                                              at, Phyxel::Core::NPCBehaviorType::Follow))
                                        LOG_INFO("{class_name}", "Companion '{{}}' rejoined at the player's side", m.name);
                                }}
                            }}
                            if (engine_) updateCursorMode(*engine_);
                        }};
                        sm->setCallbacks(cb);

                        sm->loadManifest(manifest);
                        // "globalStory": {{"world": {{"variables": {{...}}}}, "arcs": [...]}} — the
                        // manifest parsed this block but NOTHING consumed it, so every
                        // dialogue condition on an authored initial variable failed closed
                        // (Ravenmere gap G-26). Apply the initial variables ONCE here (not
                        // per scene — a scene re-entry must not reset quest progress).
                        if (storyEngine_ && gameDef.contains("globalStory") && gameDef["globalStory"].is_object()) {{
                            const nlohmann::json& gs = gameDef["globalStory"];
                            int applied = 0;
                            if (gs.contains("world") && gs["world"].is_object() &&
                                gs["world"].contains("variables") && gs["world"]["variables"].is_object()) {{
                                const nlohmann::json& vars = gs["world"]["variables"];
                                auto& ws = storyEngine_->getWorldState();
                                for (auto it = vars.begin(); it != vars.end(); ++it) {{
                                    const nlohmann::json& val = it.value();
                                    if      (val.is_boolean())        ws.setVariable(it.key(), val.get<bool>());
                                    else if (val.is_number_integer()) ws.setVariable(it.key(), val.get<int>());
                                    else if (val.is_number_float())   ws.setVariable(it.key(), val.get<float>());
                                    else if (val.is_string())         ws.setVariable(it.key(), val.get<std::string>());
                                    else continue;
                                    ++applied;
                                }}
                            }}
                            LOG_INFO("{class_name}", "globalStory: {{}} initial story variable(s) applied", applied);
                        }}
                        sm->loadStartScene();
                        // Drive the first frame so the scene actually loads
                        sm->update(0.0f);
                        LOG_INFO("{class_name}", "Loaded multi-scene game ({{}} scenes)", manifest.scenes.size());
                    }}
                }} else {{
                    // Single-scene: strip world key (pre-baked) and load directly
                    gameDef.erase("world");
                    auto result = Phyxel::Core::GameDefinitionLoader::load(gameDef, subsystems);
                    if (result.success) {{
                        LOG_INFO("{class_name}", "Loaded game: {{}} chunks, {{}} NPCs",
                                 result.chunksGenerated, result.npcsSpawned);
                    }} else {{
                        LOG_ERROR("{class_name}", "Failed to load game: {{}}", result.error);
                        return false;
                    }}
                }}

                // NAVIGATION. buildNavGrid() constructs the NavGraph AND creates
                // and starts the PathService; without it getPathService() returns
                // null and every combat path request is silently skipped. A
                // shipped game never called it — only two debug API endpoints did,
                // lazily — so NPCs in a standalone had no pathfinder at all and
                // walked into walls forever. Measured: two fighters either side of
                // a 3-tall wall with one gap 30u away stood at the wall face for
                // 100 s and never moved a millimetre along it.
                //
                // Must run AFTER the scene loads: it needs loaded chunks to derive
                // its bounds, and warns + returns if the chunk map is empty.
                if (npcManager_) {{
                    npcManager_->buildNavGrid();
                    LOG_INFO("{class_name}", "navigation: pathService={{}}",
                             npcManager_->getPathService() ? "running" : "NULL");
                }}

                // NPCs exist now, so their weapons can be hung on their grips.
                // Must run AFTER the definition load — before it there is
                // nobody to equip.
                equipNpcWeapons();

                // Restore the saved player profile (camera pose + health + XP/level)
                // from the active scene's world DB, if one was saved by a previous
                // run. Succeeds here only for WORLD-start games (the DB is open);
                // menu-start games restore in onSceneReady at the first world scene.
                if (!profileRestored_ && loadPlayerProfile()) {{
                    profileRestored_ = true;
                    LOG_INFO("{class_name}", "Restored saved player profile");
                }}
                // G-150: lay out the bar for a first run. A no-op once the player has
                // arranged it, so a restored layout is never overwritten.
                ensureDefaultActionBar();

                // Sync input manager with camera after definition load
                auto* input = engine.getInputManager();
                auto* cam = engine.getCamera();
                input->setCameraPosition(cam->getPosition());
                input->setYawPitch(cam->getYaw(), cam->getPitch());

                return true;
            }} catch (const std::exception& e) {{
                LOG_ERROR("{class_name}", "Error parsing game.json: {{}}", e.what());
                return false;
            }}
        }}

        // ====================================================================
        // Game Loop
        // ====================================================================

        void {class_name}::onHandleInput(Phyxel::Core::EngineRuntime& engine) {{
            auto* input = engine.getInputManager();
            if (!input) return;

            auto state = screen_.getState();

            // Mouse wheel -> scrollable UI panels (quest logs, lore pages,
            // long lists). Consumed by the topmost scrollable under the
            // cursor; the delta is reset either way so it can't leak into
            // other consumers as a stale value.
            if (auto* wm = engine.getWindowManager()) {{
                const float wheel = wm->getScrollDelta();
                if (wheel != 0.0f) {{
                    bool consumed = false;
                    if (auto* ui = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr) {{
                        double mx = 0.0, my = 0.0;
                        glfwGetCursorPos(wm->getHandle(), &mx, &my);
                        consumed = ui->handleScroll({{static_cast<float>(mx), static_cast<float>(my)}}, wheel);
                    }}
                    // Unconsumed wheel over the battlefield = tactical ZOOM.
                    // (A scrollable panel under the cursor still wins.)
                    // Not gated on inCombat() — same reason as the Q/E/R/F
                    // orbit below: a real-time battle has no encounter, so
                    // gating on one left the sim with no zoom at all.
                    if (!consumed) {{
                        if (auto* rig = gameplayCamera().rig())
                            rig->distance = glm::clamp(rig->distance - wheel * rig->zoomStep,
                                                       rig->distanceMin, rig->distanceMax);
                    }}
                    wm->resetScrollDelta();
                }}
            }}

            // ESC: pause/resume toggle, or close dialogue. EDGE-TRIGGERED — isKeyPressed
            // is held-state, so without this a held ESC would toggle pause every frame.
            const bool escNow = input->isKeyPressed(GLFW_KEY_ESCAPE);
            if (escNow && !escPrev_) {{
                if (dialogueSystem_ && dialogueSystem_->isActive()) {{
                    dialogueSystem_->endConversation();
                    updateCursorMode(engine);
                }} else if (state == Phyxel::UI::ScreenState::Playing) {{
                    screen_.togglePause();
                    updateCursorMode(engine);
                }} else if (state == Phyxel::UI::ScreenState::Paused ||
                           state == Phyxel::UI::ScreenState::Character) {{
                    screen_.resume();
                    updateCursorMode(engine);
                }}
            }}
            escPrev_ = escNow;

            // While typing in an AI conversation the player types freely, so the
            // tree-dialogue keybinds below (E / Enter / 1-4) must NOT fire — 'e',
            // Enter, digits are text. AI submit is the UISystem text field; ESC
            // (above) still ends the conversation. (Movement is already suppressed
            // during any dialogue — see onUpdate's !inDialogue gate.)
            const bool aiTyping = dialogueSystem_ && dialogueSystem_->isActive() &&
                                  dialogueSystem_->isAIConversation();

            // Dialogue keys are EDGE-triggered. isKeyPressed() is level-triggered, so a
            // key held across two frames (any 100 ms press at 10-20 FPS, or a normal
            // human press at 60) advanced TWO nodes: Ravenmere's Bram fired his
            // join_party action twice on one Enter, and the same Enter that ended a
            // conversation re-opened it (gap G-29).
            auto dlgEdge = [&](int glfwKey, int slot) {{
                const bool down = input->isKeyPressed(glfwKey);
                const bool edge = down && !dlgKeyDown_[slot];
                dlgKeyDown_[slot] = down;
                return edge;
            }};
            // Interact is an ACTION (E by default; F under the WoW scheme, where E strafes).
            const bool interactDown = input->isActionPressed("Interact");
            const bool eEdge = interactDown && !interactKeyDown_;
            interactKeyDown_ = interactDown;
            // Character sheet (ToggleCharacter, C by default - WoW's character pane;
            // Ravenmere G-133): Playing <-> Character. Not while a conversation owns
            // the keyboard.
            {{
                const bool charDown = input->isActionPressed("ToggleCharacter");
                const bool inDialogue = dialogueSystem_ && dialogueSystem_->isActive();
                if (charDown && !characterKeyDown_ && !inDialogue &&
                    (state == Phyxel::UI::ScreenState::Playing || state == Phyxel::UI::ScreenState::Character)) {{
                    screen_.toggleCharacter();
                    updateCursorMode(engine);
                }}
                characterKeyDown_ = charDown;
            }}
            const bool enterEdge = dlgEdge(GLFW_KEY_ENTER, 1);

            // E key: interact with NPC / advance a TREE dialogue.
            if (!aiTyping && Phyxel::UI::isGameRunning(state) && eEdge) {{
                if (dialogueSystem_ && dialogueSystem_->isActive()) {{
                    dialogueSystem_->advanceDialogue();
                }} else {{
                    if (interactionManager_) interactionManager_->tryInteract(playerCharacter_);
                    // Interact-gated scene transitions (TransitionMarkers.h): the E press at
                    // a trapdoor/ladder/waystone site fires its trigger.
                    if (playerCharacter_) {{
                        const glm::vec3 pp = playerCharacter_->getPosition();
                        triggers_.onEvent("interact", {{{{"entity", "player"}}, {{"x", pp.x}}, {{"y", pp.y}}, {{"z", pp.z}}}});
                    }}
                }}
            }}

            // Enter key: advance a TREE dialogue (AI submits via the text field).
            if (!aiTyping && dialogueSystem_ && dialogueSystem_->isActive() && enterEdge) {{
                dialogueSystem_->advanceDialogue();
            }}

            // Number keys 1-4: select TREE dialogue choices.
            for (int k = GLFW_KEY_1; k <= GLFW_KEY_4; ++k) {{
                const bool choiceEdge = dlgEdge(k, 2 + (k - GLFW_KEY_1));
                if (!aiTyping && dialogueSystem_ && dialogueSystem_->isActive() && choiceEdge) {{
                    dialogueSystem_->selectChoice(k - GLFW_KEY_1);
                }}
            }}

            // ── ACTION BAR KEYS (G-150) ──────────────────────────────────────────
            // 1-9, 0, -, = fire the bar's twelve slots; P opens the abilities panel
            // you drag from. Gated on dialogue NOT being active because 1-4 are that
            // dialogue's choice keys - otherwise both would answer the same press,
            // and picking dialogue option 2 would also cast whatever is in slot 2.
            {{
                // NAMED ACTIONS, not raw keys: rebindable in Settings -> Keybindings, and a
                // game that wants the number row for something else just moves them.
                const bool talking = dialogueSystem_ && dialogueSystem_->isActive();
                const bool barLive = !aiTyping && !talking && Phyxel::UI::isGameRunning(state);
                for (int i = 0; i < Phyxel::Core::ActionBar::SLOT_COUNT; ++i) {{
                    const bool down = input->isActionPressed("ActionSlot" + std::to_string(i + 1));
                    const bool edge = down && !barKeyDown_[i];
                    barKeyDown_[i] = down;
                    if (edge && barLive) fireActionSlot(i);
                }}
                const bool pDown = input->isActionPressed("ToggleAbilities");
                const bool pEdge = pDown && !barKeyDown_[Phyxel::Core::ActionBar::SLOT_COUNT];
                barKeyDown_[Phyxel::Core::ActionBar::SLOT_COUNT] = pDown;
                if (pEdge && barLive) {{
                    spellbookOpen_ = !spellbookOpen_;
                    LOG_INFO("ActionBar", "Abilities panel {{}}", spellbookOpen_ ? "opened" : "closed");
                }}
            }}

            // Character movement + camera are driven by GameShell's
            // updateGameplayCamera() in onUpdate (it samples input through the
            // scene's control scheme, moves the body, frames the camera, and
            // owns mouse capture). Here we only pump InputManager for
            // registered key actions, and only while actually playing.
            bool inDialogue = dialogueSystem_ && dialogueSystem_->isActive();
            if (Phyxel::UI::isGameRunning(screen_.getState()) && !inDialogue && !menuSceneActive_) {{
                input->processInput(engine.getLastDeltaTime());
            }} else {{
                // processInput is the only thing that ages INJECTED keys. When it is
                // gated off (dialogue / pause / menu scene) an injected key never
                // released, so every edge-triggered binding saw it held forever —
                // the harness could not pick a second dialogue choice or close the
                // pause menu (Ravenmere gap G-33). Age the injections regardless.
                input->tickInjection(engine.getLastDeltaTime());
            }}

            // Window resize (G-132): the HUD keeps its logical canvas and is placed into the
            // window - scaled to fit, centred. Input and world labels convert at the UI's edge.
            if (auto* rcUi = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr) {{
                const glm::uvec2 vp = renderCoordinator_->getSwapChainSize();
                if (vp != lastUiWindow_) {{ lastUiWindow_ = vp; rcUi->setWindowSize(vp.x, vp.y); }}
            }}

            // HUD layout LINT (docs/game-production/CombatUiBg3.md increment 2): whenever the set
            // of visible screens changes, measure the layout the way the renderer does and
            // write every cut-off child / overlapping panel pair to playtest/defects.jsonl.
            // A tester should never be the one to notice "the text doesn't fit".
            if (auto* lintUi = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr) {{
                std::string visibleSig;
                for (const auto& n : lintUi->visibleScreenNames()) visibleSig += n + "|";
                // Lint TWO frames after the visible set changes: the render's binding pass
                // (visibleWhen) has then decided which panels really show. Linting on the
                // change frame itself reported a combat banner / compass overlap that never
                // exists (both default visible until bound).
                if (visibleSig != lastLintSignature_) {{ lastLintSignature_ = visibleSig; lintArmFrames_ = 2; }}
                if (lintArmFrames_ > 0 && --lintArmFrames_ == 0) {{
                    const nlohmann::json defects = lintUi->lintLayout();
                    if (defects.is_array() && !defects.empty()) {{
                        std::filesystem::create_directories("playtest");
                        std::ofstream df("playtest/defects.jsonl", std::ios::app);
                        const std::string when = Phyxel::Core::WorldHealth::timestamp();
                        for (const auto& d : defects) {{
                            LOG_WARN("{class_name}", "HUD layout: {{}}", d.value("message", std::string()));
                            nlohmann::json j = {{{{"when", when}}, {{"source", "hud_lint"}}, {{"invariant", "hud_" + d.value("kind", std::string())}},
                                                {{"route_label", d.value("panel", std::string()) + " / " + d.value("other", std::string())}},
                                                {{"evidence", d.value("message", std::string())}}, {{"screens", visibleSig}}}};
                            df << j.dump() << "\\n";
                        }}
                    }}
                }}
            }}

            // BG3 mouse combat: on the player's turn, a left click resolves to
            // attack (enemy under cursor) or move (ground point) through the
            // same PlayerTurnController pick the test API uses. Edge-triggered;
            // cursor is free during combat (updateCursorMode).
            // HOVER TARGETING: resolve whatever the cursor is over each frame
            // so the nameplate under it lights up with its AC / hit chance
            // BEFORE you commit to the click. Uses the same pick the click
            // itself uses, so what you see is what you will hit.
            if (combatDirector_.inCombat() && !inDialogue && renderCoordinator_) {{
                hoveredTarget_.clear();
                if (auto* cam = engine.getCamera()) {{
                    double mx = 0.0, my = 0.0;
                    input->getCurrentMousePosition(mx, my);
                    const glm::uvec2 vp = renderCoordinator_->getSwapChainSize();
                    const float groundY = playerCharacter_ ? playerCharacter_->getPosition().y : 0.0f;
                    auto pick = playerTurn_.resolvePick(
                        *cam, {{static_cast<float>(mx), static_cast<float>(my)}},
                        {{static_cast<float>(vp.x), static_cast<float>(vp.y)}}, groundY);
                    if (pick.kind == Phyxel::Core::PlayerTurnController::PickResult::Kind::Attack)
                        hoveredTarget_ = pick.targetId;
                    // The pointer says what the click will do (BG3): crosshair = attack,
                    // hand = move (your turn), arrow = nothing.
                    if (auto* win = engine.getWindowManager()) {{
                        using CS = Phyxel::UI::WindowManager::CursorShape;
                        win->setCursorShape(!hoveredTarget_.empty() ? CS::Crosshair
                                            : (playerTurn_.isPlayerTurnActive() &&
                                               pick.kind == Phyxel::Core::PlayerTurnController::PickResult::Kind::Move)
                                                ? CS::Hand : CS::Arrow);
                    }}
                }}
            }} else {{
                hoveredTarget_.clear();
                if (auto* win = engine.getWindowManager())
                    win->setCursorShape(Phyxel::UI::WindowManager::CursorShape::Arrow);
            }}

            // Tactical camera control. Mouse-look is deliberately off during
            // combat (it used to leak into pitch and bury the camera under the
            // floor), so the BG3-style view gets its own explicit controls:
            //   Q / E     orbit the battle
            //   R / F     raise / lower the angle, inside the rig's band
            //   wheel     zoom, but only when the UI did not consume it
            //
            // NOT gated on inCombat(). It used to be, which meant a REAL-TIME
            // battle - the mode built to be watched - had no camera controls
            // whatsoever: no turn-based encounter, so this block never ran, and
            // there is no other binding. The player could only trudge around on
            // WASD. Orbiting the view is useful whenever you are not in a
            // dialogue, so that is the only thing it asks now.
            if (!inDialogue && gameplayCamera().rigName() == combatCameraRig_) {{
                auto* look = engine.getInputManager();
                auto* rig  = gameplayCamera().rig();
                const float dt = engine.getLastDeltaTime();
                if (look && rig) {{
                    float yaw = look->getYaw(), pitch = look->getPitch();
                    const float orbitRate = 90.0f;   // deg/sec
                    // ARROW KEYS are the primary controls because they are the
                    // only ones with NO conflict. The original Q/E/R/F set was
                    // half-broken: E is bound to Interact and F to Attack in the
                    // default action map (GameSettings.cpp), so orbiting right
                    // also poked NPCs and "shallower" swung the player's weapon
                    // instead of tilting the camera. Q and R are conflict-free
                    // and stay as aliases; E and F are gone.
                    // Every direction also has a LETTER alias. Not for comfort:
                    // the test API resolves keys by name through stringToKey,
                    // which does not know "LEFT"/"RIGHT"/"UP"/"DOWN", so an
                    // arrows-only scheme is unverifiable by the harness — and an
                    // unverifiable control is one I cannot honestly report as
                    // working. Q/T orbit, R/G pitch; all four are free of the
                    // default action map (unlike E=Interact and F=Attack).
                    const bool left  = input->isKeyPressed(GLFW_KEY_LEFT)  || input->isKeyPressed(GLFW_KEY_Q);
                    const bool right = input->isKeyPressed(GLFW_KEY_RIGHT) || input->isKeyPressed(GLFW_KEY_T);
                    const bool up    = input->isKeyPressed(GLFW_KEY_UP)    || input->isKeyPressed(GLFW_KEY_R);
                    const bool down  = input->isKeyPressed(GLFW_KEY_DOWN)  || input->isKeyPressed(GLFW_KEY_G);
                    if (left)  yaw   -= orbitRate * dt;
                    if (right) yaw   += orbitRate * dt;
                    if (up)    pitch -= 45.0f * dt;   // steeper
                    if (down)  pitch += 45.0f * dt;   // shallower
                    pitch = glm::clamp(pitch, rig->pitchClampMin, rig->pitchClampMax);
                    look->setYawPitch(yaw, pitch);
                }}
            }}

            // SPACE ends the player's turn (edge-triggered). Keyboard twin of the
            // HUD's End Turn button; WASD is dead in turn-based combat so Space is
            // free (Ravenmere gap G-02).
            {{
                const bool endKey = input->isKeyPressed(GLFW_KEY_SPACE);
                if (endKey && !endTurnKeyDown_ && combatDirector_.inCombat() &&
                    playerTurn_.isPlayerTurnActive() && !inDialogue) {{
                    LOG_INFO("{class_name}", "End Turn (Space)");
                    playerTurn_.endTurn();
                }}
                endTurnKeyDown_ = endKey;
            }}

            // CLICK-TO-MOVE / CLICK-TO-INTERACT outside combat (BG3, Ravenmere G-75).
            // Pointer-driven games only: a left click walks the player to the ground
            // point under the cursor along the NPCs' NavGraph, or to an NPC and
            // interacts on arrival (pointerClick below). WASD cancels a walk.
            if (pointerMode_ && !combatDirector_.inCombat() && !inDialogue &&
                renderCoordinator_ && playerCharacter_ &&
                Phyxel::UI::isGameRunning(screen_.getState())) {{
                // WoW mouse: a press-and-release WITHOUT movement is a CLICK (left selects,
                // right interacts / click-to-move when enabled); a press that moves is a
                // DRAG that the camera scheme owns (left orbits, right steers).
                double mx = 0.0, my = 0.0;
                input->getCurrentMousePosition(mx, my);
                const glm::vec2 at{{static_cast<float>(mx), static_cast<float>(my)}};
                const float kClickPx = 5.0f;
                const bool lmb = input->isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
                const bool rmb = input->isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
                if (lmb && !pointerLmbHeld_) pointerLmbPress_ = at;
                if (rmb && !pointerRmbHeld_) pointerRmbPress_ = at;
                const bool lmbMoved = lmb && glm::length(at - pointerLmbPress_) > kClickPx;
                const bool rmbMoved = rmb && glm::length(at - pointerRmbPress_) > kClickPx;
                pointerDragging_ = lmbMoved || rmbMoved;
                if (!lmb && pointerLmbHeld_ && glm::length(at - pointerLmbPress_) <= kClickPx)
                    pointerClick(engine, pointerLmbPress_, /*rightButton=*/false);
                if (!rmb && pointerRmbHeld_ && glm::length(at - pointerRmbPress_) <= kClickPx)
                    pointerClick(engine, pointerRmbPress_, /*rightButton=*/true);
                pointerLmbHeld_ = lmb;
                pointerRmbHeld_ = rmb;
            }} else {{
                pointerLmbHeld_ = pointerRmbHeld_ = false;
                pointerDragging_ = false;
            }}

            if (combatDirector_.inCombat() && playerTurn_.isPlayerTurnActive() &&
                !inDialogue && renderCoordinator_) {{
                const bool lmb = input->isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
                if (lmb && !combatLmbHeld_) {{
                    auto* cam = engine.getCamera();
                    if (cam) {{
                        // Cursor position through the InputManager's message-fed
                        // cache, NOT glfwGetCursorPos: GLFW polls the OS cursor
                        // live in normal cursor mode, which diverges from the
                        // event stream the rest of input uses (and from injected
                        // WM_MOUSEMOVE in test harnesses).
                        double mx = 0.0, my = 0.0;
                        input->getCurrentMousePosition(mx, my);
                        const glm::vec2 click{{static_cast<float>(mx), static_cast<float>(my)}};
                        // UI first: HUD widgets (the spellbar) consume the click
                        // before any world pick — no other path routes real
                        // clicks into the UISystem during combat gameplay.
                        bool uiConsumed = false;
                        if (auto* uisys = renderCoordinator_->getUISystem())
                            uiConsumed = uisys->injectClick(click);
                        if (uiConsumed)
                            LOG_INFO("{class_name}", "Combat click ({{}}, {{}}) -> UI",
                                     static_cast<int>(mx), static_cast<int>(my));
                        if (!uiConsumed) {{
                            const glm::uvec2 vp = renderCoordinator_->getSwapChainSize();
                            const glm::vec2 vps{{static_cast<float>(vp.x), static_cast<float>(vp.y)}};
                            const float groundY = playerCharacter_ ? playerCharacter_->getPosition().y : 0.0f;
                            if (!armedSpell_.empty()) {{
                                // Armed cast: an enemy under the cursor takes the
                                // spell; anything else cancels the arm (BG3-style).
                                auto pick = playerTurn_.resolvePick(*cam, click, vps, groundY);
                                if (pick.kind == Phyxel::Core::PlayerTurnController::PickResult::Kind::Attack) {{
                                    const bool castOk = playerTurn_.castSpell(armedSpell_, pick.targetId);
                                    LOG_INFO("{class_name}", "Combat click -> cast '{{}}' at '{{}}' ({{}})",
                                             armedSpell_, pick.targetId,
                                             castOk ? "ok" : playerTurn_.castBlockedReason(armedSpell_).c_str());
                                }} else {{
                                    LOG_INFO("{class_name}", "Combat click -> cast '{{}}' cancelled", armedSpell_);
                                }}
                                setArmedSpell("");
                            }} else {{
                                // Clicking a foe SELECTS it — the bracket and
                                // targeting readout persist after the swing —
                                // as well as attacking it.
                                auto pk = playerTurn_.resolvePick(*cam, click, vps, groundY);
                                if (pk.kind == Phyxel::Core::PlayerTurnController::PickResult::Kind::Attack)
                                    playerTurn_.setSelectedTarget(pk.targetId);
                                const char* resolved = playerTurn_.requestPickAt(*cam, click, vps, groundY);
                                LOG_INFO("{class_name}", "Combat click ({{}}, {{}}) -> {{}} (selected '{{}}')",
                                         static_cast<int>(mx), static_cast<int>(my), resolved,
                                         playerTurn_.selectedTarget());
                            }}
                        }}
                    }}
                }}
                combatLmbHeld_ = lmb;
            }} else {{
                combatLmbHeld_ = false;
            }}
        }}

        // One left click at viewport pixels, outside combat: HUD widgets first, then
        // an NPC under the cursor (walk to it, interact on arrival), else the ground
        // under the cursor (walk there). Shared by the real mouse and the test API's
        // pointer_click, so the L4 exercises the shipped path. Returns what happened.
        nlohmann::json {class_name}::pointerClick(Phyxel::Core::EngineRuntime& engine, const glm::vec2& click, bool rightButton) {{
            nlohmann::json out;
            out["x"] = click.x; out["y"] = click.y; out["button"] = rightButton ? "right" : "left";
            auto* cam = engine.getCamera();
            if (!renderCoordinator_ || !playerCharacter_ || !cam) {{ out["result"] = "no_scene"; return out; }}
            if (combatDirector_.inCombat()) {{ out["result"] = "in_combat"; return out; }}
            if (auto* uisys = renderCoordinator_->getUISystem(); uisys && uisys->injectClick(click)) {{
                out["result"] = "ui"; return out;
            }}
            const glm::uvec2 vp = renderCoordinator_->getSwapChainSize();
            const glm::vec2 vps{{static_cast<float>(vp.x), static_cast<float>(vp.y)}};
            if (click.x < 0.0f || click.y < 0.0f || click.x > vps.x || click.y > vps.y) {{
                out["result"] = "off_viewport"; return out;   // only the test API can ask this
            }}
            // 1) an NPC under the cursor (nearest within 32 px of its chest)
            Phyxel::Scene::NPCEntity* hitNpc = nullptr;
            float best = 32.0f;
            if (npcManager_) {{
                npcManager_->forEachNPC([&](Phyxel::Scene::NPCEntity& npc) {{
                    glm::vec2 px;
                    if (!Phyxel::Core::ClickToMove::projectToScreen(
                            *cam, npc.getPosition() + glm::vec3(0.0f, 0.9f, 0.0f), vps, px)) return;
                    if (px.x < 0.0f || px.y < 0.0f || px.x > vps.x || px.y > vps.y) return;   // off-screen
                    const float d = glm::length(px - click);
                    if (d < best) {{ best = d; hitNpc = &npc; }}
                }});
            }}
            if (hitNpc && !rightButton) {{
                // A HOSTILE under the cursor: this is an attack (G-137, "walk up to the
                // alpha wolf but get nothing"). Close enough -> the fight starts now, with
                // an armed spell as the opening action; far -> walk up and start it on
                // arrival. Everyone else: WoW, a left click SELECTS (no walking).
                const auto* hc = hitNpc->getHealthComponent();
                if (hitNpc->isHostile() && (!hc || hc->isAlive())) {{
                    const std::string id = "npc_" + hitNpc->getName();
                    const glm::vec3 npcPos = hitNpc->getPosition();
                    const glm::vec3 me = playerCharacter_->getPosition();
                    const float dist = glm::length(glm::vec2(npcPos.x - me.x, npcPos.z - me.z));
                    if (!armedSpell_.empty()) pendingOpeningCast_ = armedSpell_;
                    if (dist <= Phyxel::Core::EncounterInitiator::kGroupRadius) {{
                        const bool ok = startEncounterAgainst(id, pendingOpeningCast_.empty() ? "attacked by the player"
                                                                                               : "opening spell '" + pendingOpeningCast_ + "'");
                        out["result"] = ok ? "engaged" : "engage_failed"; out["npc"] = hitNpc->getName();
                        return out;
                    }}
                    const bool ok = clickToMove_.requestWalkTo(npcPos, 4.0f, [this, id]() {{
                        startEncounterAgainst(id, "attacked by the player (walked up)");
                    }});
                    LOG_INFO("ClickToMove", "click -> hostile '{{}}' {{}} m away: {{}}", hitNpc->getName(), static_cast<int>(dist),
                             ok ? "walking up to engage" : Phyxel::Core::ClickToMove::resultName(clickToMove_.lastResult()));
                    out["result"] = ok ? "walking_to_engage" : Phyxel::Core::ClickToMove::resultName(clickToMove_.lastResult());
                    out["npc"] = hitNpc->getName();
                    return out;
                }}
                if (!armedSpell_.empty()) setArmedSpell("");   // a spell aimed at a friend: disarm
                LOG_INFO("ClickToMove", "click -> select NPC '{{}}'", hitNpc->getName());
                out["result"] = "selected"; out["npc"] = hitNpc->getName();
                return out;
            }}
            if (hitNpc) {{
                // WoW: a right click on a character INTERACTS (walk into range, then talk).
                const glm::vec3 npcPos = hitNpc->getPosition();
                const float standoff = std::max(0.8f, hitNpc->getInteractionRadius() * 0.8f);
                const bool ok = clickToMove_.requestWalkTo(npcPos, standoff, [this]() {{
                    if (interactionManager_ && playerCharacter_) {{
                        interactionManager_->update(0.0f, playerCharacter_->getPosition(), glm::vec3(0.0f));
                        interactionManager_->tryInteract(playerCharacter_);
                        LOG_INFO("ClickToMove", "arrived at NPC -> interact");
                    }}
                }});
                LOG_INFO("ClickToMove", "click -> NPC '{{}}' at ({{}}, {{}}) standoff {{}}: {{}}",
                         hitNpc->getName(), npcPos.x, npcPos.z, standoff,
                         ok ? "walking" : Phyxel::Core::ClickToMove::resultName(clickToMove_.lastResult()));
                out["result"] = ok ? "walking" : Phyxel::Core::ClickToMove::resultName(clickToMove_.lastResult());
                out["npc"] = hitNpc->getName();
                out["goal"] = {{{{"x", npcPos.x}}, {{"y", npcPos.y}}, {{"z", npcPos.z}}}};
                return out;
            }}
            // 2) the ground: click-to-move only when the game enables it (WoW: off; a
            //    right click on the ground does nothing).
            if (rightButton || !clickToMoveEnabled_) {{ out["result"] = rightButton ? "none" : "click_to_move_off"; return out; }}
            auto* cm = engine.getChunkManager();
            glm::vec3 point;
            const bool hit = cm && Phyxel::Core::ClickToMove::pickGround(
                *cam, click, vps, [cm](const glm::ivec3& c) {{ return cm->hasVoxelAt(c); }}, 200.0f, point);
            if (!hit) {{
                LOG_INFO("ClickToMove", "click ({{}}, {{}}) hit nothing solid", static_cast<int>(click.x), static_cast<int>(click.y));
                out["result"] = "no_hit"; return out;
            }}
            const bool ok = clickToMove_.requestWalkTo(point);
            LOG_INFO("ClickToMove", "click -> ground ({{}}, {{}}, {{}}): {{}} ({{}} waypoints)",
                     point.x, point.y, point.z,
                     ok ? "walking" : Phyxel::Core::ClickToMove::resultName(clickToMove_.lastResult()),
                     clickToMove_.waypoints().size());
            out["result"] = ok ? "walking" : Phyxel::Core::ClickToMove::resultName(clickToMove_.lastResult());
            out["goal"] = {{{{"x", point.x}}, {{"y", point.y}}, {{"z", point.z}}}};
            out["waypoints"] = clickToMove_.waypoints().size();
            return out;
        }}

        // NEW GAME from inside a run (G-129, manual review 2026-09-16: "I died, hit New
        // Game, it did not restart"): before this, start_game only flipped the screen
        // state to Playing - the dead player and the old scene stayed. A fresh run: end
        // the encounter/conversation, drop the run's inventory, coins, quest progress and
        // scene re-entry positions, never resume the old save, and walk the manifest
        // from its start scene again (the player entity is respawned per scene).
        void {class_name}::newGame() {{
            LOG_INFO("{class_name}", "New game: restarting from the start scene");
            if (combatDirector_.inCombat()) combatDirector_.endEncounter();
            if (dialogueSystem_ && dialogueSystem_->isActive()) dialogueSystem_->endConversation();
            clickToMove_.cancel();
            inventory_.clear();
            wallet_ = Phyxel::Core::Currency{{}};
            objectiveTracker_.clear();
            for (const auto& o : initialObjectives_) {{
                if (!o.is_object()) continue;
                objectiveTracker_.addObjective(o.value("id", ""), o.value("title", ""), o.value("description", ""),
                                               o.value("category", "main"), o.value("priority", 0), o.value("hidden", false));
            }}
            profileRestored_ = true;   // a new run never resumes the previous run's save
            if (auto* sm = engine_ ? engine_->getSceneManager() : nullptr) {{
                sm->clearReentryStates();
                if (sm->hasManifest() && !sm->getManifest().startScene.empty()) {{
                    if (!sm->transitionTo(sm->getManifest().startScene))
                        LOG_WARN("{class_name}", "New game: could not transition to '{{}}'", sm->getManifest().startScene);
                }}
            }}
        }}

        // Character sheet tokens ({{{{sheet.<key>}}}} in resources/ui/character_screen.json;
        // Ravenmere G-133). Resolved when the screen opens, from the live CharacterSheet
        // and the player's HealthComponent, so the sheet shows the CURRENT state.
        std::optional<std::string> {class_name}::characterSheetToken(const std::string& key) const {{
            using AT = Phyxel::Core::AbilityType;
            using Skill = Phyxel::Core::Skill;
            const auto& s = playerSheet_;
            auto signedStr = [](int v) {{ return (v >= 0 ? "+" : "") + std::to_string(v); }};
            auto abilityOf = [](const std::string& k) -> std::optional<AT> {{
                if (k == "str") return AT::Strength;     if (k == "dex") return AT::Dexterity;
                if (k == "con") return AT::Constitution; if (k == "int") return AT::Intelligence;
                if (k == "wis") return AT::Wisdom;       if (k == "cha") return AT::Charisma;
                return std::nullopt;
            }};
            if (key == "name") {{
                // No character creation yet (G-01): the pre-made has no authored name.
                return (s.name.empty() || s.name == "player") ? std::string("Adventurer") : s.name;
            }}
            if (key == "race") {{
                const auto* r = Phyxel::Core::RaceRegistry::instance().getRace(s.raceId);
                return r ? r->name : s.raceId;
            }}
            if (key == "class") {{
                std::string out;
                for (const auto& cl : s.classes) {{
                    const auto* c = Phyxel::Core::ClassRegistry::instance().getClass(cl.classId);
                    if (!out.empty()) out += " / ";
                    out += c ? c->name : cl.classId;
                    if (s.classes.size() > 1) out += " " + std::to_string(cl.level);
                }}
                return out;
            }}
            if (key == "level")       return std::to_string(std::max(1, s.totalLevel()));
            if (key == "xp")          return std::to_string(s.experiencePoints);
            if (key == "xp_next")     return std::to_string(Phyxel::Core::CharacterProgression::xpForLevel(std::min(20, std::max(1, s.totalLevel()) + 1)));
            if (key == "hp" || key == "max_hp") {{
                auto* hc = playerCharacter_ ? playerCharacter_->getHealthComponent() : nullptr;
                const int hp    = hc ? static_cast<int>(std::lround(hc->getHealth()))    : s.currentHP;
                const int maxHp = hc ? static_cast<int>(std::lround(hc->getMaxHealth())) : s.maxHP;
                return std::to_string(key == "hp" ? hp : maxHp);
            }}
            if (key == "ac")          return std::to_string(s.armorClass);
            if (key == "speed")       return std::to_string(s.speed);
            if (key == "initiative")  return signedStr(s.initiative);
            if (key == "proficiency") return signedStr(s.proficiencyBonus());
            if (auto a = abilityOf(key))                       return std::to_string(s.attributes.score(*a));
            if (key.size() > 4 && key.compare(key.size() - 4, 4, "_mod") == 0)
                if (auto a = abilityOf(key.substr(0, key.size() - 4))) return signedStr(s.attributes.modifier(*a));
            if (key.rfind("save_", 0) == 0)
                if (auto a = abilityOf(key.substr(5)))         return signedStr(s.savingThrowBonus(*a));
            if (key.rfind("skill_", 0) == 0) {{
                const std::string want = key.substr(6);
                for (int i = 0; i < static_cast<int>(Skill::COUNT); ++i) {{
                    const auto sk = static_cast<Skill>(i);
                    std::string n = Phyxel::Core::ProficiencySystem::skillName(sk);
                    n.erase(std::remove(n.begin(), n.end(), ' '), n.end());   // "Sleight of Hand" -> SleightofHand
                    if (n.size() != want.size() ||
                        !std::equal(n.begin(), n.end(), want.begin(), [](char a, char b) {{
                            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }}))
                        continue;
                    return signedStr(s.skillBonus(sk)) + (s.isProficientWith(sk) ? " *" : "");
                }}
            }}
            return std::nullopt;
        }}

        // Start a turn-based encounter (G-137): every trigger, sighting and opening
        // action funnels through here so the enemy side is recorded (escapees are
        // found at the end edge) and the start is logged with its reason.
        void {class_name}::beginEncounterWith(std::vector<Phyxel::Core::CombatDirector::Combatant> combatants,
                                               const std::string& reason) {{
            if (combatants.empty()) return;
            clickToMove_.cancel();
            encounterHostiles_.clear();
            for (const auto& c : combatants) if (!c.isPlayerSide) encounterHostiles_.push_back(c.entityId);
            Phyxel::Core::DiceSystem dice;
            combatDirector_.beginEncounter(combatants, dice);
            faceCombatants();   // square off — everyone faces the enemy
            LOG_INFO("{class_name}", "Combat encounter started: {{}} combatants ({{}})", combatants.size(), reason);
        }}

        // The fight the PLAYER or a SIGHTING starts (G-137): the engaged hostile plus
        // every living hostile within EncounterInitiator::kGroupRadius of it, against the
        // player and the party. The engaged one is the selected target.
        bool {class_name}::startEncounterAgainst(const std::string& npcId, const std::string& reason) {{
            if (combatDirector_.inCombat() || !npcManager_ || !entityRegistry_) return false;
            std::vector<Phyxel::Core::AggroCandidate> cands;
            glm::vec3 anchorPos{{0.0f}}; bool found = false;
            npcManager_->forEachNPC([&](Phyxel::Scene::NPCEntity& n) {{
                Phyxel::Core::AggroCandidate c;
                c.id = "npc_" + n.getName(); c.pos = n.getPosition(); c.hostile = n.isHostile();
                const auto* hc = n.getHealthComponent(); c.alive = !hc || hc->isAlive();
                if (c.id == npcId) {{ anchorPos = c.pos; found = true; }}
                cands.push_back(std::move(c));
            }});
            if (!found) {{ LOG_WARN("{class_name}", "engage '{{}}': no such NPC", npcId); return false; }}
            const auto group = Phyxel::Core::EncounterInitiator::gatherGroup(
                npcId, anchorPos, Phyxel::Core::EncounterInitiator::kGroupRadius, cands);
            if (group.empty()) {{ LOG_INFO("{class_name}", "engage '{{}}': not a living hostile", npcId); return false; }}
            std::vector<Phyxel::Core::CombatDirector::Combatant> combatants;
            {{
                Phyxel::Core::CombatDirector::Combatant c;
                c.entityId = "player"; c.isPlayerSide = true; c.initiativeBonus = 1; c.speed = 30;
                combatants.push_back(c);
            }}
            for (const auto& m : rpgParty_.getMembers()) {{
                if (!m.isAlive || !entityRegistry_->getEntity(m.entityId)) continue;
                Phyxel::Core::CombatDirector::Combatant c;
                c.entityId = m.entityId; c.isPlayerSide = true; c.initiativeBonus = 1; c.speed = 30;
                combatants.push_back(c);
            }}
            for (const auto& id : group) {{
                Phyxel::Core::CombatDirector::Combatant c;
                c.entityId = id; c.isPlayerSide = false; c.initiativeBonus = 0; c.speed = 30;
                combatants.push_back(c);
            }}
            beginEncounterWith(std::move(combatants), reason);
            playerTurn_.setSelectedTarget(npcId);
            return true;
        }}

        void {class_name}::onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) {{
            lastDt_ = dt;  // remembered for menu-scene animations rendered in onRender

            // Opt-in standalone test API (dev/test only, `--test`). Start it lazily on
            // the first frame — by now onInitialize has wired every subsystem the API
            // exposes — then drain queued commands each frame (cheap no-op when off).
            if (engine.getConfig().testApiEnabled && !testApiRunning())
                startTestApi(engine, engine.getConfig().apiPort, "{class_name}");
            pumpTestApi();

            // Pump scene transitions every frame, in EVERY screen state — menu-scene
            // buttons and triggers set the transition; this advances it.
            if (auto* sceneMgr = engine.getSceneManager()) {{
                sceneMgr->update(dt);
                // G-115 settle hold: keep the loading screen up after the load until the
                // remesh backlog drains (>= 0.5 s so it never flashes, <= 4 s regardless).
                if (loadingHold_ >= 0.0f) {{
                    loadingHold_ += dt;
                    auto* cm = engine.getChunkManager();
                    const size_t dirty = cm ? cm->dirtyTracker().getDirtyCount() : 0;
                    const bool settled = (loadingHold_ >= 0.5f && dirty == 0) || loadingHold_ >= 4.0f;
                    if (settled) {{
                        LOG_INFO("{class_name}", "Loading screen dismissed (settled after {{}} ms, {{}} dirty chunks left)",
                                 static_cast<int>(loadingHold_ * 1000.0f), dirty);
                        loadingHold_ = -1.0f;
                        if (screen_.getState() == Phyxel::UI::ScreenState::Loading)
                            screen_.setState(menuSceneActive_ ? Phyxel::UI::ScreenState::MainMenu
                                                              : Phyxel::UI::ScreenState::Playing);
                        updateCursorMode(engine);
                    }}
                }}
            }}

            // A menu scene is showing: SceneManager + the menu renderer drive things;
            // skip world/gameplay simulation — but keep the menuWorld camera
            // orbiting (the living title screen).
            if (menuSceneActive_) {{
                if (menuCamPath_.isPlaying()) {{
                    if (auto* cam = engine.getCamera()) menuCamPath_.update(dt, *cam);
                }}
                return;
            }}
            if (menuCamPath_.isPlaying()) menuCamPath_.stop();   // gameplay owns the camera now

            if (!Phyxel::UI::isGameRunning(screen_.getState())) return;

            elapsed_ += dt;

            auto* physics = engine.getPhysicsWorld();
            if (physics) physics->stepSimulation(dt);

            // GameShell drives the gameplay camera + character control: resolves
            // the rig/scheme from the active scene's camera block (re-resolving
            // after transitions), samples input, moves the body, frames the
            // camera. See docs/CameraControlSystem.md.
            if (playerCharacter_) {{
                // During turn-based combat the TurnActor owns movement: the camera
                // keeps framing the player (tactical rig) but WASD is suppressed.
                // Same during dialogue — the speakers hold their mutual facing
                // (camera-coupled facing would stomp it every frame).
                const bool talking = dialogueSystem_ && dialogueSystem_->isActive();
                if (auto* scheme = gameplayCamera().scheme()) scheme->pointerClicks = pointerMode_;
                // G-123: in combat the body is not driven, but a mouse DRAG still orbits the
                // tactical camera (never during a conversation - the speakers hold their framing).
                gameplayCamera().setLookWhileNotDriving(combatDirector_.inCombat() && !talking);
                // G-150: while the UI is DRAGGING something, the held mouse button belongs
                // to that drag - with the MMO scheme either button held is the look
                // gesture, so dragging a spell icon across the screen also orbited the
                // camera.
                {{
                    auto* dragUi = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr;
                    gameplayCamera().setLookSuppressed(dragUi && dragUi->dragActive());
                }}
                // A click-to-move walk owns the body until it ends; WASD cancels it.
                updateGameplayCamera(engine, dt, playerCharacter_,
                                     /*driveCharacter=*/!combatDirector_.inCombat() && !talking &&
                                                        !clickToMove_.active());

                // Gameplay events -> declarative triggers (win conditions).
                if (playerCharacter_->consumeJustJumped()) triggers_.onEvent("player_jumped");
                if (playerCharacter_->consumeJustLanded()) triggers_.onEvent("player_landed");
            }}

            // Turn-based combat: enemy AI + the player-turn controller tick every
            // frame; both no-op unless an encounter is active. (Mirrors the editor
            // loop — Application.cpp ~3540; the player turn self-binds in tick.)
            combatAI_.tick(dt);
            if (playerCharacter_ && entityRegistry_) {{
                const std::string pid = entityRegistry_->getEntityId(playerCharacter_);
                playerTurn_.setPlayerEntityId(pid);
                combatAI_.setPlayerEntityId(pid);   // companions auto-fight; only the human waits
            }}
            // TACTICAL CAMERA FRAMING: point the combat camera at whoever is
            // ACTING, not permanently at the player. Without this an enemy
            // taking its turn 20 units away happens off-screen and the fight
            // is impossible to follow. The rig anchors between the player and
            // the actor and widens to hold both.
            if (auto* trig = dynamic_cast<Phyxel::Graphics::TacticalRig*>(gameplayCamera().rig())) {{
                bool framed = false;
                if (combatDirector_.inCombat() && entityRegistry_) {{
                    const std::string acting = combatDirector_.currentEntityId();
                    if (!acting.empty()) {{
                        if (auto* e = entityRegistry_->getEntity(acting)) {{
                            // The player's own turn frames the player (weight
                            // 0 keeps the familiar over-the-shoulder framing);
                            // anyone else's turn pulls the camera their way.
                            const bool isPlayer = playerCharacter_ &&
                                                  entityRegistry_->getEntityId(playerCharacter_) == acting;
                            trig->setFocus(e->getPosition(), isPlayer ? 0.0f : 0.5f);
                            framed = true;
                        }}
                    }}
                }}
                if (!framed) trig->clearFocus();
            }}

            // Caster level tracks the live sheet (cantrip dice scale at 5/11/17).
            // Save DC stays the controller default 13 = 8 + prof 2 + mod 3, the
            // standard level-1 full caster (5e PHB math) — deriving it from the
            // sheet's casting ability is a follow-up.
            playerTurn_.setCasterLevel(playerSheet_.totalLevel());
            playerTurn_.tick(dt);
            {{
                const bool talkingNow = dialogueSystem_ && dialogueSystem_->isActive();
                auto* inp = engine.getInputManager();
                const bool moveKey = inp && (inp->isActionPressed("MoveForward") || inp->isActionPressed("MoveBackward") ||
                                             inp->isActionPressed("MoveLeft") || inp->isActionPressed("MoveRight"));
                if (clickToMove_.active() && (talkingNow || combatDirector_.inCombat() || moveKey)) {{
                    clickToMove_.cancel();   // a conversation, an encounter or the keyboard owns the body
                    LOG_INFO("ClickToMove", "walk cancelled ({{}})", talkingNow ? "dialogue" : combatDirector_.inCombat() ? "combat" : "WASD");
                }}
                clickToMove_.tick(dt);
            }}
            if (clickToMove_.lastResult() != lastWalkResult_) {{
                lastWalkResult_ = clickToMove_.lastResult();
                if (lastWalkResult_ != Phyxel::Core::ClickToMove::Result::Walking && playerCharacter_) {{
                    const glm::vec3 pp = playerCharacter_->getPosition();
                    LOG_INFO("ClickToMove", "walk {{}} at ({{}}, {{}}, {{}}) after {{}} m",
                             Phyxel::Core::ClickToMove::resultName(lastWalkResult_), pp.x, pp.y, pp.z,
                             clickToMove_.walked());
                }}
            }}

            // BG3-style tactical camera: entering combat swaps to the authored
            // combat rig (default overhead birds-eye) and frees the cursor for
            // click-targeting; leaving combat restores the scene's rig. (WASD
            // suppression happens at the updateGameplayCamera call above via
            // driveCharacter=false.)
            const bool inCombatNow = combatDirector_.inCombat();
            if (inCombatNow != wasInCombat_) {{
                // Turn-based combat owns every body on the field: suspend NPC
                // behaviors for the encounter (a live Follow/patrol behavior
                // writes velocity every frame and stalls its TurnActor turn —
                // the L4 probe caught Bram's follow deadzone freezing the
                // whole encounter). Resumed on the exit edge.
                if (npcManager_)
                    npcManager_->forEachNPC([inCombatNow](Phyxel::Scene::NPCEntity& n) {{
                        n.setBehaviorSuspended(inCombatNow);
                    }});
                if (inCombatNow) {{
                    clickToMove_.cancel();   // the TurnActor owns the body now
                    preCombatRig_ = gameplayCamera().rigName();
                    if (gameplayCamera().setRigByName(combatCameraRig_))
                        LOG_INFO("{class_name}", "Tactical camera: '{{}}' (combat)", combatCameraRig_);
                    // Enter at a good tactical angle regardless of where the
                    // player happened to be looking. Mouse-look is suppressed
                    // in combat (the capture fix), so without this the whole
                    // fight inherits an arbitrary exploration pitch.
                    if (auto* look = engine.getInputManager())
                        look->setYawPitch(look->getYaw(), -52.0f);
                    // Spellbar: one button per authored spell (progression.spells).
                    // Click ARMS the spell (ember highlight via the per-element bg
                    // override); the next enemy click casts it. Rebuilt fresh each
                    // encounter, torn down on the exit edge.
                    // The code-built spell bar retired 2026-09-15: spells are rows of the
                    // data-driven action bar (hud_action_bar) now.
                    if (false && !playerSpells_.empty() && renderCoordinator_) {{
                        if (auto* uisys = renderCoordinator_->getUISystem()) {{
                            uisys->removeScreen("hud_spellbar");
                            // The root panel is sized to EXACTLY the bar strip:
                            // a freeLayout UIPanel consumes every click inside
                            // its bounds (modal semantics), so a fullscreen
                            // transparent root would eat all combat clicks —
                            // it ate the cast clicks on the first probe run.
                            // Vertical stack on the right edge, below the
                            // Initiative panel and clear of the action panel +
                            // item hotbar. 220px wide fits the longest SRD-ish
                            // name ("Sacred Flame", 12 chars at 16px/char).
                            const float bw = 220.0f, bh = 34.0f, gap = 8.0f;
                            const float totalH = (bh + gap) * playerSpells_.size() - gap;
                            auto bar = std::make_unique<Phyxel::UI::UIPanel>();
                            bar->id = "hud_spellbar";        // the layout lint names panels by id
                            bar->freeLayout = true;
                            bar->showBackground = false;
                            // Bottom-right, above the action band (health/action bar live in the
                            // bottom 120 px) and clear of the top-right initiative panel.
                            bar->anchor = Phyxel::UI::Anchor::BottomRight;
                            bar->offset = {{-20.0f, -128.0f}};
                            bar->size = {{bw, totalH}};
                            float y = 0.0f;
                            for (const auto& sid : playerSpells_) {{
                                auto btn = std::make_unique<Phyxel::UI::UIButton>();
                                const auto* sdef = Phyxel::Core::SpellRegistry::instance().getSpell(sid);
                                btn->id = "spell_" + sid;
                                btn->text = sdef ? sdef->name : sid;
                                btn->position = {{0.0f, y}};
                                btn->size = {{bw, bh}};
                                btn->onClick = [this, sid] {{ setArmedSpell(armedSpell_ == sid ? "" : sid); }};
                                bar->addChild(std::move(btn));
                                y += bh + gap;
                            }}
                            uisys->addScreen("hud_spellbar", std::move(bar));
                            uisys->showScreen("hud_spellbar");
                            refreshSpellbar();   // slot counts / disabled state from frame one
                        }}
                    }}
                }} else {{
                    const std::string back = preCombatRig_.empty() ? "third_person" : preCombatRig_;
                    // No look snapshot/restore here anymore: the old "pitch
                    // scrambled to +89 during the tactical phase" defect was the
                    // InputManager mouseCaptured leak, fixed at the root in
                    // GameplayCameraController (capture now releases while the
                    // controller isn't driving, so free-cursor click-targeting
                    // never integrates into look). Pinned by
                    // GameplayCameraControllerTest.
                    if (gameplayCamera().setRigByName(back))
                        LOG_INFO("{class_name}", "Camera restored: '{{}}' (combat over)", back);
                    if (renderCoordinator_)
                        if (auto* uisys = renderCoordinator_->getUISystem())
                            uisys->removeScreen("hud_spellbar");
                    armedSpell_.clear();
                    // The encounter can also end WITHOUT a last kill: the last hostile
                    // ESCAPED (morale flee, CombatAISystem G-31). Quest triggers gate
                    // on combat_victory, so emit it here too — once per encounter, and
                    // only if the player side is still standing.
                    auto* playerHc = playerCharacter_ ? playerCharacter_->getHealthComponent() : nullptr;
                    if (!victoryEmitted_ && !combatDirector_.playerSideWiped() &&
                        (!playerHc || playerHc->isAlive())) {{
                        victoryEmitted_ = true;
                        LOG_INFO("{class_name}", "Encounter over — the last hostile escaped");
                        triggers_.onEvent("combat_victory", {{{{"last_kill", ""}}, {{"reason", "enemies_escaped"}}}});
                    }}
                }}
                if (inCombatNow) victoryEmitted_ = false;   // enter edge: arm for this encounter
                if (!inCombatNow) {{
                    // Whoever on the enemy side is still alive ESCAPED (G-31): beaten, not
                    // brave — it keeps its distance for kFledCooldownSec, then may re-engage
                    // on sight; a click on it is always a fight (G-137).
                    const double now = glfwGetTime();
                    for (const auto& id : encounterHostiles_) {{
                        auto* e = entityRegistry_ ? entityRegistry_->getEntity(id) : nullptr;
                        const auto* hc = e ? e->getHealthComponent() : nullptr;
                        if (e && (!hc || hc->isAlive())) {{
                            encounterInitiator_.noteFled(id, now);
                            LOG_INFO("{class_name}", "'{{}}' escaped the encounter: no re-engagement on sight for {{}} s", id,
                                     static_cast<int>(Phyxel::Core::EncounterInitiator::kFledCooldownSec));
                        }}
                    }}
                    encounterHostiles_.clear();
                    pendingOpeningCast_.clear();
                }}
                wasInCombat_ = inCombatNow;
                if (engine_) updateCursorMode(*engine_);
            }}

            // Advance trigger timers/regions and run fired actions (gameplay only —
            // timers do not tick while paused or in menus).
            triggers_.update(dt, [this](const std::string& id, glm::vec3& out) -> bool {{
                if (id == "player" && playerCharacter_) {{
                    out = playerCharacter_->getPosition();
                    return true;
                }}
                return false;
            }});

            if (npcManager_) npcManager_->update(dt);

            // SIGHTINGS (G-137): out of combat, a hostile that sees the player (within
            // EncounterInitiator::kSightRange, eye line clear through the voxels) starts
            // the fight itself; an escapee waits out its cooldown first. Scanned 4x/s.
            aggroClock_ += dt;
            if (!combatDirector_.inCombat() && npcManager_ && playerCharacter_ && aggroClock_ >= 0.25f &&
                Phyxel::UI::isGameRunning(screen_.getState()) && !(dialogueSystem_ && dialogueSystem_->isActive())) {{
                aggroClock_ = 0.0f;
                auto* cm = engine.getChunkManager();
                const glm::vec3 me = playerCharacter_->getPosition();
                const double now = glfwGetTime();
                auto blocked = [cm](const glm::vec3& a, const glm::vec3& b) -> bool {{
                    if (!cm) return false;
                    const glm::vec3 d = b - a; const float len = glm::length(d);
                    if (len < 1e-3f) return false;
                    return Phyxel::Utils::marchVoxels(a, d / len, len, [cm](const glm::ivec3& c) {{ return cm->hasVoxelAt(c); }}).hit;
                }};
                std::string spotter;
                npcManager_->forEachNPC([&](Phyxel::Scene::NPCEntity& n) {{
                    if (!spotter.empty() || !n.isHostile()) return;
                    const auto* hc = n.getHealthComponent();
                    if (hc && !hc->isAlive()) return;
                    const std::string id = "npc_" + n.getName();
                    if (!encounterInitiator_.mayEngage(id, now)) return;
                    if (Phyxel::Core::EncounterInitiator::notices(n.getPosition(), me, Phyxel::Core::EncounterInitiator::kSightRange, blocked))
                        spotter = id;
                }});
                if (!spotter.empty()) startEncounterAgainst(spotter, spotter + " spotted the player");
            }}
            // OPENING CAST (G-137): a spell armed out of combat is the first action of the
            // fight it started, released as soon as the player's turn binds.
            if (!pendingOpeningCast_.empty() && combatDirector_.inCombat() &&
                playerTurn_.isPlayerTurnActive() && !playerTurn_.isBusy()) {{
                const std::string spell = pendingOpeningCast_, tgt = playerTurn_.selectedTarget();
                pendingOpeningCast_.clear();
                const bool ok = !tgt.empty() && playerTurn_.castSpell(spell, tgt);
                LOG_INFO("{class_name}", "Opening cast '{{}}' at '{{}}': {{}}", spell, tgt,
                         ok ? "ok" : playerTurn_.castBlockedReason(spell).c_str());
                setArmedSpell("");
            }}
            updateCommand(dt);   // officers re-evaluate on their own cadence
            updateHeldWeapons(); // weapons ride the grip bone; without this they
                                 // stay frozen where the character spawned
            if (storyEngine_) storyEngine_->update(dt);
            if (speechBubbleManager_) speechBubbleManager_->update(dt);

            // AI decision log clock (separate from the engine log — it records
            // WHY each combatant chose what it chose; see combat_log.jsonl and
            // POST /api/rpg/combat/log).
            Phyxel::Core::CombatLog::instance().tick(dt);

            // VFX: the director drains queued casts into the particle system
            // and integrates it. WITHOUT this the standalone spawns spell
            // effects that never tick and never draw — the VfxDirector logged
            // "Cast 'guiding_bolt' ... (1 emissions)" while the screen showed
            // nothing at all. (Editor parity: Application.cpp ~3436.)
            if (renderCoordinator_) renderCoordinator_->updateVfx(dt);

            // Update interaction manager with player position. Front is
            // EXPLICITLY zero: the engine's default playerFront is +Z, which
            // silently arms a north-facing 90° view cone — any NPC standing
            // SOUTH of the player was uninteractable (the Hearthvale "Bram
            // won't talk" hunt). Under camera-coupled facing a view cone is
            // meaningless anyway; BG3-style proximity interact = nearest NPC
            // in radius, any direction.
            if (interactionManager_ && playerCharacter_) {{
                interactionManager_->update(dt, playerCharacter_->getPosition(), glm::vec3(0.0f));
            }}

            if (dialogueSystem_) dialogueSystem_->update(dt);
        }}

        void {class_name}::onRender(Phyxel::Core::EngineRuntime& engine) {{
            auto* window = engine.getWindowManager();
            if (window && window->isMinimized()) return;

            auto* imgui = engine.getImGuiRenderer();
            if (imgui) {{
                imgui->newFrame();

                auto state = screen_.getState();

                // Data-driven screen overlays (pause / intro / victory / credits)
                // replace the old ImGui ScreenState screens (docs/HudSystem.md §11a).
                // Map the state to a "<name>:*" UISystem overlay, (un)load it on change,
                // and drive its clicks. They render with the rest of the UISystem in
                // renderCoordinator_->render() below (pause = a scrim over the frozen
                // world; intro/victory/credits = full-screen). A sceneType:"menu" scene
                // owns the screen itself, so no ScreenState overlay while one is active.
                if (renderCoordinator_) {{
                    if (auto* ui = renderCoordinator_->getUISystem()) {{
                        std::string want;
                        if (!menuSceneActive_) {{
                            switch (state) {{
                                case Phyxel::UI::ScreenState::MainMenu: want = "mainmenu"; break;
                                case Phyxel::UI::ScreenState::Paused:   want = "pause";    break;
                                case Phyxel::UI::ScreenState::Intro:    want = "intro";    break;
                                case Phyxel::UI::ScreenState::Victory:  want = "victory";  break;
                                case Phyxel::UI::ScreenState::Credits:  want = "credits";  break;
                                case Phyxel::UI::ScreenState::GameOver: want = "game_over"; break;
                                case Phyxel::UI::ScreenState::Settings: want = "settings"; break;
                                case Phyxel::UI::ScreenState::Loading:  want = "loading";  break;
                                case Phyxel::UI::ScreenState::Character: want = "character"; break;
                                default: break;
                            }}
                        }}
                        if (want != activeDataScreen_) {{
                            if (activeDataScreen_ == "pause") Phyxel::UI::unloadPauseMenuFrom(*ui);
                            else if (!activeDataScreen_.empty()) Phyxel::UI::unloadGameScreenFrom(*ui, activeDataScreen_);
                            if (!want.empty()) {{
                                Phyxel::UI::MenuActions a;
                                a.onResolveVariable = [this](const std::string& t) -> std::optional<std::string> {{
                                    if (t == "title")   return std::string("{class_name}");
                                    if (t == "tagline") return std::string("{game_tagline}");
                                    if (t == "loading_target") return loadingSceneName_;
                                    if (t.rfind("sheet.", 0) == 0) return characterSheetToken(t.substr(6));
                                    // {{keybind.<Action>}} -> current key for the keybindings sub-panel rows.
                                    if (t.rfind("keybind.", 0) == 0) {{
                                        const auto* b = settings_.findBinding(t.substr(8));
                                        if (!b) {{
                                            // Not in settings.json (an older file) - the live input map still has the default.
                                            auto* im = engine_ ? engine_->getInputManager() : nullptr;
                                            const int k = im ? im->getActionKey(t.substr(8)) : 0;
                                            return k > 0 ? Phyxel::Core::keyToString(k) : std::string("(unbound)");
                                        }}
                                        std::string s = Phyxel::Core::keyToString(b->key);
                                        if (b->modifiers) s = Phyxel::Core::modifiersToString(b->modifiers) + "+" + s;
                                        return s;
                                    }}
                                    return std::nullopt;
                                }};
                                a.onResume      = [this]() {{ screen_.resume();           if (engine_) updateCursorMode(*engine_); }};
                                a.onStartGame   = [this]() {{
                                    // New Game while a WORLD scene is active (after Game Over -> Main Menu,
                                    // or from the pause menu) restarts the run; at boot it just starts.
                                    auto* sm = engine_ ? engine_->getSceneManager() : nullptr;
                                    const auto* active = sm ? sm->getActiveScene() : nullptr;
                                    if (active && active->sceneType != Phyxel::Core::SceneType::Menu) newGame();
                                    else screen_.startGame();
                                    if (engine_) updateCursorMode(*engine_);
                                }};
                                a.onSettings    = [this]() {{ screen_.toggleSettings();   if (engine_) updateCursorMode(*engine_); }};
                                a.onMainMenu    = [this]() {{ screen_.returnToMainMenu(); if (engine_) updateCursorMode(*engine_); }};
                                a.onShowCredits = [this]() {{ screen_.showCredits(); }};
                                a.onQuit        = [this]() {{ auto* w = engine_ ? engine_->getWindowManager() : nullptr; if (w) glfwSetWindowShouldClose(w->getHandle(), GLFW_TRUE); }};
                                // Settings "Back": persist + return to the previous screen.
                                a.onBack = [this]() {{
                                    settings_.saveToFile("settings.json");
                                    screen_.goBack();
                                    if (engine_) updateCursorMode(*engine_);
                                }};
                                // Settings widgets read/apply GameSettings (changes apply live;
                                // saved on Back). Standard set: Graphics / Audio / Controls.
                                a.onGetSetting = [this](const std::string& k) -> float {{
                                    if (k == "fov")              return settings_.fov;
                                    if (k == "masterVolume")     return settings_.masterVolume;
                                    if (k == "musicVolume")      return settings_.musicVolume;
                                    if (k == "sfxVolume")        return settings_.sfxVolume;
                                    if (k == "mouseSensitivity") return settings_.mouseSensitivity;
                                    if (k == "fullscreen")       return settings_.fullscreen ? 1.0f : 0.0f;
                                    if (k == "vsync")            return static_cast<float>(settings_.vsync);
                                    if (k == "resolution") {{
                                        if (settings_.resolutionWidth <= 1280) return 0.0f;
                                        if (settings_.resolutionWidth >= 1920) return 2.0f;
                                        return 1.0f;
                                    }}
                                    if (k == "brightness") return renderCoordinator_ ? renderCoordinator_->getAmbientLightStrength() : settings_.brightness;
                                    if (k == "invertY")    return settings_.invertY ? 1.0f : 0.0f;
                                    if (k == "aiProvider") {{
                                        if (settings_.aiProvider == "openai") return 1.0f;
                                        if (settings_.aiProvider == "ollama") return 2.0f;
                                        return 0.0f;  // anthropic
                                    }}
                                    return 0.0f;
                                }};
                                a.onSetSetting = [this](const std::string& k, float v) {{
                                    auto* win = engine_ ? engine_->getWindowManager() : nullptr;
                                    auto* cam = engine_ ? engine_->getCamera() : nullptr;
                                    if (k == "fov")                   {{ settings_.fov = v; if (cam) cam->setZoom(v); }}
                                    else if (k == "masterVolume")     {{ settings_.masterVolume = v; applyAudioSettings(); }}
                                    else if (k == "musicVolume")      {{ settings_.musicVolume = v; applyAudioSettings(); }}
                                    else if (k == "sfxVolume")        {{ settings_.sfxVolume = v; applyAudioSettings(); }}
                                    else if (k == "mouseSensitivity") {{ settings_.mouseSensitivity = v; if (cam) cam->setMouseSensitivity(v); }}
                                    else if (k == "fullscreen")       {{ settings_.fullscreen = (v > 0.5f); if (win) win->setFullscreen(settings_.fullscreen); }}
                                    else if (k == "vsync") {{
                                        int mode = static_cast<int>(v + 0.5f);
                                        settings_.vsync = static_cast<Phyxel::Core::VSyncMode>(mode);
                                        auto* dev = engine_ ? engine_->getVulkanDevice() : nullptr;
                                        if (dev) {{
                                            VkPresentModeKHR pm = VK_PRESENT_MODE_IMMEDIATE_KHR;
                                            if (mode == 1) pm = VK_PRESENT_MODE_FIFO_KHR;
                                            else if (mode == 2) pm = VK_PRESENT_MODE_MAILBOX_KHR;
                                            dev->setPreferredPresentMode(pm);
                                        }}
                                    }}
                                    else if (k == "resolution") {{
                                        int idx = static_cast<int>(v + 0.5f);
                                        int w = 1600, h = 900;
                                        if (idx == 0) {{ w = 1280; h = 720; }}
                                        else if (idx == 2) {{ w = 1920; h = 1080; }}
                                        settings_.resolutionWidth = w; settings_.resolutionHeight = h;
                                        if (win) win->setSize(w, h);
                                    }}
                                    else if (k == "brightness") {{ settings_.brightness = v; if (renderCoordinator_) renderCoordinator_->setAmbientLightStrength(v); }}
                                    else if (k == "invertY")    {{ settings_.invertY = (v > 0.5f); auto* in = engine_ ? engine_->getInputManager() : nullptr; if (in) in->setInvertY(settings_.invertY); }}
                                    else if (k == "aiProvider") {{
                                        const char* provs[] = {{ "anthropic", "openai", "ollama" }};
                                        int idx = static_cast<int>(v + 0.5f); if (idx < 0 || idx > 2) idx = 0;
                                        settings_.aiProvider = provs[idx];
                                        {{ Phyxel::AI::LLMConfig cfg; cfg.provider = settings_.aiProvider; cfg.model = settings_.aiModel; cfg.apiKey = settings_.aiApiKey; if (aiConversationService_) aiConversationService_->setLLMConfig(cfg); }}
                                    }}
                                }};
                                // String settings: AI model + API key (free-text fields).
                                a.onGetSettingText = [this](const std::string& k) -> std::string {{
                                    if (k == "aiModel")  return settings_.aiModel;
                                    if (k == "aiApiKey") return settings_.aiApiKey;
                                    return std::string();
                                }};
                                a.onSetSettingText = [this](const std::string& k, const std::string& v) {{
                                    if (k == "aiModel")       settings_.aiModel = v;
                                    else if (k == "aiApiKey") settings_.aiApiKey = v;
                                    {{ Phyxel::AI::LLMConfig cfg; cfg.provider = settings_.aiProvider; cfg.model = settings_.aiModel; cfg.apiKey = settings_.aiApiKey; if (aiConversationService_) aiConversationService_->setLLMConfig(cfg); }}
                                }};
                                // Keybinding rebind: a "kb_<Action>" key button starts UISystem key
                                // capture; the captured key writes GameSettings + the live InputManager
                                // action map and refreshes the row label. ESC restores the old binding.
                                a.onRebindKey = [this](const std::string& action) {{
                                    if (!renderCoordinator_) return;
                                    auto* ui = renderCoordinator_->getUISystem();
                                    auto* in = engine_ ? engine_->getInputManager() : nullptr;
                                    if (!ui || !in) return;
                                    auto setRowText = [ui, action](const std::string& s) {{
                                        if (auto* p = ui->getScreen("settings:keybindings"))
                                            if (auto* w = p->findChild("kb_" + action))
                                                if (w->type() == Phyxel::UI::WidgetType::Button)
                                                    static_cast<Phyxel::UI::UIButton*>(w)->text = s;
                                    }};
                                    auto bindingLabel = [this](const std::string& act) -> std::string {{
                                        const auto* b = settings_.findBinding(act);
                                        if (!b) return std::string("(unbound)");
                                        std::string s = Phyxel::Core::keyToString(b->key);
                                        if (b->modifiers) s = Phyxel::Core::modifiersToString(b->modifiers) + "+" + s;
                                        return s;
                                    }};
                                    setRowText("< press a key >");
                                    ui->beginKeyCapture(
                                        [this, in, action, setRowText, bindingLabel](int key, int mods) {{
                                            settings_.setBinding(action, key, mods);
                                            in->bindAction(action, key, mods);
                                            settings_.saveToFile("settings.json");
                                            setRowText(bindingLabel(action));
                                        }},
                                        [action, setRowText, bindingLabel]() {{ setRowText(bindingLabel(action)); }});
                                }};
                                if (want == "pause") Phyxel::UI::loadPauseMenuInto(*ui, a);
                                else                 Phyxel::UI::loadGameScreenInto(*ui, want, a);
                            }}
                            activeDataScreen_ = want;
                        }}
                        if (!want.empty()) ui->handleInput(engine.getInputManager());
                    }}
                }}

                // A sceneType:"menu" scene owns the screen. It renders via the UISystem
                // (custom-Vulkan, inside renderCoordinator_->render()) — no ImGui menu.
                // Drive UISystem input so menu buttons are clickable.
                if (menuSceneActive_) {{
                    if (renderCoordinator_) {{
                        if (auto* ui = renderCoordinator_->getUISystem())
                            ui->handleInput(engine.getInputManager());
                    }}
                    imgui->endFrame();
                    if (renderCoordinator_) renderCoordinator_->render();
                    return;
                }}

                switch (state) {{
                case Phyxel::UI::ScreenState::Intro:
                case Phyxel::UI::ScreenState::Victory:
                case Phyxel::UI::ScreenState::Credits:
                case Phyxel::UI::ScreenState::GameOver:
                    // Intro / Victory / Credits are now data-driven "intro:*" /
                    // "victory:*" / "credits:*" UISystem overlays loaded + driven by the
                    // reconcile above (no ImGui). Enter Victory from gameplay with
                    // screen_.showVictory() when your win condition is met. Nothing to
                    // draw here. (docs/HudSystem.md §11a.)
                    break;

                case Phyxel::UI::ScreenState::MainMenu:
                    // Fallback title screen is now the data-driven "mainmenu:*" UISystem
                    // overlay loaded + driven by the reconcile above (no ImGui). Games
                    // with a sceneType:"menu" start scene render that menu scene instead.
                    // (docs/HudSystem.md §11a.)
                    break;

                case Phyxel::UI::ScreenState::Playing:
                    // Gameplay: the data-driven HUD (health/hotbar/objectives/combat/
                    // countdown) renders via the UISystem in renderCoordinator_->render().
                    // Timer-trigger countdowns now bind to the hud_countdown panel
                    // (countdown.active/countdown.text providers) — no ImGui. (§11a.)
                    break;

                case Phyxel::UI::ScreenState::Paused:
                    // Pause menu is now the data-driven "pause:*" UISystem overlay
                    // loaded/driven above (no ImGui) — nothing to draw here.
                    break;

                case Phyxel::UI::ScreenState::Settings:
                    // Settings is now the data-driven "settings:*" UISystem overlay
                    // loaded + driven by the reconcile above (no ImGui). Standard
                    // Graphics/Audio/Controls + keybinding rebind (keybindings
                    // sub-panel, onRebindKey), brightness, invert-Y, AI settings.
                    // Changes apply live + save on Back. (docs/HudSystem.md §11a.)
                    break;

                default:
                    break;
                }}

                // Dialogue UI is now FULLY data-driven (no ImGui): standard trees use
                // the hud_dialogue panel; AI conversations use the hud_ai_dialogue panel
                // (history + text field, wired by setupAIDialogue). During an AI
                // conversation, drive UISystem input so the text field captures typed
                // characters + Enter. (docs/HudSystem.md §11a.)
                if (dialogueSystem_ && dialogueSystem_->isActive() &&
                    dialogueSystem_->isAIConversation() && renderCoordinator_) {{
                    if (auto* ui = renderCoordinator_->getUISystem())
                        ui->handleInput(engine.getInputManager());
                }}

                // Speech bubbles + "[E] Interact" prompt: data-driven world-anchored
                // labels on the UISystem (no ImGui). Project each world position to
                // screen and queue a label; the UISystem draws them in
                // renderCoordinator_->render() below. Only while actively PLAYING —
                // not while paused/menus (the pause overlay suppresses the HUD; world
                // prompts must go too — feedback #11). (docs/HudSystem.md §11a.)
                if (state == Phyxel::UI::ScreenState::Playing && renderCoordinator_) {{
                    if (auto* ui = renderCoordinator_->getUISystem()) {{
                        // G-148: per-frame HOVER for HUD tooltips. handleInput is NOT called
                        // while playing (the HUD would eat gameplay keys and clicks), so
                        // without this nothing ever updates hover and the icon-only action
                        // bar could never tell you what its icons are. Hover consumes
                        // nothing. Cursor from the message-fed cache, like the click path.
                        //
                        // G-150: while the ABILITIES PANEL is open, drive the FULL input
                        // pass instead - a drag needs press, move and release, which hover
                        // alone cannot see. The HUD eating input is fine and expected in
                        // that window: the player opened a panel to rearrange things.
                        if (spellbookOpen_) {{
                            ui->handleInput(engine.getInputManager());
                        }} else if (auto* hin = engine.getInputManager()) {{
                            double hx = 0.0, hy = 0.0;
                            hin->getCurrentMousePosition(hx, hy);
                            ui->injectHover({{static_cast<float>(hx), static_cast<float>(hy)}});
                        }}
                        auto* win = engine.getWindowManager();
                        float sw = win ? static_cast<float>(win->getWidth()) : 1280.0f;
                        float sh = win ? static_cast<float>(win->getHeight()) : 720.0f;
                        // ABSOLUTE view, not the cached RENDER view: the render
                        // matrix is camera-relative (eye at origin) for float
                        // precision, so projecting absolute world positions
                        // through it puts everything behind the camera and
                        // every world overlay silently vanishes. This affected
                        // speech bubbles and the "[E] Interact" prompt too.
                        const glm::mat4 view = renderCoordinator_->getWorldViewMatrix();
                        const auto& proj = renderCoordinator_->getCachedProjectionMatrix();
                        glm::vec2 sp;
                        if (speechBubbleManager_) {{
                            for (const auto& b : speechBubbleManager_->getBubbles()) {{
                                glm::vec3 wp = speechBubbleManager_->getBubbleWorldPosition(b);
                                float op = speechBubbleManager_->getBubbleOpacity(b);
                                if (Phyxel::UI::UISystem::worldToScreen(wp, view, proj, sw, sh, sp))
                                    ui->addWorldLabel(sp, b.text, glm::vec4(1.0f, 1.0f, 1.0f, op), 0.85f * op);
                            }}
                        }}
                        // No interact prompts during combat (G-120): the fight's plates and
                        // readouts own that space, and E/F do nothing useful mid-encounter.
                        if (interactionManager_ && interactionManager_->shouldShowPrompt() &&
                            !combatDirector_.inCombat() &&
                            (!dialogueSystem_ || !dialogueSystem_->isActive())) {{
                            if (auto* npc = interactionManager_->getNearestInteractableNPC()) {{
                                std::string txt = interactionManager_->getActivePromptText();
                                // The prompt names the BOUND interact key (F under the WoW scheme,
                                // E otherwise) - a hardcoded "[E]" lied after the 2026-09-15 rebinding.
                                const std::string keyName = "[" + Phyxel::Core::keyToString(
                                    engine_ && engine_->getInputManager() ? engine_->getInputManager()->getActionKey("Interact") : GLFW_KEY_E) + "]";
                                if (txt.empty()) txt = keyName + " Interact";
                                else if (auto pos = txt.find("[E]"); pos != std::string::npos) txt.replace(pos, 3, keyName);
                                else if (txt[0] != '[') txt = keyName + " " + txt;   // authored text without a key marker
                                // Stack the prompt ABOVE the nameplate (bar on the head anchor at +2.15 m,
                                // name above it) instead of on top of it.
                                if (Phyxel::UI::UISystem::worldToScreen(npc->getPosition() + glm::vec3(0.0f, 2.15f, 0.0f), view, proj, sw, sh, sp)) {{
                                    sp.y -= 62.0f;   // above the readout / name / bar column
                                    ui->addWorldLabel(sp, txt, glm::vec4(1.0f, 1.0f, 0.6f, 1.0f), 0.8f);
                                }}
                            }}
                        }}

                        // COMBAT NAMEPLATES: name + health bar over every
                        // combatant, a targeting readout (AC / hit chance /
                        // reach) on the one under the cursor, and a selection
                        // bracket on the current target. The tactical view
                        // shows figures; without these you cannot tell who is
                        // who, who is hurt, or who you are about to hit.
                        if (combatDirector_.inCombat() && entityRegistry_) {{
                            const std::string sel = playerTurn_.selectedTarget();
                            const std::string hov = hoveredTarget_;
                            int npQueued = 0, npProjFail = 0, npNoEntity = 0;
                            for (const auto& p : combatDirector_.initiative().turnOrder()) {{
                                auto* e = entityRegistry_->getEntity(p.entityId);
                                if (!e) {{ ++npNoEntity; continue; }}
                                auto* hc = e->getHealthComponent();
                                if (hc && !hc->isAlive()) continue;
                                if (!Phyxel::UI::UISystem::worldToScreen(
                                        e->getPosition() + glm::vec3(0.0f, 2.15f, 0.0f),
                                        view, proj, sw, sh, sp)) {{ ++npProjFail; continue; }}

                                Phyxel::UI::UISystem::Nameplate np;
                                np.screenPos = sp;
                                // Strip the "npc_" prefix — players read names.
                                np.name = p.entityId.rfind("npc_", 0) == 0
                                              ? p.entityId.substr(4) : p.entityId;
                                np.hpFrac = (hc && hc->getMaxHealth() > 0.0f)
                                              ? hc->getHealth() / hc->getMaxHealth() : 1.0f;
                                np.hostile  = !p.isPlayer;
                                np.selected = (p.entityId == sel || p.entityId == hov);
                                // G-105 / G-120: the target ring the user asked for - "a bright animated circle on
                                // the ground". Drawn as 36 dots projected from a 0.55 m circle at the feet
                                // through the UI (the debug line pass never drew in the shipped build);
                                // the dots rotate and pulse. Gold = selected/hovered foe, blue = whoever acts.
                                {{
                                    const bool acting = (p.entityId == combatDirector_.initiative().currentEntityId());
                                    if (np.selected || acting) {{
                                    const float t = static_cast<float>(glfwGetTime());
                                    const float pulse = 0.65f + 0.35f * std::sin(t * 5.0f);
                                    const glm::vec4 col = np.selected ? glm::vec4(1.0f, 0.82f, 0.30f, pulse)
                                                                      : glm::vec4(0.55f, 0.80f, 1.0f, pulse * 0.8f);
                                    const glm::vec3 feet = e->getPosition() + glm::vec3(0.0f, 0.06f, 0.0f);
                                    // G-138: the ring reads as ON THE GROUND only if the body hides the
                                    // part of it that passes behind: a dot is dropped when the
                                    // camera->dot sight line crosses any combatant's body capsule
                                    // (its own included). The UI has no depth buffer; this is the test.
                                    const glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
                                    std::vector<Phyxel::Utils::BodyCapsule> bodies;
                                    for (const auto& q : combatDirector_.initiative().turnOrder()) {{
                                        auto* qe = entityRegistry_ ? entityRegistry_->getEntity(q.entityId) : nullptr;
                                        const auto* qh = qe ? qe->getHealthComponent() : nullptr;
                                        if (qe && (!qh || qh->isAlive())) bodies.push_back({{qe->getPosition(), 1.8f, 0.3f}});
                                    }}
                                    for (int k2 = 0; k2 < 36; ++k2) {{
                                        const float a = glm::radians(k2 * 10.0f) + t * 1.2f;
                                        const glm::vec3 wp = feet + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * 0.55f;
                                        bool hidden = false;
                                        for (const auto& bc : bodies)
                                            if (Phyxel::Utils::segmentHitsCapsule(camPos, wp, bc)) {{ hidden = true; break; }}
                                        if (hidden) continue;
                                        glm::vec2 dp;
                                        if (Phyxel::UI::UISystem::worldToScreen(wp, view, proj, sw, sh, dp))
                                            ui->addWorldMarker(dp, np.selected ? 5.0f : 4.0f, col);
                                    }}
                                    }}
                                }}
                                // Distance falloff so a far plate does not
                                // shout as loudly as the one in your face.
                                float d = playerCharacter_
                                    ? glm::length(e->getPosition() - playerCharacter_->getPosition())
                                    : 10.0f;
                                np.scale = glm::clamp(1.35f - d * 0.025f, 0.75f, 1.15f);
                                // Targeting readout on the hovered/selected foe.
                                if (!p.isPlayer && np.selected) {{
                                    const int pct = static_cast<int>(
                                        playerTurn_.hitChanceVs(p.entityId) * 100.0f);
                                    np.subtitle = "AC " + std::to_string(playerTurn_.targetAC(p.entityId)) +
                                                  "  " + std::to_string(pct) + "% to hit  " +
                                                  (playerTurn_.inReachOf(p.entityId) ? "[in reach]"
                                                                                     : "[out of reach]");
                                }}
                                ui->addNameplate(np);
                                ++npQueued;
                            }}
                            // Throttled diagnostic: if plates are invisible, this
                            // says whether they were queued at all and where the
                            // projection went (screen coords of the last one).
                            if (++nameplateDiagFrame_ % 120 == 0)
                                LOG_INFO("{class_name}",
                                         "nameplates: queued={{}} projFail={{}} noEntity={{}} lastPx=({{}},{{}}) screen={{}}x{{}}",
                                         npQueued, npProjFail, npNoEntity,
                                         static_cast<int>(sp.x), static_cast<int>(sp.y),
                                         static_cast<int>(sw), static_cast<int>(sh));
                        }}

                        // REAL-TIME NAMEPLATES. The block above walks the
                        // CombatDirector's initiative order, so it draws nothing
                        // in a real-time battle — there is no encounter and no
                        // turn order. The battle sim therefore never showed a
                        // single health bar, which is the one thing you actually
                        // need to read a 200-body melee: who is hurt, and which
                        // side they are on.
                        //
                        // Iterates live entities instead of initiative, and is
                        // bounded for BOTH cost and legibility: 400 plates is an
                        // unreadable wall of text, so only combatants within
                        // kRtPlateRange of the CAMERA (not the player — the sim
                        // camera is detached and flies free of it) get one, and
                        // names are dropped past kRtNameRange so distant ranks
                        // are bars only.
                        else if (entityRegistry_ && engine_ && engine_->getCamera()) {{
                            constexpr float kRtPlateRange = 42.0f;   // bars within this
                            constexpr float kRtNameRange  = 20.0f;   // names within this
                            constexpr int   kRtPlateCap   = 80;      // hard cap, nearest-first
                            const glm::vec3 eye = engine_->getCamera()->getPosition();

                            // The id comes from the REGISTRY KEY — Scene::Entity has no
                            // getId(); the registry is what knows an entity's id.
                            struct RtPlate {{ float d; Phyxel::Scene::Entity* e; std::string id; }};
                            std::vector<RtPlate> near;
                            near.reserve(128);
                            for (const char* type : {{"npc", "animated"}}) {{
                                for (const auto& [id, e] : entityRegistry_->getEntitiesByType(type)) {{
                                    if (!e) continue;
                                    auto* hc = e->getHealthComponent();
                                    if (!hc || !hc->isAlive()) continue;   // dead men wear no plates
                                    const float d = glm::length(e->getPosition() - eye);
                                    if (d > kRtPlateRange) continue;
                                    near.push_back({{d, e, id}});
                                }}
                            }}
                            // Nearest-first, then cap: when a brawl overflows the
                            // budget the plates you keep are the ones you can see.
                            std::sort(near.begin(), near.end(),
                                      [](const RtPlate& a, const RtPlate& b) {{ return a.d < b.d; }});
                            if (static_cast<int>(near.size()) > kRtPlateCap)
                                near.resize(kRtPlateCap);

                            // Read the player's faction through the Entity base: the concrete
                            // character classes declare their own protected `faction` member,
                            // which shadows Entity::faction() at the derived type.
                            const auto* playerEnt =
                                static_cast<const Phyxel::Scene::Entity*>(playerCharacter_);
                            const std::string myFaction = playerEnt
                                ? playerEnt->faction() : std::string(Phyxel::Scene::Entity::kNeutralFaction);
                            int rtQueued = 0;
                            for (const auto& rp : near) {{
                                if (!Phyxel::UI::UISystem::worldToScreen(
                                        rp.e->getPosition() + glm::vec3(0.0f, 2.15f, 0.0f),
                                        view, proj, sw, sh, sp)) continue;
                                auto* hc = rp.e->getHealthComponent();
                                Phyxel::UI::UISystem::Nameplate np;
                                np.screenPos = sp;
                                if (rp.d <= kRtNameRange)
                                    np.name = rp.id.rfind("npc_", 0) == 0 ? rp.id.substr(4) : rp.id;
                                np.hpFrac = (hc && hc->getMaxHealth() > 0.0f)
                                              ? hc->getHealth() / hc->getMaxHealth() : 1.0f;
                                // Colour by SIDE, not by "is it an enemy of the
                                // camera": the spectator is neutral, so relative
                                // hostility would paint both armies the same.
                                np.hostile = rp.e->faction() != myFaction;
                                np.selected = (rp.id == hoveredTarget_);
                                np.scale = glm::clamp(1.25f - rp.d * 0.018f, 0.6f, 1.1f);
                                ui->addNameplate(np);
                                ++rtQueued;
                            }}
                            if (++nameplateDiagFrame_ % 180 == 0)
                                LOG_INFO("{class_name}",
                                         "rt nameplates: queued={{}} inRange={{}} eye=({{}},{{}},{{}})",
                                         rtQueued, static_cast<int>(near.size()),
                                         static_cast<int>(eye.x), static_cast<int>(eye.y),
                                         static_cast<int>(eye.z));
                        }}
                    }}
                }}

                imgui->endFrame();
            }}

            if (renderCoordinator_) renderCoordinator_->render();
        }}

        // ── Editor-parity persistence + audio (StandaloneParityGaps.md §1) ──────

        void {class_name}::applyAudioSettings() {{
            auto* audio = engine_ ? engine_->getAudioSystem() : nullptr;
            if (!audio) return;
            audio->setChannelVolume(Phyxel::Core::AudioChannel::Master, settings_.masterVolume);
            audio->setChannelVolume(Phyxel::Core::AudioChannel::Music,  settings_.musicVolume);
            audio->setChannelVolume(Phyxel::Core::AudioChannel::SFX,    settings_.sfxVolume);
        }}

        // ── Facing (dialogue + combat juice) ────────────────────────────────────
        // Characters that talk or fight LOOK at each other. Convention matches
        // CharacterTurnBody: model faces +Z at yaw 0, yaw = atan2(dx, dz).

        void {class_name}::faceToward(Phyxel::Scene::AnimatedVoxelCharacter* ch, const glm::vec3& at) {{
            if (!ch) return;
            const glm::vec3 from = ch->getPosition();
            const float dx = at.x - from.x, dz = at.z - from.z;
            if (dx * dx + dz * dz < 0.01f) return;   // on top of each other — keep facing
            ch->setFacingYaw(std::atan2(dx, dz));
        }}

        // ====================================================================
        // DOCTRINE — how this game's officers decide (game-side policy)
        // ====================================================================
        // The engine owns squads, order propagation and the "officer is dead"
        // degradation; WHICH order gets issued is a game decision, so it lives
        // here. Swap this function and the same armies fight a different war.
        // ====================================================================
        // VISIBLE NPC WEAPONS
        // ====================================================================
        // game.json "weapon" named a damage/animation profile and nothing else,
        // so an entire army fought bare-handed while 22 weapon models sat in
        // resources/templates/weapons/. Ported from the editor's NPC held-item
        // path (Application.cpp): resolve the item, build a kinematic mesh,
        // attach it to the character's grip bone, then follow that bone.
        //
        // NOTE: no profile->id mapping here any more. GameDefinitionLoader
        // canonicalises the weapon name when it calls setWeapon(), so the id on
        // the behavior is ALREADY a real ItemRegistry id. A second mapping in
        // this file could drift from the engine's and produce the exact split
        // that caused fighters to carry swords while throwing punches: the
        // visible model resolved, the moveset did not.

        void {class_name}::equipNpcWeapons() {{
            if (!npcManager_ || !kinematicVoxelManager_ || !weaponTemplates_) return;
            int equipped = 0, missingItem = 0, missingTemplate = 0, noGrip = 0;

            npcManager_->forEachNPC([&](Phyxel::Scene::NPCEntity& npc) {{
                auto* ch = npc.getAnimatedCharacter();
                if (!ch) return;

                // The weapon profile lives on whichever combat behavior this NPC has.
                std::string profile;
                if (auto* cb = dynamic_cast<Phyxel::Scene::CombatBehavior*>(npc.getBehavior()))
                    profile = cb->getWeaponId();
                if (profile.empty()) return;

                const std::string itemId = profile;   // already canonical (see loader)
                const auto* def = Phyxel::Core::ItemRegistry::instance().getItem(itemId);
                if (!def) {{ ++missingItem; return; }}

                // Load on first use, keyed by item id so repeats are free.
                //
                // items.json stores templateFile RELATIVE to resources/templates
                // ("weapons/sword_long.voxel"), while loadTemplate resolves
                // against the working directory. Passing it through unprefixed
                // failed all 120 fighters with noTemplate while the files sat
                // right there on disk.
                const Phyxel::VoxelTemplate* tmpl = weaponTemplates_->getTemplate(itemId);
                if (!tmpl) {{
                    const std::string full = "resources/templates/" + def->templateFile;
                    if (!weaponTemplates_->loadTemplate(full, itemId))
                        weaponTemplates_->loadTemplate(def->templateFile, itemId);  // absolute/bare fallback
                    tmpl = weaponTemplates_->getTemplate(itemId);
                }}
                if (!tmpl) {{
                    if (missingTemplate == 0)
                        LOG_WARN("{class_name}", "weapon template not loadable: {{}} (item {{}})",
                                 def->templateFile, itemId);
                    ++missingTemplate;
                    return;
                }}

                auto voxels = Phyxel::Core::ItemPropManager::voxelsFromTemplate(*tmpl);
                if (voxels.empty()) {{ ++missingTemplate; return; }}

                // BAKE held.scale, exactly as ItemPropManager::buildPropGeometry
                // does. voxelsFromTemplate returns RAW template geometry; for a
                // fine-voxel item ("# grid: 81") that is ~81x too big. Skipping
                // this drew a sword the size of a house next to each fighter -
                // the attachment was fine, the geometry was unusable.
                const float heldScale = def->held.scale > 0.0f ? def->held.scale : 1.0f;
                if (heldScale != 1.0f)
                    for (auto& v : voxels) {{ v.localPos *= heldScale; v.scale *= heldScale; }}

                // NPCManager registers entities as "npc_" + name; key the same
                // way so updateHeldWeapons can find the owner again.
                const std::string entityId = npc.getName();
                HeldWeapon hw;
                hw.kinId = kinematicVoxelManager_->add("npcheld_" + entityId, std::move(voxels));
                hw.anchorId = ch->attachToBone(def->held.gripBone, glm::vec3(0.02f),
                                               def->held.gripOffset, glm::vec4(0.0f),
                                               "npc_held_anchor");
                if (hw.anchorId < 0) {{
                    // No grip bone on this rig: drop the mesh rather than leave
                    // a weapon floating at the world origin.
                    kinematicVoxelManager_->remove(hw.kinId);
                    ++noGrip;
                    return;
                }}
                hw.itemId = itemId;
                npcHeld_[entityId] = hw;
                ++equipped;
            }});

            LOG_INFO("{class_name}",
                     "NPC weapons: equipped={{}} unknownItem={{}} noTemplate={{}} noGripBone={{}}",
                     equipped, missingItem, missingTemplate, noGrip);
        }}

        void {class_name}::updateHeldWeapons() {{
            if (npcHeld_.empty() || !kinematicVoxelManager_ || !npcManager_) return;
            for (auto it = npcHeld_.begin(); it != npcHeld_.end(); ) {{
                auto* npc = npcManager_->getNPC(it->first);   // keyed by NPC NAME
                auto* ch = npc ? npc->getAnimatedCharacter() : nullptr;
                const auto* hd = Phyxel::Core::ItemRegistry::instance().getItem(it->second.itemId);
                if (!ch || !hd) {{
                    // Owner gone: take its weapon with it, or the mesh is
                    // orphaned mid-air for the rest of the battle.
                    kinematicVoxelManager_->remove(it->second.kinId);
                    it = npcHeld_.erase(it);
                    continue;
                }}
                glm::vec3 pos; glm::quat rot;
                const bool gotXform = ch->getAttachmentTransform(it->second.anchorId, pos, rot);
                // WHERE IS THE WEAPON, ACTUALLY? "equipped=N" only proves the
                // attach call returned an id. If getAttachmentTransform fails we
                // never call setTransform and the mesh stays wherever add() put
                // it - i.e. nowhere near the hand. Report the owner's position
                // beside the weapon's so the two can be compared directly.
                {{
                    static int s_heldTick = 0;
                    if ((++s_heldTick % 300) == 1) {{
                        const glm::vec3 owner = ch->getPosition();
                        LOG_INFO("{class_name}",
                                 "held '{{}}': xform={{}} weapon=({{}},{{}},{{}}) owner=({{}},{{}},{{}})",
                                 it->second.itemId, gotXform ? "ok" : "FAIL",
                                 static_cast<int>(pos.x), static_cast<int>(pos.y),
                                 static_cast<int>(pos.z),
                                 static_cast<int>(owner.x), static_cast<int>(owner.y),
                                 static_cast<int>(owner.z));
                    }}
                }}
                if (gotXform) {{
                    const auto& h = hd->held;
                    glm::mat4 t = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
                    t = t * glm::eulerAngleYXZ(glm::radians(h.gripEulerDeg.y),
                                               glm::radians(h.gripEulerDeg.x),
                                               glm::radians(h.gripEulerDeg.z));
                    kinematicVoxelManager_->setTransform(it->second.kinId, t);
                }}
                ++it;
            }}
        }}

        void {class_name}::installDoctrine() {{
            command_.setDecisionInterval(3.0f);
            command_.setDoctrine([](const Phyxel::AI::CommandStructure::SquadSituation& s) {{
                using Order = Phyxel::AI::CommandStructure::Order;
                const float strength = s.strength > 0
                    ? static_cast<float>(s.alive) / s.strength : 1.0f;

                // Shattered or badly bloodied: break off.
                if (strength <= 0.34f || s.healthFraction < 0.3f) return Order::FallBack;
                // Hurt but holding together: dig in and make them come.
                if (strength <= 0.6f || s.healthFraction < 0.55f) return Order::Hold;
                // Strong and the enemy is close: try to take them in the side.
                if (s.nearestEnemyDist < 26.0f && strength > 0.8f) return Order::Flank;
                return Order::Advance;
            }});
        }}

        // Per-frame command tick: build each squad's situation from live
        // entities, let the doctrine issue orders, and log the changes so a
        // battle's command decisions are readable after the fact.
        void {class_name}::updateCommand(float dt) {{
            if (!entityRegistry_) return;
            command_.update(dt,
                [this](const Phyxel::AI::CommandStructure::Squad& sq) {{
                    Phyxel::AI::CommandStructure::SquadSituation sit;
                    sit.strength = static_cast<int>(sq.members.size());
                    glm::vec3 sum(0.0f);
                    float hpSum = 0.0f;
                    for (const auto& id : sq.members) {{
                        auto* e = entityRegistry_->getEntity(id);
                        if (!e) continue;
                        auto* hc = e->getHealthComponent();
                        if (!hc || !hc->isAlive()) continue;
                        ++sit.alive;
                        sum += e->getPosition();
                        hpSum += (hc->getMaxHealth() > 0.0f)
                                     ? hc->getHealth() / hc->getMaxHealth() : 1.0f;
                    }}
                    if (sit.alive > 0) {{
                        sit.centre = sum / static_cast<float>(sit.alive);
                        sit.healthFraction = hpSum / sit.alive;
                    }}
                    // Nearest hostile + enemy centre of mass.
                    glm::vec3 esum(0.0f);
                    int ecount = 0;
                    for (const char* type : {{"animated", "npc"}}) {{
                        for (const auto& [id, e] : entityRegistry_->getEntitiesByType(type)) {{
                            if (!e) continue;
                            auto* hc = e->getHealthComponent();
                            if (!hc || !hc->isAlive()) continue;
                            if (e->faction() == sq.faction) continue;
                            if (e->faction() == Phyxel::Scene::Entity::kNeutralFaction) continue;
                            esum += e->getPosition(); ++ecount;
                            const float d = glm::length(e->getPosition() - sit.centre);
                            if (d < sit.nearestEnemyDist) sit.nearestEnemyDist = d;
                            if (d < 30.0f) ++sit.enemiesNear;
                        }}
                    }}
                    if (ecount > 0) sit.enemyCentre = esum / static_cast<float>(ecount);
                    return sit;
                }},
                [](const Phyxel::AI::CommandStructure::Squad& sq,
                   Phyxel::AI::CommandStructure::Order o) {{
                    LOG_INFO("Command", "squad '{{}}' ({{}}) -> {{}}", sq.id, sq.faction,
                             Phyxel::AI::CommandStructure::orderName(o));
                }});
        }}

        // ====================================================================
        // Behavior-tree ACTION VOCABULARY (game-side, no engine changes)
        // ====================================================================
        // These are the verbs this GAME offers to JSON-authored behavior trees.
        // They live here, not in the engine, which is the point: adding a new
        // kind of fighter should be a game-side edit (a ~2 minute build) or
        // pure data, never an engine rebuild.
        //
        // Author a behavior in game.json / a .bt.json file:
        //   {{"type":"Selector","children":[
        //      {{"type":"Action","action":"flee_below","hp":0.3}},
        //      {{"type":"Action","action":"keep_distance","range":14}},
        //      {{"type":"Action","action":"cast_at_enemy","spell":"fire_bolt",
        //        "cooldown":2.0,"damage":9}},
        //      {{"type":"Action","action":"charge_enemy","speed":1.0}}]}}
        void {class_name}::registerBehaviorActions() {{
            auto& reg = Phyxel::AI::BTActionRegistry::instance();

            // Nearest hostile (faction-aware) — the shared helper the verbs use.
            auto nearestFoe = [](Phyxel::AI::ActionContext& ctx) -> Phyxel::Scene::Entity* {{
                if (!ctx.entityRegistry || !ctx.self) return nullptr;
                Phyxel::Scene::Entity* best = nullptr;
                float bestD2 = 1e9f;
                for (const char* type : {{"animated", "npc"}}) {{
                    for (const auto& [id, e] : ctx.entityRegistry->getEntitiesByType(type)) {{
                        if (!e || e == ctx.self) continue;
                        auto* hc = e->getHealthComponent();
                        if (!hc || !hc->isAlive()) continue;
                        if (!ctx.self->hostileTo(*e)) continue;
                        const glm::vec3 d = e->getPosition() - ctx.self->getPosition();
                        const float d2 = d.x * d.x + d.z * d.z;
                        if (d2 < bestD2) {{ bestD2 = d2; best = e; }}
                    }}
                }}
                return best;
            }};
            auto charOf = [](Phyxel::Scene::Entity* e) -> Phyxel::Scene::AnimatedVoxelCharacter* {{
                if (auto* npc = dynamic_cast<Phyxel::Scene::NPCEntity*>(e)) return npc->getAnimatedCharacter();
                return dynamic_cast<Phyxel::Scene::AnimatedVoxelCharacter*>(e);
            }};

            // charge_enemy: close on the nearest foe and swing in reach.
            reg.add("charge_enemy", [nearestFoe, charOf](const nlohmann::json& p) {{
                const float speed = p.value("speed", 1.0f);
                const float reach = p.value("reach", 2.0f);
                return Phyxel::AI::makeAction("charge_enemy",
                    [nearestFoe, charOf, speed, reach](float, Phyxel::AI::ActionContext& ctx) {{
                        auto* foe = nearestFoe(ctx);
                        auto* me  = charOf(ctx.self);
                        if (!foe || !me) return Phyxel::AI::ActionStatus::Failure;
                        const glm::vec3 selfPos = ctx.self->getPosition();
                        const glm::vec3 foePos  = foe->getPosition();
                        glm::vec3 to = foePos - selfPos; to.y = 0.0f;
                        const float d = glm::length(to);
                        if (d > 1e-4f) me->setFacingYaw(std::atan2(to.x / d, to.z / d));

                        // A WALL BETWEEN US IS NOT A TARGET IN REACH. This used
                        // to be a bare distance test, so two fighters on
                        // opposite faces of a wall were each "within reach" of
                        // the other and both stood there swinging at stone —
                        // the thin-air punching — while a besieging horde
                        // stalled against a rampart instead of finding the gate.
                        const bool blocked =
                            ctx.chunkManager &&
                            !Phyxel::AI::TacticalSpace::canSee(*ctx.chunkManager, selfPos, foePos);

                        // Telemetry, because "the fix did nothing" and "the fix
                        // never ran" look identical from outside and have cost
                        // four measurement rounds. A null chunkManager here
                        // means every wall check silently no-ops.
                        {{
                            static int s_chargeTick = 0;
                            if ((++s_chargeTick % 600) == 0)
                                LOG_INFO("{class_name}",
                                         "charge_enemy: world={{}} blocked={{}} d={{}}",
                                         ctx.chunkManager ? "yes" : "NULL",
                                         blocked ? 1 : 0, static_cast<int>(d));
                        }}

                        if (d <= reach && !blocked) {{
                            me->setControlInput(0.0f, 0.0f, 0.0f);
                            me->lightAttack();
                        }} else if (blocked) {{
                            // Slide ALONG the obstacle rather than into it. No
                            // pathfinder is reachable from a BT action, so this
                            // is deliberately a local rule: skirting a wall
                            // finds a gate eventually, standing at it never does.
                            const glm::vec3 dir = (d > 1e-4f) ? to / d : glm::vec3(1, 0, 0);
                            const glm::vec3 side(-dir.z, 0.0f, dir.x);
                            const glm::vec3 slide = glm::normalize(dir * 0.35f + side * 0.94f);
                            me->setFacingYaw(std::atan2(slide.x, slide.z));
                            me->setControlInput(-speed, 0.0f, 0.0f);
                        }} else {{
                            me->setControlInput(-speed, 0.0f, 0.0f);
                        }}
                        return Phyxel::AI::ActionStatus::Running;
                    }});
            }});

            // keep_distance: hold a stand-off band from the nearest foe.
            reg.add("keep_distance", [nearestFoe, charOf](const nlohmann::json& p) {{
                const float want  = p.value("range", 12.0f);
                const float speed = p.value("speed", 0.8f);
                return Phyxel::AI::makeAction("keep_distance",
                    [nearestFoe, charOf, want, speed](float, Phyxel::AI::ActionContext& ctx) {{
                        auto* foe = nearestFoe(ctx);
                        auto* me  = charOf(ctx.self);
                        if (!foe || !me) return Phyxel::AI::ActionStatus::Failure;
                        glm::vec3 to = foe->getPosition() - ctx.self->getPosition(); to.y = 0.0f;
                        const float d = glm::length(to);
                        if (d > 1e-4f) me->setFacingYaw(std::atan2(to.x / d, to.z / d));
                        if (d < want * 0.6f)      me->setControlInput(speed, 0.0f, 0.0f);   // back off
                        else if (d > want * 1.2f) me->setControlInput(-speed, 0.0f, 0.0f);  // close in
                        else                      me->setControlInput(0.0f, 0.0f, 0.0f);
                        return Phyxel::AI::ActionStatus::Running;
                    }});
            }});

            // cast_at_enemy: throw a spell on a cooldown, through the game's
            // own cast visual + the damage funnel.
            reg.add("cast_at_enemy", [this, nearestFoe, charOf](const nlohmann::json& p) {{
                const std::string spell = p.value("spell", "fire_bolt");
                const float cooldown    = p.value("cooldown", 2.0f);
                const float damage      = p.value("damage", 8.0f);
                const float range       = p.value("range", 30.0f);
                auto timer = std::make_shared<float>(0.0f);
                return Phyxel::AI::makeAction("cast_at_enemy",
                    [this, nearestFoe, charOf, spell, cooldown, damage, range, timer]
                    (float dt, Phyxel::AI::ActionContext& ctx) {{
                        if (*timer > 0.0f) *timer -= dt;
                        auto* foe = nearestFoe(ctx);
                        if (!foe) return Phyxel::AI::ActionStatus::Failure;
                        const glm::vec3 selfPos = ctx.self->getPosition();
                        const glm::vec3 foePos  = foe->getPosition();
                        glm::vec3 to = foePos - selfPos; to.y = 0.0f;
                        const float d = glm::length(to);
                        if (d > range) return Phyxel::AI::ActionStatus::Failure;

                        // WALLS STOP SPELLS. This was a bare distance test, so
                        // an archer sealed behind a rampart shot attackers
                        // straight through the stonework. Returning Failure (not
                        // Running) lets the Selector fall through to the next
                        // child — the caster repositions instead of standing
                        // there discharging into a wall.
                        if (ctx.chunkManager &&
                            !Phyxel::AI::TacticalSpace::canSee(*ctx.chunkManager, selfPos, foePos))
                            return Phyxel::AI::ActionStatus::Failure;

                        if (auto* me = charOf(ctx.self))
                            if (d > 1e-4f) me->setFacingYaw(std::atan2(to.x / d, to.z / d));
                        if (*timer <= 0.0f) {{
                            *timer = cooldown;
                            const std::string tid = ctx.entityRegistry
                                ? ctx.entityRegistry->getEntityId(foe) : std::string();
                            const std::string sid = ctx.selfId;
                            const glm::vec3 tp = foe->getPosition();
                            playCastVisualFor(charOf(ctx.self), spell, tp,
                                [this, foe, tid, sid, damage]() {{
                                    if (combatSystem_)
                                        combatSystem_->applyDamage(foe, tid, damage, sid,
                                                                   Phyxel::Core::DamageType::Fire);
                                }});
                        }}
                        return Phyxel::AI::ActionStatus::Running;
                    }});
            }});

            // flee_below: run from the nearest foe under an hp fraction.
            reg.add("flee_below", [nearestFoe, charOf](const nlohmann::json& p) {{
                const float frac  = p.value("hp", 0.3f);
                const float speed = p.value("speed", 1.0f);
                return Phyxel::AI::makeAction("flee_below",
                    [nearestFoe, charOf, frac, speed](float, Phyxel::AI::ActionContext& ctx) {{
                        auto* hc = ctx.self ? ctx.self->getHealthComponent() : nullptr;
                        if (!hc || hc->getMaxHealth() <= 0.0f) return Phyxel::AI::ActionStatus::Failure;
                        if (hc->getHealth() / hc->getMaxHealth() >= frac)
                            return Phyxel::AI::ActionStatus::Failure;   // not afraid yet
                        auto* foe = nearestFoe(ctx);
                        auto* me  = charOf(ctx.self);
                        if (!foe || !me) return Phyxel::AI::ActionStatus::Failure;
                        glm::vec3 away = ctx.self->getPosition() - foe->getPosition(); away.y = 0.0f;
                        const float d = glm::length(away);
                        if (d > 1e-4f) me->setFacingYaw(std::atan2(away.x / d, away.z / d));
                        me->setControlInput(-speed, 0.0f, 0.0f);
                        return Phyxel::AI::ActionStatus::Running;
                    }});
            }});

            LOG_INFO("{class_name}", "Behavior actions registered: {{}}",
                     static_cast<int>(reg.names().size()));
        }}

        // Arm/disarm a spellbar spell. The armed slot glows ember through the
        // per-element bg override; everything else reverts to the theme.
        // Refuses to arm a spell that can't be cast (no slots / not prepared),
        // reading the SAME castBlockedReason the cast path enforces.
        void {class_name}::setArmedSpell(const std::string& id) {{
            if (!id.empty()) {{
                const std::string why = playerTurn_.castBlockedReason(id);
                // "action spent" / "not your turn" are turn-flow states, not
                // spell states — arming ahead of your turn is fine.
                if (why == "no slots" || why == "not prepared" || why == "not known" ||
                    why == "unknown spell") {{
                    LOG_INFO("{class_name}", "Cannot arm '{{}}': {{}}", id, why);
                    refreshSpellbar();
                    return;
                }}
            }}
            armedSpell_ = id;
            if (!id.empty()) LOG_INFO("{class_name}", "Spell armed: '{{}}'", id);
            refreshSpellbar();
        }}

        // ── UNIFIED ACTION BAR (G-150) ──────────────────────────────────────
        // Fire whatever slot `slot` holds. One path for a click and for the keybind, so
        // the two can never drift apart.
        bool {class_name}::fireActionSlot(int slot) {{
            const auto& sl = playerProfile_.actionBar.at(slot);
            if (sl.empty()) return false;
            using K = Phyxel::Core::ActionSlot::Kind;
            if (sl.kind == K::Action && sl.id == "attack") {{
                std::string tgt = playerTurn_.selectedTarget();
                if (tgt.empty()) tgt = hoveredTarget_;
                if (tgt.empty()) {{
                    LOG_INFO("ActionBar", "Action bar: Attack with no target selected");
                    return false;
                }}
                playerTurn_.setSelectedTarget(tgt);
                const bool ok = playerTurn_.requestAttack(tgt);
                LOG_INFO("ActionBar", "Action bar slot {{}}: Attack -> '{{}}' ({{}})",
                         slot, tgt, ok ? "ok" : "refused");
                return ok;
            }}
            if (sl.kind == K::Action && sl.id == "end_turn") {{
                playerTurn_.endTurn();
                LOG_INFO("ActionBar", "Action bar slot {{}}: End Turn", slot);
                return true;
            }}
            if (sl.kind == K::Spell) {{
                setArmedSpell(armedSpell_ == sl.id ? "" : sl.id);
                return true;
            }}
            // Assignable and saved, but nothing can fire it yet - said out loud rather
            // than failing silently (docs/game-production/RavenmereGapLedger.md G-151).
            LOG_INFO("ActionBar", "Action bar slot {{}} holds item '{{}}' - items cannot be used yet",
                     slot, sl.id);
            return false;
        }}

        // First-run layout. Runs only while the bar has NEVER been arranged: once the
        // player touches it, the arrangement is theirs and is never rewritten - otherwise
        // clearing your last slot would silently refill the whole bar.
        void {class_name}::ensureDefaultActionBar() {{
            auto& bar = playerProfile_.actionBar;
            if (!bar.isDefault()) return;
            using K = Phyxel::Core::ActionSlot::Kind;
            const int last = Phyxel::Core::ActionBar::SLOT_COUNT - 1;
            bar.assign(0, K::Action, "attack");
            int next = 1;
            for (const auto& sid : playerSpells_) {{
                if (next >= last) break;            // keep the last slot for End Turn
                bar.assign(next++, K::Spell, sid);
            }}
            bar.assign(last, K::Action, "end_turn");
            LOG_INFO("ActionBar", "Default action bar laid out ({{}} of {{}} slots filled)",
                     bar.filledCount(), Phyxel::Core::ActionBar::SLOT_COUNT);
        }}

        // A drag landed. The record is the DROP TARGET's row (or an empty one when the
        // drag was released over nothing), carrying "_drag" = what was picked up.
        void {class_name}::onActionBarDrop(const Phyxel::UI::HudRecord& r) {{
            const auto dragIt = r.texts.find("_drag");
            if (dragIt == r.texts.end() || dragIt->second.empty()) return;
            const std::string drag = dragIt->second;
            const auto dropIt = r.texts.find("_drop");
            const bool onTarget = dropIt != r.texts.end() && dropIt->second == "target";
            const auto slotIt = r.floats.find("slot");
            const int to = slotIt != r.floats.end() ? static_cast<int>(slotIt->second) : -1;

            auto& bar = playerProfile_.actionBar;
            bool changed = false;
            if (drag.rfind("slot:", 0) == 0) {{
                const int from = std::atoi(drag.c_str() + 5);
                // Released off the bar entirely: that is how you clear a slot.
                if (!onTarget)      changed = bar.clear(from);
                else if (to != from) changed = bar.swap(from, to);
            }} else if (onTarget) {{
                const auto colon = drag.find(':');
                if (colon != std::string::npos) {{
                    changed = bar.assign(to,
                                         Phyxel::Core::ActionSlot::kindFromString(drag.substr(0, colon)),
                                         drag.substr(colon + 1));
                }}
            }}
            if (changed) {{
                LOG_INFO("ActionBar", "Bar drop: '{{}}' -> slot {{}} ({{}})",
                         drag, to, onTarget ? "assigned" : "cleared");
                savePlayerProfile();   // the arrangement is worthless if it does not persist
            }}
        }}

        // Repaint the spellbar from live state: armed = ember, out-of-slots =
        // dimmed + disabled, and each leveled spell shows its remaining slots.
        void {class_name}::refreshSpellbar() {{
            if (!renderCoordinator_) return;
            auto* uisys = renderCoordinator_->getUISystem();
            if (!uisys) return;
            auto* bar = uisys->getScreen("hud_spellbar");
            if (!bar) return;
            for (const auto& sid : playerSpells_) {{
                auto* w = bar->findChild("spell_" + sid);
                auto* b = w ? dynamic_cast<Phyxel::UI::UIButton*>(w) : nullptr;
                if (!b) continue;
                const auto* sd = Phyxel::Core::SpellRegistry::instance().getSpell(sid);
                const std::string why = playerTurn_.castBlockedReason(sid);
                const bool depleted = (why == "no slots" || why == "not prepared");
                std::string label = sd ? sd->name : sid;
                if (sd && !sd->isCantrip()) {{
                    const int lvl = sd->level;
                    const int left = (lvl >= 1 && lvl <= Phyxel::Core::SpellSlots::MAX_SPELL_LEVEL)
                                         ? playerCaster_.slots().remaining[lvl - 1] : 0;
                    label += " (" + std::to_string(left) + ")";   // remaining slots
                }}
                b->text = label;
                b->enabled = !depleted;
                b->customBg = (sid == armedSpell_ && !armedSpell_.empty())
                                  ? glm::vec4(0.85f, 0.55f, 0.20f, 1.0f)
                                  : glm::vec4(0.0f);
            }}
        }}

        // Cast visual for the player's spells: cast animation (SpellAnimMapper
        // family plan) + spell VFX, with damage/heal applied at the RELEASE
        // frame. Mirrors the editor host (Application::playCastVisual) so a
        // spell looks the same shipped as it does in the editor. Falls back to
        // immediate VFX+resolution when no animation plan is available.
        void {class_name}::playCastVisual(const std::string& spellId,
                                          const glm::vec3& targetPos,
                                          std::function<void()> onRelease) {{
            playCastVisualFor(playerCharacter_, spellId, targetPos, std::move(onRelease));
        }}

        void {class_name}::playCastVisualFor(Phyxel::Scene::AnimatedVoxelCharacter* caster,
                                             const std::string& spellId,
                                             const glm::vec3& targetPos,
                                             std::function<void()> onRelease) {{
            glm::vec3 origin = caster ? caster->getPosition() + glm::vec3(0.0f, 1.4f, 0.0f)
                                      : targetPos;
            Phyxel::VfxSpellModifiers mods;
            Phyxel::VfxCastContext ctx;
            ctx.caster = origin;
            ctx.targets.push_back(targetPos);
            auto fire = [this, spellId, mods, ctx, onRelease]() {{
                auto* d = renderCoordinator_ ? renderCoordinator_->getVfxDirector() : nullptr;
                if (d) d->cast(Phyxel::resolveSpellVfx(spellId, mods), ctx);
                if (onRelease) onRelease();
            }};
            bool animated = false;
            if (caster) {{
                auto& reg = Phyxel::Core::SpellRegistry::instance();
                auto& mapper = Phyxel::Core::SpellAnimMapper::instance();
                if (!mapper.isLoaded())
                    mapper.loadConfig("resources/spells/anim/spell_anim_families.json");
                const auto* def = reg.getSpell(spellId);
                if (def && mapper.isLoaded()) {{
                    auto plan = mapper.resolve(*def, 2, [caster](const std::string& clip) {{
                        for (const auto& c : caster->getAnimationClips())
                            if (c.name == clip) return c.duration;
                        return 0.0f;
                    }});
                    if (plan.valid) {{
                        std::vector<Phyxel::Scene::AnimatedVoxelCharacter::CastSegment> segs;
                        for (const auto& s : plan.segments) segs.push_back({{s.clip, s.speed, s.loops}});
                        glm::vec3 dd = targetPos - caster->getPosition();
                        if (glm::length(glm::vec2(dd.x, dd.z)) > 0.01f)
                            caster->setFacingYaw(std::atan2(dd.x, dd.z));
                        caster->setOnCastRelease(fire);
                        animated = caster->castSpell(segs);
                    }}
                }}
            }}
            if (!animated) fire();   // fallback: immediate VFX + resolution
        }}

        Phyxel::Scene::AnimatedVoxelCharacter* {class_name}::characterOf(const std::string& entityId) {{
            if (!entityRegistry_) return nullptr;
            auto* e = entityRegistry_->getEntity(entityId);
            if (!e) return nullptr;
            if (auto* npc = dynamic_cast<Phyxel::Scene::NPCEntity*>(e)) return npc->getAnimatedCharacter();
            return dynamic_cast<Phyxel::Scene::AnimatedVoxelCharacter*>(e);
        }}

        void {class_name}::faceCombatants() {{
            const auto& order = combatDirector_.initiative().turnOrder();
            for (const auto& me : order) {{
                auto* meCh = characterOf(me.entityId);
                if (!meCh) continue;
                float bestD2 = 1e30f;
                const Phyxel::Scene::AnimatedVoxelCharacter* target = nullptr;
                for (const auto& other : order) {{
                    if (other.isPlayer == me.isPlayer) continue;   // face the OPPOSING side
                    auto* oCh = characterOf(other.entityId);
                    if (!oCh) continue;
                    const glm::vec3 d = oCh->getPosition() - meCh->getPosition();
                    const float d2 = d.x * d.x + d.z * d.z;
                    if (d2 < bestD2) {{ bestD2 = d2; target = oCh; }}
                }}
                if (target) faceToward(meCh, target->getPosition());
            }}
        }}

        void {class_name}::grantXP(int xp, const char* why) {{
            if (playerSheet_.classes.empty()) return;   // no progression authored
            Phyxel::Core::DiceSystem dice;
            const int before = playerSheet_.totalLevel();
            const bool leveled = Phyxel::Core::CharacterProgression::awardXP(
                playerSheet_, xp, dice, /*autoLevel=*/true, /*useAverageHP=*/true);
            LOG_INFO("{class_name}", "+{{}} XP ({{}}) — total {{}} XP, level {{}}",
                     xp, why, playerSheet_.experiencePoints, playerSheet_.totalLevel());
            if (leveled) {{
                LOG_INFO("{class_name}", "LEVEL UP! {{}} -> {{}}", before, playerSheet_.totalLevel());
                triggers_.onEvent("player_level_up", {{{{"level", playerSheet_.totalLevel()}}}});
            }}
        }}

        void {class_name}::savePlayerProfile() {{
            auto* cm = engine_ ? engine_->getChunkManager() : nullptr;
            auto* ws = cm ? cm->m_streamingManager.getWorldStorage() : nullptr;
            if (!ws || !ws->getDb()) {{
                LOG_WARN("{class_name}", "savePlayerProfile: no world database open");
                return;
            }}
            if (auto* cam = engine_->getCamera()) {{
                playerProfile_.cameraPosition = cam->getPosition();
                playerProfile_.cameraYaw     = cam->getYaw();
                playerProfile_.cameraPitch   = cam->getPitch();
            }}
            if (auto* hc = playerCharacter_ ? playerCharacter_->getHealthComponent() : nullptr) {{
                playerProfile_.health    = hc->getHealth();
                playerProfile_.maxHealth = hc->getMaxHealth();
            }}
            if (!playerSheet_.classes.empty()) {{
                playerProfile_.xp    = playerSheet_.experiencePoints;
                playerProfile_.level = playerSheet_.totalLevel();
            }}
            playerProfile_.inventoryData = inventory_.toJson();
            playerProfile_.currencyData = {{{{"cp", wallet_.copper}}, {{"sp", wallet_.silver}}, {{"ep", wallet_.electrum}}, {{"gp", wallet_.gold}}, {{"pp", wallet_.platinum}}}};
            if (playerProfile_.saveToDb(ws->getDb())) {{
                LOG_INFO("{class_name}", "Player profile saved (player_state table)");
            }} else {{
                LOG_WARN("{class_name}", "Player profile save FAILED");
            }}
        }}

        bool {class_name}::loadPlayerProfile() {{
            auto* cm = engine_ ? engine_->getChunkManager() : nullptr;
            auto* ws = cm ? cm->m_streamingManager.getWorldStorage() : nullptr;
            if (!ws || !ws->getDb()) return false;
            if (!playerProfile_.loadFromDb(ws->getDb())) return false;
            if (auto* cam = engine_->getCamera()) {{
                cam->setPosition(playerProfile_.cameraPosition);
                cam->setYaw(playerProfile_.cameraYaw);
                cam->setPitch(playerProfile_.cameraPitch);
            }}
            if (auto* hc = playerCharacter_ ? playerCharacter_->getHealthComponent() : nullptr) {{
                hc->setMaxHealth(playerProfile_.maxHealth);
                hc->setHealth(playerProfile_.health);
            }}
            if (!playerProfile_.inventoryData.is_null() && !playerProfile_.inventoryData.empty())
                inventory_.fromJson(playerProfile_.inventoryData);
                // Legacy saves (before 2026-09-15) carried coins as inventory ITEMS: move them
                // to the wallet so an old profile never shows a "gold_crown x30" block again.
                for (const char* coinId : {{"gold_crown", "gold_crowns", "gold", "gp", "coins"}}) {{
                    const int n = inventory_.countItem(coinId);
                    if (n > 0) {{ inventory_.removeItem(coinId, n); wallet_.addCoins(0, 0, 0, n, 0);
                                 LOG_INFO("{class_name}", "Profile migration: {{}} x{{}} -> wallet ({{}})", coinId, n, wallet_.toString()); }}
                }}
                if (playerProfile_.currencyData.is_object() && !playerProfile_.currencyData.empty()) {{
                    const auto& c = playerProfile_.currencyData;
                    wallet_ = Phyxel::Core::Currency{{}};
                    wallet_.addCoins(c.value("cp", 0), c.value("sp", 0), c.value("ep", 0), c.value("gp", 0), c.value("pp", 0));
                }}
            // Progression restore: XP round-trips directly; LEVEL is rebuilt by
            // re-running levelUp so class HP/hit-dice accrue properly (average
            // HP, deterministic — matches how the XP was originally earned).
            if (!playerSheet_.classes.empty() && playerProfile_.xp > 0) {{
                playerSheet_.experiencePoints = playerProfile_.xp;
                Phyxel::Core::DiceSystem dice;
                while (playerSheet_.totalLevel() < playerProfile_.level) {{
                    auto res = Phyxel::Core::CharacterProgression::levelUp(
                        playerSheet_, playerSheet_.classes[0].classId, dice, /*useAverageHP=*/true);
                    if (!res.success) break;
                }}
                LOG_INFO("{class_name}", "Progression restored: {{}} XP, level {{}}",
                         playerSheet_.experiencePoints, playerSheet_.totalLevel());
            }}
            return true;
        }}

        void {class_name}::onShutdown() {{
            LOG_INFO("{class_name}", "Shutting down...");
            // Release any cursor grab FIRST — quitting from Playing otherwise
            // tears the window down while it holds GLFW_CURSOR_DISABLED, which
            // can leave the OS cursor confined/hidden until the desktop refocuses.
            if (engine_ && engine_->getWindowManager())
                engine_->getWindowManager()->setCursorVisible(true);
            stopTestApi();  // stop the test API before tearing down subsystems it references
            savePlayerProfile();  // quit-save: profile -> active scene's world DB
            settings_.saveToFile("settings.json");
            renderCoordinator_.reset();
            entities_.clear();
            playerCharacter_ = nullptr;
            aiConversationService_.reset();
            interactionManager_.reset();
            speechBubbleManager_.reset();
            dialogueSystem_.reset();
            storyEngine_.reset();
            npcManager_.reset();
            entityRegistry_.reset();
        }}
    """)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Scaffold a new Phyxel game project."
    )
    parser.add_argument("name", help="Project/class name (e.g. MyAwesomeGame)")
    parser.add_argument(
        "--output", "-o",
        help="Output directory (default: Documents/PhyxelProjects/<name>)",
        default=None,
    )
    parser.add_argument(
        "--game-definition", "-g",
        help="Path to a game definition JSON file to generate from",
        default=None,
    )
    parser.add_argument(
        "--force", "-f",
        help="Regenerate the scaffold into an existing project directory "
             "(overwrites the generated C++/CMake files; back up local edits)",
        action="store_true",
    )
    args = parser.parse_args()

    phyxel_root = Path(__file__).resolve().parent.parent

    if args.output:
        output_dir = Path(args.output).resolve()
    else:
        docs = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Documents" / "PhyxelProjects" / args.name
        output_dir = docs

    if any((output_dir / f).exists() for f in ["CMakeLists.txt", "main.cpp"]) and not args.force:
        print(f"Error: {output_dir} already contains project files.", file=sys.stderr)
        print("Use --force to regenerate the scaffold in place (game.json/worlds/resources",
              file=sys.stderr)
        print("are only rewritten if --game-definition is passed; back up local C++ edits!).",
              file=sys.stderr)
        sys.exit(1)

    game_definition = None
    if args.game_definition:
        def_path = Path(args.game_definition).resolve()
        if not def_path.exists():
            print(f"Error: Game definition not found: {def_path}", file=sys.stderr)
            sys.exit(1)
        game_definition = json.loads(def_path.read_text(encoding="utf-8-sig"))

    create_project(args.name, output_dir, phyxel_root, game_definition)


if __name__ == "__main__":
    main()
