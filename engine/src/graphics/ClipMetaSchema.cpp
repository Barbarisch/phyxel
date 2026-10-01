#include "graphics/ClipMetaSchema.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

namespace Phyxel::ClipMeta {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Builtin fallback = the numeric keys the runtime read before the schema existed (A0 #6
// table in applyClipMetaFromFile). A packaged game missing the JSON keeps today's behaviour;
// factor coordinates then read as unknown keys, which is loud, not silent.
std::map<std::string, KeySpec> builtinSchema() {
    std::map<std::string, KeySpec> m;
    auto f = [&](const char* k, const char* field) { KeySpec s; s.type = ValueType::Float; s.runtimeField = field; m[k] = s; };
    auto b = [&](const char* k, const char* field) { KeySpec s; s.type = ValueType::Bool;  s.runtimeField = field; m[k] = s; };
    KeySpec type; type.type = ValueType::String; type.runtimeField = "clipType"; m["type"] = type;
    b("warpEnabled", "warpEnabled");       f("authoredFallDist", "authoredFallDist");
    f("takeoffEnd", "takeoffEnd");         f("contactFrame", "contactFrame");
    f("warpScaleMin", "warpScaleMin");     f("warpScaleMax", "warpScaleMax");
    f("hitFrameFraction", "hitFrameFraction");
    { KeySpec s; s.type = ValueType::Float; s.runtimeField = "hitFrameFraction"; s.alias = true; m["releaseFrame"] = s; }
    b("interruptible", "interruptible");   f("interruptAfter", "interruptAfter");
    f("stairStepHeight", "stairStepHeight"); f("stairStepDepth", "stairStepDepth");
    f("contactFrame1", "contactFrame1");   f("contactFrame2", "contactFrame2");
    f("footIKSurfaceReach", "footIKSurfaceReach"); f("footIKBodyRange", "footIKBodyRange");
    f("stanceL", "stanceL");               f("stanceR", "stanceR");
    b("footIKEnabled", "footIKEnabled");   // A5: runtime opt-out per clip
    for (const char* k : {"castFamily", "castRole", "meleeFamily", "meleeRole", "weaponFamily",
                          "weaponRole", "gatheringRole", "source", "ckpt", "seed", "cfg", "rep",
                          "promoted_from"}) {
        KeySpec s; s.type = ValueType::String; s.toolOnly = true; m[k] = s;
    }
    { KeySpec s; s.type = ValueType::Float; s.toolOnly = true; m["impactFrameFraction"] = s; }
    return m;
}

struct Loaded {
    std::map<std::string, KeySpec> keys;
    bool fromFile = false;
};

const Loaded& loaded() {
    static Loaded L;
    static std::once_flag once;
    std::call_once(once, [] {
        L.keys = builtinSchema();
        std::ifstream in(schemaPath());
        if (!in.is_open()) {
            LOG_WARN("ClipMeta", "schema file {} not found — builtin numeric keys only; factor "
                     "coordinates will read as unknown keys", schemaPath());
            return;
        }
        try {
            nlohmann::json j; in >> j;
            std::map<std::string, KeySpec> fromJson;
            for (auto it = j.at("keys").begin(); it != j.at("keys").end(); ++it) {
                const auto& spec = it.value();
                KeySpec s;
                const std::string t = spec.value("type", "float");
                if      (t == "float")  s.type = ValueType::Float;
                else if (t == "bool")   s.type = ValueType::Bool;
                else if (t == "string") s.type = ValueType::String;
                else if (t == "enum")   s.type = ValueType::Enum;
                else throw std::runtime_error("key '" + it.key() + "': unknown type '" + t + "'");
                if (spec.contains("values"))
                    for (const auto& v : spec["values"]) s.values.push_back(v.get<std::string>());
                if (s.type == ValueType::Enum && s.values.empty())
                    throw std::runtime_error("key '" + it.key() + "': enum without values");
                s.toolOnly     = spec.value("toolOnly", false);
                s.factor       = spec.value("factor", false);
                s.runtimeField = spec.value("runtime", std::string());
                s.alias        = spec.value("alias", false);
                fromJson[it.key()] = s;
            }
            L.keys = std::move(fromJson);
            L.fromFile = true;
        } catch (const std::exception& e) {
            LOG_ERROR("ClipMeta", "schema file {} unreadable ({}) — builtin numeric keys only",
                      schemaPath(), e.what());
        }
    });
    return L;
}

bool parseBool(const std::string& v, float& out) {
    const std::string l = lower(v);
    if (l == "1" || l == "true")  { out = 1.0f; return true; }
    if (l == "0" || l == "false") { out = 0.0f; return true; }
    return false;
}

bool parseFloat(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    out = std::strtof(v.c_str(), &end);
    return end && *end == '\0';
}

const std::string* factorOf(const AnimationClip& clip, const char* key) {
    auto it = clip.factors.find(key);
    return it == clip.factors.end() ? nullptr : &it->second;
}

} // namespace

const char* schemaPath() { return "resources/anim/clip_meta_schema.json"; }
const std::map<std::string, KeySpec>& schema() { return loaded().keys; }
bool schemaLoadedFromFile() { return loaded().fromFile; }

Parsed parseFields(const std::string& kvText) {
    Parsed p;
    const auto& keys = schema();
    std::istringstream ss(kvText);
    std::string kv;
    bool sawExplicitHit = false;
    while (ss >> kv) {
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = kv.substr(0, eq);
        const std::string v = kv.substr(eq + 1);
        auto it = keys.find(k);
        if (it == keys.end()) {
            p.issues.push_back({Issue::UnknownKey, k, v, "unknown key '" + k + "'"});
            continue;
        }
        const KeySpec& spec = it->second;
        switch (spec.type) {
        case ValueType::Float: {
            float f = 0.0f;
            if (!parseFloat(v, f)) { p.issues.push_back({Issue::WrongType, k, v, "'" + k + "' expects a number, got '" + v + "'"}); break; }
            if (spec.factor) p.factors[k] = v;
            else if (spec.alias) { if (!sawExplicitHit) p.numbers[spec.runtimeField] = f; }
            else {
                p.numbers[spec.runtimeField.empty() ? k : spec.runtimeField] = f;
                if (k == "hitFrameFraction") sawExplicitHit = true;
            }
            break;
        }
        case ValueType::Bool: {
            float f = 0.0f;
            if (!parseBool(v, f)) { p.issues.push_back({Issue::WrongType, k, v, "'" + k + "' expects 0/1, got '" + v + "'"}); break; }
            if (spec.factor) p.factors[k] = (f != 0.0f) ? "1" : "0";
            else p.numbers[spec.runtimeField.empty() ? k : spec.runtimeField] = f;
            break;
        }
        case ValueType::String:
            if (k == "type") p.type = v;
            else if (spec.factor) p.factors[k] = lower(v);
            else p.strings[k] = v;
            break;
        case ValueType::Enum: {
            const std::string lv = lower(v);
            if (std::find(spec.values.begin(), spec.values.end(), lv) == spec.values.end()) {
                std::string allowed;
                for (const auto& a : spec.values) allowed += (allowed.empty() ? "" : "|") + a;
                p.issues.push_back({Issue::BadEnum, k, v, "'" + k + "=" + v + "' is not one of " + allowed});
                break;
            }
            if (spec.factor) p.factors[k] = lv; else p.strings[k] = lv;
            break;
        }
        }
    }
    return p;
}

void applyToClip(AnimationClip& clip, const Parsed& p) {
    if (!p.type.empty()) clip.clipType = p.type;
    for (const auto& [field, v] : p.numbers) {
        if      (field == "warpEnabled")        clip.warpEnabled        = (v != 0.0f);
        else if (field == "authoredFallDist")   clip.authoredFallDist   = v;
        else if (field == "takeoffEnd")         clip.takeoffEnd         = v;
        else if (field == "contactFrame")       clip.contactFrame       = v;
        else if (field == "warpScaleMin")       clip.warpScaleMin       = v;
        else if (field == "warpScaleMax")       clip.warpScaleMax       = v;
        else if (field == "hitFrameFraction")   clip.hitFrameFraction   = v;
        else if (field == "interruptible")      clip.interruptible      = (v != 0.0f);
        else if (field == "interruptAfter")     clip.interruptAfter     = v;
        else if (field == "stairStepHeight")    clip.stairStepHeight    = v;
        else if (field == "stairStepDepth")     clip.stairStepDepth     = v;
        else if (field == "contactFrame1")      clip.contactFrame1      = v;
        else if (field == "contactFrame2")      clip.contactFrame2      = v;
        else if (field == "footIKSurfaceReach") clip.footIKSurfaceReach = v;
        else if (field == "footIKBodyRange")    clip.footIKBodyRange    = v;
        else if (field == "footIKEnabled")      clip.footIKEnabled      = (v != 0.0f);
        else if (field == "stanceL")            clip.stanceL            = v;
        else if (field == "stanceR")            clip.stanceR            = v;
        // tool-only numerics (footIKEnabled, impactFrameFraction) have no runtime field
    }
    for (const auto& [k, v] : p.factors) clip.factors[k] = v;
}

std::vector<std::string> validateFile(const std::string& animPath) {
    std::vector<std::string> out;
    std::ifstream in(animPath);
    if (!in.is_open()) { out.push_back("cannot open " + animPath); return out; }
    std::string line;
    const std::string prefix = "# clip_meta:";
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line[0] != '#') break;
        if (line.compare(0, prefix.size(), prefix) != 0) continue;
        std::istringstream ss(line.substr(prefix.size()));
        std::string clipName, rest;
        ss >> clipName;
        std::getline(ss, rest);
        for (const auto& issue : parseFields(rest).issues) out.push_back(clipName + ": " + issue.message);
    }
    return out;
}

