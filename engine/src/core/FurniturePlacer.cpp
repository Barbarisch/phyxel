#include "core/FurniturePlacer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/StructureRealizer.h"   // thicknessMicro — the clamped converter the realizer built with
#include "utils/Logger.h"

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

struct Piece {
    std::string type;
    bool center;
    int count = 1;        ///< recipe "count": place up to N of this piece
    double perArea = 0.0; ///< recipe "per_area": one per N floor cells (scales with room size)
    int pass = 1;         ///< M4 furnishing pass rank (see passRankFor): 0 heavy, 1 light,
                          ///< 2 lighting, 3 clutter. Pieces place in pass order, so the
                          ///< structural fixtures claim their spots BEFORE light furniture.
};

// ---- M4 FURNISHING PASSES ------------------------------------------------
// The room is furnished the way a room is actually built out: the heavy, built-in,
// often VENTED fixtures land first and claim their wall (a hearth cannot be shuffled
// aside for a stool — it carries a flue through the roof), then the movable furniture
// packs into what is left, then lighting, then clutter. Before M4 everything placed in
// recipe-declaration order, so a table listed above a fireplace could take the hearth's
// wall.
//
// Pass rank is per-TYPE engine data (like mountFor), not recipe data, so the hardcoded
// fallback recipes order correctly too; a recipe may override it with "pass".
enum PassRank { PASS_HEAVY = 0, PASS_LIGHT = 1, PASS_LIGHTING = 2, PASS_CLUTTER = 3 };

int passRankFromName(const std::string& s) {
    if (s == "heavy")    return PASS_HEAVY;
    if (s == "lighting") return PASS_LIGHTING;
    if (s == "clutter")  return PASS_CLUTTER;
    return PASS_LIGHT;
}

/// True for fixtures that BURN and therefore need a flue to the outside — the
/// subset of heavy fixtures place_chimney (#14) must serve.
bool isVentedType(const std::string& type) {
    return type == "fireplace" || type == "forge_hearth" || type == "oven_bread";
}

int passRankFor(const std::string& type) {
    // Heavy: vented hearths + built-in millwork that defines the room's function.
    if (isVentedType(type)) return PASS_HEAVY;
    if (type == "tavern_bar" || type == "back_bar" || type == "counter" ||
        type == "meat_rail"  || type == "chopping_block")
        return PASS_HEAVY;
    // Lighting (M5 gives this pass its own stage + real point lights).
    if (type == "candle_stand" || type == "wall_lantern" || type == "chandelier")
        return PASS_LIGHTING;
    return PASS_LIGHT;
}

// Map a room's free-text purpose onto the CANONICAL recipe key (the same substring rules the
// old hardcoded map used, factored out so the data recipes share them).
std::string canonicalPurpose(const std::string& purpose) {
    const std::string p = lower(purpose);
    auto has = [&](const char* k) { return p.find(k) != std::string::npos; };
    if (has("taproom") || has("tap"))                          return "taproom";
    if (has("kitchen"))                                        return "kitchen";
    if (has("bed") || has("chamber") || has("solar"))          return "bedchamber";
    if (has("hall") || has("living") || has("great"))          return "hall";
    if (has("forge") || has("smith") || has("anvil"))          return "forge";
    if (has("bakehouse") || has("oven") || has("bake"))        return "bakehouse";
    if (has("shambles") || has("butcher"))                     return "shambles";
    if (has("dispensary") || has("apothecary"))                return "dispensary";
    if (has("sales") || has("shopfront") || has("shopfloor"))  return "salesroom";
    if (has("service") || has("pantry") || has("store"))       return "service";
    return "default";
}

// The legacy hardcoded map — the FALLBACK when no data recipe covers a purpose (never
// tier-filtered: it predates tiers).
std::vector<Piece> hardcodedRecipeFor(const std::string& canon) {
    if (canon == "taproom")    return {{"tavern_bar", false}, {"back_bar", false}, {"bar_stool", false}, {"tavern_table", true}, {"bench", false}, {"stool", false}, {"fireplace", false}, {"candle_stand", false}};
    if (canon == "kitchen")    return {{"counter", false}, {"fireplace", false}, {"stool", false}};
    if (canon == "bedchamber") return {{"bed", false}, {"chest", false}, {"stool", false}, {"wardrobe", false}, {"rug", true}};
    if (canon == "hall")       return {{"fireplace", false}, {"table", true}, {"bench", false}, {"chair", false}};
    if (canon == "forge")      return {{"forge_hearth", false}, {"anvil", true}, {"bellows", false}, {"tool_rack", false}, {"barrel", false}};
    if (canon == "bakehouse")  return {{"oven_bread", false}, {"counter", false}, {"barrel", false}};
    if (canon == "shambles")   return {{"counter", false}, {"chopping_block", true}, {"meat_rail", false}, {"barrel", false}};
    if (canon == "dispensary") return {{"counter", false}, {"back_bar", false}, {"chest", false}, {"candle_stand", false}};
    if (canon == "salesroom")  return {{"counter", false}, {"back_bar", false}, {"barrel", false}, {"chest", false}};
    if (canon == "service")    return {{"barrel", false}, {"chest", false}};
    return {{"chest", false}};
}

