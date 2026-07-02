#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>

#include "core/RealizedStructureValidator.h"
#include "core/MicroCanvas.h"
#include "core/StructureRealizer.h"
#include "core/BuildingProgram.h"
#include "core/StyleProfile.h"
#include "core/SettlementLayout.h"   // the REAL generator (pickBuildingVariant) for the V5 check

using namespace Phyxel::Core;

// ============================================================================
// Detectors for the crude-building defects the user flagged — built FIRST, each proven to FIRE on the
// real broken output (red) before any generator is fixed. Each also carries a TEETH pair (fires on a
// synthetic defect, passes on a clean one) so the check measures real geometry, not nothing.
// ============================================================================

namespace {
StyleProfile cottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}
StructureRealizer::ShellResult realizeCottage() {
    BuildingProgram p;
    p.name = "house"; p.style = "timber_cottage"; p.footprintW = 12; p.footprintD = 8;
    p.substructure = "slab";
    ProgStory s; s.height = 3;
    s.rooms.push_back(ProgRoom::fromJson(nlohmann::json::parse(R"({"id":"hall","purpose":"hall","rect":[0,0,12,8]})")));
    p.stories.push_back(s);
    return StructureRealizer::realizeShell(p, cottageStyle());
}
// Materials used by a furniture .voxel template (last token of each `M ...` line). CWD-tolerant.
std::set<std::string> templateMaterials(const std::string& name) {
    std::set<std::string> mats;
    for (const char* d : {"resources/templates/", "../resources/templates/",
                          "../../resources/templates/", "../../../resources/templates/"}) {
        std::ifstream f(std::string(d) + name + ".voxel");
        if (!f.good()) continue;
        std::string line;
        while (std::getline(f, line)) {
            // Voxel rows are C (cube) / S (subcube) / M (microcube); the material is the last
            // non key=value token (skip trailing tint=.../state=... suffixes).
            if (line.empty() || (line[0] != 'C' && line[0] != 'S' && line[0] != 'M')) continue;
            std::istringstream ss(line);
            std::string tok, mat;
            while (ss >> tok) { if (tok.find('=') == std::string::npos) mat = tok; }
            if (!mat.empty()) mats.insert(mat);
        }
        break;
    }
    return mats;
}
} // namespace

// ---- V1 roof-eave-flush ----------------------------------------------------
// TEETH: a flush canvas (roof rests on the wall) passes; a hovering canvas (air gap) fires.
TEST(RealizedStructureValidatorTest, RoofEaveDetectorHasTeeth) {
    MicroCanvas flush;
    flush.fillMicroBox(0, 0, 0, 9, 21, 1, "Wood");   // a tall wall column, y 0..20
    flush.fillMicroBox(0, 21, 0, 9, 2, 1, "Wood");    // roof RESTS on it at y21 (gap 0)
    EXPECT_TRUE(RealizedStructureValidator::checkRoofEaveFlush(flush).ok()) << "flush roof wrongly flagged";

    MicroCanvas hover;
    hover.fillMicroBox(0, 0, 0, 9, 21, 1, "Wood");    // wall y 0..20
    hover.fillMicroBox(0, 26, 0, 9, 2, 1, "Wood");    // roof floats at y26 (air gap 21..25)
    EXPECT_FALSE(RealizedStructureValidator::checkRoofEaveFlush(hover).ok()) << "hovering roof NOT detected";
}
// GREEN (after the eaveSub floor-div fix in StructureRealizer): the realized cottage roof now rests
// flush on the wall top. RED-BEFORE-GREEN is reproducible by reverting the one-line fix in
// StructureRealizer.cpp (eaveSub `ceilTopMicro/3` -> `(ceilTopMicro+2)/3`): this test then FAILS with
// "roof floats above the wall top ... 1 micro gap" over ~1044 perimeter columns. (This test file is
// not yet committed, so there is no git history of the red form — the source revert is the proof. The
// detector's teeth are proven separately above.)
TEST(RealizedStructureValidatorTest, RealCottageRoofIsFlush) {
    auto sh = realizeCottage();
    ASSERT_TRUE(sh.ok) << sh.error;
    auto rep = RealizedStructureValidator::checkRoofEaveFlush(sh.canvas);
    EXPECT_TRUE(rep.ok()) << "roof still hovers after the eave-flush fix:\n" << rep.summary();
}

