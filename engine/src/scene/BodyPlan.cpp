#include "scene/BodyPlan.h"
#include "utils/Logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Phyxel {
namespace Scene {

namespace {

MorphologyType morphologyFromName(const std::string& name) {
    if (name == "humanoid")  return MorphologyType::Humanoid;
    if (name == "quadruped") return MorphologyType::Quadruped;
    if (name == "arachnid")  return MorphologyType::Arachnid;
    if (name == "dragon")    return MorphologyType::Dragon;
    return MorphologyType::Unknown;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

int findExact(const Skeleton& skeleton, const std::string& name) {
    auto it = skeleton.boneMap.find(name);
    return it != skeleton.boneMap.end() ? it->second : -1;
}

} // namespace

// ---------------------------------------------------------------------------
// BodyPlan
// ---------------------------------------------------------------------------

BodyPlan::Resolved BodyPlan::resolveAgainst(const Skeleton& skeleton) const {
    Resolved r;

    r.rootBoneId = findExact(skeleton, rootBone);
    if (r.rootBoneId < 0) {
        // Same fallback the legacy m_ikHipBoneId path used: first bone whose
        // lowercase name contains an alias substring.
        for (const auto& alias : hipAliases) {
            for (const auto& [name, id] : skeleton.boneMap) {
                if (toLower(name).find(alias) != std::string::npos) {
                    r.rootBoneId = id;
                    break;
                }
            }
            if (r.rootBoneId >= 0) break;
        }
    }

    for (const auto& leg : legs) {
        Resolved::Leg rl;
        rl.upperId = findExact(skeleton, leg.upper);
        rl.midId   = findExact(skeleton, leg.mid);
        rl.footId  = findExact(skeleton, leg.foot);
        rl.footIK  = leg.footIK;
        r.legs.push_back(rl);
    }

    for (const auto& seg : segments) {
        int id = findExact(skeleton, seg.bone);
        if (id < 0) {
            LOG_WARN_FMT("BodyPlan", "plan '" << this->id
                         << "': segment bone not in skeleton: " << seg.bone);
            continue;
        }
        r.segments.emplace_back(id, seg.isArm);
    }
    return r;
}

BodyPlan BodyPlan::fromJson(const nlohmann::json& j) {
    BodyPlan p;
    p.id         = j.value("id", "");
    p.morphology = morphologyFromName(j.value("morphology", "humanoid"));
    p.gaitClass  = j.value("gaitClass", "");
    p.rootBone   = j.value("rootBone", "");
    p.gripBone   = j.value("gripBone", "");
    p.headBone   = j.value("headBone", "");

    if (j.contains("hipAliases"))
        for (const auto& a : j["hipAliases"])
            p.hipAliases.push_back(a.get<std::string>());

    if (j.contains("legs")) {
        for (const auto& l : j["legs"]) {
            LegChain leg;
            leg.id     = l.value("id", "");
            leg.upper  = l.value("upper", "");
            leg.mid    = l.value("mid", "");
            leg.foot   = l.value("foot", "");
            leg.footIK = l.value("footIK", false);
            p.legs.push_back(leg);
        }
    }

    if (j.contains("segments")) {
        for (const auto& s : j["segments"]) {
            SegmentDef seg;
            seg.bone  = s.value("bone", "");
            seg.isArm = s.value("isArm", false);
            p.segments.push_back(seg);
        }
    }

    if (j.contains("clipDefaults"))
        for (auto it = j["clipDefaults"].begin(); it != j["clipDefaults"].end(); ++it) {
            if (it.value().is_string()) {
                p.clipDefaults[it.key()] = it.value().get<std::string>();
            } else if (it.value().is_object()) {          // {"clip": ..., "sprint": ...}
                p.clipDefaults[it.key()] = it.value().value("clip", std::string());
                const std::string sprint = it.value().value("sprint", std::string());
                if (!sprint.empty()) p.clipSprint[it.key()] = sprint;
            }
        }
    if (j.contains("clipFallbacks"))
        for (auto it = j["clipFallbacks"].begin(); it != j["clipFallbacks"].end(); ++it)
            p.clipFallbacks[it.key()] = it.value().get<std::string>();

    if (j.contains("spineChain"))
        for (const auto& b : j["spineChain"]) p.spineChain.push_back(b.get<std::string>());

    if (j.contains("masks"))
        for (auto it = j["masks"].begin(); it != j["masks"].end(); ++it) {
            std::vector<std::string> roots;
            for (const auto& r : it.value()) roots.push_back(r.get<std::string>());
            p.masks[it.key()] = roots;
        }

    if (j.contains("transitions")) {
        const auto& t = j["transitions"];
        // Clamp at the boundary: a negative or NaN default would break the blend advance
        // (blendFactor += dt / blend). 0 is a legitimate hard cut.
        p.defaultBlend = std::max(0.0f, t.value("defaultBlend", 0.2f));
        if (t.contains("edges"))
            for (const auto& e : t["edges"]) {
                TransitionEdge edge;
                edge.from      = e.value("from", std::string("*"));
                edge.to        = e.value("to", std::string());
                edge.blend     = e.contains("blend") ? std::max(0.0f, e.value("blend", 0.0f)) : -1.0f;
                edge.phaseSync = e.value("phaseSync", false);
                if (!edge.to.empty()) p.transitions.push_back(edge);
            }
    }

    if (j.contains("capsule")) {
        const auto& c = j["capsule"];
        p.capsule.mode = c.value("mode", std::string("legacy")) == "xz_extent"
                             ? Capsule::Mode::XZExtent
                             : Capsule::Mode::Legacy;
        p.capsule.minHalfWidth = c.value("minHalfWidth", 0.12f);
        p.capsule.maxHalfWidth = c.value("maxHalfWidth", 0.60f);
    }
    return p;
}

const BodyPlan::TransitionEdge* BodyPlan::findTransition(const std::string& from,
                                                         const std::string& to) const {
    const TransitionEdge* wildcard = nullptr;
    for (const auto& e : transitions) {
        if (e.to != to) continue;
        if (e.from == from) return &e;          // exact beats wildcard
        if (e.from == "*" && !wildcard) wildcard = &e;
    }
    return wildcard;
}

BodyPlan BodyPlan::builtinHumanoid() {
    // Field-for-field mirror of resources/body_plans/humanoid.json. The bone
    // strings and segment ORDER are the legacy hardcodes from
    // resolveFootBoneIds / buildSegmentBoxes — do not reorder or rename;
    // CharacterGoldenPoseTest pins the resulting ids and box table.
    BodyPlan p;
    p.id         = "humanoid";
    p.morphology = MorphologyType::Humanoid;
    p.gaitClass  = "biped_fsm";
    p.rootBone   = "mixamorig:Hips";
    p.hipAliases = { "hip" };
    p.gripBone   = "RightHand";
    p.headBone   = "mixamorig:Head";

    p.legs = {
        { "left",  "mixamorig:LeftUpLeg",  "mixamorig:LeftLeg",  "mixamorig:LeftFoot",  true },
        { "right", "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", true },
    };

    p.segments = {
        { "mixamorig:Head",         false },
        { "mixamorig:Spine2",       false },
        { "mixamorig:Spine1",       false },
        { "mixamorig:Hips",         false },
        { "mixamorig:LeftArm",      true  },
        { "mixamorig:RightArm",     true  },
        { "mixamorig:LeftForeArm",  true  },
        { "mixamorig:RightForeArm", true  },
        { "mixamorig:LeftUpLeg",    false },
        { "mixamorig:RightUpLeg",   false },
        { "mixamorig:LeftLeg",      false },
        { "mixamorig:RightLeg",     false },
    };

    // A3 item 2: the humanoid clip table, transcribed from the pre-refactor FSM switch
    // (ClipSelectionTest::kLegacyTable is the independent transcription that pins it).
    p.clipDefaults = {
        {"Idle", "idle"},                    {"StartWalk", "start_walking"},
        {"Walk", "walk"},                    {"Run", "run"},
        {"Jump", "jump"},                    {"Fall", "jump_down"},
        {"Land", "landing"},                 {"Crouch", "standing_to_crouched"},
        {"CrouchIdle", "crouch_idle"},       {"CrouchWalk", "crouched_walking"},
        {"StandUp", "crouch_to_stand"},      {"KnockedOut", "ko_lay"},
        {"GetUp", "get_up"},                 {"TurnLeft", "left_turn"},
        {"TurnRight", "right_turn"},         {"StrafeLeft", "left_strafe_walk"},
        {"StrafeRight", "right_strafe_walk"},{"WalkStrafeLeft", "left_strafe_walk"},
        {"WalkStrafeRight", "right_strafe_walk"}, {"BackwardWalk", "walking_backward"},
        {"StopWalk", "female_stop_walking"}, {"StopRun", "run_to_stop"},
        {"ClimbStairs", "stair_up"},         {"DescendStairs", "stair_down"},
        {"SitDown", "stand_to_sit"},         {"SittingIdle", "sitting_idle"},
        {"SitStandUp", "sit_to_stand"},      {"Preview", ""},
    };
    p.clipSprint = {
        {"Run", "fast_run"},
        {"StrafeLeft", "left_strafe"},       {"StrafeRight", "right_strafe"},
        {"WalkStrafeLeft", "left_strafe"},   {"WalkStrafeRight", "right_strafe"},
    };
    p.clipFallbacks = {
        {"Block", "body_block"}, {"Dodge", "roll_forward"}, {"HitReact", "idle"},
        {"Death", "idle"},       {"Celebrate", "taunt"},    {"Cast", "idle"},
    };
    p.spineChain = { "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2" };   // A3 lean chain

    // A3 transition graph (mirror of humanoid.json "transitions").
    p.defaultBlend = 0.2f;
    p.transitions = {
        {"Walk", "Run",      0.25f, true},  {"Run",  "Walk",     0.25f, true},
        {"Idle", "Walk",     0.15f, false}, {"Walk", "Idle",     0.20f, false},
        {"*",    "HitReact", 0.08f, false}, {"*",    "Attack",   0.10f, false},
        {"*",    "Dodge",    0.08f, false}, {"*",    "Death",    0.15f, false},
        {"*",    "Jump",     0.10f, false},
    };

    p.capsule.mode = Capsule::Mode::Legacy;
    p.capsule.minHalfWidth = 0.12f;
    p.capsule.maxHalfWidth = 0.60f;
    return p;
}

// ---------------------------------------------------------------------------
// BodyPlanRegistry
// ---------------------------------------------------------------------------

BodyPlanRegistry& BodyPlanRegistry::instance() {
    static BodyPlanRegistry inst;
    return inst;
}

bool BodyPlanRegistry::registerPlan(const BodyPlan& plan) {
    if (plan.id.empty()) return false;
    m_plans[plan.id] = plan;
    // First plan registered for a morphology wins (one active plan per
    // morphology for now; per-rig selection is a later phase).
    if (!m_byMorphology.count(plan.morphology))
        m_byMorphology[plan.morphology] = plan.id;
    return true;
}

int BodyPlanRegistry::loadFromDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    int loaded = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;

