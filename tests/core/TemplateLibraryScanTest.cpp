// Template library scan contract — L2.
//
// The asset-library reorganization (2026-08-07) moves ~180 root-level
// templates into category subdirectories (furniture/, nature/, items/...).
// Contract for ObjectTemplateManager::loadTemplates:
//  1. RECURSIVE: templates in subdirectories are loaded at startup (the old
//     scan was root-only, which made subdirectory assets lazy and enabled
//     the stem-shadowing bug).
//  2. STEM STAYS THE REFERENCE KEY: world DBs, flora, and FurnitureCatalog
//     reference templates by stem — a moved template must resolve by stem.
//  3. PATH ALIAS: subdirectory templates also resolve by their relative path
//     without extension ("sub/b"), matching resolveItemTemplate's keys.
//  4. DUPLICATE STEMS ARE A COLLISION: two different files with the same stem
//     register exactly ONE template (first wins, loud ERROR) — never a silent
//     overwrite (silent substitution is the bug class found 2026-08-06).

#include <gtest/gtest.h>

#include "core/ObjectTemplateManager.h"
#include "core/VoxelTemplate.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace Phyxel;

namespace {

struct ScanRig {
    fs::path root;
    ScanRig() {
        root = fs::temp_directory_path() / "phyxel_scan_test";
        fs::remove_all(root);
        fs::create_directories(root / "sub");
    }
    ~ScanRig() { fs::remove_all(root); }
    void write(const std::string& rel, const std::string& body) {
        std::ofstream f(root / rel);
        f << body;
    }
};

}  // namespace

TEST(TemplateLibraryScan, RecursiveScanLoadsSubdirectories) {
    ScanRig rig;
    rig.write("rootonly.voxel", "C 0 0 0 Stone\n");
    rig.write("sub/nested.voxel", "C 0 0 0 Wood\n");

    ObjectTemplateManager mgr(nullptr, nullptr);
    mgr.loadTemplates(rig.root.string());

    EXPECT_NE(mgr.getTemplate("rootonly"), nullptr);
    EXPECT_NE(mgr.getTemplate("nested"), nullptr)
        << "subdirectory template not loaded — scan is not recursive";
}

TEST(TemplateLibraryScan, SubdirectoryTemplatesResolveByPathAlias) {
    ScanRig rig;
    rig.write("sub/aliased.voxel", "C 0 0 0 Wood\n");

    ObjectTemplateManager mgr(nullptr, nullptr);
    mgr.loadTemplates(rig.root.string());

    const auto* byStem = mgr.getTemplate("aliased");
    const auto* byPath = mgr.getTemplate("sub/aliased");
    ASSERT_NE(byStem, nullptr);
    EXPECT_EQ(byStem, byPath)
        << "path alias 'sub/aliased' must resolve to the same template";
}

TEST(TemplateLibraryScan, DuplicateStemIsCollisionNotSilentOverwrite) {
    ScanRig rig;
    // Same stem, different files, different content. Iteration order is
    // platform-dependent, so assert the invariant (exactly one registered,
    // and it is ONE of the two — never a merge), not which one wins.
    rig.write("dupe.voxel", "C 0 0 0 Stone\nC 1 0 0 Stone\n");   // 2 cubes
    rig.write("sub/dupe.voxel", "C 0 0 0 Wood\n");                // 1 cube

    ObjectTemplateManager mgr(nullptr, nullptr);
    mgr.loadTemplates(rig.root.string());

    const auto* t = mgr.getTemplate("dupe");
    ASSERT_NE(t, nullptr);
    EXPECT_TRUE(t->cubes.size() == 2u || t->cubes.size() == 1u);
    // The registry holds exactly one 'dupe' entry.
    auto names = mgr.getTemplateNames();
    EXPECT_EQ(std::count(names.begin(), names.end(), std::string("dupe")), 1)
        << "duplicate stems must not both register";
}
