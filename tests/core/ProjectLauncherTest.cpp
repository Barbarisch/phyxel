#include <gtest/gtest.h>
#include "core/ProjectInfo.h"
#include "core/LauncherState.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Helper: create a temporary directory that cleans up on destruction
class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("phyxel_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }
    std::string str() const { return path_.string(); }
private:
    fs::path path_;
};

// Helper: write a string to a file
static void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p);
    f << content;
}

// ============================================================================
// ProjectInfo Tests
// ============================================================================

TEST(ProjectInfoTest, ToJsonAndFromJson) {
    Phyxel::Core::ProjectInfo info;
    info.name = "TestGame";
    info.path = "C:/Projects/TestGame";
    info.lastOpened = 1234567890;
    info.hasGameDefinition = true;
    info.hasCMakeLists = false;
    info.hasWorldDatabase = true;

    auto j = info.toJson();
    auto restored = Phyxel::Core::ProjectInfo::fromJson(j);

    EXPECT_EQ(restored.name, "TestGame");
    EXPECT_EQ(restored.path, "C:/Projects/TestGame");
    EXPECT_EQ(restored.lastOpened, 1234567890);
    EXPECT_TRUE(restored.hasGameDefinition);
    EXPECT_FALSE(restored.hasCMakeLists);
    EXPECT_TRUE(restored.hasWorldDatabase);
}

TEST(ProjectInfoTest, FromJsonMissingFields) {
    nlohmann::json j = {{"name", "Partial"}};
    auto info = Phyxel::Core::ProjectInfo::fromJson(j);
    EXPECT_EQ(info.name, "Partial");
    EXPECT_EQ(info.path, "");
    EXPECT_EQ(info.lastOpened, 0);
    EXPECT_FALSE(info.hasGameDefinition);
}

TEST(ProjectInfoTest, IsValidProject_WithEngineJson) {
    TempDir dir;
    // No engine.json -> invalid
    EXPECT_FALSE(Phyxel::Core::ProjectInfo::isValidProject(dir.str()));

    // Add engine.json -> valid
    writeFile(dir.path() / "engine.json", "{\"window\":{\"title\":\"Test\"}}");
    EXPECT_TRUE(Phyxel::Core::ProjectInfo::isValidProject(dir.str()));
}

TEST(ProjectInfoTest, DiscoverProjects_FindsValid) {
    TempDir base;

    // Create two project dirs: one valid, one not
    fs::create_directories(base.path() / "GameA");
    writeFile(base.path() / "GameA" / "engine.json", "{\"window\":{\"title\":\"Game Alpha\"}}");

    fs::create_directories(base.path() / "NotAProject");
    writeFile(base.path() / "NotAProject" / "readme.txt", "not a project");

    fs::create_directories(base.path() / "GameB");
    writeFile(base.path() / "GameB" / "engine.json", "{}");
    writeFile(base.path() / "GameB" / "game.json", "{}");
    fs::create_directories(base.path() / "GameB" / "worlds");
    writeFile(base.path() / "GameB" / "worlds" / "default.db", "");

    auto projects = Phyxel::Core::ProjectInfo::discoverProjects(base.str());
    ASSERT_EQ(projects.size(), 2);

    // Find GameA — should have title from engine.json
    auto it = std::find_if(projects.begin(), projects.end(),
        [](const auto& p) { return p.name == "Game Alpha"; });
    ASSERT_NE(it, projects.end());
    EXPECT_FALSE(it->hasGameDefinition);

    // Find GameB
    auto it2 = std::find_if(projects.begin(), projects.end(),
        [](const auto& p) { return p.name == "GameB"; });
    ASSERT_NE(it2, projects.end());
    EXPECT_TRUE(it2->hasGameDefinition);
    EXPECT_TRUE(it2->hasWorldDatabase);
}

TEST(ProjectInfoTest, DiscoverProjects_EmptyDir) {
    TempDir base;
    auto projects = Phyxel::Core::ProjectInfo::discoverProjects(base.str());
    EXPECT_TRUE(projects.empty());
}

TEST(ProjectInfoTest, DiscoverProjects_NonexistentDir) {
    auto projects = Phyxel::Core::ProjectInfo::discoverProjects("C:/nonexistent_dir_xyz_12345");
    EXPECT_TRUE(projects.empty());
}

TEST(ProjectInfoTest, ScaffoldProject_CreatesStructure) {
    TempDir base;
    std::string path = Phyxel::Core::ProjectInfo::scaffoldProject("MyGame", base.str());

    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(fs::exists(fs::path(path) / "engine.json"));
    EXPECT_TRUE(fs::is_directory(fs::path(path) / "worlds"));
    EXPECT_TRUE(fs::is_directory(fs::path(path) / "resources" / "templates"));
    EXPECT_TRUE(fs::is_directory(fs::path(path) / "resources" / "sounds"));

    // Verify engine.json content
    std::ifstream f(fs::path(path) / "engine.json");
    auto j = nlohmann::json::parse(f);
    EXPECT_EQ(j["window"]["title"], "MyGame");
}