    // Sorted for deterministic first-plan-per-morphology registration.
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        std::ifstream in(file);
        if (!in.is_open()) continue;
        try {
            nlohmann::json j;
            in >> j;
            BodyPlan p = BodyPlan::fromJson(j);
            if (registerPlan(p)) {
                ++loaded;
            } else {
                LOG_WARN_FMT("BodyPlan", "skipping plan with no id: " << file.string());
            }
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("BodyPlan", "failed to parse " << file.string()
                          << ": " << e.what());
        }
    }
    return loaded;
}

void BodyPlanRegistry::ensureLoaded() {
    if (!m_loadAttempted) {
        m_loadAttempted = true;
        int n = loadFromDirectory("resources/body_plans");
        if (n > 0) LOG_INFO_FMT("BodyPlan", "loaded " << n << " body plans");
    }
    // The humanoid plan must always exist, JSON or not.
    if (!m_plans.count("humanoid")) registerPlan(BodyPlan::builtinHumanoid());
}

const BodyPlan& BodyPlanRegistry::planFor(MorphologyType m) const {
    auto it = m_byMorphology.find(m);
    if (it != m_byMorphology.end()) {
        auto pit = m_plans.find(it->second);
        if (pit != m_plans.end()) return pit->second;
    }
    auto hit = m_plans.find("humanoid");
    if (hit != m_plans.end()) return hit->second;
    // ensureLoaded guarantees humanoid; static fallback for misuse before it.
    static const BodyPlan sBuiltin = BodyPlan::builtinHumanoid();
    return sBuiltin;
}

