#include <gtest/gtest.h>

#include "graphics/AnimationSystem.h"
#include "scene/BodyPlan.h"
#include "scene/motion/MotionOracle.h"
#include "scene/motion/MotionOracleSampling.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Roadmap R2 per-import gate "oracle on idle/walk" (docs/CharacterAnimationRoadmap.md).
// Every manifest in resources/characters/ names an imported rig (resources/animated_characters/
// <id>.anim) and the skeleton it was bound to (humanoid, or a creature-forge species rig). The
// import re-uses that skeleton's clips, so it is judged RELATIVE to it: on idle and walk, each
// MotionOracle metric divided by body height may not exceed the source rig's own value plus a
// small band. Feet + knee chains come from the rig's best body plan (the suffix rule in
// findFeet only knows humanoid ToeBase/Foot names). Ground = the lowest box bottom at bind; a
// foot's standing height = its bind height above that ground (footClearanceRef).
// Self-overlap is not judged offline (sampleClip yields joints, not segment boxes).
// ============================================================================

using namespace Phyxel;
using namespace Phyxel::Scene;
using namespace Phyxel::Scene::Motion;

namespace {

struct Rig {
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
    VoxelModel model;
    bool ok = false;
    const AnimationClip* clip(const std::string& n) const {
        for (const auto& c : clips) if (c.name == n) return &c;
        return nullptr;
    }
};

Rig load(const std::string& path) {
    Rig r;
    AnimationSystem sys;
    r.ok = sys.loadFromFile(path, r.skeleton, r.clips, r.model) && !r.skeleton.bones.empty();
    return r;
}

std::vector<glm::mat4> bindGlobals(const Skeleton& sk) {
    std::vector<glm::mat4> g(sk.bones.size());
    for (size_t i = 0; i < sk.bones.size(); ++i) {
        const auto& b = sk.bones[i];
        glm::mat4 local = glm::translate(glm::mat4(1.0f), b.localPosition) * glm::mat4_cast(b.localRotation) *
                          glm::scale(glm::mat4(1.0f), b.localScale);
        g[i] = b.parentId >= 0 ? g[b.parentId] * local : local;
    }
    return g;
}

struct Bounds { float lo = 1e9f, hi = -1e9f; };

Bounds bindBoxBounds(const Rig& r) {
    const auto g = bindGlobals(r.skeleton);
    Bounds b;
    for (const auto& s : r.model.shapes) {
        if (s.boneId < 0 || s.boneId >= static_cast<int>(g.size())) continue;
        const glm::vec3 c = glm::vec3(g[s.boneId] * glm::vec4(s.offset, 1.0f));
        b.lo = std::min(b.lo, c.y - 0.5f * s.size.y);
        b.hi = std::max(b.hi, c.y + 0.5f * s.size.y);
    }
    return b;
}

struct Judged {
    bool ok = false;
    float height = 0.0f;
    MotionOracleMetrics m;
};

BodyPlan::Resolved bestPlan(const Skeleton& sk) {
    auto& reg = BodyPlanRegistry::instance();
    reg.ensureLoaded();
    BodyPlan::Resolved best;
    int bestScore = -1;
    for (const auto& id : reg.getAllPlanIds()) {
        const BodyPlan* p = reg.planById(id);
        if (!p) continue;
        auto r = p->resolveAgainst(sk);
        int legs = 0;
        for (const auto& l : r.legs) if (l.upperId >= 0 && l.midId >= 0 && l.footId >= 0) ++legs;
        const int score = (r.rootBoneId >= 0 ? 100 : 0) + legs * 10 + static_cast<int>(r.segments.size());
        if (score > bestScore) { bestScore = score; best = r; }
    }
    return best;
}

// plantedWindow: MotionOracle's default is an absolute 3 cm. Comparing a rig scaled by k with its
// source needs the window scaled by k too, or the two clips are planted by different rules (the
// black bear at k = 0.69 read a 0.72 walk-speed mismatch from that alone, 2026-10-02).
Judged judge(const Rig& r, const std::string& clipName, float plantedWindow = 0.03f) {
    Judged j;
    const AnimationClip* c = r.clip(clipName);
    if (!c) return j;
    const Bounds b = bindBoxBounds(r);
    j.height = b.hi - b.lo;
    const auto plan = bestPlan(r.skeleton);
    const auto g = bindGlobals(r.skeleton);
    OracleOptions opt;
    opt.plantedWindow = plantedWindow;
    float footRef = 1e9f;
    for (const auto& l : plan.legs) {
        if (l.footId < 0) continue;
        opt.footJoints.push_back(static_cast<size_t>(l.footId));
        footRef = std::min(footRef, g[l.footId][3].y - b.lo);
        if (l.upperId >= 0 && l.midId >= 0)
            opt.kneeChains.push_back({static_cast<size_t>(l.upperId), static_cast<size_t>(l.midId),
                                      static_cast<size_t>(l.footId)});
    }
    if (opt.footJoints.empty()) return j;
    const float ground = b.lo;
    opt.ground = [ground](float, float) { return ground; };
    opt.footClearanceRef = std::max(0.0f, footRef);
    opt.authoredSpeed = clipName == "walk" ? c->speed : 0.0f;
    SampledClip s = sampleClip(r.skeleton, *c, 60.0f);
    j.m = evaluateMotion(s.frames, s.secondsPerFrame, s.chainEdges, opt);
    j.ok = j.m.valid;
    return j;
}

std::string sourceRigFor(const nlohmann::json& m) {
    const std::string body = m.value("body", "");
    if (body == "humanoid") return "resources/animated_characters/humanoid.anim";
    std::ifstream in("tools/creature_forge/bestiary.json");
    nlohmann::json b; in >> b;
    for (const auto& e : b)
        if (e.is_object() && e.value("spec", "") == "specs/" + body + ".json") return e.value("out", "");
    return "";
}

// The metrics, in one order, so bands can be calibrated per metric.
constexpr int kMetrics = 5;
// maxKneeInversion is a DISTANCE (knee behind the hip-ankle midpoint along forward, MotionOracle.cpp),
// so it is divided by height like the others — comparing it raw flagged every scaled-up import
// (owlbear 0.2758 = bear 0.1726 x leg scale 1.598, 2026-10-02).
const char* kMetricNames[kMetrics] = {"penetration/H", "stanceFloat/H", "stanceResidual/H", "kneeInversion/H",
                                      "speedMismatch"};
const float kFixedBand[kMetrics] = {0.01f, 0.01f, 0.02f, 0.02f, 0.10f};

std::array<float, kMetrics> values(const Judged& j) {
    return {j.m.maxPenetration / j.height, j.m.maxStanceFloat / j.height, j.m.stanceResidual / j.height,
            j.m.maxKneeInversion / j.height, std::max(0.0f, j.m.speedMismatch)};
}

// Humanoid band = the worst deviation a SHIPPED humanoid variant (same clips, other proportions,
// tools/anim_pipeline/derive_humanoid_variant.py) already shows against humanoid.anim. An import
// may move as differently from the source as the variants we ship do — no more.
std::map<std::string, std::array<float, kMetrics>> humanoidBands(std::ostringstream& report) {
    const char* variants[] = {"ogre", "goblin", "troll", "orc_warrior", "zombie", "skeleton_warrior",
                              "hag", "golem", "devil", "angel", "elemental"};
    const Rig base = load("resources/animated_characters/humanoid.anim");
    std::map<std::string, std::array<float, kMetrics>> band;
    for (const char* clipName : {"idle", "walk"}) {
        std::array<float, kMetrics> b{};
        for (int k = 0; k < kMetrics; ++k) b[k] = kFixedBand[k];
        const Judged jb = judge(base, clipName);
        for (const char* v : variants) {
            const Rig r = load(std::string("resources/animated_characters/") + v + ".anim");
            if (!r.ok) { report << "variant " << v << ": did not load\n"; continue; }
            const Judged jv = judge(r, clipName);
            if (!jv.ok || !jb.ok) { report << "variant " << v << "/" << clipName << ": not evaluable\n"; continue; }
            const auto a = values(jv), c = values(jb);
            report << "variant " << v << "/" << clipName;
            for (int k = 0; k < kMetrics; ++k) report << " " << kMetricNames[k] << "=" << a[k];
            report << "\n";
            for (int k = 0; k < kMetrics; ++k) b[k] = std::max(b[k], a[k] - c[k]);
        }
        band[clipName] = b;
        for (int k = 0; k < kMetrics; ++k)
            report << "band humanoid/" << clipName << " " << kMetricNames[k] << " " << b[k] << "\n";
    }
    return band;
}

} // namespace

