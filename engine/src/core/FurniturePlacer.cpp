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
    return {"kitchen", "bedchamber", "hall", "store", "other"};
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

std::vector<FurniturePlacement> FurniturePlacer::furnish(const ProgStory& story,
                                                         const glm::ivec3& origin, int floorY) {
    std::vector<FurniturePlacement> out;
    for (const auto& room : story.rooms) {
        const int rx = room.rect.x, rz = room.rect.z, rw = room.rect.w, rd = room.rect.d;
        if (rw < 2 || rd < 2) continue;   // too small to furnish

        // Which of the 4 walls carry a door/window (avoid backing furniture onto them).
        bool doorWall[4] = {false, false, false, false};
        for (const auto& po : story.portals) {
            if (po.a != room.id && po.b != room.id) continue;
            if (po.px == rx)        doorWall[0] = true;
            else if (po.px == rx + rw) doorWall[1] = true;
            if (po.pz == rz)        doorWall[2] = true;
            else if (po.pz == rz + rd) doorWall[3] = true;
        }

        auto wallCell = [&](int w) -> std::pair<int, int> {
            switch (w) {
                case 0:  return {rx,          rz + rd / 2};
                case 1:  return {rx + rw - 1, rz + rd / 2};
                case 2:  return {rx + rw / 2, rz};
                default: return {rx + rw / 2, rz + rd - 1};
            }
        };

        std::set<std::pair<int, int>> occupied;
        bool usedWall[4] = {false, false, false, false};
        auto place = [&](const std::string& type, int cx, int cz, int rot) {
            if (!occupied.insert({cx, cz}).second) return;  // already a piece here
            FurniturePlacement f;
            f.type = type;
            f.room = room.id;
            f.rotation = rot;
            f.worldPos = glm::ivec3(origin.x + cx, floorY, origin.z + cz);
            out.push_back(f);
        };

        for (const auto& piece : recipeFor(room.purpose)) {
            if (piece.center) {
                place(piece.type, rx + rw / 2, rz + rd / 2, 0);
                continue;
            }
            // Prefer a wall with no door and not yet used; fall back to any unused wall.
            int chosen = -1;
            for (int w = 0; w < 4; ++w) if (!doorWall[w] && !usedWall[w]) { chosen = w; break; }
            if (chosen < 0) for (int w = 0; w < 4; ++w) if (!usedWall[w]) { chosen = w; break; }
            if (chosen < 0) chosen = 0;
            usedWall[chosen] = true;
            auto cell = wallCell(chosen);
            place(piece.type, cell.first, cell.second,
                  facingIntoRoom(WALLS[chosen].inwardDx, WALLS[chosen].inwardDz));
        }
    }
    return out;
}

} // namespace Core
} // namespace Phyxel