// ---- factor selection ----------------------------------------------------------------

namespace {
// Compare one declared coordinate against the character's value: absent = any (0 points,
// no veto); equal = +1; different = veto.
bool coordinate(const AnimationClip& clip, const char* key, const std::string& want, int& score) {
    const std::string* have = factorOf(clip, key);
    if (!have) return true;
    if (*have != lower(want)) return false;
    ++score;
    return true;
}
} // namespace

int baseMatchScore(const AnimationClip& clip, const Factors& f) {
    const std::string* role = factorOf(clip, "role");
    if (!role || *role != "base") return -1;
    const std::string* state = factorOf(clip, "state");
    if (!state || f.state.empty() || *state != lower(f.state)) return -1;
    int score = 0;
    if (!coordinate(clip, "gait", f.gait, score)) return -1;
    if (!coordinate(clip, "grip", f.grip, score)) return -1;
    if (!coordinate(clip, "load", f.load, score)) return -1;
    if (!coordinate(clip, "condition", f.condition, score)) return -1;
    if (!coordinate(clip, "mood", f.mood, score)) return -1;
    return score;
}

bool layerMatches(const AnimationClip& clip, const Factors& f) {
    const std::string* role = factorOf(clip, "role");
    if (!role || *role != "layer") return false;
    int score = 0;
    return coordinate(clip, "gait", f.gait, score) && coordinate(clip, "state", f.state, score) &&
           coordinate(clip, "grip", f.grip, score) && coordinate(clip, "load", f.load, score) &&
           coordinate(clip, "condition", f.condition, score) && coordinate(clip, "mood", f.mood, score);
}

Composition selectComposition(const std::vector<AnimationClip>& clips, const Factors& f) {
    Composition c;
    int best = -1;
    for (size_t i = 0; i < clips.size(); ++i) {
        const int s = baseMatchScore(clips[i], f);
        if (s > best) { best = s; c.base = static_cast<int>(i); }   // first wins on ties
    }
    for (size_t i = 0; i < clips.size(); ++i)
        if (layerMatches(clips[i], f)) c.layers.push_back(static_cast<int>(i));
    return c;
}

} // namespace Phyxel::ClipMeta
