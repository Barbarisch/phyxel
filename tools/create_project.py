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
  python tools/create_project.py MyAwesomeGame --game-definition frozen_highlands.json
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
        add_custom_command(TARGET ${{PROJECT_NAME}} POST_BUILD
            COMMENT "Copying runtime assets to output directory"
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
    extra_members.append("    std::unique_ptr<Phyxel::Graphics::RenderCoordinator> renderCoordinator_;")

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

    extra_includes.append('#include "core/ObjectTemplateManager.h"')

    includes_str = "\n".join(sorted(set(extra_includes)))
    members_str = "\n".join(extra_members)

    # Build header file (no textwrap.dedent — control indentation directly)
    header_lines = [
        "#pragma once",
        '#include "core/GameCallbacks.h"',
        '#include "core/EngineRuntime.h"',
        *sorted(set(extra_includes)),
        "#include <memory>",
        "",
        f"class {class_name} : public Phyxel::Core::GameCallbacks {{",
        "public:",
        f"    bool onInitialize(Phyxel::Core::EngineRuntime& engine) override;",
        f"    void onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) override;",
        f"    void onRender(Phyxel::Core::EngineRuntime& engine) override;",
        f"    void onHandleInput(Phyxel::Core::EngineRuntime& engine) override;",
        f"    void onShutdown() override;",
        "",
        "private:",
        f"    bool loadGameDefinition(Phyxel::Core::EngineRuntime& engine);",
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

    print(f"Created project '{name}' in {output_dir}")
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
    return textwrap.dedent(f"""\
        #include "{class_name}.h"
        #include "core/ChunkManager.h"
        #include "core/GameDefinitionLoader.h"
        #include "graphics/Camera.h"
        #include "graphics/CameraManager.h"
        #include "graphics/RenderCoordinator.h"
        #include "input/InputManager.h"
        #include "vulkan/VulkanDevice.h"
        #include "vulkan/RenderPipeline.h"
        #include "ui/WindowManager.h"
        #include "ui/ImGuiRenderer.h"
        #include "utils/PerformanceProfiler.h"
        #include "utils/PerformanceMonitor.h"
        #include "utils/Logger.h"
        #include <nlohmann/json.hpp>
        #include <glm/glm.hpp>
        #include <fstream>
        #include <filesystem>

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

            // Load game definition (NPCs, story, camera — world is pre-baked in SQLite)
            if (!loadGameDefinition(engine)) {{
                LOG_WARN("{class_name}", "No game.json found — starting with empty world");
            }}

            LOG_INFO("{class_name}", "Game initialized");
            return true;
        }}

        bool {class_name}::loadGameDefinition(Phyxel::Core::EngineRuntime& engine) {{
            if (!std::filesystem::exists("game.json")) return false;

            std::ifstream f("game.json");
            if (!f.is_open()) return false;

            try {{
                nlohmann::json gameDef = nlohmann::json::parse(f);

                // Strip world generation — chunks are pre-baked in worlds/default.db.
                // The loader would overwrite them if the "world" key is present.
                gameDef.erase("world");

                Phyxel::Core::GameSubsystems subsystems;
                subsystems.chunkManager    = engine.getChunkManager();
                subsystems.npcManager      = npcManager_.get();
                subsystems.entityRegistry  = entityRegistry_.get();
                subsystems.dialogueSystem  = dialogueSystem_.get();
                subsystems.storyEngine     = storyEngine_.get();
                subsystems.camera          = engine.getCamera();

                auto result = Phyxel::Core::GameDefinitionLoader::load(gameDef, subsystems);
                if (result.success) {{
                    LOG_INFO("{class_name}", "Loaded game: {{}} chunks, {{}} NPCs",
                             result.chunksGenerated, result.npcsSpawned);
                }} else {{
                    LOG_ERROR("{class_name}", "Failed to load game: {{}}", result.error);
                }}

                // Sync input manager with camera after definition load
                auto* input = engine.getInputManager();
                auto* cam = engine.getCamera();
                input->setCameraPosition(cam->getPosition());
                input->setYawPitch(cam->getYaw(), cam->getPitch());

                return result.success;
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
            if (input) input->processInput(0.016f);
        }}

        void {class_name}::onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) {{
            elapsed_ += dt;

            auto* physics = engine.getPhysicsWorld();
            if (physics) physics->stepSimulation(dt);

            if (npcManager_) npcManager_->update(dt);
            if (storyEngine_) storyEngine_->update(dt);
            if (speechBubbleManager_) speechBubbleManager_->update(dt);
        }}

        void {class_name}::onRender(Phyxel::Core::EngineRuntime& engine) {{
            auto* window = engine.getWindowManager();
            if (window && window->isMinimized()) return;

            auto* imgui = engine.getImGuiRenderer();
            if (imgui) {{
                imgui->newFrame();
                // Render dialogue UI via ImGuiRenderer
                if (dialogueSystem_ && dialogueSystem_->isActive()) {{
                    imgui->renderDialogueBox(dialogueSystem_.get());
                }}
                imgui->endFrame();
            }}

            if (renderCoordinator_) renderCoordinator_->render();
        }}

        void {class_name}::onShutdown() {{
            LOG_INFO("{class_name}", "Shutting down...");
            renderCoordinator_.reset();
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
    args = parser.parse_args()

    phyxel_root = Path(__file__).resolve().parent.parent

    if args.output:
        output_dir = Path(args.output).resolve()
    else:
        docs = Path(os.environ.get("USERPROFILE", os.path.expanduser("~"))) / "Documents" / "PhyxelProjects" / args.name
        output_dir = docs

    if any((output_dir / f).exists() for f in ["CMakeLists.txt", "main.cpp"]):
        print(f"Error: {output_dir} already contains project files.", file=sys.stderr)
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
