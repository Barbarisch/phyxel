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
    extra_members.append("    std::unique_ptr<Phyxel::Graphics::RenderCoordinator> renderCoordinator_;")
    extra_members.append("    Phyxel::Scene::AnimatedVoxelCharacter* playerCharacter_ = nullptr;")
    extra_members.append("    std::vector<std::unique_ptr<Phyxel::Scene::Entity>> entities_;")
    extra_members.append("    Phyxel::UI::GameScreen screen_;")
    extra_members.append("    Phyxel::Core::TriggerSystem triggers_;  // declarative when/then win conditions (game.json \"triggers\")")
    extra_includes.append('#include "core/GameDefinitionLoader.h"')
    extra_members.append("    Phyxel::Core::GameSubsystems gameSubsystems_;  // persistent: the SceneManager keeps a pointer to it")
    # Editor-parity gameplay state (docs/game-production/StandaloneParityGaps.md §1):
    # objectives + persistence exist in the SHIPPED game, not just the editor host.
    extra_includes.append('#include "core/ObjectiveTracker.h"')
    extra_includes.append('#include "core/PlayerProfile.h"')
    # Turn-based combat in the SHIPPED game (StandaloneParityGaps.md §1, CombatDirector
    # row): the same director/AI/player-turn stack the editor wires (Application.cpp
    # ~554-589 + 1847-1859), minus editor-only cast visuals. game.json "combat.mode"
    # selects the ruleset; a "start_combat" trigger action begins authored encounters.
    extra_includes.append('#include "core/CombatDirector.h"')
    extra_includes.append('#include "core/CombatAISystem.h"')
    extra_includes.append('#include "core/PlayerTurnController.h"')
    extra_includes.append('#include "core/CombatSystem.h"')
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
    extra_members.append("    Phyxel::Core::CharacterSheet playerSheet_;  // progression: XP/level/classes")
    extra_members.append("    int killXp_ = 0;       // XP per enemy killed (game.json progression.kill_xp)")
    extra_members.append("    int objectiveXp_ = 0;  // XP per objective completed (progression.objective_xp)")
    extra_members.append("    bool profileRestored_ = false;  // profile restore runs ONCE per session, at the first world-scene load (a menu-start game has no world DB open at boot)")
    # Inventory: loot via the give_item trigger action; persists in the profile blob.
    extra_includes.append('#include "core/Inventory.h"')
    extra_members.append("    Phyxel::Core::Inventory inventory_;  // player inventory (loot; persisted via PlayerProfile)")
    # BG3-style tactical camera: swap to an overhead/isometric rig while an
    # encounter runs, restore the scene's rig after (combat.camera in game.json).
    extra_members.append('    std::string combatCameraRig_ = "overhead";  // rig while in combat (game.json combat.camera)')
    extra_members.append("    std::string preCombatRig_;      // rig to restore when the encounter ends")
    extra_members.append("    float preCombatYaw_ = 0.0f, preCombatPitch_ = 0.0f;  // look restored with the rig")
    extra_members.append("    bool wasInCombat_ = false;      // combat camera edge detection")
    # menuWorld: menu scenes with an authored world get a looping CameraPath
    # orbit behind their UI (PresentationPolish.md §3 Tier 1).
    extra_includes.append('#include "graphics/CameraManager.h"')
    extra_members.append("    Phyxel::Graphics::CameraPath menuCamPath_;  // drives the menuWorld orbit while a menu scene is up")
    extra_members.append("    bool combatLmbHeld_ = false;    // click-to-act edge detection (BG3 mouse combat)")
    extra_members.append("    Phyxel::Core::ObjectiveTracker objectiveTracker_;  // quest-log spine; game.json \"objectives\" load here")
    extra_members.append("    Phyxel::Core::PlayerProfile playerProfile_;        // persisted to the active scene's world DB (player_state table)")
    extra_members.append("    std::string loadingSceneName_;  // destination scene shown on the loading screen")

    # Menu-scene support: a JSON-driven menu renderer for sceneType:"menu" scenes.
    # When the loaded game uses menu scenes, the SceneManager drives the flow and
    # this renders the active menu — so the built-in ScreenState shell does NOT
    # double up on top of it (see docs/GameCreationGuide.md, "Menus").
    extra_includes.append('#include "ui/GameMenuRenderer.h"')
    # Data-driven UISystem HUD + menus (custom-Vulkan, no ImGui) — see docs/HudSystem.md.
    extra_includes.append('#include "ui/MenuDefinition.h"')   # loadHudInto / loadMenuInto / MenuActions
    extra_includes.append('#include "ui/UISystem.h"')
    extra_includes.append('#include "ui/HudDataContext.h"')
    extra_includes.append('#include "core/HealthComponent.h"')
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

    if has_npcs or game_definition:
        extra_includes.append('#include "ui/DialogueSystem.h"')
        extra_includes.append('#include "ui/SpeechBubbleManager.h"')
        extra_members.append("    std::unique_ptr<Phyxel::UI::DialogueSystem> dialogueSystem_;")
        extra_members.append("    std::unique_ptr<Phyxel::UI::SpeechBubbleManager> speechBubbleManager_;")

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
        "    Phyxel::Scene::AnimatedVoxelCharacter* apiPlayer() override { return playerCharacter_; }",
        "    Phyxel::Core::CombatDirector*       apiCombatDirector() override { return &combatDirector_; }",
        "    Phyxel::Core::CombatAISystem*       apiCombatAI() override       { return &combatAI_; }",
        "    Phyxel::Core::PlayerTurnController* apiPlayerTurn() override     { return &playerTurn_; }",
        "    Phyxel::Core::CharacterSheet*       apiPlayerSheet() override    { return &playerSheet_; }",
        "    Phyxel::Core::Inventory*            apiInventory() override      { return &inventory_; }",
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
                hud.setText ("combat.roundText", [this]{
                    char buf[32];
                    snprintf(buf, sizeof(buf), "COMBAT  -  Round %d", combatDirector_.currentRound());
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
            bool shouldCapture = !menuSceneActive_ &&
                                 !Phyxel::UI::isMouseFree(screen_.getState()) && !inDialogue &&
                                 !combatDirector_.inCombat();
            window->setCursorVisible(!shouldCapture);
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
                nullptr, nullptr
            );
            renderCoordinator_->setNPCManager(npcManager_.get());
            // The player character lives in entities_, NOT the NPCManager. Without
            // this the RenderCoordinator's entities pointer stays null and
            // renderEntities()/renderInstancedCharacters() skip the player entirely
            // — the world renders as empty terrain with an invisible (but loaded +
            // camera-followed) body. (game-dev feedback round 5 — UIShowcase.)
            renderCoordinator_->setEntities(&entities_);
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
                triggers_.onEvent("entity_died", {{{{"id", ev.targetId}}}});
                if (ev.targetId == "player") {{
                    triggers_.onEvent("player_died", {{{{"id", ev.targetId}}}});
                    return;
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
                        triggers_.onEvent("combat_victory", {{{{"last_kill", ev.targetId}}}});
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
            playerTurn_.setCombatDirector(&combatDirector_);
            playerTurn_.setEntityRegistry(entityRegistry_.get());
            playerTurn_.setBodyProvider(bodyProvider);
            playerTurn_.setCombatSystem(combatSystem_.get());

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
                }} else if (type == "save_game") {{
                    // Authorable save point: {{"type":"save_game"}} persists the
                    // player profile to the active scene's world DB.
                    savePlayerProfile();
                }} else if (type == "give_item") {{
                    // Authorable loot: {{"type":"give_item","id":"moonpetal_remedy","count":1}}
                    const std::string id = a.value("id", "");
                    const int count = a.value("count", 1);
                    if (!id.empty()) {{
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
                    if (combatants.empty()) {{
                        LOG_WARN("{class_name}", "start_combat: no participants (trigger '{{}}')", tid);
                    }} else {{
                        if (combatDirector_.inCombat()) combatDirector_.endEncounter();
                        Phyxel::Core::DiceSystem dice;
                        combatDirector_.beginEncounter(combatants, dice);
                        faceCombatants();   // square off — everyone faces the enemy
                        LOG_INFO("{class_name}", "Combat encounter started: {{}} combatants (trigger '{{}}')",
                                 combatants.size(), tid);
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
                    for (const auto& o : gameDef["objectives"]) {{
                        objectiveTracker_.addObjective(
                            o.value("id", ""), o.value("title", ""),
                            o.value("description", ""), o.value("category", "main"),
                            o.value("priority", 0), o.value("hidden", false));
                    }}
                    LOG_INFO("{class_name}", "Loaded {{}} objective(s) from game.json",
                             gameDef["objectives"].size());
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
                    LOG_INFO("{class_name}", "Combat mode: {{}} (camera: {{}})", mode, combatCameraRig_);
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
                    killXp_      = prog.value("kill_xp", 0);
                    objectiveXp_ = prog.value("objective_xp", 0);
                    LOG_INFO("{class_name}", "Progression: {{}} {{}} (kill_xp={{}}, objective_xp={{}})",
                             playerSheet_.raceId, cl.classId, killXp_, objectiveXp_);
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
                        }};
                        cb.clearNPCs = [this]() {{
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
                                if (!menuSceneActive_ &&
                                    screen_.getState() != Phyxel::UI::ScreenState::Loading) {{
                                    LOG_INFO("{class_name}", "Loading screen shown (-> '{{}}')", sceneName);
                                    screen_.setState(Phyxel::UI::ScreenState::Loading);
                                }}
                            }} else if (screen_.getState() == Phyxel::UI::ScreenState::Loading) {{
                                LOG_INFO("{class_name}", "Loading screen dismissed");
                                screen_.setState(Phyxel::UI::ScreenState::Playing);
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
                        cb.onSceneReady = [this](const std::string& /*sceneId*/) {{
                            auto* smgr = engine_ ? engine_->getSceneManager() : nullptr;
                            const auto* active = smgr ? smgr->getActiveScene() : nullptr;
                            bool isMenu = active && active->sceneType == Phyxel::Core::SceneType::Menu;
                            if (isMenu) return;  // keep the menu visible
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
                            if (!Phyxel::UI::isGameRunning(screen_.getState())) {{
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
                            }}
                            // Companions travel WITH the player: scene loads clear
                            // all entities, so respawn absent party members near
                            // the player on every world scene. (Exploration follow
                            // behavior is a future increment — they stand by until
                            // combat, where start_combat auto-enlists them.)
                            if (npcManager_ && entityRegistry_ && playerCharacter_) {{
                                for (const auto& m : rpgParty_.getMembers()) {{
                                    if (!m.isAlive || entityRegistry_->getEntity(m.entityId)) continue;
                                    const glm::vec3 at = playerCharacter_->getPosition() + glm::vec3(1.5f, 0.0f, 1.5f);
                                    // Explicit anim file: spawnNPC does NOT default an empty
                                    // path (unlike the game.json loader) — a companion spawned
                                    // with "" has no rig, so the combat TurnActor can never
                                    // bind its body and its turn stalls the whole encounter.
                                    if (npcManager_->spawnNPC(m.name, "resources/animated_characters/humanoid.anim",
                                                              at, Phyxel::Core::NPCBehaviorType::Idle))
                                        LOG_INFO("{class_name}", "Companion '{{}}' rejoined at the player's side", m.name);
                                }}
                            }}
                            if (engine_) updateCursorMode(*engine_);
                        }};
                        sm->setCallbacks(cb);

                        sm->loadManifest(manifest);
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

                // Restore the saved player profile (camera pose + health + XP/level)
                // from the active scene's world DB, if one was saved by a previous
                // run. Succeeds here only for WORLD-start games (the DB is open);
                // menu-start games restore in onSceneReady at the first world scene.
                if (!profileRestored_ && loadPlayerProfile()) {{
                    profileRestored_ = true;
                    LOG_INFO("{class_name}", "Restored saved player profile");
                }}

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
                    if (auto* ui = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr) {{
                        double mx = 0.0, my = 0.0;
                        glfwGetCursorPos(wm->getHandle(), &mx, &my);
                        ui->handleScroll({{static_cast<float>(mx), static_cast<float>(my)}}, wheel);
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
                }} else if (state == Phyxel::UI::ScreenState::Paused) {{
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

            // E key: interact with NPC / advance a TREE dialogue.
            if (!aiTyping && Phyxel::UI::isGameRunning(state) && input->isKeyPressed(GLFW_KEY_E)) {{
                if (dialogueSystem_ && dialogueSystem_->isActive()) {{
                    dialogueSystem_->advanceDialogue();
                }} else if (interactionManager_) {{
                    interactionManager_->tryInteract(playerCharacter_);
                }}
            }}

            // Enter key: advance a TREE dialogue (AI submits via the text field).
            if (!aiTyping && dialogueSystem_ && dialogueSystem_->isActive() && input->isKeyPressed(GLFW_KEY_ENTER)) {{
                dialogueSystem_->advanceDialogue();
            }}

            // Number keys 1-4: select TREE dialogue choices.
            if (!aiTyping && dialogueSystem_ && dialogueSystem_->isActive()) {{
                for (int k = GLFW_KEY_1; k <= GLFW_KEY_4; ++k) {{
                    if (input->isKeyPressed(k)) {{
                        dialogueSystem_->selectChoice(k - GLFW_KEY_1);
                    }}
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
            }}

            // BG3 mouse combat: on the player's turn, a left click resolves to
            // attack (enemy under cursor) or move (ground point) through the
            // same PlayerTurnController pick the test API uses. Edge-triggered;
            // cursor is free during combat (updateCursorMode).
            if (combatDirector_.inCombat() && playerTurn_.isPlayerTurnActive() &&
                !inDialogue && renderCoordinator_) {{
                const bool lmb = input->isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
                if (lmb && !combatLmbHeld_) {{
                    auto* win = engine.getWindowManager();
                    auto* cam = engine.getCamera();
                    if (win && cam) {{
                        double mx = 0.0, my = 0.0;
                        glfwGetCursorPos(win->getHandle(), &mx, &my);
                        const glm::uvec2 vp = renderCoordinator_->getSwapChainSize();
                        const float groundY = playerCharacter_ ? playerCharacter_->getPosition().y : 0.0f;
                        const char* resolved = playerTurn_.requestPickAt(
                            *cam, {{static_cast<float>(mx), static_cast<float>(my)}},
                            {{static_cast<float>(vp.x), static_cast<float>(vp.y)}}, groundY);
                        LOG_INFO("{class_name}", "Combat click ({{}}, {{}}) -> {{}}",
                                 static_cast<int>(mx), static_cast<int>(my), resolved);
                    }}
                }}
                combatLmbHeld_ = lmb;
            }} else {{
                combatLmbHeld_ = false;
            }}
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
                updateGameplayCamera(engine, dt, playerCharacter_,
                                     /*driveCharacter=*/!combatDirector_.inCombat() && !talking);

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
            playerTurn_.tick(dt);

            // BG3-style tactical camera: entering combat swaps to the authored
            // combat rig (default overhead birds-eye) and frees the cursor for
            // click-targeting; leaving combat restores the scene's rig. (WASD
            // suppression happens at the updateGameplayCamera call above via
            // driveCharacter=false.)
            const bool inCombatNow = combatDirector_.inCombat();
            if (inCombatNow != wasInCombat_) {{
                auto* look = engine.getInputManager();
                if (inCombatNow) {{
                    preCombatRig_ = gameplayCamera().rigName();
                    if (look) {{ preCombatYaw_ = look->getYaw(); preCombatPitch_ = look->getPitch(); }}
                    if (gameplayCamera().setRigByName(combatCameraRig_))
                        LOG_INFO("{class_name}", "Tactical camera: '{{}}' (combat)", combatCameraRig_);
                }} else {{
                    const std::string back = preCombatRig_.empty() ? "third_person" : preCombatRig_;
                    // Restore the LOOK along with the rig — the tactical phase can
                    // leave the InputManager's pitch scrambled (rig pitch clamps +
                    // convention differences), which would put the restored camera
                    // under the floor looking up.
                    if (look) look->setYawPitch(preCombatYaw_, preCombatPitch_);
                    if (gameplayCamera().setRigByName(back))
                        LOG_INFO("{class_name}", "Camera restored: '{{}}' (combat over)", back);
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
            if (storyEngine_) storyEngine_->update(dt);
            if (speechBubbleManager_) speechBubbleManager_->update(dt);

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
                                case Phyxel::UI::ScreenState::Settings: want = "settings"; break;
                                case Phyxel::UI::ScreenState::Loading:  want = "loading";  break;
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
                                    // {{keybind.<Action>}} -> current key for the keybindings sub-panel rows.
                                    if (t.rfind("keybind.", 0) == 0) {{
                                        const auto* b = settings_.findBinding(t.substr(8));
                                        if (!b) return std::string("(unbound)");
                                        std::string s = Phyxel::Core::keyToString(b->key);
                                        if (b->modifiers) s = Phyxel::Core::modifiersToString(b->modifiers) + "+" + s;
                                        return s;
                                    }}
                                    return std::nullopt;
                                }};
                                a.onResume      = [this]() {{ screen_.resume();           if (engine_) updateCursorMode(*engine_); }};
                                a.onStartGame   = [this]() {{ screen_.startGame();         if (engine_) updateCursorMode(*engine_); }};
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
                        auto* win = engine.getWindowManager();
                        float sw = win ? static_cast<float>(win->getWidth()) : 1280.0f;
                        float sh = win ? static_cast<float>(win->getHeight()) : 720.0f;
                        const auto& view = renderCoordinator_->getCachedViewMatrix();
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
                        if (interactionManager_ && interactionManager_->shouldShowPrompt() &&
                            (!dialogueSystem_ || !dialogueSystem_->isActive())) {{
                            if (auto* npc = interactionManager_->getNearestInteractableNPC()) {{
                                std::string txt = interactionManager_->getActivePromptText();
                                if (txt.empty()) txt = "[E] Interact";
                                if (Phyxel::UI::UISystem::worldToScreen(npc->getPosition() + glm::vec3(0.0f, 2.0f, 0.0f), view, proj, sw, sh, sp))
                                    ui->addWorldLabel(sp, txt, glm::vec4(1.0f, 1.0f, 0.6f, 1.0f), 0.8f);
                            }}
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
