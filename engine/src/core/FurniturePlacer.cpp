#include "core/FurniturePlacer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace Phyxel {
namespace Core {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// wall ids: 0 = min-x, 1 = max-x, 2 = min-z, 3 = max-z; value = INWARD normal.
struct WallDef { int inwardDx, inwardDz; };
const WallDef WALLS[4] = {{+1, 0}, {-1, 0}, {0, +1}, {0, -1}};

struct Piece { std::string type; bool center; };

// What furniture a room gets, by purpose. Casegoods back onto a wall; a table is centred.
std::vector<Piece> recipeFor(const std::string& purpose) {
    const std::string p = lower(purpose);
    auto has = [&](const char* k) { return p.find(k) != std::string::npos; };
    if (has("taproom") || has("tap"))                return {{"tavern_bar", false}, {"back_bar", false}, {"bar_stool", false}, {"tavern_table", true}, {"bench", false}, {"fireplace", false}, {"candle_stand", false}};
    if (has("kitchen"))                              return {{"counter", false}, {"fireplace", false}};
    if (has("bed") || has("chamber") || has("solar")) return {{"bed", false}, {"chest", false}};
    if (has("hall") || has("living") || has("great")) return {{"fireplace", false}, {"table", true}, {"bench", false}};
    if (has("forge") || has("smith") || has("anvil"))    return {{"forge_hearth", false}, {"anvil", true}, {"bellows", false}, {"tool_rack", false}, {"barrel", false}};
    // bakehouse (bakery): the vented masonry bread oven on the back wall + a kneading counter + flour
    // barrels. The oven is fire-managed + gets a place_chimney stack (like the forge).
    if (has("bakehouse") || has("oven") || has("bake")) return {{"oven_bread", false}, {"counter", false}, {"barrel", false}};
    // butcher's shop (shambles): a stall-board counter (street display) + the chopping block (the
    // defining work fixture) + a meat rail of iron hooks + a stock barrel. New grounded assets.
    if (has("shambles") || has("butcher")) return {{"counter", false}, {"chopping_block", true}, {"meat_rail", false}, {"barrel", false}};
    // apothecary dispensary: a dispensing counter + shelves of jars/bottles behind (back_bar carries
    // bottles) + a coffer + a candelabra for the dim apothecary light. Reuses grounded assets.
    if (has("dispensary") || has("apothecary")) return {{"counter", false}, {"back_bar", false}, {"chest", false}, {"candle_stand", false}};
    // shopfront / salesroom (general store, merchant): a sales counter + shelved goods on the back
    // wall + stock barrels + a coffer (the strongbox/till). All reuse existing grounded assets.
    if (has("sales") || has("shopfront") || has("shopfloor")) return {{"counter", false}, {"back_bar", false}, {"barrel", false}, {"chest", false}};
    if (has("service") || has("pantry") || has("store")) return {{"barrel", false}, {"chest", false}};
    return {{"chest", false}};
}

} // namespace

CubeSpan placedCubeSpan(int microW, int microD, int rotation, const glm::ivec3& backDir,
                        int extTMicro, int baseCubeX, int baseCubeZ) {
    auto floorDiv9 = [](int a) { int q = a / 9; if (a % 9 != 0 && a < 0) --q; return q; };
    const int r = ((rotation % 360) + 360) % 360;
    const int Wm = (r == 90 || r == 270) ? microD : microW;   // rotated micro extents
    const int Dm = (r == 90 || r == 270) ? microW : microD;
    // anchor = footprint corner in micro, inset toward the room by extT on each wall-abutting axis
    const int Ox = baseCubeX * 9 - backDir.x * extTMicro;
    const int Oz = baseCubeZ * 9 - backDir.z * extTMicro;
    CubeSpan s;
    s.minX = floorDiv9(Ox);  s.maxX = floorDiv9(Ox + Wm);
    s.minZ = floorDiv9(Oz);  s.maxZ = floorDiv9(Oz + Dm);
    return s;
}

std::vector<std::string> FurniturePlacer::requiredFurniture(const std::string& purpose) {
    std::vector<std::string> types;
    for (const auto& pc : recipeFor(purpose)) types.push_back(pc.type);
    return types;
}

