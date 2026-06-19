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
        "private:",
        f"    bool loadGameDefinition(Phyxel::Core::EngineRuntime& engine);",
        f"    Phyxel::Scene::Entity* spawnEntity(const std::string& type, const glm::vec3& pos, const std::string& animFile);",
        f"    void updateCursorMode(Phyxel::Core::EngineRuntime& engine);",
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

        int main(int argc, char* argv[]) {{
            // Write a log next to the exe so a packaged game that exits early is
            // diagnosable (boot errors land in {name_lower}.log instead of nowhere).
            Phyxel::Utils::Logger::enableFileOutput(true, "{name_lower}.log");
            LOG_INFO("main", "{name} starting");

            Phyxel::Core::EngineConfig config;
            Phyxel::Core::EngineConfig::loadFromFile("engine.json", config);

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
        #include "utils/PerformanceProfiler.h"
        #include "utils/PerformanceMonitor.h"
        #include "utils/Logger.h"
        #include <nlohmann/json.hpp>
        #include <glm/glm.hpp>
        #include <GLFW/glfw3.h>
        #include <fstream>
        #include <filesystem>
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
            bool inDialogue = dialogueSystem_ && dialogueSystem_->isActive();
            // A menu scene always wants a free cursor (its buttons are clickable).
            bool shouldCapture = !menuSceneActive_ &&
                                 !Phyxel::UI::isMouseFree(screen_.getState()) && !inDialogue;
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

            // Declarative trigger actions (game.json "triggers"): wire to the shell.
            // Conditions like {{when: {{event: "player_jumped"}}}} can drive
            // show_victory / show_credits / transition_scene / quit_game with no code.
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
                if (auto* hudUi = renderCoordinator_ ? renderCoordinator_->getUISystem() : nullptr)
                    Phyxel::UI::loadHudInto(*hudUi,
                        gameDef.contains("hud") ? &gameDef["hud"] : nullptr);

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

            // ESC: pause/resume toggle, or close dialogue
            if (input->isKeyPressed(GLFW_KEY_ESCAPE)) {{
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

            // E key: interact with NPC / advance dialogue
            if (Phyxel::UI::isGameRunning(state) && input->isKeyPressed(GLFW_KEY_E)) {{
                if (dialogueSystem_ && dialogueSystem_->isActive()) {{
                    dialogueSystem_->advanceDialogue();
                }} else if (interactionManager_) {{
                    interactionManager_->tryInteract(playerCharacter_);
                }}
            }}

            // Enter key: advance dialogue
            if (dialogueSystem_ && dialogueSystem_->isActive() && input->isKeyPressed(GLFW_KEY_ENTER)) {{
                dialogueSystem_->advanceDialogue();
            }}

            // Number keys 1-4: select dialogue choices
            if (dialogueSystem_ && dialogueSystem_->isActive()) {{
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
        }}

        void {class_name}::onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) {{
            lastDt_ = dt;  // remembered for menu-scene animations rendered in onRender

            // Pump scene transitions every frame, in EVERY screen state — menu-scene
            // buttons and triggers set the transition; this advances it.
            if (auto* sceneMgr = engine.getSceneManager()) {{
                sceneMgr->update(dt);
            }}

            // A menu scene is showing: SceneManager + the menu renderer drive things;
            // skip world/gameplay simulation entirely.
            if (menuSceneActive_) return;

            if (!Phyxel::UI::isGameRunning(screen_.getState())) return;

            elapsed_ += dt;

            auto* physics = engine.getPhysicsWorld();
            if (physics) physics->stepSimulation(dt);

            // GameShell drives the gameplay camera + character control: resolves
            // the rig/scheme from the active scene's camera block (re-resolving
            // after transitions), samples input, moves the body, frames the
            // camera. See docs/CameraControlSystem.md.
            if (playerCharacter_) {{
                updateGameplayCamera(engine, dt, playerCharacter_);

                // Gameplay events -> declarative triggers (win conditions).
                if (playerCharacter_->consumeJustJumped()) triggers_.onEvent("player_jumped");
                if (playerCharacter_->consumeJustLanded()) triggers_.onEvent("player_landed");
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

            // Update interaction manager with player position
            if (interactionManager_ && playerCharacter_) {{
                interactionManager_->update(dt, playerCharacter_->getPosition());
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
                                default: break;
                            }}
                        }}
                        if (want != activeDataScreen_) {{
                            if (activeDataScreen_ == "pause") Phyxel::UI::unloadPauseMenuFrom(*ui);
                            else if (!activeDataScreen_.empty()) Phyxel::UI::unloadGameScreenFrom(*ui, activeDataScreen_);
                            if (!want.empty()) {{
                                Phyxel::UI::MenuActions a;
                                a.onResolveVariable = [](const std::string& t) -> std::optional<std::string> {{
                                    if (t == "title")   return std::string("{class_name}");
                                    if (t == "tagline") return std::string("{game_tagline}");
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
                                    return 0.0f;
                                }};
                                a.onSetSetting = [this](const std::string& k, float v) {{
                                    auto* win = engine_ ? engine_->getWindowManager() : nullptr;
                                    auto* cam = engine_ ? engine_->getCamera() : nullptr;
                                    if (k == "fov")                   {{ settings_.fov = v; if (cam) cam->setZoom(v); }}
                                    else if (k == "masterVolume")     {{ settings_.masterVolume = v; }}
                                    else if (k == "musicVolume")      {{ settings_.musicVolume = v; }}
                                    else if (k == "sfxVolume")        {{ settings_.sfxVolume = v; }}
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
                    // Graphics/Audio/Controls; changes apply live + save on Back.
                    // Deferred: keybinding rebind, brightness, invert-Y, AI settings.
                    // (docs/HudSystem.md §11a.)
                    break;

                default:
                    break;
                }}

                // Render dialogue UI. Standard dialogue TREES render through the
                // data-driven hud_dialogue panel (default_hud.json) on the UISystem,
                // so the ImGui box must be gated to AI conversations only — otherwise
                // a tree conversation stacks TWO overlapping speaker/text/choices
                // boxes. Mirrors the editor (Application.cpp). AI conversations still
                // need the ImGui box for scrollable history + text input.
                // (game-dev feedback round 5 — UIShowcase.)
                if (dialogueSystem_ && dialogueSystem_->isActive() &&
                    dialogueSystem_->isAIConversation()) {{
                    imgui->renderDialogueBox(dialogueSystem_.get());
                }}

                // Speech bubbles + "[E] Interact" prompt: data-driven world-anchored
                // labels on the UISystem (no ImGui). Project each world position to
                // screen and queue a label; the UISystem draws them in
                // renderCoordinator_->render() below. (docs/HudSystem.md §11a.)
                if (renderCoordinator_) {{
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

        void {class_name}::onShutdown() {{
            LOG_INFO("{class_name}", "Shutting down...");
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