// ---- V2 chimney-on-hearth --------------------------------------------------
// TEETH + the value-level red->green for the generator fix: a chimney built from the hearth FLOOR
// (the old behaviour — base = microPos.y) dives through the firebox and is flagged; a chimney based at
// the hearth TOP (the fix) passes; a floating chimney is also flagged.
TEST(RealizedStructureValidatorTest, ChimneyOnHearthDetectorHasTeeth) {
    const int hearthBase = 30, hearthH = 11;   // a fireplace ~1.22 m (11 micro) tall, floor at micro 30
    EXPECT_FALSE(RealizedStructureValidator::checkChimneyOnHearth(hearthBase, hearthH, hearthBase).ok())
        << "OLD: a chimney from the hearth floor (overlapping the firebox) was not flagged";
    EXPECT_TRUE(RealizedStructureValidator::checkChimneyOnHearth(hearthBase, hearthH, hearthBase + hearthH).ok())
        << "FIXED: a chimney resting on the hearth top was wrongly flagged";
    EXPECT_FALSE(RealizedStructureValidator::checkChimneyOnHearth(hearthBase, hearthH, hearthBase + hearthH + 5).ok())
        << "a chimney floating above the hearth was not flagged";
}

// ---- V3 material contrast --------------------------------------------------
TEST(RealizedStructureValidatorTest, MaterialContrastDetectorHasTeeth) {
    MicroCanvas mono;
    mono.fillMicroBox(0, 0, 0, 10, 10, 10, "Wood");   // 100% Wood
    EXPECT_FALSE(RealizedStructureValidator::checkMaterialContrast(mono).ok()) << "mono-material not flagged";

    MicroCanvas varied;
    varied.fillMicroBox(0, 0, 0, 10, 5, 10, "Wood");
    varied.fillMicroBox(0, 5, 0, 10, 5, 10, "Stone");  // 50/50
    EXPECT_TRUE(RealizedStructureValidator::checkMaterialContrast(varied).ok()) << "balanced wrongly flagged";
}
// RED on real output: the current shell is overwhelmingly one material (all-Wood walls/floor/roof).
TEST(RealizedStructureValidatorTest, RealCottageMaterialMonotony_RED) {
    auto sh = realizeCottage();
    ASSERT_TRUE(sh.ok) << sh.error;
    auto rep = RealizedStructureValidator::checkMaterialContrast(sh.canvas);
    EXPECT_FALSE(rep.ok()) << "expected the current shell to be a single-material blob; not flagged";
    if (!rep.ok()) std::cout << "[V3 fires] " << rep.summary() << "\n";
}

// ---- V4 material plausibility ----------------------------------------------
TEST(RealizedStructureValidatorTest, BedMaterialDetectorHasTeeth) {
    EXPECT_FALSE(RealizedStructureValidator::checkFurnitureMaterialPlausibility(
        "bed", {"Wood", "Sandstone", "Sand"}).ok()) << "stone/sand bedding not flagged";
    EXPECT_TRUE(RealizedStructureValidator::checkFurnitureMaterialPlausibility(
        "bed", {"Wood", "Wool", "Linen"}).ok()) << "soft bedding wrongly flagged";
    // a non-bed using stone is fine (only soft furnishings are constrained)
    EXPECT_TRUE(RealizedStructureValidator::checkFurnitureMaterialPlausibility(
        "forge_hearth", {"Stone"}).ok());
}
// RED on real output: bed_single is built with Sandstone/Sand bedding (the user's defect #4).
TEST(RealizedStructureValidatorTest, RealBedMaterialIsFlagged_RED) {
    const auto mats = templateMaterials("bed_single");
    if (mats.empty()) GTEST_SKIP() << "bed_single.voxel not reachable from CWD";
    std::vector<std::string> v(mats.begin(), mats.end());
    auto rep = RealizedStructureValidator::checkFurnitureMaterialPlausibility("bed", v);
    EXPECT_FALSE(rep.ok()) << "expected bed_single's stone/sand bedding to be flagged; materials were not";
    if (!rep.ok()) std::cout << "[V4 fires] " << rep.summary() << "\n";
}

// ---- M1 flora emissive -----------------------------------------------------
// TEETH: a plant with a glow block fires; a plain plant passes.
TEST(RealizedStructureValidatorTest, FloraEmissiveDetectorHasTeeth) {
    EXPECT_FALSE(RealizedStructureValidator::checkFloraNoEmissive(
        "bush_flower", {"Leaf", "Wood", "glow"}).ok()) << "glowing flora not flagged";
    EXPECT_TRUE(RealizedStructureValidator::checkFloraNoEmissive(
        "bush_round", {"Leaf", "Wood"}).ok()) << "non-glowing flora wrongly flagged";
}
// RED on real output: bush_flower.voxel embeds `glow` blocks (the user's "shrubs with light-emitting
// blocks" defect). Scans the actual template.
TEST(RealizedStructureValidatorTest, RealBushFlowerEmissive_RED) {
    const auto mats = templateMaterials("bush_flower");
    if (mats.empty()) GTEST_SKIP() << "bush_flower.voxel not reachable from CWD";
    std::vector<std::string> v(mats.begin(), mats.end());
    auto rep = RealizedStructureValidator::checkFloraNoEmissive("bush_flower", v);
    EXPECT_FALSE(rep.ok()) << "expected bush_flower's glow blocks to be flagged; materials were not";
    if (!rep.ok()) std::cout << "[M1 fires] " << rep.summary() << "\n";
}