std::vector<FurniturePlacement> FurniturePlacer::placeSurfaceClutter(
    const std::string& room, const Rect& surface, int topY,
    const std::vector<std::string>& items, unsigned seed) {
    std::vector<FurniturePlacement> out;
    if (surface.w <= 0 || surface.d <= 0 || items.empty()) return out;
    // candidate cells = the surface footprint; each holds at most one clutter item (no overlap).
    std::vector<glm::ivec2> cells;
    for (int x = surface.x; x < surface.x1(); ++x)
        for (int z = surface.z; z < surface.z1(); ++z)
            cells.push_back({x, z});
    // deterministic Fisher-Yates shuffle (LCG) so the scatter is stable per seed.
    unsigned s = seed ? seed : 1u;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s; };
    for (int i = static_cast<int>(cells.size()) - 1; i > 0; --i)
        std::swap(cells[i], cells[rnd() % static_cast<unsigned>(i + 1)]);
    const size_t n = std::min(items.size(), cells.size());   // never overflow the surface
    for (size_t i = 0; i < n; ++i) {
        FurniturePlacement p;
        p.type = items[i]; p.room = room; p.rotation = 0;
        p.worldPos = glm::ivec3(cells[i].x, topY, cells[i].y);
        out.push_back(p);
    }
    return out;
}

std::vector<std::string> FurniturePlacer::knownPurposes() {
    // One representative per recipe branch in recipeFor(); their union is the full vocabulary.
    return {"taproom", "kitchen", "bedchamber", "hall", "store", "forge", "salesroom", "bakehouse",
            "dispensary", "shambles", "other"};
}

std::vector<FixtureLabel> FurniturePlacer::labelFixtures(
    const ProgStory& story, const std::vector<FurniturePlacement>& placements) {
    // room id -> purpose, and room id -> ordinal among same-purpose rooms (in story order).
    std::map<std::string, std::string> purposeOf;
    std::map<std::string, int> purposeIdxOf;
    std::map<std::string, int> seenPerPurpose;
    for (const auto& rm : story.rooms) {
        purposeOf[rm.id] = rm.purpose;
        purposeIdxOf[rm.id] = seenPerPurpose[rm.purpose]++;   // ordinal among same-purpose rooms
    }
    std::vector<FixtureLabel> labels;
    labels.reserve(placements.size());
    for (const auto& pl : placements) {
        FixtureLabel L;
        L.room = pl.room;
        L.type = pl.type;
        auto itp = purposeOf.find(pl.room);
        L.purpose = (itp == purposeOf.end()) ? std::string() : itp->second;
        auto iti = purposeIdxOf.find(pl.room);
        L.purposeIndex = (iti == purposeIdxOf.end()) ? 0 : iti->second;
        labels.push_back(L);
    }
    return labels;
}