// DATA recipes (furniture quality B): loaded from resources/furnishing_recipes.json; a data
// entry OVERRIDES the hardcoded map for its purpose. Each piece may declare `tiers` (which
// wealth tiers receive it); absent tiers = every tier.
struct DataPiece {
    Piece piece;
    std::vector<std::string> tiers;   // empty = all tiers
};
std::map<std::string, std::vector<DataPiece>>& dataRecipes() {
    static std::map<std::string, std::vector<DataPiece>> r;
    return r;
}

// What furniture a room gets, by purpose + wealth tier. Casegoods back onto a wall; a table
// is centred; mounting (floor/wall/ceiling) is per-TYPE data (mountFor), not recipe data.
std::vector<Piece> recipeFor(const std::string& purpose, const std::string& wealthTier) {
    const std::string canon = canonicalPurpose(purpose);
    const auto& data = dataRecipes();
    auto it = data.find(canon);
    if (it == data.end() && canon != "default") it = data.find("default");
    std::vector<Piece> out;
    if (it != data.end()) {
        for (const auto& dp : it->second) {
            if (!dp.tiers.empty() && !wealthTier.empty() &&
                std::find(dp.tiers.begin(), dp.tiers.end(), wealthTier) == dp.tiers.end())
                continue;   // this piece belongs to richer/poorer households
            out.push_back(dp.piece);
        }
    } else {
        out = hardcodedRecipeFor(canon);
        for (auto& p : out) p.pass = passRankFor(p.type);
    }
    // M4: order by pass (heavy -> light -> lighting -> clutter). STABLE, so the
    // recipe's declaration order still decides within a pass — placement stays
    // deterministic for a given recipe file.
    std::stable_sort(out.begin(), out.end(),
                     [](const Piece& a, const Piece& b) { return a.pass < b.pass; });
    return out;
}

} // namespace

int FurniturePlacer::passRank(const std::string& type) { return passRankFor(type); }
bool FurniturePlacer::isVentedFixture(const std::string& type) { return isVentedType(type); }

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
    // Tier-agnostic UNION (empty tier = every piece) — the coverage gate must see the full
    // vocabulary any tier could emit.
    for (const auto& pc : recipeFor(purpose, "")) types.push_back(pc.type);
    return types;
}

// ---- Claims Ledger increment 3: plan-derived furnishing --------------------

int FurniturePlacer::planExteriorThicknessMicro(const AssemblyPlan& plan) {
    for (const auto& w : plan.walls)
        if (w.type == "exterior") return StructureRealizer::thicknessMicro(w.thickness);
    return StructureRealizer::thicknessMicro(0.333);
}

int FurniturePlacer::planInteriorThicknessMicro(const AssemblyPlan& plan) {
    for (const auto& w : plan.walls)
        if (w.type == "interior") return StructureRealizer::thicknessMicro(w.thickness);
    return StructureRealizer::thicknessMicro(0.222);
}

std::vector<Rect> FurniturePlacer::planStairRects(const AssemblyPlan& plan, int storyIndex) {
    std::vector<Rect> rects;
    for (const auto& sr : plan.stairs)
        if (sr.fromStory == storyIndex || sr.toStory == storyIndex)
            rects.push_back(Rect{sr.x, sr.z, sr.w, sr.d});
    return rects;
}

std::vector<FurniturePlacement> FurniturePlacer::furnishFromPlan(
        const ProgStory& story, int storyIndex,
        const glm::ivec3& origin, int floorY,
        const AssemblyPlan& plan,
        const std::map<std::string, Footprint>& footprints,
        std::vector<UnplacedFixture>* unplaced,
        const std::string& wealthTier) {
    return furnish(story, origin, floorY, footprints, unplaced,
                   planExteriorThicknessMicro(plan), wealthTier,
                   planStairRects(plan, storyIndex),
                   planInteriorThicknessMicro(plan));
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
    // KI-5b: the placement carries PER-AXIS wall-band insets (a corner piece backs an
    // exterior wall on one axis and an interior partition on the other — one shared
    // inset can't be flush to both); -1 falls back to the caller's exterior thickness.
    const int tx = p.insetMicroX >= 0 ? p.insetMicroX : extTMicro;
    const int tz = p.insetMicroZ >= 0 ? p.insetMicroZ : extTMicro;
    return glm::ivec3(p.worldPos.x * 9 - p.backDir.x * tx,
                      surfaceMicroY,
                      p.worldPos.z * 9 - p.backDir.z * tz);
}

