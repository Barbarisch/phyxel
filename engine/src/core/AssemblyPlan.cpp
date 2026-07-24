#include "core/AssemblyPlan.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

namespace {
int    ji(const nlohmann::json& j, const char* k, int def = 0)    { return (j.contains(k) && j[k].is_number()) ? j[k].get<int>() : def; }
double jd(const nlohmann::json& j, const char* k, double def = 0) { return (j.contains(k) && j[k].is_number()) ? j[k].get<double>() : def; }
std::string js(const nlohmann::json& j, const char* k, const std::string& def = "") { return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : def; }

glm::ivec3 jivec3(const nlohmann::json& j, const char* k) {
    glm::ivec3 v(0);
    if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
        v = {j[k][0].get<int>(), j[k][1].get<int>(), j[k][2].get<int>()};
    return v;
}
glm::vec3 jvec3(const nlohmann::json& j, const char* k, const glm::vec3& def) {
    if (j.contains(k) && j[k].is_array() && j[k].size() >= 3)
        return {j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()};
    return def;
}
} // namespace

FoundationColumn FoundationColumn::fromJson(const nlohmann::json& j) {
    return {ji(j, "x"), ji(j, "z"), ji(j, "bearing_y"), ji(j, "top_y"), js(j, "material")};
}
nlohmann::json FoundationColumn::toJson() const {
    return {{"x", x}, {"z", z}, {"bearing_y", bearingY}, {"top_y", topY}, {"material", material}};
}

WallSegment WallSegment::fromJson(const nlohmann::json& j) {
    WallSegment w;
    w.x0 = ji(j, "x0"); w.z0 = ji(j, "z0"); w.x1 = ji(j, "x1"); w.z1 = ji(j, "z1");
    w.baseY = ji(j, "base_y"); w.height = ji(j, "height");
    w.thickness = jd(j, "thickness", 0.333);
    w.material = js(j, "material");
    w.type = js(j, "type", "exterior");
    return w;
}
nlohmann::json WallSegment::toJson() const {
    return {{"x0", x0}, {"z0", z0}, {"x1", x1}, {"z1", z1}, {"base_y", baseY},
            {"height", height}, {"thickness", thickness}, {"material", material}, {"type", type}};
}

FloorPatch FloorPatch::fromJson(const nlohmann::json& j) {
    FloorPatch f;
    f.x = ji(j, "x"); f.z = ji(j, "z"); f.w = ji(j, "w"); f.d = ji(j, "d"); f.y = ji(j, "y");
    f.thickness = jd(j, "thickness", 0.333);
    f.material = js(j, "material");
    f.role = js(j, "role", "floor");
    return f;
}
nlohmann::json FloorPatch::toJson() const {
    return {{"x", x}, {"z", z}, {"w", w}, {"d", d}, {"y", y},
            {"thickness", thickness}, {"material", material}, {"role", role}};
}

TrimBox TrimBox::fromJson(const nlohmann::json& j) {
    TrimBox t;
    t.x = ji(j, "x"); t.y = ji(j, "y"); t.z = ji(j, "z");
    t.w = ji(j, "w"); t.h = ji(j, "h"); t.d = ji(j, "d");
    t.role = js(j, "role");
    t.material = js(j, "material");
    return t;
}
nlohmann::json TrimBox::toJson() const {
    return {{"x", x}, {"y", y}, {"z", z}, {"w", w}, {"h", h}, {"d", d},
            {"role", role}, {"material", material}};
}

OpeningCut OpeningCut::fromJson(const nlohmann::json& j) {
    OpeningCut o;
    o.x = ji(j, "x"); o.y = ji(j, "y"); o.z = ji(j, "z");
    o.w = ji(j, "w"); o.h = ji(j, "h"); o.d = ji(j, "d");
    o.kind = js(j, "kind", "door");
    o.infill = js(j, "infill", "open");
    if (j.contains("reveal"))
        for (const auto& e : j["reveal"]) o.reveal.push_back(TrimBox::fromJson(e));
    return o;
}
nlohmann::json OpeningCut::toJson() const {
    nlohmann::json rv = nlohmann::json::array();
    for (const auto& t : reveal) rv.push_back(t.toJson());
    return {{"x", x}, {"y", y}, {"z", z}, {"w", w}, {"h", h}, {"d", d},
            {"kind", kind}, {"infill", infill}, {"reveal", rv}};
}

