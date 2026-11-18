#include "core/SettlementMap.h"

#include <algorithm>
#include <vector>

namespace Phyxel {
namespace Core {

bool MapLot::inParcel(int x, int z) const {
    if (parcel.empty()) return x >= rect.x && x < rect.x1() && z >= rect.z && z < rect.z1();
    for (const auto& r : parcel) if (x >= r.x && x < r.x1() && z >= r.z && z < r.z1()) return true;
    return false;
}

int MapLot::parcelArea() const {
    if (parcel.empty()) return rect.w * rect.d;
    int n = 0;
    for (const auto& r : parcel) n += r.w * r.d;   // the rects are disjoint by construction (row runs merged)
    return n;
}

Rect MapLot::parcelRect() const { return parcel.size() == 1 ? parcel[0] : rect; }

ParcelTerrace planParcelTerrace(const Rect& pr, int skirt, const std::function<int(int, int)>& groundAt) {
    ParcelTerrace t;
    if (pr.w < 2 || pr.d < 2 || !groundAt) return t;
    std::vector<int> tops;
    tops.reserve(static_cast<size_t>(pr.w) * pr.d);
    for (int x = pr.x; x < pr.x1(); ++x) for (int z = pr.z; z < pr.z1(); ++z) tops.push_back(groundAt(x, z));
    std::sort(tops.begin(), tops.end());
    t.ok = true; t.grade = tops[tops.size() / 2];
    t.cells.reserve(static_cast<size_t>(pr.w + 2 * skirt) * (pr.d + 2 * skirt));
    for (int x = pr.x - skirt; x < pr.x1() + skirt; ++x)
        for (int z = pr.z - skirt; z < pr.z1() + skirt; ++z) {
            int target = t.grade;
            const int dx = std::max({pr.x - x, x - (pr.x1() - 1), 0}), dz = std::max({pr.z - z, z - (pr.z1() - 1), 0});
            const int dist = std::max(dx, dz);
            if (dist > 0) {   // the skirt: ramp grade -> natural, at most one cube a cell
                const int nat = groundAt(x, z);
                target = (nat > t.grade) ? std::min(nat, t.grade + dist) : std::max(nat, t.grade - dist);
            }
            t.cells.push_back({x, z, target});
        }
    return t;
}

nlohmann::json SettlementMap::toJson() const {
    nlohmann::json j;
    j["era"] = era;
    j["tier"] = tier;
    j["morphology"] = morphology;
    j["seed"] = seed;
    j["origin"] = {{"x", originX}, {"y", originY}, {"z", originZ}};
    j["size"] = {{"w", W}, {"d", D}};
    j["entry_node"] = entryNode;
    j["bake_ms"] = bakeMs;
    j["refusal"] = refusal;
    if (!marketNote.empty()) j["market_note"] = marketNote;
    if (marketLane > 0) j["market_lane"] = marketLane;
    if (!crossings.empty()) {   // D69 (c1)
        nlohmann::json cs = nlohmann::json::array();
        for (const auto& c : crossings) {
            nlohmann::json cells = nlohmann::json::array();
            for (const auto& [x, z] : c.cells) cells.push_back({x, z});
            cs.push_back({{"way", c.way}, {"kind", c.kind}, {"cells", cells}});
        }
        j["crossings"] = cs;
    }
    j["dropped_lots"] = droppedLots;
    j["target_lots"] = targetLots;
    j["buildable_fraction"] = buildableFraction;
    nlohmann::json ns = nlohmann::json::array();
    for (const auto& n : nodes) ns.push_back({{"id", n.id}, {"x", n.x}, {"z", n.z}, {"y", n.y}});
    j["nodes"] = ns;
    nlohmann::json ws = nlohmann::json::array();
    for (const auto& w : ways)
        ws.push_back({{"id", w.id}, {"highway", w.highway}, {"nodes", w.nodes}, {"width", w.width}, {"grade", w.grade}, {"cls", w.cls}});
    j["ways"] = ws;
    nlohmann::json ls = nlohmann::json::array();
    for (const auto& l : lots) {
        nlohmann::json lj = {{"id", l.id}, {"rect", l.rect.toJson()}, {"footprint", l.footprint.toJson()}, {"typology", l.typology},
                             {"stories", l.stories}, {"orientation", l.orientation}, {"ground", l.ground}, {"way", l.way},
                             {"setback", l.setback}, {"interest", l.interest}};
        if (l.hasDoor) { lj["door"] = {l.doorX, l.doorZ}; lj["door_normal"] = {l.doorNX, l.doorNZ}; }   // M5b-1a
        if (!l.style.empty()) lj["style"] = l.style;
        if (l.hasSeat) { lj["way_level"] = l.wayLevel; lj["pad_y"] = l.padY; lj["threshold"] = l.threshold; }
        if (l.hasTerrace) lj["terrace_y"] = l.terraceY;
        if (l.hasApproach) {   // increment D
            nlohmann::json ap = nlohmann::json::array();
            for (const auto& a : l.approach) ap.push_back({{"x", a.x}, {"z", a.z}, {"level", a.level}});
            lj["approach"] = std::move(ap);
        }
        if (!l.parcel.empty()) {   // M5b-2
            nlohmann::json pj = nlohmann::json::array();
            for (const auto& r : l.parcel) pj.push_back(r.toJson());
            lj["parcel"] = std::move(pj);
        }
        ls.push_back(std::move(lj));
    }
    j["lots"] = ls;
    nlohmann::json as = nlohmann::json::array();
    for (const auto& a : areas) {
        nlohmann::json aj = {{"id", a.id}, {"landuse", a.landuse}, {"rect", a.rect.toJson()}};
        if (a.lot >= 0) aj["lot"] = a.lot;   // M5c-2: a croft's lot
        as.push_back(std::move(aj));
    }
    j["areas"] = as;
    // M5d-1: written only when the tier asked for a wall - an unwalled tier's record is byte-identical to before
    if (wall.present || !wall.refusal.empty()) {
        nlohmann::json wj = {{"barrier", "wall"}, {"present", wall.present}, {"margin", wall.margin}};
        if (!wall.refusal.empty()) wj["refusal"] = wall.refusal;
        nlohmann::json ns = nlohmann::json::array(), gs = nlohmann::json::array(), ws = nlohmann::json::array();
        for (const auto& c : wall.corners()) ns.push_back({c.x, c.z});
        for (const auto& g : wall.gates) gs.push_back({{"x", g.x}, {"z", g.z}, {"way", g.way}, {"width", g.width}});
        for (const auto& g : wall.waterGaps) ws.push_back({{"x", g.x}, {"z", g.z}, {"cells", g.cells}});
        wj["nodes"] = ns; wj["gates"] = gs; wj["water_gaps"] = ws;
        if (!wall.towers.empty()) {   // M5d-3: written only when the wall carries tower entries
            nlohmann::json ts = nlohmann::json::array();
            for (const auto& t : wall.towers) {
                nlohmann::json tj = {{"x", t.x}, {"z", t.z}, {"corner", {t.cornerX, t.cornerZ}}, {"size", t.size}, {"built", t.built}};
                if (t.built) { tj["door_side"] = std::string(1, t.doorSide); tj["door"] = {t.doorX, t.doorZ}; tj["out"] = {t.outX, t.outZ}; }
                else tj["skipped"] = t.skipped;
                ts.push_back(std::move(tj));
            }
            wj["towers"] = ts;
        }
        wj["closed"] = wall.present && wall.waterGaps.empty();
        wj["cells"] = wall.ring.size();
        j["barriers"] = nlohmann::json::array({wj});
    }
    return j;
}

std::vector<MapWall::Pt> MapWall::corners() const {
    std::vector<Pt> out;
    const size_t n = ring.size();
    if (n < 4) return out;
    for (size_t i = 0; i < n; ++i) {
        const Pt& a = ring[(i + n - 1) % n]; const Pt& b = ring[i]; const Pt& c = ring[(i + 1) % n];
        if ((b.x - a.x) != (c.x - b.x) || (b.z - a.z) != (c.z - b.z)) out.push_back(b);
    }
    return out;
}

std::vector<MapWall::Pt> MapWall::expand(const std::vector<Pt>& cs) {
    std::vector<Pt> out;
    const size_t n = cs.size();
    for (size_t i = 0; i < n; ++i) {
        const Pt a = cs[i], b = cs[(i + 1) % n];
        if (a.x != b.x && a.z != b.z) return {};   // not axis-aligned: not a ring this record can hold
        const int dx = (b.x > a.x) - (b.x < a.x), dz = (b.z > a.z) - (b.z < a.z);
        for (Pt c = a; c.x != b.x || c.z != b.z; c.x += dx, c.z += dz) out.push_back(c);
    }
    return out;
}

SettlementMap SettlementMap::fromJson(const nlohmann::json& j) {
    SettlementMap m;
    if (!j.is_object()) return m;
    m.era = j.value("era", std::string());
    m.tier = j.value("tier", std::string());
    m.morphology = j.value("morphology", std::string());
    m.seed = j.value("seed", 0u);
    if (j.contains("origin") && j["origin"].is_object()) {
        m.originX = j["origin"].value("x", 0); m.originY = j["origin"].value("y", 0); m.originZ = j["origin"].value("z", 0);
    }
    if (j.contains("size") && j["size"].is_object()) { m.W = j["size"].value("w", 0); m.D = j["size"].value("d", 0); }
    m.entryNode = j.value("entry_node", -1);
    m.bakeMs = j.value("bake_ms", 0.0f);
    m.refusal = j.value("refusal", std::string());
    m.marketNote = j.value("market_note", std::string());
    m.marketLane = j.value("market_lane", 0);
    if (j.contains("crossings") && j["crossings"].is_array())
        for (const auto& cj : j["crossings"]) {
            Crossing c; c.way = cj.value("way", -1); c.kind = cj.value("kind", std::string("ford"));
            if (cj.contains("cells") && cj["cells"].is_array())
                for (const auto& cell : cj["cells"]) if (cell.is_array() && cell.size() == 2) c.cells.push_back({cell[0].get<int>(), cell[1].get<int>()});
            m.crossings.push_back(std::move(c));
        }
    m.droppedLots = j.value("dropped_lots", 0);
    m.targetLots = j.value("target_lots", 0);
    m.buildableFraction = j.value("buildable_fraction", -1.0f);
    if (j.contains("nodes") && j["nodes"].is_array())
        for (const auto& n : j["nodes"]) m.nodes.push_back({n.value("id", 0), n.value("x", 0), n.value("z", 0), n.value("y", 0)});
    if (j.contains("ways") && j["ways"].is_array())
        for (const auto& w : j["ways"]) {
            MapWay mw;
            mw.id = w.value("id", 0); mw.highway = w.value("highway", std::string()); mw.width = w.value("width", 0.0f);
            mw.grade = w.value("grade", 0.0f); mw.cls = w.value("cls", 0);
            if (w.contains("nodes") && w["nodes"].is_array()) for (const auto& n : w["nodes"]) mw.nodes.push_back(n.get<int>());
            m.ways.push_back(std::move(mw));
        }
    if (j.contains("lots") && j["lots"].is_array())
        for (const auto& l : j["lots"]) {
            MapLot ml;
            ml.id = l.value("id", 0);
            if (l.contains("rect")) ml.rect = Rect::fromJson(l["rect"]);
            if (l.contains("footprint")) ml.footprint = Rect::fromJson(l["footprint"]);
            ml.typology = l.value("typology", std::string()); ml.stories = l.value("stories", 1);
            ml.orientation = l.value("orientation", 0); ml.ground = l.value("ground", std::string("graded"));
            ml.way = l.value("way", -1); ml.setback = l.value("setback", 1); ml.interest = l.value("interest", 0.0f);
            if (l.contains("door") && l["door"].is_array() && l["door"].size() == 2 && l.contains("door_normal") &&
                l["door_normal"].is_array() && l["door_normal"].size() == 2) {
                ml.hasDoor = true;
                ml.doorX = l["door"][0].get<int>(); ml.doorZ = l["door"][1].get<int>();
                ml.doorNX = l["door_normal"][0].get<int>(); ml.doorNZ = l["door_normal"][1].get<int>();
            }
            ml.style = l.value("style", std::string());
            if (l.contains("terrace_y")) { ml.hasTerrace = true; ml.terraceY = l.value("terrace_y", 0); }
            if (l.contains("approach") && l["approach"].is_array()) {
                ml.hasApproach = true;
                for (const auto& a : l["approach"])
                    if (a.is_object()) ml.approach.push_back({a.value("x", 0), a.value("z", 0), a.value("level", 0)});
            }
            if (l.contains("parcel") && l["parcel"].is_array())   // M5b-2; absent = the rect
                for (const auto& r : l["parcel"]) if (r.is_array() && r.size() == 4) ml.parcel.push_back(Rect::fromJson(r));
            if (l.contains("pad_y") && l.contains("way_level") && l.contains("threshold")) {
                ml.hasSeat = true; ml.wayLevel = l.value("way_level", 0); ml.padY = l.value("pad_y", 0); ml.threshold = l.value("threshold", 0);
            }
            m.lots.push_back(std::move(ml));
        }
    if (j.contains("areas") && j["areas"].is_array())
        for (const auto& a : j["areas"]) {
            MapArea ma;
            ma.id = a.value("id", 0); ma.landuse = a.value("landuse", std::string());
            if (a.contains("rect")) ma.rect = Rect::fromJson(a["rect"]);
            ma.lot = a.value("lot", -1);
            m.areas.push_back(std::move(ma));
        }
    if (j.contains("barriers") && j["barriers"].is_array())
        for (const auto& b : j["barriers"]) {
            if (b.value("barrier", std::string()) != "wall") continue;
            m.wall.present = b.value("present", false);
            m.wall.refusal = b.value("refusal", std::string());
            m.wall.margin = b.value("margin", 0);
            std::vector<MapWall::Pt> cs;
            bool nodesOk = true;
            for (const auto& n : b.value("nodes", nlohmann::json::array())) {
                if (!n.is_array() || n.size() < 2 || !n[0].is_number_integer() || !n[1].is_number_integer()) { nodesOk = false; break; }
                cs.push_back({n[0].get<int>(), n[1].get<int>()});
            }
            m.wall.ring = nodesOk ? MapWall::expand(cs) : std::vector<MapWall::Pt>();
            // the audit of M5d-1 (bug A): a present wall whose nodes are not a ring (fewer than four corners, a leg off
            // the axes, a malformed node) is REFUSED on read - never present with an empty ring
            if (m.wall.present && (cs.size() < 4 || m.wall.ring.size() < 8)) {
                m.wall.present = false; m.wall.ring.clear();
                if (m.wall.refusal.empty()) m.wall.refusal = "the record's wall nodes are not a ring (" + std::to_string(cs.size()) + " usable nodes)";
            }
            for (const auto& g : b.value("gates", nlohmann::json::array())) m.wall.gates.push_back({g.value("x", 0), g.value("z", 0), g.value("way", -1), g.value("width", 0)});
            for (const auto& g : b.value("water_gaps", nlohmann::json::array())) m.wall.waterGaps.push_back({g.value("x", 0), g.value("z", 0), g.value("cells", 0)});
            for (const auto& t : b.value("towers", nlohmann::json::array())) {   // M5d-3
                if (!t.is_object()) continue;
                MapWall::Tower mt;
                mt.x = t.value("x", 0); mt.z = t.value("z", 0); mt.size = t.value("size", 0); mt.built = t.value("built", false);
                mt.skipped = t.value("skipped", std::string());
                const auto readPair = [&t](const char* key, int& a, int& c) {
                    if (!t.contains(key) || !t[key].is_array() || t[key].size() != 2 || !t[key][0].is_number_integer() || !t[key][1].is_number_integer()) return false;
                    a = t[key][0].get<int>(); c = t[key][1].get<int>(); return true;
                };
                const bool hasCorner = readPair("corner", mt.cornerX, mt.cornerZ);
                const bool hasDoor = readPair("door", mt.doorX, mt.doorZ), hasOut = readPair("out", mt.outX, mt.outZ);
                const std::string side = t.value("door_side", std::string());
                if (!side.empty()) mt.doorSide = side[0];
                // a built tower whose record cannot place its drum or its door is NOT BUILT on read, and says so
                if (mt.built && (!hasCorner || !hasDoor || !hasOut || side.empty() || mt.size <= 0)) {
                    mt.built = false; mt.skipped = "the record's tower carries no drum, corner or door";
                }
                if (mt.built) {   // the audit of M5d-3 (defect 5): the corner and the door inside the drum's square, the side a side, the out cell the door's neighbour on that side
                    const Rect bb = mt.bbox();
                    const auto in = [&bb](int x, int z) { return x >= bb.x && x < bb.x1() && z >= bb.z && z < bb.z1(); };
                    const int nx = mt.doorSide == 'E' ? 1 : mt.doorSide == 'W' ? -1 : 0, nz = mt.doorSide == 'N' ? 1 : mt.doorSide == 'S' ? -1 : 0;
                    if ((nx == 0 && nz == 0) || !in(mt.cornerX, mt.cornerZ) || !in(mt.doorX, mt.doorZ) || mt.outX != mt.doorX + nx || mt.outZ != mt.doorZ + nz) {
                        mt.built = false; mt.skipped = "the record's tower does not hold together (its corner, door, side or out cell)";
                    }
                }
                m.wall.towers.push_back(std::move(mt));
            }
        }
    return m;
}

}  // namespace Core
}  // namespace Phyxel