FurniturePlacer::FurnitureEdit FurniturePlacer::planEdit(
    const Rect& room, int curX, int curZ, const std::string& op, int rotationArg) {
    FurnitureEdit e;
    if (room.w < 1 || room.d < 1) { e.error = "degenerate room"; return e; }
    const int rx = room.x, rz = room.z, rw = room.w, rd = room.d;

    // wall id: 0=min-x(west), 1=max-x(east), 2=min-z(south), 3=max-z(north)
    auto wallCell = [&](int wll) -> std::pair<int, int> {
        switch (wll) {
            case 0:  return {rx,          rz + rd / 2};
            case 1:  return {rx + rw - 1, rz + rd / 2};
            case 2:  return {rx + rw / 2, rz};
            default: return {rx + rw / 2, rz + rd - 1};
        }
    };
    auto wallFacing = [&](int wll) {
        switch (wll) {                              // inward normal -> facing INTO the room
            case 0:  return facingIntoRoom(+1, 0);  // off west wall -> face +x (270)
            case 1:  return facingIntoRoom(-1, 0);  // off east wall -> face -x (90)
            case 2:  return facingIntoRoom(0, +1);  // off south wall -> face +z (0)
            default: return facingIntoRoom(0, -1);  // off north wall -> face -z (180)
        }
    };
    auto seatOnWall = [&](int wll) {
        auto c = wallCell(wll);
        e.x = c.first; e.z = c.second; e.rotation = wallFacing(wll); e.ok = true;
    };
    auto currentWall = [&]() {                      // nearest wall to (curX,curZ)
        const int d[4] = {curX - rx, (rx + rw - 1) - curX, curZ - rz, (rz + rd - 1) - curZ};
        int best = 0;
        for (int w = 1; w < 4; ++w) if (d[w] < d[best]) best = w;
        return best;
    };

    if (op == "rotate") {
        e.x = curX; e.z = curZ; e.rotation = ((rotationArg % 360) + 360) % 360; e.ok = true;
        return e;
    }
    if (op == "center")     { e.x = rx + rw / 2; e.z = rz + rd / 2; e.rotation = 0; e.ok = true; return e; }
    if (op == "wall:west")  { seatOnWall(0); return e; }
    if (op == "wall:east")  { seatOnWall(1); return e; }
    if (op == "wall:south") { seatOnWall(2); return e; }
    if (op == "wall:north") { seatOnWall(3); return e; }
    if (op == "opposite_wall") {
        int cur = currentWall();
        int opp = (cur == 0) ? 1 : (cur == 1) ? 0 : (cur == 2) ? 3 : 2;   // 0<->1 (x), 2<->3 (z)
        seatOnWall(opp);
        return e;
    }
    e.error = "unknown op: " + op;
    return e;
}

int FurniturePlacer::facingIntoRoom(int inwardDx, int inwardDz) {
    if (inwardDx > 0) return 270;  // against min-x wall, front +x
    if (inwardDx < 0) return 90;   // against max-x wall, front -x
    if (inwardDz > 0) return 0;    // against min-z wall, front +z
    return 180;                    // against max-z wall, front -z
}

glm::ivec3 FurniturePlacer::microWorldPos(const FurniturePlacement& p, int extTMicro,
                                          int surfaceMicroY) {
    // X/Z: the cube origin in MICRO units, inset by the exterior-wall thickness along -backDir (toward
    // the room) so the piece clears the thin wall band occupying the outer extTMicro of the perimeter
    // cube — flush against the wall's interior face, never inside it. (backDir 0 -> no inset.)
    // Y: the absolute walkable-surface micro-Y, NOT the integer-truncated cube worldPos.y (which sank
    // furniture by the floor thickness). Whole-micro shifts are exact (extTMicro is a micro count).
    return glm::ivec3(p.worldPos.x * 9 - p.backDir.x * extTMicro,
                      surfaceMicroY,
                      p.worldPos.z * 9 - p.backDir.z * extTMicro);
}

