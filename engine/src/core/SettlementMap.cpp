#include "core/SettlementMap.h"

namespace Phyxel {
namespace Core {

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
        ls.push_back(std::move(lj));
    }
    j["lots"] = ls;
    nlohmann::json as = nlohmann::json::array();
    for (const auto& a : areas) as.push_back({{"id", a.id}, {"landuse", a.landuse}, {"rect", a.rect.toJson()}});
    j["areas"] = as;
    return j;
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
            m.areas.push_back(std::move(ma));
        }
    return m;
}

}  // namespace Core
}  // namespace Phyxel