const BodyPlan& BodyPlanRegistry::planForSkeleton(MorphologyType m,
                                                  const Skeleton& skeleton) const {
    const BodyPlan* best = nullptr;
    int bestScore = -1;
    for (const auto& [id, plan] : m_plans) {
        if (plan.morphology != m) continue;
        BodyPlan::Resolved r = plan.resolveAgainst(skeleton);
        int score = (r.rootBoneId >= 0 ? 1 : 0);
        for (const auto& leg : r.legs)
            score += (leg.upperId >= 0) + (leg.midId >= 0) + (leg.footId >= 0);
        score += (int)r.segments.size();
        if (score > bestScore) {
            bestScore = score;
            best = &plan;
        }
    }
    // A plan that resolves almost nothing is not a match — fall back to the
    // morphology default (which itself falls back to humanoid).
    if (best && bestScore >= 3) return *best;
    return planFor(m);
}

const BodyPlan* BodyPlanRegistry::planById(const std::string& id) const {
    auto it = m_plans.find(id);
    return it != m_plans.end() ? &it->second : nullptr;
}

std::vector<std::string> BodyPlanRegistry::getAllPlanIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, _] : m_plans) ids.push_back(id);
    return ids;
}

void BodyPlanRegistry::clear() {
    m_plans.clear();
    m_byMorphology.clear();
    m_loadAttempted = false;
}

} // namespace Scene
} // namespace Phyxel