TEST(ImportedRigOracle, EveryImportMovesNoWorseThanTheSkeletonItWasBoundTo) {
    namespace fs = std::filesystem;
    ASSERT_TRUE(fs::is_directory("resources/characters")) << "run from the repo root";
    int judged = 0;
    std::ostringstream report, fails;
    const auto hBand = humanoidBands(report);
    // per-import verdicts -> build/coverage/import_oracle.json (read by tools/character_add.py
    // --oracle-verdicts to refuse a written import that fails here, with this reason)
    nlohmann::json verdicts = nlohmann::json::object();
    for (const auto& entry : fs::directory_iterator("resources/characters")) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        nlohmann::json m; in >> m;
        const std::string id = m.value("id", "");
        if (m.value("status", "") == "refused") continue;   // already refused: not a live rig
        nlohmann::json& v = verdicts[id];
        v["pass"] = true;
        v["reasons"] = nlohmann::json::array();
        const Rig imp = load("resources/animated_characters/" + id + ".anim");
        const Rig src = load(sourceRigFor(m));
        ASSERT_TRUE(imp.ok) << id << ": imported rig did not load";
        ASSERT_TRUE(src.ok) << id << ": source rig did not load (" << sourceRigFor(m) << ")";
        for (const char* clipName : {"idle", "walk"}) {
            const Judged b = judge(src, clipName);
            const float k = b.ok && b.height > 0 ? bindBoxBounds(imp).hi - bindBoxBounds(imp).lo : 0.0f;
            const Judged a = judge(imp, clipName, b.ok && b.height > 0 ? 0.03f * k / b.height : 0.03f);
            if (!a.ok || !b.ok) {
                fails << id << "/" << clipName << ": not evaluable (feet or clip missing)\n";
                v["pass"] = false;
                v["reasons"].push_back(std::string(clipName) + ": not evaluable (feet or clip missing)");
                continue;
            }
            ++judged;
            const auto vi = values(a), vs = values(b);
            const bool humanoid = m.value("body", "") == "humanoid";
            for (int k = 0; k < kMetrics; ++k) {
                float band = humanoid ? hBand.at(clipName)[k] : kFixedBand[k];
                // skate is judged RELATIVE to the source's own: the forge walks already skate
                // ~1 body-height/s by this metric, so a fixed 0.02 flags any proportion change.
                // 10 % is a chosen tolerance, not a calibrated one (roadmap R2 ledger).
                if (k == 2) band = std::max(band, 0.10f * vs[k]);
                report << id << "/" << clipName << " " << kMetricNames[k] << " import " << vi[k] << " source " << vs[k]
                       << " band " << band << "\n";
                if (vi[k] > vs[k] + band) {
                    std::ostringstream r;
                    r << clipName << " " << kMetricNames[k] << ": import " << vi[k] << " > source " << vs[k] << " + " << band;
                    fails << id << "/" << r.str() << "\n";
                    v["pass"] = false;
                    v["reasons"].push_back(r.str());
                }
            }
        }
    }
    std::printf("[ImportedRigOracle] %d clip pairs judged\n%s", judged, report.str().c_str());
    fs::create_directories("build/coverage");
    std::ofstream("build/coverage/import_oracle.json") << verdicts.dump(1) << "\n";
    EXPECT_GT(judged, 0) << "no manifests found";
    EXPECT_TRUE(fails.str().empty()) << fails.str();
}
