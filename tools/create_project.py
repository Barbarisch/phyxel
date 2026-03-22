#!/usr/bin/env python3
"""
create_project.py — Scaffold a new Phyxel game project.

Creates a standalone game directory with:
  - CMakeLists.txt linking against phyxel_core
  - A GameCallbacks stub (MyGame.h / MyGame.cpp)
  - main.cpp entry point
  - engine.json default config

Usage:
  python tools/create_project.py MyAwesomeGame
  python tools/create_project.py MyAwesomeGame --output ~/projects/MyAwesomeGame
"""

import argparse
import os
import sys
import textwrap
from pathlib import Path


def create_project(name: str, output_dir: Path, phyxel_root: Path) -> None:
    """Generate all project files."""
    output_dir.mkdir(parents=True, exist_ok=True)

    class_name = name.replace("-", "").replace("_", "")
    header_guard = name.upper().replace("-", "_").replace(" ", "_") + "_H"

    # Compute path to Phyxel root (relative if same drive, absolute otherwise)
    try:
        phyxel_path = os.path.relpath(phyxel_root, output_dir).replace(os.sep, '/')
    except ValueError:
        # Different drives on Windows — use absolute path
        phyxel_path = str(phyxel_root).replace(os.sep, '/')

    files = {
        "CMakeLists.txt": textwrap.dedent(f"""\
            cmake_minimum_required(VERSION 3.15)
            project({name} LANGUAGES CXX)
            set(CMAKE_CXX_STANDARD 17)
            set(CMAKE_CXX_STANDARD_REQUIRED ON)

            # ── Find Phyxel engine ──────────────────────────────────
            # Option A: installed via cmake --install
            #   find_package(Phyxel REQUIRED)
            #   target_link_libraries(${{PROJECT_NAME}} PRIVATE Phyxel::phyxel_core)
            #
            # Option B: add Phyxel source tree directly (default)
            set(PHYXEL_ROOT "${{CMAKE_CURRENT_SOURCE_DIR}}/{phyxel_path}"
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
            endif()
        """),

        f"{class_name}.h": textwrap.dedent(f"""\
            #pragma once
            #include "core/GameCallbacks.h"
            #include "core/EngineRuntime.h"

            class {class_name} : public Phyxel::Core::GameCallbacks {{
            public:
                bool onInitialize(Phyxel::Core::EngineRuntime& engine) override;
                void onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) override;
                void onRender(Phyxel::Core::EngineRuntime& engine) override;
                void onHandleInput(Phyxel::Core::EngineRuntime& engine) override;
                void onShutdown() override;

            private:
                float elapsed_ = 0.0f;
            }};
        """),

        f"{class_name}.cpp": textwrap.dedent(f"""\
            #include "{class_name}.h"
            #include "core/ChunkManager.h"
            #include "graphics/Camera.h"
            #include "graphics/RenderCoordinator.h"
            #include "input/InputManager.h"
            #include "ui/WindowManager.h"
            #include "ui/ImGuiRenderer.h"
            #include "utils/Logger.h"
            #include <glm/glm.hpp>
            #include <memory>

            // Game-owned render coordinator
            static std::unique_ptr<Phyxel::Graphics::RenderCoordinator> s_renderCoordinator;

            bool {class_name}::onInitialize(Phyxel::Core::EngineRuntime& engine) {{
                LOG_INFO("{class_name}", "Initializing...");

                // TODO: Build your world here
                auto* chunks = engine.getChunkManager();
                for (int x = 0; x < 32; ++x)
                    for (int z = 0; z < 32; ++z)
                        chunks->addCube(glm::ivec3(x, 15, z));
                chunks->rebuildAllChunkFaces();

                // Position the camera
                auto* cam = engine.getCamera();
                cam->setPosition(glm::vec3(16.0f, 25.0f, 16.0f));
                cam->setYaw(-90.0f);
                cam->setPitch(-30.0f);
                auto* input = engine.getInputManager();
                input->setCameraPosition(cam->getPosition());
                input->setYawPitch(cam->getYaw(), cam->getPitch());

                // Create the render coordinator
                s_renderCoordinator = std::make_unique<Phyxel::Graphics::RenderCoordinator>(
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

                return true;
            }}

            void {class_name}::onHandleInput(Phyxel::Core::EngineRuntime& engine) {{
                auto* input = engine.getInputManager();
                if (input) input->processInput(0.016f);
            }}

            void {class_name}::onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) {{
                elapsed_ += dt;
                auto* physics = engine.getPhysicsWorld();
                if (physics) physics->stepSimulation(dt);
            }}

            void {class_name}::onRender(Phyxel::Core::EngineRuntime& engine) {{
                auto* window = engine.getWindowManager();
                if (window && window->isMinimized()) return;
                auto* imgui = engine.getImGuiRenderer();
                if (imgui) {{ imgui->newFrame(); imgui->endFrame(); }}
                if (s_renderCoordinator) s_renderCoordinator->render();
            }}

            void {class_name}::onShutdown() {{
                LOG_INFO("{class_name}", "Shutting down...");
                s_renderCoordinator.reset();
            }}
        """),

        "main.cpp": textwrap.dedent(f"""\
            #include "{class_name}.h"
            #include "core/EngineRuntime.h"
            #include "core/EngineConfig.h"

            int main() {{
                Phyxel::Core::EngineConfig config;
                Phyxel::Core::EngineConfig::loadFromFile("engine.json", config);

                Phyxel::Core::EngineRuntime engine;
                if (!engine.initialize(config)) return 1;

                {class_name} game;
                engine.run(game);
                return 0;
            }}
        """),

        "engine.json": textwrap.dedent("""\
            {
                "window": {
                    "width": 1280,
                    "height": 720,
                    "title": "%s"
                },
                "rendering": {
                    "maxChunkRenderDistance": 96.0,
                    "chunkInclusionDistance": 128.0
                },
                "world": {
                    "databasePath": "worlds/default.db"
                },
                "api": {
                    "port": 8090
                }
            }
        """) % name,
    }

    for filename, content in files.items():
        filepath = output_dir / filename
        filepath.write_text(content, encoding="utf-8")

    print(f"Created project '{name}' in {output_dir}")
    print()
    print("To build:")
    print(f"  cd {output_dir}")
    print(f"  cmake -B build -S .")
    print(f"  cmake --build build --config Debug")
    print(f"  .\\build\\Debug\\{name}.exe")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Scaffold a new Phyxel game project."
    )
    parser.add_argument("name", help="Project/class name (e.g. MyAwesomeGame)")
    parser.add_argument(
        "--output", "-o",
        help="Output directory (default: ./<name>)",
        default=None,
    )
    args = parser.parse_args()

    phyxel_root = Path(__file__).resolve().parent.parent
    output_dir = Path(args.output) if args.output else Path.cwd() / args.name
    output_dir = output_dir.resolve()

    if any((output_dir / f).exists() for f in ["CMakeLists.txt", "main.cpp"]):
        print(f"Error: {output_dir} already contains project files.", file=sys.stderr)
        sys.exit(1)

    create_project(args.name, output_dir, phyxel_root)


if __name__ == "__main__":
    main()
