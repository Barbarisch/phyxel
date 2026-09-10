#include "core/HearthForge.h"
#include "core/FurniturePlacer.h"

#include <algorithm>
#include <climits>
#include <map>
#include <vector>

namespace Phyxel {
namespace Core {

namespace {

// ---------------------------------------------------------------------------
// A tiny local micro grid the presets paint into, so a body can be authored
// unrotated (opening toward +Z, breast at z=0) with plain fill/clear ops and
// emitted through ONE rotation. Same pivot convention as
// PlacedObjectManager::spawnTemplateMicro / FurniturePlacer::placeSurfaceItems
// (rotate about the AABB max), so a rotated body occupies exactly the cube span
// FurniturePlacer::placedCubeSpan reserves for it.
// ---------------------------------------------------------------------------
/// Rotate a body-local (x,z) about the AABB max of a w x d body. Same pivot
/// convention as PlacedObjectManager::spawnTemplateMicro, so a rotated body occupies
/// exactly the cube span FurniturePlacer::placedCubeSpan reserves for it.
glm::ivec2 rotateLocal(int x, int z, int w, int d, int rotationDeg) {
    switch ((((rotationDeg % 360) + 360) % 360) / 90) {
        case 1:  return {d - 1 - z, x};
        case 2:  return {w - 1 - x, d - 1 - z};
        case 3:  return {z, w - 1 - x};
        default: return {x, z};
    }
}

class BodyGrid {
public:
    BodyGrid(int w, int h, int d) : m_w(w), m_h(h), m_d(d), m_cells((size_t)w * h * d) {}

    void fill(int x0, int x1, int y0, int y1, int z0, int z1, const std::string& mat) {
        for (int x = std::max(0, x0); x <= std::min(m_w - 1, x1); ++x)
            for (int y = std::max(0, y0); y <= std::min(m_h - 1, y1); ++y)
                for (int z = std::max(0, z0); z <= std::min(m_d - 1, z1); ++z)
                    at(x, y, z) = mat;
    }
    void clear(int x0, int x1, int y0, int y1, int z0, int z1) {
        fill(x0, x1, y0, y1, z0, z1, std::string());
    }

    /// Emit every non-air cell into `c`, rotated by `rotationDeg` about the body's
    /// AABB max and translated to (ax, ay, az) — the placed anchor in local micro.
    void emit(MicroCanvas& c, int ax, int ay, int az, int rotationDeg) const {
        for (int x = 0; x < m_w; ++x)
            for (int y = 0; y < m_h; ++y)
                for (int z = 0; z < m_d; ++z) {
                    const std::string& m = at(x, y, z);
                    if (m.empty()) continue;
                    const glm::ivec2 r = rotate(x, z, rotationDeg);
                    c.setMicroCell(ax + r.x, ay + y, az + r.y, m);
                }
    }

    /// Rotate a local (x,z) about the AABB max, matching emit().
    glm::ivec2 rotate(int x, int z, int rotationDeg) const {
        return rotateLocal(x, z, m_w, m_d, rotationDeg);
    }