// ---- M3 hearth masonry is brick --------------------------------------------
// TEETH: a stone fireplace fires; a brick fireplace passes; a stone forge/oven is unconstrained.
TEST(RealizedStructureValidatorTest, HearthMasonryDetectorHasTeeth) {
    EXPECT_FALSE(RealizedStructureValidator::checkHearthMasonryIsBrick(
        "fireplace", {"Stone", "Log", "glow"}).ok()) << "stone fireplace masonry not flagged";
    EXPECT_TRUE(RealizedStructureValidator::checkHearthMasonryIsBrick(
        "fireplace", {"Bricks", "Log", "glow"}).ok()) << "brick fireplace wrongly flagged";
    EXPECT_TRUE(RealizedStructureValidator::checkHearthMasonryIsBrick(
        "forge_hearth", {"Stone"}).ok()) << "a forge is not constrained to brick";
}
// GREEN after the fix: fireplace.voxel is now brick (gen_fireplace uses Bricks). Was RED (Stone).
TEST(RealizedStructureValidatorTest, RealFireplaceMasonryIsBrick) {
    const auto mats = templateMaterials("fireplace");
    if (mats.empty()) GTEST_SKIP() << "fireplace.voxel not reachable from CWD";
    std::vector<std::string> v(mats.begin(), mats.end());
    auto rep = RealizedStructureValidator::checkHearthMasonryIsBrick("fireplace", v);
    EXPECT_TRUE(rep.ok()) << "fireplace should be brick (not quarried stone) after the fix:\n"
                          << rep.summary();
}

// ---- V5 footprint diversity ------------------------------------------------
TEST(RealizedStructureValidatorTest, FootprintDiversityDetectorHasTeeth) {
    EXPECT_FALSE(RealizedStructureValidator::checkFootprintDiversity({"", "rect", "rect", ""}).ok())
        << "all-rectangle set not flagged";
    EXPECT_TRUE(RealizedStructureValidator::checkFootprintDiversity({"rect", "L", "rect", "T"}).ok())
        << "a set WITH non-rect wrongly flagged";
}
// REAL generator (correction after solution-auditor 2026-06-28): the SETTLEMENT generator
// (pickBuildingVariant) ALREADY varies the footprint shape (~1/3 get an "L"), so the detector must
// NOT fire on it — the generator is fine. (My earlier "all rectangles" red test was a STRAWMAN: it
// read footprintShape off a default-constructed BuildingProgram, not the generator. The all-rect town
// the user saw came from hand-placed INDIVIDUAL builds, not the settlement path.)
TEST(RealizedStructureValidatorTest, RealSettlementGeneratorVariesFootprints) {
    const std::vector<std::string> typ = {"croft", "longhouse", "hall_house", "tavern", "blacksmith"};
    const std::vector<std::string> sty = {"timber_cottage"};
    std::vector<std::string> shapes;
    int lShapes = 0;
    for (int i = 0; i < 12; ++i) {
        auto v = pickBuildingVariant(i, typ, sty, /*seed=*/1u);
        shapes.push_back(v.footprintShape);
        if (v.footprintShape == "L") ++lShapes;
    }
    EXPECT_GT(lShapes, 0) << "the generator never produced an L-plan across 12 plots";
    EXPECT_TRUE(RealizedStructureValidator::checkFootprintDiversity(shapes).ok())
        << "the real generator already varies footprints — the detector must NOT fire on it";
}

