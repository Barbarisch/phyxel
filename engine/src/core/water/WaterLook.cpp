#include "core/water/WaterLook.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>

namespace Phyxel::Core::Water {

namespace {
float clampNote(float v, float lo, float hi, const char* name, std::vector<std::string>* notes) {
    const float c = std::clamp(v, lo, hi);
    if (c != v && notes) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s %.4g clamped to %.4g (range %.4g..%.4g)", name, v, c, lo, hi);
        notes->push_back(buf);
    }
    return c;
}
}  // namespace

WaterLook clampLook(const WaterLook& in, std::vector<std::string>* notes) {
    WaterLook o = in;
    if (o.hasClarity()) o.clarity = clampNote(o.clarity, 0.1f, 100.0f, "clarity", notes);
    if (o.hasTint()) {
        o.tint.x = clampNote(o.tint.x, 0.0f, 1.0f, "tint.r", notes);
        o.tint.y = clampNote(o.tint.y, 0.0f, 1.0f, "tint.g", notes);
        o.tint.z = clampNote(o.tint.z, 0.0f, 1.0f, "tint.b", notes);
    }
    if (o.hasTurbidity()) o.turbidity = clampNote(o.turbidity, 0.0f, 1.0f, "turbidity", notes);
    if (o.hasRoughness()) o.roughness = clampNote(o.roughness, 0.0f, 2.0f, "roughness", notes);
    return o;
}

WaterLookPacked packLook(const WaterLook& l) {
    WaterLookPacked p;
    if (l.hasTint()) { p.look0.x = l.tint.x; p.look0.y = l.tint.y; p.look0.z = l.tint.z; }
    if (l.hasClarity()) p.look0.w = l.clarity;
    if (l.hasTurbidity()) p.look1.x = l.turbidity;
    if (l.hasRoughness()) p.look1.y = l.roughness;
    return p;
}

std::string lookToJson(const WaterLook& l) {
    nlohmann::json j = nlohmann::json::object();
    if (l.hasClarity()) j["clarity"] = l.clarity;
    if (l.hasTint()) j["tint"] = {l.tint.x, l.tint.y, l.tint.z};
    if (l.hasTurbidity()) j["turbidity"] = l.turbidity;
    if (l.hasRoughness()) j["roughness"] = l.roughness;
    return j.dump();
}

WaterLook lookFromJson(const std::string& text) {
    WaterLook l;
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return l;
    if (j.contains("clarity") && j["clarity"].is_number()) l.clarity = j["clarity"].get<float>();
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() == 3)
        l.tint = glm::vec3(j["tint"][0].get<float>(), j["tint"][1].get<float>(), j["tint"][2].get<float>());
    if (j.contains("turbidity") && j["turbidity"].is_number()) l.turbidity = j["turbidity"].get<float>();
    if (j.contains("roughness") && j["roughness"].is_number()) l.roughness = j["roughness"].get<float>();
    return l;
}

}  // namespace Phyxel::Core::Water