    const std::string& at(int x, int y, int z) const {
        return m_cells[((size_t)x * m_h + y) * m_d + z];
    }

private:
    std::string& at(int x, int y, int z) {
        return m_cells[((size_t)x * m_h + y) * m_d + z];
    }
    int m_w, m_h, m_d;
    std::vector<std::string> m_cells;
};

/// Carve the THROAT: the gather that takes smoke from the fire up into the flue.
/// Shared by every preset — a hearth whose flue does not reach its own firebox is
/// a chimney-shaped decoration (the defect the pre-forge stack shipped: it rested
/// on a SOLID mantel, so the flue started above 3 micro of brick).
void carveThroat(BodyGrid& g, int flueCx, int flueCz, int y0, int y1) {
    g.clear(flueCx - HearthForge::kFlueHalfMicro, flueCx + HearthForge::kFlueHalfMicro,
            y0, y1,
            flueCz - HearthForge::kFlueHalfMicro, flueCz + HearthForge::kFlueHalfMicro);
}

}  // namespace

bool HearthForge::isVented(const std::string& type) {
    return type == "fireplace" || type == "forge_hearth" || type == "oven_bread";
}

// ---------------------------------------------------------------------------
// The presets. ONE algorithm — masonry mass -> fire cavity -> fuel/embers ->
// throat into the flue — with per-type numbers taken from the SAME
// object_dimensions.json canon the furniture templates were generated against
// (FurnitureConformanceTest gates those numbers), so moving the hearth from a
// spawned template into the shell changes where it lives, not how big it is.
// ---------------------------------------------------------------------------
HearthForge::Body HearthForge::bodyOf(const std::string& type) {
    Body b;
    if (type == "fireplace") {
        // canon 'hearth' 1.5 w x 1.2 h x 0.6 d -> 14 x 11 x 5 micro.
        b.known = true; b.w = 14; b.h = 11; b.d = 5;
        b.flueCx = 7; b.flueCz = 2;
        b.fireX = 7; b.fireY = 3; b.fireZ = 2;
        b.material = "Bricks";
        // Burns CORDWOOD: the firebox is dressed with billet props, not with a
        // fuel bed baked into the brickwork. Clear span x3..10 (8 micro) above
        // the base slab, whose top is y2.
        b.fuelItem = "firewood";
        b.fuelSpanMicro = 8;     // clear width  x3..10
        b.fuelDepthMicro = 4;    // clear depth  z1..4  -> a 2-billet bed + 1 crossing log
        b.fuelFloorY = 2;
    } else if (type == "forge_hearth") {
        // canon 'forge_hearth' width 1.0 / work_top 0.8 / depth 0.8 -> 9 x 7 micro,
        // block top at y6 (0.778 m). Height here stops at the HOOD (y8): the flue
        // above it is the stack's job now, not a stub baked into the body.
        b.known = true; b.w = 9; b.h = 9; b.d = 7;
        b.flueCx = 4; b.flueCz = 2;
        b.fireX = 4; b.fireY = 6; b.fireZ = 3;
        b.material = "Stone";
    } else if (type == "oven_bread") {
        // canon 'oven_bread' width 1.6 / depth 1.4 / interior 0.9 / work_top 0.8 ->
        // 14 x 12 micro mass, hearth floor at y7 (0.778 m), dome shell closing at y11.
        b.known = true; b.w = 14; b.h = 12; b.d = 12;
        b.flueCx = 7; b.flueCz = 2;
        b.fireX = 7; b.fireY = 8; b.fireZ = 6;
        b.material = "Stone";
    }
    return b;
}

Footprint HearthForge::footprintOf(const std::string& type) {
    const Body b = bodyOf(type);
    Footprint fp;
    if (!b.known) return fp;
    fp.width  = (b.w + 8) / 9;      // cubes across the front
    fp.depth  = (b.d + 8) / 9;      // cubes into the room
    fp.microW = b.w - 1;            // max micro index (the placer's convention)
    fp.microD = b.d - 1;
    fp.microH = b.h - 1;
    return fp;
}

std::vector<Rect> HearthForge::stairRectsForStory(const BuildingProgram& program,
                                                  int storyIndex) {
    const int nStory = static_cast<int>(program.stories.size());
    std::vector<Rect> rects;
    std::vector<long long> seen;
    for (const auto& st : program.stories)
        for (const auto& sr : st.stairs) {
            int a = sr.fromStory, b = sr.toStory;
            if (a > b) std::swap(a, b);
            if (a < 0 || b >= nStory || b != a + 1) continue;      // adjacent only
            if (sr.rect.w < 1 || sr.rect.d < 1) continue;
            const long long key = ((long long)a << 48) | ((long long)b << 32) |
                                  ((long long)(sr.rect.x + 4096) << 16) |
                                  (long long)(sr.rect.z + 4096);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            if (a == storyIndex || b == storyIndex) rects.push_back(sr.rect);
        }
    return rects;
}

std::vector<FurniturePlacement> HearthForge::siteHearths(
        const ProgStory& story, const std::map<std::string, Footprint>& footprints,
        int extTMicro, int intTMicro, const std::vector<Rect>& reservedRects,
        const std::string& wealthTier) {
    // The hearth's footprint comes from the FORGE, not from an asset sidecar — the
    // forge owns the geometry now. Every other type keeps its template footprint so
    // the heavy-pass packing is identical to the furnish pass that follows.
    std::map<std::string, Footprint> fps = footprints;
    for (const char* t : {"fireplace", "forge_hearth", "oven_bread"})
        fps[t] = footprintOf(t);

    const auto all = FurniturePlacer::furnish(story, glm::ivec3(0), /*floorY=*/0, fps,
                                              /*unplaced=*/nullptr, extTMicro, wealthTier,
                                              reservedRects, intTMicro);
    std::vector<FurniturePlacement> vented;
    for (const auto& p : all)
        if (isVented(p.type)) vented.push_back(p);
    return vented;
}

int HearthForge::siteIntoProgram(ProgStory& story,
                                 const std::map<std::string, Footprint>& footprints,
                                 int extTMicro, int intTMicro,
                                 const std::vector<Rect>& reservedRects,
                                 const std::string& wealthTier) {
    // Idempotent: drop any hearths a previous siting pass wrote (the program gate's
    // bounded repair re-rolls the layout and re-sites), keep hand-authored others.
    story.fixtures.erase(std::remove_if(story.fixtures.begin(), story.fixtures.end(),
                                        [](const ProgFixture& f) { return isVented(f.type); }),
                         story.fixtures.end());

    const auto sited = siteHearths(story, footprints, extTMicro, intTMicro,
                                   reservedRects, wealthTier);
    for (const auto& p : sited) {
        const Footprint fp = footprintOf(p.type);
        const bool turned = (((p.rotation % 360) + 360) % 360) % 180 != 0;
        ProgFixture f;
        f.type = p.type;
        f.room = p.room;
        f.rotation = ((p.rotation % 360) + 360) % 360;
        f.facing = (f.rotation == 90) ? "west" : (f.rotation == 180) ? "south"
                 : (f.rotation == 270) ? "east" : "north";
        f.rect = Rect{p.worldPos.x, p.worldPos.z,
                      turned ? std::max(1, fp.depth) : std::max(1, fp.width),
                      turned ? std::max(1, fp.width) : std::max(1, fp.depth)};
        story.fixtures.push_back(f);
    }
    return static_cast<int>(sited.size());
}

bool HearthForge::stackCrossesUpperRoomMiddle(const Rect& sc, const BuildingProgram& program,
                                              int story, std::string* which) {
    for (size_t si = static_cast<size_t>(story) + 1; si < program.stories.size(); ++si)
        for (const auto& rm : program.stories[si].rooms) {
            const int ox0 = std::max(sc.x, rm.rect.x), ox1 = std::min(sc.x1(), rm.rect.x1());
            const int oz0 = std::max(sc.z, rm.rect.z), oz1 = std::min(sc.z1(), rm.rect.z1());
            if (ox1 <= ox0 || oz1 <= oz0) continue;              // does not cross this room
            // Against a wall is fine - and so is the first cube in from it: a thick
            // exterior wall (stone, 0.667 m) pushes the flue of a hearth that backs
            // onto it into cube 1, which reads as a chimney BREAST along that wall,
            // not a column through the floor (Ravenmere lot 3: refused on the
            // upstairs landing for exactly this one-cube offset).
            constexpr int kBreastBand = 1;
            if (ox0 <= rm.rect.x + kBreastBand || ox1 >= rm.rect.x1() - kBreastBand ||
                oz0 <= rm.rect.z + kBreastBand || oz1 >= rm.rect.z1() - kBreastBand) continue;
            if (which) *which = rm.id + " on story " + std::to_string(si);
            return true;
        }
    return false;
}

int HearthForge::siteAllStories(BuildingProgram& program,
                                const std::map<std::string, Footprint>& footprints,
                                int extTMicro, int intTMicro, const std::string& wealthTier,
                                std::vector<std::string>* notes) {
    constexpr int kMaxResiteAttempts = 8;
    int sited = 0;
    // Stories in order, ACCUMULATING each stack's column: an upstairs hearth may not be
    // sited on top of the stack rising from the one below it. Mirrors
    // FurniturePlacer::planReservedRects exactly.
    std::vector<Rect> stacksBelow;
    for (size_t si = 0; si < program.stories.size(); ++si) {
        ProgStory& st = program.stories[si];
        std::vector<Rect> reserved = stairRectsForStory(program, static_cast<int>(si));
        reserved.insert(reserved.end(), stacksBelow.begin(), stacksBelow.end());

        int sx0 = INT_MAX, sz0 = INT_MAX, sx1 = INT_MIN, sz1 = INT_MIN;
        std::map<std::string, Rect> rooms;
        for (const auto& rm : st.rooms) {
            rooms[rm.id] = rm.rect;
            sx0 = std::min(sx0, rm.rect.x);   sz0 = std::min(sz0, rm.rect.z);
            sx1 = std::max(sx1, rm.rect.x1()); sz1 = std::max(sz1, rm.rect.z1());
        }
        const Rect stFootprint{sx0, sz0, sx1 - sx0, sz1 - sz0};

        int count = 0;
        int baseline = -1;                       // hearths the unblotted siting produced
        std::vector<ProgFixture> lastGood;       // the previous attempt's fixtures
        for (int attempt = 0; attempt < kMaxResiteAttempts; ++attempt) {
            count = siteIntoProgram(st, footprints, extTMicro, intTMicro, reserved, wealthTier);
            if (baseline < 0) baseline = count;
            if (count < baseline) {
                // The blots left the placer no wall for it and it DROPPED the hearth. A lost
                // hearth is a silent failure; a stack the realizer refuses is a loud one.
                // Keep the previous siting and say so.
                st.fixtures = lastGood;
                count = baseline;
                if (notes)
                    notes->push_back("hearth re-siting on story " + std::to_string(si) +
                                     " would LOSE the hearth - kept the previous spot; the "
                                     "realizer will refuse this program if its stack still "
                                     "rises through a room above");
                break;
            }
            lastGood = st.fixtures;
            std::vector<Rect> blots;
            for (const auto& fx : st.fixtures) {
                if (!isVented(fx.type)) continue;
                auto it = rooms.find(fx.room);
                if (it == rooms.end()) continue;
                const Rect stack = poseOf(fx, it->second, stFootprint, extTMicro, intTMicro).stackCubes;
                std::string which;
                if (!stackCrossesUpperRoomMiddle(stack, program, static_cast<int>(si), &which)) continue;
                // Forbid this spot (and its neighbours along both axes) and let the heavy pass
                // find another wall for it. A hearth on the same partition one cell over would
                // cross the same chamber, so the blot is deliberately wider than the footprint.
                blots.push_back(Rect{fx.rect.x - 2, fx.rect.z - 2, fx.rect.w + 4, fx.rect.d + 4});
                if (notes)
                    notes->push_back("hearth '" + fx.type + "' in '" + fx.room + "' at (" +
                                     std::to_string(fx.rect.x) + "," + std::to_string(fx.rect.z) +
                                     ") would rise through the middle of " + which +
                                     " -> re-siting (attempt " + std::to_string(attempt + 1) + ")");
            }
            if (blots.empty()) break;
            reserved.insert(reserved.end(), blots.begin(), blots.end());
            if (attempt == kMaxResiteAttempts - 1 && notes)
                notes->push_back("hearth re-siting EXHAUSTED on story " + std::to_string(si) +
                                 " - the realizer will refuse this program");
        }
        sited += count;

        for (const auto& fx : st.fixtures) {
            if (!isVented(fx.type)) continue;
            auto it = rooms.find(fx.room);
            if (it == rooms.end()) continue;
            stacksBelow.push_back(poseOf(fx, it->second, stFootprint, extTMicro, intTMicro).stackCubes);
            // The chimney breast DISPLACES any window on the wall it backs onto (the furnish
            // pass let the vented piece take window cells for exactly this reason): drop
            // the exterior window portals whose span overlaps the hearth along that wall,
            // so the realizer never cuts a window into masonry.
            const glm::ivec3 bd = FurniturePlacer::backDirFor(it->second, fx.rect);
            const Rect& rm = it->second;
            auto& portals = st.portals;
            const size_t before = portals.size();
            portals.erase(std::remove_if(portals.begin(), portals.end(), [&](const ProgPortal& po) {
                if (po.kind != "window") return false;
                if (po.a != "exterior" && po.b != "exterior") return false;
                const int w = std::max(1, po.width);
                if (bd.x != 0) {                                    // backs an x-wall
                    const int coord = bd.x < 0 ? rm.x : rm.x1();
                    if (po.px != coord) return false;
                    return po.pz < fx.rect.z + fx.rect.d && fx.rect.z < po.pz + w;
                }
                if (bd.z != 0) {                                    // backs a z-wall
                    const int coord = bd.z < 0 ? rm.z : rm.z1();
                    if (po.pz != coord) return false;
                    return po.px < fx.rect.x + fx.rect.w && fx.rect.x < po.px + w;
                }
                return false;
            }), portals.end());
            if (notes && portals.size() != before)
                notes->push_back("hearth '" + fx.type + "' in '" + fx.room + "' displaced " +
                                 std::to_string(before - portals.size()) + " window(s) on its wall");
        }
    }
    return sited;
}

HearthForge::Pose HearthForge::poseOf(const ProgFixture& fx, const Rect& room,
                                      const Rect& footprint, int extTMicro, int intTMicro) {
    Pose p;
    const Body b = bodyOf(fx.type);
    if (!b.known) return p;
    const int rot = ((fx.rotation % 360) + 360) % 360;

    // The SAME anchor the furnish pass computes (FurniturePlacer::microWorldPos): the
    // footprint's min corner, inset off each wall it backs onto — so the built-in
    // occupies exactly the cells the furnish pass reserves for it.
    const glm::ivec3 backDir = FurniturePlacer::backDirFor(room, fx.rect);
    int insetX = -1, insetZ = -1;
    FurniturePlacer::wallInsetsFor(room, footprint, backDir, extTMicro, intTMicro,
                                   insetX, insetZ);
    p.anchorX = fx.rect.x * 9 - backDir.x * (insetX >= 0 ? insetX : 0);
    p.anchorZ = fx.rect.z * 9 - backDir.z * (insetZ >= 0 ? insetZ : 0);

    const glm::ivec2 rFlue = rotateLocal(b.flueCx, b.flueCz, b.w, b.d, rot);
    p.flueCx = p.anchorX + rFlue.x;
    p.flueCz = p.anchorZ + rFlue.y;

    auto fdiv9 = [](int a) { int q = a / 9; if (a % 9 != 0 && a < 0) --q; return q; };
    const int sx0 = fdiv9(p.flueCx - kStackHalfMicro), sx1 = fdiv9(p.flueCx + kStackHalfMicro);
    const int sz0 = fdiv9(p.flueCz - kStackHalfMicro), sz1 = fdiv9(p.flueCz + kStackHalfMicro);
    p.stackCubes = Rect{sx0, sz0, sx1 - sx0 + 1, sz1 - sz0 + 1};

    const CubeSpan span = placedCubeSpan(b.w - 1, b.d - 1, rot, backDir, extTMicro,
                                         fx.rect.x, fx.rect.z);
    p.bodyCubes = Rect{span.minX, span.minZ, span.width(), span.depth()};
    return p;
}

HearthRecord HearthForge::paintBody(MicroCanvas& c, const ProgFixture& fx, const Rect& room,
                                    const Rect& footprint, int story, int walkMicroY,
                                    int extTMicro, int intTMicro, const StyleProfile& style) {
    HearthRecord rec;
    const Body b = bodyOf(fx.type);
    if (!b.known) return rec;   // caller reports; never silently paints a guess

    const int rot = ((fx.rotation % 360) + 360) % 360;
    const std::string mat = style.materialOf("hearth", b.material);
    const Pose pose = poseOf(fx, room, footprint, extTMicro, intTMicro);
    const int ax = pose.anchorX, az = pose.anchorZ;

    BodyGrid g(b.w, b.h, b.d);
    if (fx.type == "fireplace") {
        g.fill(0, b.w - 1, 0, 1, 0, b.d - 1, mat);                 // hearth base slab
        g.fill(0, b.w - 1, 0, b.h - 1, 0, 0, mat);                 // breast (to the wall)
        g.fill(0, 2, 2, b.h - 1, 0, b.d - 1, mat);                 // side pillars
        g.fill(b.w - 3, b.w - 1, 2, b.h - 1, 0, b.d - 1, mat);
        g.fill(0, b.w - 1, b.h - 3, b.h - 1, 0, b.d - 1, mat);     // lintel / mantel
        g.clear(3, b.w - 4, 2, b.h - 4, 1, b.d - 1);               // firebox, opening +Z
        // NO fuel bed painted here. The wood is a pile of ITEM PROPS (see the
        // fuel plan below): masonry-baked logs cannot burn, cannot be lit, and
        // read as a pale slab wedged in the brick.
        carveThroat(g, b.flueCx, b.flueCz, b.h - 3, b.h - 1);      // gather through the mantel
    } else if (fx.type == "forge_hearth") {
        g.fill(0, b.w - 1, 0, 6, 0, b.d - 1, mat);                 // solid hearth block
        g.clear(3, b.w - 4, 5, 6, 2, 4);                           // firepot recess
        g.fill(3, b.w - 4, 5, 5, 2, 4, "glow");                    // coals
        g.fill(b.w / 2, b.w / 2, 5, 5, 2, 2, "Metal");             // tuyere (bellows inlet)
        g.fill(1, b.w - 2, 7, 8, 0, 2, mat);                       // hood over the fire, to the wall
        carveThroat(g, b.flueCx, b.flueCz, 7, 8);                  // under-hood gather
    } else {   // oven_bread
        g.fill(0, b.w - 1, 0, b.h - 1, 0, b.d - 1, mat);           // masonry mass
        g.clear(3, b.w - 4, 7, b.h - 2, 2, b.d - 3);               // baking chamber
        g.fill(3, b.w - 4, 7, 7, 2, b.d - 3, "glow");              // coals on the oven floor
        g.clear(5, b.w - 6, 8, 10, b.d - 2, b.d - 1);              // arched mouth, opening +Z
        carveThroat(g, b.flueCx, b.flueCz, b.h - 1, b.h - 1);      // dome shell -> flue
    }
    g.emit(c, ax, walkMicroY, az, rot);

    // The flame anchor follows the body through the SAME rotation as its geometry.
    const glm::ivec2 rFire = g.rotate(b.fireX, b.fireZ, rot);

    rec.type = fx.type;
    rec.room = fx.room;
    rec.story = story;
    rec.rotation = rot;
    rec.x = pose.bodyCubes.x; rec.z = pose.bodyCubes.z;
    rec.w = pose.bodyCubes.w; rec.d = pose.bodyCubes.d;
    rec.baseMicroY = walkMicroY;
    rec.mantelMicroY = walkMicroY + b.h;
    rec.stackTopMicroY = rec.mantelMicroY;      // until paintStack runs
    rec.flueX = pose.flueCx - kFlueHalfMicro;
    rec.flueZ = pose.flueCz - kFlueHalfMicro;
    rec.flueW = rec.flueD = 2 * kFlueHalfMicro + 1;
    rec.stackX = pose.flueCx - kStackHalfMicro;
    rec.stackZ = pose.flueCz - kStackHalfMicro;
    rec.stackW = rec.stackD = 2 * kStackHalfMicro + 1;
    rec.fireMicroX = ax + rFire.x;
    rec.fireMicroY = walkMicroY + b.fireY;
    rec.fireMicroZ = az + rFire.y;
    rec.material = mat;

    // ---- the FUEL plan (cordwood hearths only) ------------------------------
    // Planned here because this is the only stage that knows the firebox's clear
    // span; SPAWNED in furnish, as item props. Count comes from the geometry, so
    // a wider firebox burns a bigger fire without anyone retuning a constant.
    if (!b.fuelItem.empty()) {
        const glm::ivec2 rFuel = rotateLocal(b.flueCx, b.fireZ, b.w, b.d, rot);
        rec.fuelItem = b.fuelItem;
        rec.fuelLitItem = "flaming_log";
        rec.fuelMicroX = ax + rFuel.x;
        rec.fuelMicroZ = az + rFuel.y;
        rec.fuelMicroY = walkMicroY + b.fuelFloorY;
        rec.fuelSpanMicro = b.fuelSpanMicro;
        // Billets lie ACROSS the opening (parallel to the wall the hearth backs
        // onto), which is how wood sits in a real firebox — and how it has to
        // sit, since a 0.35 m billet does not fit front-to-back in a 0.44 m box
        // once you allow for the fire. The billet model's long axis is +Z, so
        // crossing it costs a further 90 degrees on top of the hearth's own.
        rec.fuelRotation = (rot + 90) % 360;
        // Base row front-to-back on the billet pitch, then a crossing log on top
        // once there is a bed for it to rest on. A deeper firebox therefore
        // burns a bigger fire with no constant to retune.
        const int row = std::max(1, std::min(3, b.fuelDepthMicro / kBilletPitchMicro));
        rec.fuelCount = std::min(kMaxFuelBillets, row + (row >= 2 ? 1 : 0));
    }
    return rec;
}

std::vector<HearthForge::FuelBillet> HearthForge::fuelBillets(const HearthRecord& rec) {
    std::vector<FuelBillet> out;
    if (rec.fuelItem.empty() || rec.fuelCount <= 0) return out;

    const int rot = ((rec.fuelRotation % 360) + 360) % 360;
    // The base row runs along the firebox DEPTH — perpendicular to the billets
    // themselves. Which world axis that is depends on how the hearth is turned.
    const bool rowAlongZ = (rot % 180) == 90;
    const int row = std::max(1, rec.fuelCount - (rec.fuelCount >= 3 ? 1 : 0));
    const float pitch = static_cast<float>(kBilletPitchMicro);
    const float span = pitch * (row - 1);

    for (int i = 0; i < row; ++i) {
        const float off = static_cast<float>(i) * pitch - span * 0.5f;
        FuelBillet fb;
        fb.x = rec.fuelMicroX + (rowAlongZ ? 0.0f : off);
        fb.z = rec.fuelMicroZ + (rowAlongZ ? off : 0.0f);
        fb.y = static_cast<float>(rec.fuelMicroY);
        fb.rotationDeg = rot;
        out.push_back(fb);
    }
    // The crossing log rests ON the bed — and it is the one that burns, so the
    // flame and the firelight come from the middle of the pile, not its edge.
    if (rec.fuelCount > row) {
        FuelBillet top;
        top.x = static_cast<float>(rec.fuelMicroX);
        top.z = static_cast<float>(rec.fuelMicroZ);
        top.y = rec.fuelMicroY + 1.0f;      // one billet height up
        top.rotationDeg = rot;
        top.lit = true;
        out.push_back(top);
    } else {
        out.back().lit = true;              // a one-log fire still has to burn
    }
    return out;
}

void HearthForge::paintStack(MicroCanvas& c, HearthRecord& rec, int topMicroY) {
    if (rec.stackW <= 0 || topMicroY < rec.mantelMicroY) return;

    // 1) MASONRY first — the whole 5x5 column, straight through every floor slab,
    //    ceiling and roof course it crosses. Painting over them is not destruction:
    //    the shell is still unexported canvas, so the stack simply IS what occupies
    //    those cells, and the greedy exporter coarsens the result as one solid.
    c.fillMicroBox(rec.stackX, rec.mantelMicroY, rec.stackZ,
                   rec.stackW, topMicroY - rec.mantelMicroY + 1, rec.stackD, rec.material);

    // 2) FLUE second — carve it as AIR, stopping below the cap so the pot seals the
    //    top (the flue is hidden from above, and rain/birds do not read as a hole).
    //    This carve is where the floors "yield": a slab that used to sit inside the
    //    flue is simply not there.
    const int flueTop = topMicroY - kCapRows;
    if (flueTop >= rec.mantelMicroY)
        c.fillMicroBox(rec.flueX, rec.mantelMicroY, rec.flueZ,
                       rec.flueW, flueTop - rec.mantelMicroY + 1, rec.flueD, std::string());
    rec.stackTopMicroY = topMicroY;
}

Rect HearthForge::stackCubeRect(const HearthRecord& rec) {
    if (rec.stackW <= 0) return Rect{};
    auto fdiv9 = [](int a) { int q = a / 9; if (a % 9 != 0 && a < 0) --q; return q; };
    const int x0 = fdiv9(rec.stackX), x1 = fdiv9(rec.stackX + rec.stackW - 1);
    const int z0 = fdiv9(rec.stackZ), z1 = fdiv9(rec.stackZ + rec.stackD - 1);
    return Rect{x0, z0, x1 - x0 + 1, z1 - z0 + 1};
}

}  // namespace Core
}  // namespace Phyxel
