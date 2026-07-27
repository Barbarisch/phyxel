#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <tuple>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Canvas digest harness (Claims Ledger; docs/structure-generation/ClaimsLedger.md).
// NOT a golden test: it writes a deterministic digest of realized canvases to
// canvas_digest_actual.txt (gitignored). Recording-only pipeline increments
// prove "ZERO canvas change" by running this harness on the before and after
// builds and diffing the two artifacts. Fixtures deliberately exercise every
// painting path in realizeShell: stairs, exterior door + window trim on all
// four walls (both axes, both signs), shuttered + glazed infill (open and
// closed leaves), interior door + arch on both partition axes, and quoins.
// ============================================================================

namespace {

uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

uint64_t digestCanvas(const MicroCanvas& c, size_t& cellsOut) {
    auto cells = c.occupiedCells();
    std::sort(cells.begin(), cells.end(), [](const glm::ivec3& a, const glm::ivec3& b) {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
    uint64_t h = 1469598103934665603ull;
    for (const auto& v : cells) {
        h = fnv1a(h, &v.x, sizeof(int));
        h = fnv1a(h, &v.y, sizeof(int));
        h = fnv1a(h, &v.z, sizeof(int));
        const std::string m = c.materialAt(v.x, v.y, v.z);
        h = fnv1a(h, m.data(), m.size());
    }
    cellsOut = cells.size();
    return h;
}

StyleProfile styleFromJson(const char* id, const std::string& json) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(json));
    return *reg.get(id);
}

StyleProfile plainStyle() {
    return styleFromJson("timber_cottage", R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "WoodPlanks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Log" },
            "roof": { "pitch": 0.8 }
        }
    })");
}

StyleProfile quoinStyle() {
    return styleFromJson("stone_quoins", R"({
        "stone_quoins": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "StoneBricks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Sandstone" },
            "flags": { "quoins": true },
            "roof": { "pitch": 0.8 }
        }
    })");
}

ProgPortal portal(const std::string& a, const std::string& b, int px, int pz,
                  int w, int h, const std::string& kind, const std::string& infill = "open") {
    ProgPortal p; p.a = a; p.b = b; p.px = px; p.pz = pz;
    p.width = w; p.height = h; p.kind = kind; p.infill = infill;
    return p;
}

// Exterior door + windows on ALL FOUR walls (px==0, px==W, pz==0, pz==D) with
// shuttered AND glazed infill - covers both alongZ branches, both alongX
// branches, sill/ledge, jamb/lintel, and closed + open leaf paths.
BuildingProgram windowsAllWalls() {
    BuildingProgram p;
    p.name = "digest_windows"; p.style = "timber_cottage";
    p.footprintW = 8; p.footprintD = 9; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom r; r.id = "hall"; r.rect = {0, 0, 8, 9}; r.purpose = "living";
    st.rooms.push_back(r);
    st.portals.push_back(portal("exterior", "hall", 0, 3, 1, 2, "door"));
    st.portals.push_back(portal("exterior", "hall", 0, 6, 2, 1, "window", "shuttered"));  // west
    st.portals.push_back(portal("exterior", "hall", 8, 2, 2, 1, "window", "glass"));      // east
    st.portals.push_back(portal("exterior", "hall", 3, 0, 2, 1, "window", "shuttered"));  // south
    st.portals.push_back(portal("exterior", "hall", 5, 9, 2, 1, "window", "glass"));      // north
    p.stories.push_back(st);
    return p;
}

// Interior door (x-axis partition) + interior arch (z-axis partition).
BuildingProgram interiorOpenings() {
    BuildingProgram p;
    p.name = "digest_interior"; p.style = "timber_cottage";
    p.footprintW = 9; p.footprintD = 9; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "a"; a.rect = {0, 0, 4, 9}; a.purpose = "living";
    ProgRoom b; b.id = "b"; b.rect = {4, 0, 5, 5}; b.purpose = "kitchen";
    ProgRoom cRoom; cRoom.id = "c"; cRoom.rect = {4, 5, 5, 4}; cRoom.purpose = "storage";
    st.rooms.push_back(a); st.rooms.push_back(b); st.rooms.push_back(cRoom);
    st.portals.push_back(portal("exterior", "a", 0, 4, 1, 2, "door"));
    st.portals.push_back(portal("a", "b", 4, 2, 1, 2, "door"));   // partition at x=4
    st.portals.push_back(portal("b", "c", 6, 5, 1, 2, "arch"));   // partition at z=5
    p.stories.push_back(st);
    return p;
}

// Two stories + a switchback stair (stair pass + stairwell hole).
BuildingProgram twoStoryStair() {
    BuildingProgram p;
    p.name = "digest_stair"; p.style = "timber_cottage";
    p.footprintW = 7; p.footprintD = 9; p.substructure = "slab";
    for (int s = 0; s < 2; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = s ? "loft" : "hall"; r.rect = {0, 0, 7, 9};
        r.purpose = s ? "bedroom" : "living";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    p.stories[0].portals.push_back(portal("exterior", "hall", 0, 3, 1, 2, "door"));
    ProgStair sr; sr.fromStory = 0; sr.toStory = 1;
    sr.rect = {2, 3, 2, 4}; sr.form = "switchback";
    p.stories[0].stairs.push_back(sr);
    return p;
}

// Rectangular stone building with quoins (pass 4.5) + a door + a glazed window.
BuildingProgram quoinBuilding() {
    BuildingProgram p;
    p.name = "digest_quoins"; p.style = "stone_quoins";
    p.footprintW = 8; p.footprintD = 6; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom r; r.id = "hall"; r.rect = {0, 0, 8, 6}; r.purpose = "living";
    st.rooms.push_back(r);
    st.portals.push_back(portal("exterior", "hall", 3, 0, 1, 2, "door"));
    st.portals.push_back(portal("exterior", "hall", 5, 6, 2, 1, "window", "glass"));
    p.stories.push_back(st);
    return p;
}

} // namespace

TEST(CanvasDigestTest, WriteDigestArtifact) {
    struct Fixture { const char* name; BuildingProgram prog; StyleProfile style; };
    const Fixture fixtures[] = {
        {"windows_all_walls", windowsAllWalls(), plainStyle()},
        {"interior_openings", interiorOpenings(), plainStyle()},
        {"two_story_stair",   twoStoryStair(),   plainStyle()},
        {"quoin_building",    quoinBuilding(),   quoinStyle()},
    };

    std::ofstream out("canvas_digest_actual.txt");
    ASSERT_TRUE(out.good());
    for (const auto& f : fixtures) {
        auto r = StructureRealizer::realizeShell(f.prog, f.style);
        ASSERT_TRUE(r.ok) << f.name << ": " << r.error;
        ASSERT_FALSE(r.canvas.empty()) << f.name;
        size_t cells = 0;
        const uint64_t h = digestCanvas(r.canvas, cells);
        char line[128];
        snprintf(line, sizeof(line), "%s cells=%zu fnv=%016llx\n",
                 f.name, cells, static_cast<unsigned long long>(h));
        out << line;
    }
}
