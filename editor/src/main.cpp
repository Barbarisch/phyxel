#include "Application.h"
#include "utils/Logger.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>

int main(int argc, char* argv[]) {
    Phyxel::Application app;

    // Parse command-line arguments
    std::string gameDefPath;
    std::string projectDir;
    std::string assetEditorFile;
    std::string animEditorFile;
    std::string interactionEditorFile;
    std::string interactionEditorChar;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--game" || arg == "-g") && i + 1 < argc) {
            gameDefPath = argv[++i];
        } else if ((arg == "--project" || arg == "-p") && i + 1 < argc) {
            projectDir = argv[++i];
        } else if ((arg == "--asset-editor" || arg == "-ae") && i + 1 < argc) {
            assetEditorFile = argv[++i];
        } else if ((arg == "--anim-editor" || arg == "-ame") && i + 1 < argc) {
            animEditorFile = argv[++i];
        } else if ((arg == "--interaction-editor" || arg == "-ie") && i + 1 < argc) {
            interactionEditorFile = argv[++i];
        } else if ((arg == "--character") && i + 1 < argc) {
            interactionEditorChar = argv[++i];
        }
    }

    // --project points to a game project directory.
    // Resolve game.json and worlds from it while keeping engine cwd for shaders.
    if (!projectDir.empty()) {
        std::filesystem::path projPath = std::filesystem::absolute(projectDir);
        if (!std::filesystem::is_directory(projPath)) {
            LOG_ERROR("Main", "Project directory does not exist: {}", projPath.string());
            return -1;
        }
        // Use the project's game.json unless --game was also specified
        if (gameDefPath.empty()) {
            auto projGameJson = projPath / "game.json";
            if (std::filesystem::exists(projGameJson)) {
                gameDefPath = projGameJson.string();
            }
        }
        // Pass the project directory to Application so it can override worldsDir
        app.setProjectDir(projPath.string());
    }

    if (!assetEditorFile.empty()) {
        app.setAssetEditorFile(assetEditorFile);
    }
    if (!animEditorFile.empty()) {
        app.setAnimEditorFile(animEditorFile);
    }
    if (!interactionEditorFile.empty()) {
        app.setInteractionEditorFile(interactionEditorFile, interactionEditorChar);
    }

    try {
        // Initialize the application
        if (!app.initialize(gameDefPath)) {
            LOG_ERROR("Main", "Failed to initialize application!");
            return -1;
        }

        // Run the main loop
        app.run();

    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Main", "Application error: " << e.what());
        return -1;
    }

    return 0;
}