CornerZone CornerZone::fromJson(const nlohmann::json& j) {
    CornerZone c;
    c.x = ji(j, "x"); c.z = ji(j, "z");
    c.dx = ji(j, "dx", 1); c.dz = ji(j, "dz", 1);
    c.baseY = ji(j, "base_y"); c.topY = ji(j, "top_y");
    c.legLongMicro = ji(j, "leg_long_micro", 4);
    c.legShortMicro = ji(j, "leg_short_micro", 3);
    c.material = js(j, "material");
    return c;
}
nlohmann::json CornerZone::toJson() const {
    return {{"x", x}, {"z", z}, {"dx", dx}, {"dz", dz},
            {"base_y", baseY}, {"top_y", topY},
            {"leg_long_micro", legLongMicro}, {"leg_short_micro", legShortMicro},
            {"material", material}};
}

RoofPanel RoofPanel::fromJson(const nlohmann::json& j) {
    RoofPanel r;
    r.x0 = ji(j, "x0"); r.z0 = ji(j, "z0"); r.x1 = ji(j, "x1"); r.z1 = ji(j, "z1");
    r.eaveY = ji(j, "eave_y");
    r.pitch = jd(j, "pitch", 0.8);
    r.style = js(j, "style", "gable");
    r.material = js(j, "material");
    return r;
}
nlohmann::json RoofPanel::toJson() const {
    return {{"x0", x0}, {"z0", z0}, {"x1", x1}, {"z1", z1}, {"eave_y", eaveY},
            {"pitch", pitch}, {"style", style}, {"material", material}};
}

StairRecord StairRecord::fromJson(const nlohmann::json& j) {
    StairRecord s;
    s.x = ji(j, "x"); s.z = ji(j, "z"); s.w = ji(j, "w"); s.d = ji(j, "d");
    s.fromStory = ji(j, "from_story"); s.toStory = ji(j, "to_story", 1);
    s.baseY = ji(j, "base_y"); s.topY = ji(j, "top_y");
    s.botWalkMicro = ji(j, "bot_walk_micro"); s.topWalkMicro = ji(j, "top_walk_micro");
    s.form = js(j, "form", "switchback");
    s.holeX = ji(j, "hole_x"); s.holeZ = ji(j, "hole_z");
    s.holeW = ji(j, "hole_w"); s.holeD = ji(j, "hole_d");
    return s;
}
nlohmann::json StairRecord::toJson() const {
    return {{"x", x}, {"z", z}, {"w", w}, {"d", d},
            {"from_story", fromStory}, {"to_story", toStory},
            {"base_y", baseY}, {"top_y", topY},
            {"bot_walk_micro", botWalkMicro}, {"top_walk_micro", topWalkMicro},
            {"form", form},
            {"hole_x", holeX}, {"hole_z", holeZ}, {"hole_w", holeW}, {"hole_d", holeD}};
}

FixturePlacement FixturePlacement::fromJson(const nlohmann::json& j) {
    FixturePlacement f;
    f.archetype = js(j, "archetype");
    f.templateName = js(j, "template");
    f.worldPos = jivec3(j, "world_pos");
    f.rotation = ji(j, "rotation");
    return f;
}
nlohmann::json FixturePlacement::toJson() const {
    return {{"archetype", archetype}, {"template", templateName},
            {"world_pos", {worldPos.x, worldPos.y, worldPos.z}}, {"rotation", rotation}};
}

LightPlacement LightPlacement::fromJson(const nlohmann::json& j) {
    LightPlacement l;
    l.pos = jvec3(j, "pos", glm::vec3(0.0f));
    l.color = jvec3(j, "color", glm::vec3(1.0f, 0.8f, 0.5f));
    l.radius = jd(j, "radius", 7.0);
    return l;
}
nlohmann::json LightPlacement::toJson() const {
    return {{"pos", {pos.x, pos.y, pos.z}}, {"color", {color.r, color.g, color.b}},
            {"radius", radius}};
}

