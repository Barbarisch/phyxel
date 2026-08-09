// exportVoxels() must reproduce the canvas EXACTLY.
//
// The canvas is painted at micro resolution, then greedily coarsened into cubes /
// subcubes / microcubes on export. Everything downstream — placement, the world,
// the render — sees only the EXPORT. Every detector written so far reads the
// canvas, so a coarsening that loses or misplaces cells is invisible to all of
// them: the canvas says the wall is a flush unbroken plane (measured), the world
// shows recessed bays you can see the interior through, and nothing in between
// has ever been compared.
//
// Contract: rasterize the export back to micro cells and it is the canvas, cell
// for cell.

#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <string>

#include "core/BuildingProgram.h"
#include "core/MicroCanvas.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

struct Key {
    int x, y, z;
    bool operator<(const Key& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }
};

// Micro cells covered by one exported voxel.
void expand(const CanvasVoxel& v, std::map<Key, std::string>& out) {
    int gx, gy, gz, n;
    switch (v.res) {
        case CanvasRes::Cube:
            gx = v.cube.x * 9; gy = v.cube.y * 9; gz = v.cube.z * 9; n = 9; break;
        case CanvasRes::Subcube:
            gx = v.cube.x * 9 + v.sub.x * 3;
            gy = v.cube.y * 9 + v.sub.y * 3;
            gz = v.cube.z * 9 + v.sub.z * 3; n = 3; break;
        default:
            gx = v.cube.x * 9 + v.sub.x * 3 + v.micro.x;
            gy = v.cube.y * 9 + v.sub.y * 3 + v.micro.y;
            gz = v.cube.z * 9 + v.sub.z * 3 + v.micro.z; n = 1; break;
    }
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            for (int c = 0; c < n; ++c)
                out[{gx + a, gy + b, gz + c}] = v.material;
}

}  // namespace

TEST(CanvasExportFidelity, ExportReproducesTheCanvasCellForCell) {
    RoomProgramRegistry reg;
    bool okc = false;
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json"})
        if (reg.loadFromFile(p)) { okc = true; break; }
    ASSERT_TRUE(okc);
    StyleProfileRegistry styles;
    bool oks = false;
    for (const char* p : {"resources/structure_styles.json", "../resources/structure_styles.json"})
        if (styles.loadFromFile(p)) { oks = true; break; }
    ASSERT_TRUE(oks);
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 20});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(p, 99u, tavern));
    auto shell = StructureRealizer::realizeShell(p, *styles.get(p.style));
    ASSERT_TRUE(shell.ok) << shell.error;

    glm::ivec3 lo(0), hi(0);
    ASSERT_TRUE(shell.canvas.microBounds(lo, hi));

    std::map<Key, std::string> exported;
    for (const auto& v : shell.canvas.exportVoxels()) expand(v, exported);

    int missing = 0, extra = 0, wrongMat = 0;
    Key firstMissing{0, 0, 0};
    bool haveFirst = false;
    for (int gx = lo.x; gx <= hi.x; ++gx)
        for (int gy = lo.y; gy <= hi.y; ++gy)
            for (int gz = lo.z; gz <= hi.z; ++gz) {
                const std::string canv = shell.canvas.materialAt(gx, gy, gz);
                auto it = exported.find({gx, gy, gz});
                const bool inExp = it != exported.end();
                if (!canv.empty() && !inExp) {
                    if (!haveFirst) { firstMissing = {gx, gy, gz}; haveFirst = true; }
                    ++missing;
                } else if (canv.empty() && inExp) {
                    ++extra;
                } else if (!canv.empty() && inExp && it->second != canv) {
                    ++wrongMat;
                }
            }

    std::cout << "[ MEASURED ] export vs canvas: missing=" << missing
              << " extra=" << extra << " wrong_material=" << wrongMat << "\n";
    if (haveFirst)
        std::cout << "  first missing micro (" << firstMissing.x << "," << firstMissing.y
                  << "," << firstMissing.z << ")  == cube (" << firstMissing.x / 9 << ","
                  << firstMissing.y / 9 << "," << firstMissing.z / 9 << ")\n";

    EXPECT_EQ(missing, 0) << "export DROPPED solid cells the canvas painted";
    EXPECT_EQ(extra, 0) << "export invented solid cells the canvas left empty";
    EXPECT_EQ(wrongMat, 0) << "export changed a cell's material";
}