// ---- MOUNTING (quality B): sconces/racks on the wall, the chandelier from the ceiling. ----
FurniturePlacer::Mount FurniturePlacer::mountFor(const std::string& type) {
    if (type == "wall_lantern" || type == "tool_rack") return Mount::Wall;
    if (type == "chandelier") return Mount::Ceiling;
    return Mount::Floor;
}

int FurniturePlacer::mountedMicroY(const std::string& type, int surfaceMicroY, int ceilingMicroY,
                                   int templateMicroH) {
    switch (mountFor(type)) {
        case Mount::Wall:
            // wall_lantern: the 60-72 in sconce mounting convention; 60 in = 1.52 m -> 14 micro
            // (low end — a flame at face height). tool_rack: ~1.0 m working reach (INFERRED).
            return surfaceMicroY + (type == "tool_rack" ? 9 : 14);
        case Mount::Ceiling: {
            // hang 1 micro below the ceiling; NEVER breach walk clearance (agent 16 + margin)
            const int base = ceilingMicroY - std::max(0, templateMicroH) - 1;
            return std::max(base, surfaceMicroY + 18);
        }
        default:
            return surfaceMicroY;
    }
}

bool FurniturePlacer::loadRecipesFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("FurniturePlacer", "furnishing_recipes parse error in " << path << ": "
                     << e.what());
        return false;
    }
    if (!j.contains("recipes") || !j["recipes"].is_object()) return false;
    auto& reg = dataRecipes();
    reg.clear();
    for (auto it = j["recipes"].begin(); it != j["recipes"].end(); ++it) {
        if (!it.value().is_array()) continue;
        std::vector<DataPiece> pieces;
        for (const auto& e : it.value()) {
            if (!e.is_object() || !e.contains("type")) continue;
            DataPiece dp;
            dp.piece.type = e["type"].get<std::string>();
            dp.piece.center = e.value("place", std::string("wall")) == "center";
            dp.piece.count = std::max(1, e.value("count", 1));
            dp.piece.perArea = std::max(0.0, e.value("per_area", 0.0));
            // M4 pass: per-TYPE engine default (passRankFor), overridable per recipe
            // entry with "pass": "heavy"|"light"|"lighting"|"clutter".
            dp.piece.pass = e.contains("pass")
                ? passRankFromName(e.value("pass", std::string("light")))
                : passRankFor(dp.piece.type);
            // `as:"item"`: this piece REALIZES as a pickable item prop (spawned
            // via ItemPropManager), not a baked template. `item` overrides the
            // item id (default: the type). Placement/reservation is unchanged —
            // only the realization differs (ItemPlacementPlan.md step 2).
            if (e.value("as", std::string()) == "item")
                itemFormRegistry()[dp.piece.type] = e.value("item", dp.piece.type);
            if (e.contains("tiers") && e["tiers"].is_array())
                for (const auto& t : e["tiers"])
                    if (t.is_string()) dp.tiers.push_back(t.get<std::string>());
            pieces.push_back(std::move(dp));
        }
        reg[it.key()] = std::move(pieces);
    }

    // Per-purpose SURFACE ITEM sets ("surface_items"): item ids scattered on
    // table tops as pickable item props (ItemPlacementPlan.md). Optional key.
    auto& surf = surfaceItemRecipes();
    surf.clear();
    if (j.contains("surface_items") && j["surface_items"].is_object()) {
        for (auto it = j["surface_items"].begin(); it != j["surface_items"].end(); ++it) {
            if (!it.value().is_array()) continue;
            std::vector<std::string> items;
            for (const auto& e : it.value())
                if (e.is_string()) items.push_back(e.get<std::string>());
            if (!items.empty()) surf[it.key()] = std::move(items);
        }
    }
    return !reg.empty();
}

void FurniturePlacer::clearRecipes() {
    dataRecipes().clear();
    surfaceItemRecipes().clear();
    itemFormRegistry().clear();
}

std::map<std::string, std::vector<std::string>>& FurniturePlacer::surfaceItemRecipes() {
    static std::map<std::string, std::vector<std::string>> reg;
    return reg;
}

std::map<std::string, std::string>& FurniturePlacer::itemFormRegistry() {
    static std::map<std::string, std::string> reg;
    return reg;
}

std::string FurniturePlacer::itemFormFor(const std::string& type) {
    const auto& reg = itemFormRegistry();
    auto it = reg.find(type);
    return it == reg.end() ? std::string() : it->second;
}

std::vector<std::string> FurniturePlacer::surfaceItemsFor(const std::string& purpose) {
    const auto& reg = surfaceItemRecipes();
    // Substring matching like canonicalPurpose: "taproom_main" hits "taproom".
    for (const auto& [key, items] : reg)
        if (purpose.find(key) != std::string::npos) return items;
    // Fallback: the tavern-ish default set — MUST resolve in items.json
    // (ItemPlacementTest gates every listed id).
    return {"tankard", "bottle_wine", "plate"};
}