TEST(ProjectInfoTest, ScaffoldProject_EmptyName) {
    TempDir base;
    std::string path = Phyxel::Core::ProjectInfo::scaffoldProject("", base.str());
    EXPECT_TRUE(path.empty());
}

TEST(ProjectInfoTest, ScaffoldProject_ExistingDir_ReturnsPath) {
    TempDir base;
    // First scaffold
    std::string path1 = Phyxel::Core::ProjectInfo::scaffoldProject("Existing", base.str());
    ASSERT_FALSE(path1.empty());
    // Second scaffold of same name returns the existing path
    std::string path2 = Phyxel::Core::ProjectInfo::scaffoldProject("Existing", base.str());
    EXPECT_EQ(path1, path2);
}

TEST(ProjectInfoTest, GetDefaultProjectsDir_NotEmpty) {
    std::string dir = Phyxel::Core::ProjectInfo::getDefaultProjectsDir();
    EXPECT_FALSE(dir.empty());
}

// ============================================================================
// LauncherState Tests
// ============================================================================

TEST(LauncherStateTest, SaveAndLoad) {
    TempDir base;

    Phyxel::Core::LauncherState state;

    Phyxel::Core::ProjectInfo proj;
    proj.name = "SavedProject";
    proj.path = "C:/test/SavedProject";
    state.addRecentProject(proj);

    EXPECT_TRUE(state.save(base.str()));

    Phyxel::Core::LauncherState loaded;
    EXPECT_TRUE(loaded.load(base.str()));

    auto& entries = loaded.getRecentEntries();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].name, "SavedProject");
    EXPECT_EQ(entries[0].path, "C:/test/SavedProject");
    EXPECT_GT(entries[0].lastOpened, 0);
}

TEST(LauncherStateTest, LoadNonexistent_OK) {
    TempDir base;
    Phyxel::Core::LauncherState state;
    EXPECT_TRUE(state.load(base.str())); // no file = OK
    EXPECT_TRUE(state.getRecentEntries().empty());
}

TEST(LauncherStateTest, AddRecentProject_Upserts) {
    Phyxel::Core::LauncherState state;

    Phyxel::Core::ProjectInfo proj;
    proj.name = "GameA";
    proj.path = "C:/projects/GameA";
    state.addRecentProject(proj);
    state.addRecentProject(proj); // duplicate

    EXPECT_EQ(state.getRecentEntries().size(), 1);
}

TEST(LauncherStateTest, AddRecentProject_MostRecentFirst) {
    Phyxel::Core::LauncherState state;

    Phyxel::Core::ProjectInfo a;
    a.name = "A"; a.path = "C:/a";
    state.addRecentProject(a);

    Phyxel::Core::ProjectInfo b;
    b.name = "B"; b.path = "C:/b";
    state.addRecentProject(b);

    auto& entries = state.getRecentEntries();
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0].name, "B"); // most recent first
    EXPECT_EQ(entries[1].name, "A");
}

TEST(LauncherStateTest, AddRecentProject_CapsAt20) {
    Phyxel::Core::LauncherState state;
    for (int i = 0; i < 25; i++) {
        Phyxel::Core::ProjectInfo p;
        p.name = "Project" + std::to_string(i);
        p.path = "C:/p/" + std::to_string(i);
        state.addRecentProject(p);
    }
    EXPECT_EQ(state.getRecentEntries().size(), 20);
}

TEST(LauncherStateTest, MergedProjects_IncludesDiscoveredAndRecent) {
    TempDir base;

    // Create a project on disk
    fs::create_directories(base.path() / "OnDisk");
    writeFile(base.path() / "OnDisk" / "engine.json", "{}");

    // Add a recent project that also exists on disk
    Phyxel::Core::LauncherState state;
    Phyxel::Core::ProjectInfo recent;
    recent.name = "OnDisk";
    recent.path = (base.path() / "OnDisk").string();
    state.addRecentProject(recent);

    auto merged = state.getMergedProjects(base.str());
    // Should have exactly 1 entry (discovered + recent merged)
    ASSERT_EQ(merged.size(), 1);
    EXPECT_GT(merged[0].lastOpened, 0); // has timestamp from recent
}

TEST(LauncherStateTest, MergedProjects_PrunesDeletedRecent) {
    TempDir base;

    Phyxel::Core::LauncherState state;
    Phyxel::Core::ProjectInfo ghost;
    ghost.name = "Deleted";
    ghost.path = "C:/nonexistent/Deleted";
    state.addRecentProject(ghost);

    auto merged = state.getMergedProjects(base.str());
    // The ghost project should be pruned (directory doesn't exist)
    EXPECT_TRUE(merged.empty());
}