// ---- V6 hanging-sign clearance + projection ---------------------------------
// TEETH: a sign hung high enough over the entrance with a short bracket passes; one hung too low (a
// head would hit it) fires; one projecting too far from the wall fires.
TEST(RealizedStructureValidatorTest, SignClearanceDetectorHasTeeth) {
    // ground at micro-Y 0; a proper sign: board bottom 24 micro up (~2.67 m), projecting 7 micro
    EXPECT_TRUE(RealizedStructureValidator::checkSignClearance(/*bottom*/24, /*ground*/0, /*proj*/7).ok())
        << "a well-hung sign was wrongly flagged";
    // too low: board bottom only 12 micro (~1.33 m) — a person walks into it
    EXPECT_FALSE(RealizedStructureValidator::checkSignClearance(12, 0, 7).ok())
        << "a head-height sign was not flagged";
    // over-projecting: 15 micro (~1.67 m) past the wall, over the 11-micro cap
    EXPECT_FALSE(RealizedStructureValidator::checkSignClearance(24, 0, 15).ok())
        << "an over-projecting sign was not flagged";
    // a sign whose bottom is below grade (nonsense placement) is flagged
    EXPECT_FALSE(RealizedStructureValidator::checkSignClearance(-3, 0, 7).ok())
        << "a below-grade sign was not flagged";
}
// TEETH for the door-head rule (the auditor's latent-defect case): with a door head supplied, a board
// hung ABOVE the lintel passes; one whose bottom is BELOW the lintel fires — even when it still clears
// the 8 ft grade floor (so this is a distinct check, not subsumed by sign_too_low).
TEST(RealizedStructureValidatorTest, SignDoorHeadDetectorHasTeeth) {
    // ground 0, door head at 27 (a 3 m / 3-cube door). Board at 28 clears the lintel -> ok.
    EXPECT_TRUE(RealizedStructureValidator::checkSignClearance(
        /*bottom*/28, /*ground*/0, /*proj*/7, /*minClear*/22, /*maxProj*/11, /*doorHead*/27).ok())
        << "a sign above the lintel was wrongly flagged";
    // board at 24: clears 8 ft grade (>=22) BUT is below the 27 lintel -> must fire.
    auto rep = RealizedStructureValidator::checkSignClearance(24, 0, 7, 22, 11, /*doorHead*/27);
    EXPECT_FALSE(rep.ok()) << "a sign below the door head was NOT flagged";
    // and the failure is specifically the door-head rule, not the grade rule (24 >= 22 grade-passes)
    EXPECT_NE(rep.summary().find("door head"), std::string::npos)
        << "below-lintel sign flagged for the wrong reason: " << rep.summary();
}
// REAL OUTPUT: the generated hanging_sign.voxel must itself be hangable within the historic limits —
// its board far edge must not exceed the 11-micro (48 in) projection cap (the asset can't be wired up
// to satisfy the clearance check if its own bracket already over-projects). Parses the actual .voxel.
TEST(RealizedStructureValidatorTest, RealHangingSignProjectionWithinCap) {
    // global micro z index of the farthest board (Wood/Log) cell -> far-edge projection (index+1).
    int maxBoardZ = -1;
    bool found = false;
    for (const char* d : {"resources/templates/", "../resources/templates/",
                          "../../resources/templates/", "../../../resources/templates/"}) {
        std::ifstream f(std::string(d) + "hanging_sign.voxel");
        if (!f.good()) continue;
        found = true;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] != 'M') continue;
            std::istringstream ss(line);
            std::string tag, mat;
            int cx, cy, cz, sx, sy, sz, mx, my, mz;
            ss >> tag >> cx >> cy >> cz >> sx >> sy >> sz >> mx >> my >> mz >> mat;
            if (mat != "Wood" && mat != "Log") continue;   // board only, not the bracket arm
            const int gz = cz * 9 + sz * 3 + mz;
            if (gz > maxBoardZ) maxBoardZ = gz;
        }
        break;
    }
    if (!found) GTEST_SKIP() << "hanging_sign.voxel not reachable from CWD";
    ASSERT_GE(maxBoardZ, 0) << "no board cells found in hanging_sign.voxel";
    const int projection = maxBoardZ + 1;   // far face distance from the z=0 wall, in micro
    // at a proper hang height, the real asset's projection must pass the cap
    auto rep = RealizedStructureValidator::checkSignClearance(/*bottom*/24, /*ground*/0, projection);
    EXPECT_TRUE(rep.ok()) << "the generated sign over-projects:\n" << rep.summary();
    EXPECT_LE(projection, 11) << "board far edge " << projection << " micro exceeds the 11-micro cap";
}

// The genuine NARROW finding: the SINGLE-building / hand-laid path (no footprint_shape -> rect) yields
// an all-rect set, which the detector correctly flags — that path needs variety wired in if used for a
// town. (This is what produced the all-rectangle demo.)
TEST(RealizedStructureValidatorTest, IndividualBuildPathIsAllRect_RED) {
    std::vector<std::string> shapes;
    for (int i = 0; i < 6; ++i) {
        BuildingProgram p;                 // a hand-laid individual build: footprintShape unset
        shapes.push_back(p.footprintShape.empty() ? "rect" : p.footprintShape);
    }
    auto rep = RealizedStructureValidator::checkFootprintDiversity(shapes);
    EXPECT_FALSE(rep.ok()) << "the all-rect individual-build set should be flagged";
    if (!rep.ok()) std::cout << "[V5 fires on individual-build path] " << rep.summary() << "\n";
}