float FurniturePlacer::templateTopUnits(const VoxelTemplate& tmpl) {
    float top = 0.0f;
    for (const auto& c : tmpl.cubes)
        top = std::max(top, float(c.relativePos.y + 1));
    for (const auto& s : tmpl.subcubes)
        top = std::max(top, s.parentRelativePos.y + (s.subcubePos.y + 1) / 3.0f);
    for (const auto& m : tmpl.microcubes)
        top = std::max(top, m.parentRelativePos.y + m.subcubePos.y / 3.0f
                            + (m.microcubePos.y + 1) / 9.0f);
    if (tmpl.isFineGrid()) {
        const float cell = 1.0f / float(tmpl.fineGridResolution);
        for (const auto& v : tmpl.fineVoxels)
            top = std::max(top, (v.pos.y + 1) * cell);
    }
    return top;
}

std::vector<FurniturePlacer::SurfaceItemSpot> FurniturePlacer::placeSurfaceItems(
    const std::string& room, const VoxelTemplate& tableTmpl,
    const glm::ivec3& worldMicro, int rotationDeg,
    const std::vector<std::string>& items, unsigned seed) {
    std::vector<SurfaceItemSpot> out;
    if (items.empty()) return out;

    // ---- template-local micro AABB + the TOP-SURFACE rect ----
    // Accumulate exactly like PlacedObjectManager::computeMicroPlacedBounds (cube
    // -> 9-micro span, subcube -> 3, microcube -> 1). A second accumulator keeps
    // only primitives whose top row reaches within 1 micro of the template max —
    // the actual tabletop plane — so legs, stretchers, or a bar's lower shelf
    // never widen the item surface.
    glm::ivec3 mmin(INT_MAX), mmax(INT_MIN);
    struct P { glm::ivec3 origin; int span; };
    std::vector<P> prims;
    auto acc = [&](const glm::ivec3& origin, int span) {
        mmin = glm::min(mmin, origin);
        mmax = glm::max(mmax, origin + glm::ivec3(span - 1));
        prims.push_back({origin, span});
    };
    for (const auto& c : tableTmpl.cubes)      acc(c.relativePos * 9, 9);
    for (const auto& s : tableTmpl.subcubes)   acc(s.parentRelativePos * 9 + s.subcubePos * 3, 3);
    for (const auto& m : tableTmpl.microcubes) acc(m.parentRelativePos * 9 + m.subcubePos * 3 + m.microcubePos, 1);
    if (mmin.x > mmax.x) return out;   // empty template (fine-grid tables don't exist)

    // ---- occupancy raster over the local micro AABB ----
    // Furniture templates are small (a back bar is 27x16x3); the raster lets us
    // find EVERY upward-facing surface — a shelving unit stocks all its shelves,
    // not just the top board.
    const glm::ivec3 dims = mmax - mmin + glm::ivec3(1);
    const size_t vol = (size_t)dims.x * dims.y * dims.z;
    if (vol == 0 || vol > 500000) return out;
    std::vector<uint8_t> occ(vol, 0);
    auto at = [&](int x, int y, int z) -> uint8_t& {
        return occ[((size_t)(x - mmin.x) * dims.y + (y - mmin.y)) * dims.z + (z - mmin.z)];
    };
    for (const auto& p : prims)
        for (int x = 0; x < p.span; ++x)
            for (int y = 0; y < p.span; ++y)
                for (int z = 0; z < p.span; ++z)
                    at(p.origin.x + x, p.origin.y + y, p.origin.z + z) = 1;

    // ---- surface planes: occupied cells with >= 3 micro of headroom above
    // (or open sky at the AABB top). Grouped by exact surface row. ----
    std::map<int, std::array<int, 5>> levels;   // y -> {minX, minZ, maxX, maxZ, area}
    for (int x = mmin.x; x <= mmax.x; ++x)
        for (int z = mmin.z; z <= mmax.z; ++z)
            for (int y = mmin.y; y <= mmax.y; ++y) {
                if (!at(x, y, z)) continue;
                if (y < mmax.y && at(x, y + 1, z)) continue;   // not a surface
                bool sky = true;
                int clear = 0;
                for (int yy = y + 1; yy <= mmax.y; ++yy) {
                    if (at(x, yy, z)) { sky = false; break; }
                    ++clear;
                }
                if (!sky && clear < 3) continue;   // a bottle needs ~0.33 u headroom
                auto it = levels.find(y);
                if (it == levels.end())
                    levels[y] = {x, z, x, z, 1};
                else {
                    auto& L = it->second;
                    L[0] = std::min(L[0], x); L[1] = std::min(L[1], z);
                    L[2] = std::max(L[2], x); L[3] = std::max(L[3], z);
                    ++L[4];
                }
            }
    // Filter non-surfaces, then merge planes one micro apart (micro-stepped
    // tops) into the HIGHER row, top-down, cap 4. Filters, each live-derived:
    //  - extent >= 2 both axes (a leg's foot, a 1-micro panel-top row) — but a
    //    real shelf is only 2 micro deep once its back panel eats a row, so 3
    //    was too strict (it emptied the back bar's shelves);
    //  - FILL >= 60% of the rect (the top RIM RING — panel top + end caps —
    //    spans the full footprint rect at ~38% fill; placing on its rect
    //    floated items a micro above the real top shelf);
    //  - area >= 20 cells.
    struct Plane { int y; int minX, minZ, maxX, maxZ; };
    std::vector<Plane> planes;
    for (auto it = levels.rbegin(); it != levels.rend(); ++it) {
        const auto& L = it->second;
        const int ex = L[2] - L[0] + 1, ez = L[3] - L[1] + 1;
        if (L[4] < 20 || ex < 2 || ez < 2) continue;
        if (L[4] * 10 < ex * ez * 6) continue;   // fill < 60% -> a ring/frame, not a shelf
        if (!planes.empty() && planes.back().y - it->first <= 1) {
            auto& P = planes.back();   // union into the higher step
            P.minX = std::min(P.minX, L[0]); P.minZ = std::min(P.minZ, L[1]);
            P.maxX = std::max(P.maxX, L[2]); P.maxZ = std::max(P.maxZ, L[3]);
            continue;
        }
        if (planes.size() >= 4) break;
        planes.push_back({it->first, L[0], L[1], L[2], L[3]});
    }
    if (planes.empty()) return out;

    // ---- rotate about the SAME pivot spawnTemplateMicro uses (full-AABB mmax) ----
    const int rotSteps = ((rotationDeg % 360) + 360) % 360 / 90;
    auto rotMicro = [&](const glm::ivec3& p) -> glm::ivec3 {
        switch (rotSteps) {
            case 1: return glm::ivec3(mmax.z - p.z, p.y, p.x);
            case 2: return glm::ivec3(mmax.x - p.x, p.y, mmax.z - p.z);
            case 3: return glm::ivec3(p.z, p.y, mmax.x - p.x);
            default: return p;
        }
    };

    unsigned s = seed ? seed : 1u;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s; };
    for (const auto& pl : planes) {
        glm::ivec3 rmin(INT_MAX), rmax(INT_MIN);
        for (int cx = 0; cx < 2; ++cx) for (int cz = 0; cz < 2; ++cz) {
            const glm::ivec3 corner(cx ? pl.maxX : pl.minX, pl.y, cz ? pl.maxZ : pl.minZ);
            const glm::ivec3 r = rotMicro(corner);
            rmin = glm::min(rmin, r);  rmax = glm::max(rmax, r);
        }
        // world-space float rect (units), rim inset, measured surface Y
        const float minX = (worldMicro.x + rmin.x) / 9.0f;
        const float maxX = (worldMicro.x + rmax.x + 1) / 9.0f;
        const float minZ = (worldMicro.z + rmin.z) / 9.0f;
        const float maxZ = (worldMicro.z + rmax.z + 1) / 9.0f;
        // EXACT surface Y — no epsilon here. spawnProp rests the item's lowest
        // corner on pos.y with its own tiny anti-z-fight lift; stacking a
        // second epsilon read as items hovering above the shelf.
        const float topY = (worldMicro.y + pl.y + 1) / 9.0f;
        const float kRim = 0.20f;   // keep item footprints off the edge
        float x0 = minX + kRim, x1 = maxX - kRim, z0 = minZ + kRim, z1 = maxZ - kRim;
        if (x1 < x0) x0 = x1 = (minX + maxX) * 0.5f;   // narrow surface -> center line
        if (z1 < z0) z0 = z1 = (minZ + maxZ) * 0.5f;

        // seeded jittered grid, one item per cell (>= ~0.45 u spacing)
        const float kPitch = 0.55f;
        const int cols = std::max(1, (int)std::floor((x1 - x0) / kPitch) + 1);
        const int rows = std::max(1, (int)std::floor((z1 - z0) / kPitch) + 1);
        std::vector<glm::vec2> cells;
        for (int i = 0; i < cols; ++i)
            for (int j = 0; j < rows; ++j)
                cells.push_back({cols == 1 ? (x0 + x1) * 0.5f : x0 + (x1 - x0) * i / float(cols - 1),
                                 rows == 1 ? (z0 + z1) * 0.5f : z0 + (z1 - z0) * j / float(rows - 1)});
        for (int i = (int)cells.size() - 1; i > 0; --i)
            std::swap(cells[i], cells[rnd() % (unsigned)(i + 1)]);
        const float jit = std::min(0.08f, kPitch * 0.15f);
        const size_t n = std::min(items.size(), cells.size());   // never overflow the surface
        for (size_t i = 0; i < n; ++i) {
            SurfaceItemSpot spot;
            spot.type = items[i];
            const float jx = ((rnd() % 1000) / 999.0f - 0.5f) * 2.0f * jit;
            const float jz = ((rnd() % 1000) / 999.0f - 0.5f) * 2.0f * jit;
            spot.worldPos = glm::vec3(std::min(x1, std::max(x0, cells[i].x + jx)), topY,
                                      std::min(z1, std::max(z0, cells[i].y + jz)));
            spot.yawDeg = float(rnd() % 360);
            out.push_back(spot);
        }
    }
    return out;
}

