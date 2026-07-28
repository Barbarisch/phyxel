#include <gtest/gtest.h>
#include "graphics/RenderCoordinator.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Phyxel::Graphics::RenderCoordinator;

// Regression pin for the 2026-07-27 "invisible creatures" bug.
//
// Every animated character in the scene batches into ONE shared instance buffer
// (RenderCoordinator::kCharacterInstanceCapacity), sized in PARTS, not characters.
// A character whose slice lands past the cap is not drawn at all; before the
// all-or-nothing clamp, one straddling the boundary drew only some of its
// part-groups and rendered as a collapsed blob (which is what made a perfectly
// healthy deer.anim look like a corrupt asset).
//
// The capacity was 10000 — fine for the hand-authored humanoid rigs (a few hundred
// parts) but only ~3 of the imported Quaternius creature rigs, which carry 3.0-4.7k
// parts each. The failure was totally silent.
//
// This test reads the ACTUAL shipped .anim library and fails if the budget can no
// longer hold a reasonable crowd of the densest rig present — so importing an even
// denser creature in future trips here instead of silently vanishing in-game.
namespace {

// One 'Box' line in the MODEL section == one renderable character part.
int countBoxLines(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return -1;
    int n = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Box ", 0) == 0) ++n;
    }
    return n;
}

} // namespace

TEST(CharacterInstanceBudgetTest, BudgetHoldsACrowdOfTheDensestShippedRig) {
    const std::filesystem::path dir = "resources/animated_characters";
    ASSERT_TRUE(std::filesystem::exists(dir))
        << "run tests from the repo root — " << dir << " not found";

    int densestParts = 0;
    std::string densestRig;
    int rigsScanned = 0;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".anim") continue;
        const int parts = countBoxLines(entry.path());
        if (parts <= 0) continue;
        ++rigsScanned;
        if (parts > densestParts) {
            densestParts = parts;
            densestRig = entry.path().filename().string();
        }
    }

    ASSERT_GT(rigsScanned, 0) << "no .anim rigs found to measure";
    ASSERT_GT(densestParts, 0);

    const uint64_t required =
        static_cast<uint64_t>(densestParts) * RenderCoordinator::kMinSimultaneousDenseCreatures;

    EXPECT_GE(RenderCoordinator::kCharacterInstanceCapacity, required)
        << "Character instance buffer is too small for the shipped rig library.\n"
        << "  densest rig      : " << densestRig << " (" << densestParts << " parts)\n"
        << "  required (x" << RenderCoordinator::kMinSimultaneousDenseCreatures << ") : "
        << required << "\n"
        << "  capacity         : " << RenderCoordinator::kCharacterInstanceCapacity << "\n"
        << "Characters past the cap render NOTHING and log a warning. Raise "
           "RenderCoordinator::kCharacterInstanceCapacity.";
}

// The historical value, pinned so nobody "tidies" it back down. 10000 could not
// even hold three of the dense creature rigs.
TEST(CharacterInstanceBudgetTest, CapacityIsWellAboveTheOldSilentlyFailingValue) {
    EXPECT_GT(RenderCoordinator::kCharacterInstanceCapacity, 10000u);
}
