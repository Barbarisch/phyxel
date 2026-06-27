#include "core/FurniturePlacer.h"

#include <cctype>
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
    if (has("taproom") || has("tap"))                return {{"tavern_bar", false}, {"back_bar", false}, {"bar_stool", false}, {"tavern_table", true}, {"bench", false}, {"fireplace", false}};
    if (has("kitchen"))                              return {{"counter", false}, {"fireplace", false}};
    if (has("bed") || has("chamber") || has("solar")) return {{"bed", false}, {"chest", false}};
    if (has("hall") || has("living") || has("great")) return {{"fireplace", false}, {"table", true}, {"bench", false}};
    if (has("service") || has("pantry") || has("store")) return {{"barrel", false}, {"chest", false}};
    return {{"chest", false}};
}

} // namespace

std::vector<std::string> FurniturePlacer::requiredFurniture(const std::string& purpose) {
    std::vector<std::string> types;
    for (const auto& pc : recipeFor(purpose)) types.push_back(pc.type);
    return types;
}

std::vector<std::string> FurniturePlacer::knownPurposes() {
    // One representative per recipe branch in recipeFor(); their union is the full vocabulary.
    return {"taproom", "kitchen", "bedchamber", "hall", "store", "other"};
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

std::vector<FurniturePlacement> FurniturePlacer::furnish(
    const ProgStory& story, const glm::ivec3& origin, int floorY,
    const std::map<std::string, Footprint>& footprints) {
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
        bool usedWall[4] = {false, false, false, false};
        auto footprintOf = [&](const std::string& type) -> Footprint {
            auto it = footprints.find(type);
            return it == footprints.end() ? Footprint{} : it->second;
        };
        // Cells a piece of (width along the wall, depth into the room) covers when backed onto wall
        // `w` (or centred). Anchored at the wall, centred along it. Returns {} if degenerate.
        auto coverFor = [&](int w, Footprint fp, bool center) -> std::vector<std::pair<int, int>> {
            const int width = std::max(1, fp.width), depth = std::max(1, fp.depth);
            int x0, z0, ew, ed;   // origin + extents in (x, z)
            if (center) {
                x0 = rx + rw / 2 - width / 2; z0 = rz + rd / 2 - depth / 2; ew = width; ed = depth;
            } else if (w == 0) {            // west wall, depth extends +x
                x0 = rx;                 z0 = rz + rd / 2 - width / 2; ew = depth; ed = width;
            } else if (w == 1) {            // east wall, depth extends -x
                x0 = rx + rw - depth;    z0 = rz + rd / 2 - width / 2; ew = depth; ed = width;
            } else if (w == 2) {            // south wall, depth extends +z
                x0 = rx + rw / 2 - width / 2; z0 = rz;               ew = width; ed = depth;
            } else {                        // north wall, depth extends -z
                x0 = rx + rw / 2 - width / 2; z0 = rz + rd - depth;  ew = width; ed = depth;
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
            int mx = cells[0].first, mz = cells[0].second;
            for (const auto& c : cells) { occupied.insert(c); mx = std::min(mx, c.first); mz = std::min(mz, c.second); }
            FurniturePlacement f;
            f.type = type; f.room = room.id; f.rotation = rot;
            f.worldPos = glm::ivec3(origin.x + mx, floorY, origin.z + mz);   // anchor = footprint corner
            out.push_back(f);
        };

        for (const auto& piece : recipeFor(room.purpose)) {
            const Footprint fp = footprintOf(piece.type);
            if (piece.center) {
                auto cells = coverFor(0, fp, /*center=*/true);
                if (fits(cells)) reserve(cells, piece.type, 0);
                continue;
            }
            // Try walls in preference order, placing the footprint on the first wall where it
            // actually fits. A piece that fits nowhere is skipped (not forced into an overlap).
            int order[4]; int n = 0;
            for (int w = 0; w < 4; ++w) if (!usedWall[w] && !doorWall[w]) order[n++] = w;  // free, no door
            for (int w = 0; w < 4; ++w) if (!usedWall[w] &&  doorWall[w]) order[n++] = w;  // free door wall
            for (int k = 0; k < n; ++k) {
                const int w = order[k];
                auto cells = coverFor(w, fp, /*center=*/false);
                if (!fits(cells)) continue;
                usedWall[w] = true;
                reserve(cells, piece.type, facingIntoRoom(WALLS[w].inwardDx, WALLS[w].inwardDz));
                break;
            }
        }
    }
    return out;
}

} // namespace Core
} // namespace Phyxel