std::vector<FurniturePlacement> FurniturePlacer::furnish(
    const ProgStory& story, const glm::ivec3& origin, int floorY,
    const std::map<std::string, Footprint>& footprints,
    std::vector<UnplacedFixture>* unplaced, int extTMicro, const std::string& wealthTier,
    const std::vector<Rect>& reservedRects, int intTMicro) {
    std::vector<FurniturePlacement> out;
    // KI-5b: footprint bounds (union of room rects) distinguish EXTERIOR walls
    // (footprint edge, full extTMicro band inside the edge cube) from INTERIOR
    // partitions (band straddles the boundary — only its half sits in this room's
    // edge cube). Approximation: winged notch edges read as interior (bbox test);
    // over-inset there is the old behavior, disclosed.
    int fpMinX = INT_MAX, fpMinZ = INT_MAX, fpMaxX1 = INT_MIN, fpMaxZ1 = INT_MIN;
    for (const auto& r2 : story.rooms) {
        fpMinX = std::min(fpMinX, r2.rect.x);   fpMinZ = std::min(fpMinZ, r2.rect.z);
        fpMaxX1 = std::max(fpMaxX1, r2.rect.x1()); fpMaxZ1 = std::max(fpMaxZ1, r2.rect.z1());
    }
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
        // KI-5d: reserved rects (stair bases + arriving stair wells) + a 1-cell landing
        // margin — furniture used to be placed straight onto stair cells.
        for (const auto& rr : reservedRects)
            for (int x = rr.x - 1; x < rr.x + rr.w + 1; ++x)
                for (int z = rr.z - 1; z < rr.z + rr.d + 1; ++z)
                    if (x >= rx && x < rx + rw && z >= rz && z < rz + rd)
                        blocked.insert({x, z});

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
        // backDir = the OUTWARD normal of EVERY room-edge (wall) the footprint abuts (corners + interior
        // partitions included); a piece in the interior -> (0,0,0). The consumer insets off these walls.
        auto backDirOf = [&](int mnx, int mnz, int mxx, int mxz) -> glm::ivec3 {
            glm::ivec3 bd(0);
            if (mnx == rx)               bd.x = -1;
            else if (mxx == rx + rw - 1) bd.x = +1;
            if (mnz == rz)               bd.z = -1;
            else if (mxz == rz + rd - 1) bd.z = +1;
            return bd;
        };
        // The TRUE cube span a piece occupies once placed (reservation == render): the footprint PLUS the
        // micro-spill from the wall-inset anchor. extTMicro==0 -> legacy (reserve the bare footprint).
        auto spanCellsOf = [&](const std::vector<std::pair<int, int>>& cells, int rot,
                               const Footprint& fp) -> std::vector<std::pair<int, int>> {
            if (extTMicro <= 0 || cells.empty()) return cells;
            int mnx = cells[0].first, mnz = cells[0].second, mxx = mnx, mxz = mnz;
            for (const auto& c : cells) {
                mnx = std::min(mnx, c.first);  mnz = std::min(mnz, c.second);
                mxx = std::max(mxx, c.first);  mxz = std::max(mxz, c.second);
            }
            const glm::ivec3 bd = backDirOf(mnx, mnz, mxx, mxz);
            const int mW = fp.microW > 0 ? fp.microW : std::max(1, fp.width) * 9 - 1;
            const int mD = fp.microD > 0 ? fp.microD : std::max(1, fp.depth) * 9 - 1;
            const CubeSpan s = placedCubeSpan(mW, mD, rot, bd, extTMicro, mnx, mnz);
            std::vector<std::pair<int, int>> out2;
            for (int x = s.minX; x <= s.maxX; ++x)
                for (int z = s.minZ; z <= s.maxZ; ++z) out2.push_back({x, z});
            return out2;
        };
        // Reserve a placed piece: occupy its TRUE span, but anchor worldPos + backDir from the FOOTPRINT.
        auto reserve = [&](const std::vector<std::pair<int, int>>& fpCells,
                           const std::vector<std::pair<int, int>>& spanCells, const std::string& type,
                           int rot) {
            int mnx = fpCells[0].first, mnz = fpCells[0].second, mxx = mnx, mxz = mnz;
            for (const auto& c : fpCells) {
                mnx = std::min(mnx, c.first);  mnz = std::min(mnz, c.second);
                mxx = std::max(mxx, c.first);  mxz = std::max(mxz, c.second);
            }
            for (const auto& c : spanCells) occupied.insert(c);
            FurniturePlacement f;
            f.type = type; f.room = room.id; f.rotation = rot;
            f.backDir = backDirOf(mnx, mnz, mxx, mxz);
            f.worldPos = glm::ivec3(origin.x + mnx, floorY, origin.z + mnz);   // anchor = footprint corner
            // KI-5b: PER-AXIS wall insets. Exterior wall -> the full band; interior
            // partition -> the half of the straddling band inside this room's edge
            // cube (a sconce inset 9 micro off a 2-micro partition floated ~0.8 m off
            // the wall). Corners get each axis's own inset — flush to both walls.
            // ONLY when the caller supplied the thickness (extTMicro > 0): the legacy
            // two-step convention (furnish with extTMicro=0, real thickness applied
            // later via microWorldPos's parameter) must keep the -1 sentinel — baking
            // 0 here poisoned the fallback and embedded furniture INSIDE walls
            // (auditor-caught: MicroPlacementOverlapTest went 0 -> 234 overlaps).
            if (extTMicro > 0) {
                if (f.backDir.x != 0) {
                    const bool extX = (f.backDir.x < 0 && rx == fpMinX) ||
                                      (f.backDir.x > 0 && rx + rw == fpMaxX1);
                    f.insetMicroX = extX ? extTMicro : (intTMicro + 1) / 2;
                }
                if (f.backDir.z != 0) {
                    const bool extZ = (f.backDir.z < 0 && rz == fpMinZ) ||
                                      (f.backDir.z > 0 && rz + rd == fpMaxZ1);
                    f.insetMicroZ = extZ ? extTMicro : (intTMicro + 1) / 2;
                }
            }
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
            const auto span = spanCellsOf(cells, rot, fp);
            if (!fits(span)) return false;   // the true placed span (with spill) collides
            reserve(cells, span, type, rot);
            return true;
        };

        // Forge floor: cluster the work triangle. Detected by room purpose so it only fires for a smithy.
        const std::string roomPurpose = lower(room.purpose);
        const bool forgeFloor = roomPurpose.find("forge") != std::string::npos
                             || roomPurpose.find("smith") != std::string::npos
                             || roomPurpose.find("anvil") != std::string::npos;
        std::pair<int, int> forgeCell{-1, -1}, anvilCell{-1, -1}, tableCell{-1, -1},
                            barCell{-1, -1};

        for (const auto& piece : recipeFor(room.purpose, wealthTier)) {
            const Footprint fp = footprintOf(piece.type);
            const int width = std::max(1, fp.width);
            // AREA-SCALED density (`count` / `per_area` recipe fields): a real
            // taproom seats many — one table per ~N floor cells, not a single
            // showpiece table. Rep 0 stays REQUIRED (unplaced-reported); later
            // reps are best-effort — a full room just stops placing.
            int reps = std::max(1, piece.count);
            if (piece.perArea > 0.0)
                reps = std::max(reps, static_cast<int>((rw * rd) / piece.perArea));
            reps = std::min(reps, 12);
            for (int rep = 0; rep < reps; ++rep) {
            bool placed = false;

            // CEILING-hung pieces (chandelier) float over the room centre and reserve NO floor
            // cells — a chandelier belongs directly ABOVE the centred table, not in a fight with
            // it for the same cells. Their Y is resolved by the consumer via mountedMicroY.
            if (mountFor(piece.type) == Mount::Ceiling) {
                FurniturePlacement p;
                p.type = piece.type;
                p.room = room.id;
                p.rotation = 0;
                p.backDir = glm::ivec3(0);
                p.worldPos = glm::ivec3(origin.x + rx + rw / 2, floorY, origin.z + rz + rd / 2);
                out.push_back(p);
                break;   // ceiling pieces don't rep — one over the room centre
            }

            // Work-triangle clustering: the anvil hugs the forge; the quench (barrel) hugs the anvil.
            if (forgeFloor && piece.type == "anvil" && forgeCell.first >= 0)
                placed = placeNear(forgeCell, fp, piece.type);
            else if (forgeFloor && piece.type == "barrel" && anvilCell.first >= 0)
                placed = placeNear(anvilCell, fp, piece.type);
            // SEATING AROUND THE TABLE: a bench in a room with a centred table seats BESIDE the table
            // (facing it), not marooned on a far wall — and in a narrow room where no free wall slot
            // remains it's the reason the bench used to drop. placeNear hugs the table and faces the
            // room centre (== the table), so the bench fronts it. Falls through to wall-packing if the
            // table's surround is full.
            else if (piece.type == "bench" && tableCell.first >= 0)
                placed = placeNear(tableCell, fp, piece.type);
            // Taverns: stools/chairs pull up to the (nearest recorded) table;
            // bar stools line the bar front — seating belongs AT the furniture
            // it serves, not marooned along a far wall.
            else if ((piece.type == "stool" || piece.type == "chair") && tableCell.first >= 0)
                placed = placeNear(tableCell, fp, piece.type);
            else if (piece.type == "bar_stool" && barCell.first >= 0)
                placed = placeNear(barCell, fp, piece.type);

            if (!placed && piece.center) {
                auto cells = coverAt(0, fp, 0, /*center=*/true);
                auto span = spanCellsOf(cells, 0, fp);
                if (fits(cells) && fits(span)) { reserve(cells, span, piece.type, 0); placed = true; }
            }
            if (!placed) {
                // PACK along walls: scan non-door walls first, then door walls; within each wall scan
                // every offset for the first free slot. Multiple pieces share a wall (no one-per-wall cap).
                // A CHEST opens perpendicular to its wall, so it must open into CLEARANCE — prefer the
                // wall whose inward normal runs along the room's LONGER axis (clearance = the room extent
                // perpendicular to the wall: rw for the x-walls 0/1, rd for the z-walls 2/3). Otherwise a
                // 2-wide chest in a narrow room backs a long wall and its lid opens into the near opposite
                // wall (chest_facing_wrong). Other pieces keep the stable index order.
                const bool opensToRoom = piece.type.find("chest") != std::string::npos;
                auto clearance = [&](int w) { return (w < 2) ? rw : rd; };
                int order[4]; int n = 0;
                for (int w = 0; w < 4; ++w) if (!doorWall[w]) order[n++] = w;   // non-door walls first
                for (int w = 0; w < 4; ++w) if ( doorWall[w]) order[n++] = w;   // then door walls
                if (opensToRoom) {   // stable-sort each (non-door | door) group by clearance, desc
                    std::stable_sort(order, order + n, [&](int a, int b) {
                        if (doorWall[a] != doorWall[b]) return !doorWall[a];   // keep non-door first
                        return clearance(a) > clearance(b);
                    });
                }
                for (int k = 0; k < n && !placed; ++k) {
                    const int w = order[k];
                    const int runLen = (w < 2) ? rd : rw;       // walls 0/1 run along z, 2/3 along x
                    const int rot = facingIntoRoom(WALLS[w].inwardDx, WALLS[w].inwardDz);
                    const int maxAlong = runLen - width;        // last valid offset
                    // Offset scan order. An OPENING piece (chest) scans MIDDLE-OUT so it avoids the two
                    // corner offsets, where it would abut a PERPENDICULAR wall and its clasp would open
                    // into that wall's cells (the real byre chest_closed_8: at a corner its +Z lid ran
                    // into the side partition). A mid-wall slot keeps the clasp clear. Others: left-to-
                    // right (unchanged).
                    std::vector<int> alongs;
                    alongs.reserve(maxAlong + 1);
                    if (opensToRoom && maxAlong > 0) {
                        for (int off = 0; off <= maxAlong; ++off) {   // mid, mid-1, mid+1, ...
                            const int a = maxAlong / 2 + ((off % 2) ? (off + 1) / 2 : -(off / 2));
                            if (a >= 0 && a <= maxAlong) alongs.push_back(a);
                        }
                    } else {
                        for (int a = 0; a <= maxAlong; ++a) alongs.push_back(a);
                    }
                    for (int along : alongs) {
                        auto cells = coverAt(w, fp, along, /*center=*/false);
                        if (!fits(cells)) continue;
                        // reserve the TRUE placed span (footprint + micro-spill); skip the slot if the
                        // span collides with an already-placed piece (this is what kills the overlaps).
                        auto span = spanCellsOf(cells, rot, fp);
                        if (!fits(span)) continue;
                        reserve(cells, span, piece.type, rot);
                        placed = true;
                        break;
                    }
                }
            }
            if (placed) {   // remember anchors so later pieces can hug them
                const auto& last = out.back();
                if (forgeFloor && piece.type == "forge_hearth")
                    forgeCell = {last.worldPos.x - origin.x, last.worldPos.z - origin.z};
                else if (forgeFloor && piece.type == "anvil")
                    anvilCell = {last.worldPos.x - origin.x, last.worldPos.z - origin.z};
                // Seating hugs the FIRST placed table (the centred main table —
                // later per_area reps wall-pack, and anchoring to those dragged
                // the bench to a wall; bedside tables never anchor seating).
                else if (tableCell.first < 0 &&
                         piece.type.find("table") != std::string::npos &&
                         piece.type.find("bedside") == std::string::npos)
                    tableCell = {last.worldPos.x - origin.x, last.worldPos.z - origin.z};
                else if (barCell.first < 0 && piece.type == "tavern_bar")
                    barCell = {last.worldPos.x - origin.x, last.worldPos.z - origin.z};
            }
            if (!placed) {
                // honest: rep 0 is the REQUIRED piece — never silently dropped.
                // Later reps are area-scaled best-effort; a full room stops here.
                if (rep == 0 && unplaced) unplaced->push_back({room.id, piece.type});
                break;
            }
            }   // reps
        }
    }
    return out;
}

} // namespace Core
} // namespace Phyxel