std::vector<FurniturePlacement> FurniturePlacer::furnish(
    const ProgStory& story, const glm::ivec3& origin, int floorY,
    const std::map<std::string, Footprint>& footprints,
    std::vector<UnplacedFixture>* unplaced) {
    std::vector<FurniturePlacement> out;
    for (const auto& room : story.rooms) {
        const int rx = room.rect.x, rz = room.rect.z, rw = room.rect.w, rd = room.rect.d;
        if (rw < 2 || rd < 2) continue;   // too small to furnish

        // Which of the 4 walls carry a door/window (avoid backing furniture onto them), and the
        // in-room cells next to each opening (so a deep piece doesn't block a doorway).
        bool doorWall[4] = {false, false, false, false};
        std::set<std::pair<int, int>> blocked;   // doorway thresholds inside this room
        for (const auto& po : story.portals) {
            if (po.a != room.id && po.b != room.id) continue;
            if (po.px == rx)        doorWall[0] = true;
            else if (po.px == rx + rw) doorWall[1] = true;
            if (po.pz == rz)        doorWall[2] = true;
            else if (po.pz == rz + rd) doorWall[3] = true;
            for (int dx = -1; dx <= 0; ++dx)
                for (int dz = -1; dz <= 0; ++dz) {
                    const int cx = po.px + dx, cz = po.pz + dz;
                    if (cx >= rx && cx < rx + rw && cz >= rz && cz < rz + rd) blocked.insert({cx, cz});
                }
        }

        std::set<std::pair<int, int>> occupied;
        auto footprintOf = [&](const std::string& type) -> Footprint {
            auto it = footprints.find(type);
            return it == footprints.end() ? Footprint{} : it->second;
        };
        // Cells a piece covers, backed onto wall `w` at offset `along` (from the wall's run start),
        // OR centred. `width` runs ALONG the wall, `depth` extends INTO the room. {} if degenerate.
        auto coverAt = [&](int w, Footprint fp, int along, bool center) -> std::vector<std::pair<int, int>> {
            const int width = std::max(1, fp.width), depth = std::max(1, fp.depth);
            int x0, z0, ew, ed;
            if (center) {
                x0 = rx + rw / 2 - width / 2; z0 = rz + rd / 2 - depth / 2; ew = width; ed = depth;
            } else if (w == 0) {            // west wall, run along z, depth +x
                x0 = rx;                 z0 = rz + along; ew = depth; ed = width;
            } else if (w == 1) {            // east wall, run along z, depth -x
                x0 = rx + rw - depth;    z0 = rz + along; ew = depth; ed = width;
            } else if (w == 2) {            // south wall, run along x, depth +z
                x0 = rx + along;         z0 = rz;         ew = width; ed = depth;
            } else {                        // north wall, run along x, depth -z
                x0 = rx + along;         z0 = rz + rd - depth; ew = width; ed = depth;
            }
            std::vector<std::pair<int, int>> cells;
            for (int x = x0; x < x0 + ew; ++x)
                for (int z = z0; z < z0 + ed; ++z) cells.push_back({x, z});
            return cells;
        };
        auto fits = [&](const std::vector<std::pair<int, int>>& cells) {
            if (cells.empty()) return false;
            for (const auto& c : cells) {
                if (c.first < rx || c.first >= rx + rw || c.second < rz || c.second >= rz + rd)
                    return false;                               // out of room
                if (occupied.count(c) || blocked.count(c)) return false;  // overlap / doorway
            }
            return true;
        };
        auto reserve = [&](const std::vector<std::pair<int, int>>& cells, const std::string& type,
                           int rot) {
            int mnx = cells[0].first, mnz = cells[0].second, mxx = mnx, mxz = mnz;
            for (const auto& c : cells) {
                occupied.insert(c);
                mnx = std::min(mnx, c.first);  mnz = std::min(mnz, c.second);
                mxx = std::max(mxx, c.first);  mxz = std::max(mxz, c.second);
            }
            // backDir = the OUTWARD normal of EVERY room-edge (wall) the footprint abuts — so the
            // consumer insets off ALL of them, including the perpendicular wall at a corner and an
            // interior partition (any room edge carries a wall). A piece in the interior -> (0,0,0).
            glm::ivec3 bd(0);
            if (mnx == rx)               bd.x = -1;            // at min-x edge: wall on -x
            else if (mxx == rx + rw - 1) bd.x = +1;            // at max-x edge: wall on +x
            if (mnz == rz)               bd.z = -1;
            else if (mxz == rz + rd - 1) bd.z = +1;
            FurniturePlacement f;
            f.type = type; f.room = room.id; f.rotation = rot; f.backDir = bd;
            f.worldPos = glm::ivec3(origin.x + mnx, floorY, origin.z + mnz);   // anchor = footprint corner
            out.push_back(f);
        };

        // Place a piece at the nearest FREE footprint slot to an anchor cell, facing the room centre.
        // This is the work-triangle clustering primitive (the anvil hugs the forge; the quench hugs
        // the anvil) — a tight functional cluster, not a piece marooned at the room centre.
        auto placeNear = [&](std::pair<int, int> anchor, Footprint fp, const std::string& type) -> bool {
            const int width = std::max(1, fp.width), depth = std::max(1, fp.depth);
            int bestX = 0, bestZ = 0, bestD = -1;
            for (int x = rx; x + width <= rx + rw; ++x)
                for (int z = rz; z + depth <= rz + rd; ++z) {
                    std::vector<std::pair<int, int>> cells;
                    for (int cx = x; cx < x + width; ++cx)
                        for (int cz = z; cz < z + depth; ++cz) cells.push_back({cx, cz});
                    if (!fits(cells)) continue;
                    const int ccx = x + width / 2, ccz = z + depth / 2;
                    const int d = std::max(std::abs(ccx - anchor.first), std::abs(ccz - anchor.second));
                    if (bestD < 0 || d < bestD) { bestD = d; bestX = x; bestZ = z; }
                }
            if (bestD < 0) return false;                    // nothing free in the room
            std::vector<std::pair<int, int>> cells;
            for (int cx = bestX; cx < bestX + width; ++cx)
                for (int cz = bestZ; cz < bestZ + depth; ++cz) cells.push_back({cx, cz});
            const int dcx = (rx + rw / 2) - (bestX + width / 2);
            const int dcz = (rz + rd / 2) - (bestZ + depth / 2);
            const int rot = (std::abs(dcx) >= std::abs(dcz))
                ? facingIntoRoom(dcx > 0 ? 1 : (dcx < 0 ? -1 : 0), 0)
                : facingIntoRoom(0, dcz > 0 ? 1 : (dcz < 0 ? -1 : 0));
            reserve(cells, type, rot);
            return true;
        };

        // Forge floor: cluster the work triangle. Detected by room purpose so it only fires for a smithy.
        const std::string roomPurpose = lower(room.purpose);
        const bool forgeFloor = roomPurpose.find("forge") != std::string::npos
                             || roomPurpose.find("smith") != std::string::npos
                             || roomPurpose.find("anvil") != std::string::npos;
        std::pair<int, int> forgeCell{-1, -1}, anvilCell{-1, -1};

        for (const auto& piece : recipeFor(room.purpose)) {
            const Footprint fp = footprintOf(piece.type);
            const int width = std::max(1, fp.width);
            bool placed = false;

            // Work-triangle clustering: the anvil hugs the forge; the quench (barrel) hugs the anvil.
            if (forgeFloor && piece.type == "anvil" && forgeCell.first >= 0)
                placed = placeNear(forgeCell, fp, piece.type);
            else if (forgeFloor && piece.type == "barrel" && anvilCell.first >= 0)
                placed = placeNear(anvilCell, fp, piece.type);

            if (!placed && piece.center) {
                auto cells = coverAt(0, fp, 0, /*center=*/true);
                if (fits(cells)) { reserve(cells, piece.type, 0); placed = true; }
            }
            if (!placed) {
                // PACK along walls: scan non-door walls first, then door walls; within each wall scan
                // every offset for the first free slot. Multiple pieces share a wall (no one-per-wall cap).
                int order[4]; int n = 0;
                for (int w = 0; w < 4; ++w) if (!doorWall[w]) order[n++] = w;   // non-door walls first
                for (int w = 0; w < 4; ++w) if ( doorWall[w]) order[n++] = w;   // then door walls
                for (int k = 0; k < n && !placed; ++k) {
                    const int w = order[k];
                    const int runLen = (w < 2) ? rd : rw;       // walls 0/1 run along z, 2/3 along x
                    for (int along = 0; along + width <= runLen; ++along) {
                        auto cells = coverAt(w, fp, along, /*center=*/false);
                        if (!fits(cells)) continue;
                        // reserve() derives backDir from which room edges the footprint abuts (insets
                        // off every wall it touches — corners + partitions included).
                        reserve(cells, piece.type, facingIntoRoom(WALLS[w].inwardDx, WALLS[w].inwardDz));
                        placed = true;
                        break;
                    }
                }
            }
            if (placed && forgeFloor) {   // remember anchors so the next triangle piece can hug them
                const auto& last = out.back();
                if (piece.type == "forge_hearth")
                    forgeCell = {last.worldPos.x - origin.x, last.worldPos.z - origin.z};
                else if (piece.type == "anvil")
                    anvilCell = {last.worldPos.x - origin.x, last.worldPos.z - origin.z};
            }
            if (!placed && unplaced) unplaced->push_back({room.id, piece.type});  // honest: never silent
        }
    }
    return out;
}

} // namespace Core
} // namespace Phyxel