std::string AssemblyPlan::featureAt(const glm::ivec3& p) const {
    // Openings first: a carved doorway/window cube is not "wall" even though the
    // wall band covers it. Classification uses ONLY the recorded "clear" reveal
    // boxes (the carved passage, structure-local micro); trim boxes (jamb/lintel/
    // sill/leaf) stay part of the wall/facade reading.
    for (const auto& o : openings)
        for (const auto& t : o.reveal) {
            if (t.role != "clear") continue;
            if (p.x * 9 + 9 <= t.x || p.x * 9 >= t.x + t.w) continue;
            if (p.y * 9 + 9 <= t.y || p.y * 9 >= t.y + t.h) continue;
            if (p.z * 9 + 9 <= t.z || p.z * 9 >= t.z + t.d) continue;
            return "opening";
        }
    // Quoined corners next — more specific than the wall band they dress.
    for (const auto& q : corners)
        if (p.x == q.x && p.z == q.z && p.y >= q.baseY && p.y < q.topY)
            return "quoin";
    // Walls next — the load-bearing answer consumers usually want ("does a chest
    // back onto this cell?"). Exterior segments are per-edge-cell: the wall band
    // lives in cube (x0,z0); (x1,z1) is the outside neighbor. Interior segments are
    // a partition PLANE on the cube boundary at coord, straddling both adjacent cubes.
    for (const auto& w : walls) {
        if (p.y < w.baseY || p.y >= w.baseY + w.height) continue;
        if (w.type == "interior") {
            if (w.x0 == w.x1) {          // plane along Z at x = x0
                if ((p.x == w.x0 - 1 || p.x == w.x0) &&
                    p.z >= std::min(w.z0, w.z1) && p.z < std::max(w.z0, w.z1))
                    return "wall";
            } else {                     // plane along X at z = z0
                if ((p.z == w.z0 - 1 || p.z == w.z0) &&
                    p.x >= std::min(w.x0, w.x1) && p.x < std::max(w.x0, w.x1))
                    return "wall";
            }
        } else if (p.x == w.x0 && p.z == w.z0) {
            return "wall";
        }
    }
    // Stairs before floors: the flight volume inside the well is "stair", and — at
    // the emergence (upper-slab) level — so is the well HOLE, which the floor patch
    // rect still covers even though the realizer cut it (the standing
    // misclassification of stairwell holes as slab). Outside the hole at that level
    // the slab is intact and falls through to the floor records.
    for (const auto& s : stairs) {
        if (p.y < s.baseY || p.y >= s.topY) continue;
        if (p.x < s.x || p.x >= s.x + s.w || p.z < s.z || p.z >= s.z + s.d) continue;
        if (p.y == s.topY - 1 && s.holeW > 0 && s.holeD > 0) {
            const int hx0 = s.holeX / 9, hx1 = (s.holeX + s.holeW - 1) / 9;
            const int hz0 = s.holeZ / 9, hz1 = (s.holeZ + s.holeD - 1) / 9;
            if (p.x < hx0 || p.x > hx1 || p.z < hz0 || p.z > hz1) continue;
        }
        return "stair";
    }
    for (const auto& f : floors) {
        if (p.y == f.y && p.x >= f.x && p.x < f.x + f.w && p.z >= f.z && p.z < f.z + f.d)
            return f.role == "ceiling" ? "ceiling" : "floor";
    }
    for (const auto& fc : foundation) {
        if (p.x == fc.x && p.z == fc.z && p.y >= fc.bearingY && p.y < fc.topY)
            return "foundation";
    }
    for (const auto& r : roof) {
        // Coarse: anything at/above the eave within the panel extent (+1 cube of
        // overhang) is roof. The slope profile is a refinement, not a correctness need.
        if (p.y >= r.eaveY && p.x >= r.x0 - 1 && p.x <= r.x1 + 1 &&
            p.z >= r.z0 - 1 && p.z <= r.z1 + 1)
            return "roof";
    }
    return "";
}

AssemblyPlan AssemblyPlan::fromJson(const nlohmann::json& j) {
    AssemblyPlan p;
    auto load = [&](const char* key, auto& vec, auto parser) {
        if (j.contains(key)) for (const auto& e : j[key]) vec.push_back(parser(e));
    };
    load("foundation", p.foundation, FoundationColumn::fromJson);
    load("walls",      p.walls,      WallSegment::fromJson);
    load("floors",     p.floors,     FloorPatch::fromJson);
    load("openings",   p.openings,   OpeningCut::fromJson);
    load("stairs",     p.stairs,     StairRecord::fromJson);
    load("corners",    p.corners,    CornerZone::fromJson);
    load("roof",       p.roof,       RoofPanel::fromJson);
    load("fixtures",   p.fixtures,   FixturePlacement::fromJson);
    load("lights",     p.lights,     LightPlacement::fromJson);
    return p;
}
nlohmann::json AssemblyPlan::toJson() const {
    auto dump = [](const auto& vec) {
        nlohmann::json a = nlohmann::json::array();
        for (const auto& e : vec) a.push_back(e.toJson());
        return a;
    };
    return {{"foundation", dump(foundation)}, {"walls", dump(walls)}, {"floors", dump(floors)},
            {"openings", dump(openings)}, {"stairs", dump(stairs)}, {"corners", dump(corners)},
            {"roof", dump(roof)}, {"fixtures", dump(fixtures)}, {"lights", dump(lights)}};
}

} // namespace Core
} // namespace Phyxel
