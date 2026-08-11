#include "core/BuildingProgram.h"

namespace Phyxel {
namespace Core {

namespace {
int jint(const nlohmann::json& j, const char* k, int def) {
    return (j.contains(k) && j[k].is_number()) ? j[k].get<int>() : def;
}
std::string jstr(const nlohmann::json& j, const char* k, const std::string& def = "") {
    return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : def;
}
} // namespace

Rect Rect::fromJson(const nlohmann::json& j) {
    Rect r;
    if (j.is_array() && j.size() >= 4) {
        r.x = j[0].get<int>();
        r.z = j[1].get<int>();
        r.w = j[2].get<int>();
        r.d = j[3].get<int>();
    }
    return r;
}

ProgRoom ProgRoom::fromJson(const nlohmann::json& j) {
    ProgRoom r;
    r.id = jstr(j, "id");
    if (j.contains("rect")) r.rect = Rect::fromJson(j["rect"]);
    r.purpose = jstr(j, "purpose", "generic");
    r.floorMat = jstr(j, "floor_mat");
    return r;
}
nlohmann::json ProgRoom::toJson() const {
    return {{"id", id}, {"rect", rect.toJson()}, {"purpose", purpose}, {"floor_mat", floorMat}};
}

ProgPortal ProgPortal::fromJson(const nlohmann::json& j) {
    ProgPortal p;
    if (j.contains("between") && j["between"].is_array() && j["between"].size() >= 2) {
        p.a = j["between"][0].get<std::string>();
        p.b = j["between"][1].get<std::string>();
    }
    if (j.contains("pos") && j["pos"].is_array() && j["pos"].size() >= 2) {
        p.px = j["pos"][0].get<int>();
        p.pz = j["pos"][1].get<int>();
    }
    p.width = jint(j, "width", 2);
    p.height = jint(j, "height", 3);
    p.kind = jstr(j, "kind", "door");
    p.infill = jstr(j, "infill", "open");
    if (j.contains("door") && j["door"].is_object()) {
        const auto& d = j["door"];
        p.lockable = d.value("lockable", false);
        p.key = d.value("key", std::string());
    }
    return p;
}
nlohmann::json ProgPortal::toJson() const {
    nlohmann::json j = {{"between", {a, b}}, {"pos", {px, pz}},
                        {"width", width}, {"height", height}, {"kind", kind}};
    if (kind == "window" && infill != "open") j["infill"] = infill;
    if (kind == "door") j["door"] = {{"lockable", lockable}, {"key", key}};
    return j;
}

ProgStair ProgStair::fromJson(const nlohmann::json& j) {
    ProgStair s;
    s.fromStory = jint(j, "from_story", 0);
    s.toStory = jint(j, "to_story", 1);
    if (j.contains("rect")) s.rect = Rect::fromJson(j["rect"]);
    s.kind = jstr(j, "kind", "straight");
    s.form = jstr(j, "form", "switchback");
    return s;
}
nlohmann::json ProgStair::toJson() const {
    return {{"from_story", fromStory}, {"to_story", toStory},
            {"rect", rect.toJson()}, {"kind", kind}, {"form", form}};
}

ProgFixture ProgFixture::fromJson(const nlohmann::json& j) {
    ProgFixture f;
    f.type = jstr(j, "type");
    if (j.contains("rect")) f.rect = Rect::fromJson(j["rect"]);
    f.facing = jstr(j, "facing", "south");
    f.room = jstr(j, "room");
    // Engine rotation convention: 0 -> front +z, 90 -> -x, 180 -> -z, 270 -> +x.
    // World axes (docs/CoordinateSystem.md): +X = east, +Z = north. So a fixture
    // FACING north is rotation 0. A hand-authored fixture names a compass facing
    // instead of a number; map it so both forms reach the realizer as one value.
    if (j.contains("rotation") && j["rotation"].is_number())
        f.rotation = ((jint(j, "rotation", 0) % 360) + 360) % 360;
    else
        f.rotation = (f.facing == "west")  ? 90
                   : (f.facing == "south") ? 180
                   : (f.facing == "east")  ? 270 : 0;
    return f;
}
nlohmann::json ProgFixture::toJson() const {
    return {{"type", type}, {"rect", rect.toJson()}, {"facing", facing}, {"room", room},
            {"rotation", rotation}};
}

ProgStory ProgStory::fromJson(const nlohmann::json& j) {
    ProgStory s;
    s.height = jint(j, "height", 4);
    if (j.contains("rooms"))    for (const auto& e : j["rooms"])    s.rooms.push_back(ProgRoom::fromJson(e));
    if (j.contains("portals"))  for (const auto& e : j["portals"])  s.portals.push_back(ProgPortal::fromJson(e));
    if (j.contains("stairs"))   for (const auto& e : j["stairs"])   s.stairs.push_back(ProgStair::fromJson(e));
    if (j.contains("fixtures")) for (const auto& e : j["fixtures"]) s.fixtures.push_back(ProgFixture::fromJson(e));
    return s;
}
nlohmann::json ProgStory::toJson() const {
    nlohmann::json rooms = nlohmann::json::array(), portals = nlohmann::json::array(),
                   stairs = nlohmann::json::array(), fixtures = nlohmann::json::array();
    for (const auto& r : this->rooms)    rooms.push_back(r.toJson());
    for (const auto& p : this->portals)  portals.push_back(p.toJson());
    for (const auto& s : this->stairs)   stairs.push_back(s.toJson());
    for (const auto& f : this->fixtures) fixtures.push_back(f.toJson());
    return {{"height", height}, {"rooms", rooms}, {"portals", portals},
            {"stairs", stairs}, {"fixtures", fixtures}};
}

BuildingProgram BuildingProgram::fromJson(const nlohmann::json& j) {
    BuildingProgram b;
    b.name = jstr(j, "name");
    b.style = jstr(j, "style");
    b.function = jstr(j, "function", "house");
    if (j.contains("footprint") && j["footprint"].is_array() && j["footprint"].size() >= 2) {
        b.footprintW = j["footprint"][0].get<int>();
        b.footprintD = j["footprint"][1].get<int>();
    }
    b.substructure = jstr(j, "substructure", "slab");
    b.roofStyle = jstr(j, "roof_style");
    b.typology = jstr(j, "typology");
    b.footprintShape = jstr(j, "footprint_shape");
    b.front = jstr(j, "front");
    if (j.contains("stories"))
        for (const auto& e : j["stories"]) b.stories.push_back(ProgStory::fromJson(e));
    return b;
}
nlohmann::json BuildingProgram::toJson() const {
    nlohmann::json stories = nlohmann::json::array();
    for (const auto& s : this->stories) stories.push_back(s.toJson());
    return {{"name", name}, {"style", style}, {"function", function},
            {"footprint", {footprintW, footprintD}}, {"substructure", substructure},
            {"roof_style", roofStyle}, {"typology", typology}, {"footprint_shape", footprintShape},
            {"front", front}, {"stories", stories}};
}

} // namespace Core
} // namespace Phyxel
