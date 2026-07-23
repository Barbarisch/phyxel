#include <gtest/gtest.h>
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/AppearancePresetRegistry.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AppearancePresetRegistry;

// Golden regression for the body-plan refactor (docs/CharacterLibraryPlan.md
// Phase D): pins the ENGINE-POSED character — bone global transforms across
// representative clips, derived controller scalars, the segment-box table, and
// the foot-IK bone identity — against checked-in baselines. Any refactor of
// AnimatedVoxelCharacter must keep these byte/epsilon-identical for humanoids.
//
// Record mode: set PHYXEL_GOLDEN_RECORD=1 and run; baselines are rewritten in
// tests/golden/character_poses/. Record only from a known-good build, inspect
// the diff, and commit the baseline alongside the change that justifies it.
//
// Layers per case:
//   SCALARS  halfHeight halfWidth maxStep footOffset
//   SEGMENT  boneName hx hy hz isArm            (order is contract)
//   CLIP/T/B pose samples: every bone's global position + rotation at fixed
//            normalized times, via seekToClip (the real evaluation path:
//            updateAnimation -> posture lean -> global transforms).
//
// Tests run from the repo root (see CharacterCapsuleScalingTest).

namespace {

constexpr float kPosEps  = 1e-4f;
constexpr float kQuatEps = 1e-5f;   // 1 - |dot| tolerance

const char* kRepClips[] = {
    "idle", "walk", "run",
    "stand_to_sit", "sitting_idle", "sit_to_stand",
    "attack", "death_front",
};
const float kSampleTimes[] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 0.999f };

bool recordMode() {
    const char* v = std::getenv("PHYXEL_GOLDEN_RECORD");
    return v && v[0] == '1';
}

std::unique_ptr<AnimatedVoxelCharacter> makeCharacter(const std::string& animFile,
                                                      const char* presetId) {
    auto ch = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0.0f));
    if (presetId) {
        auto& presets = AppearancePresetRegistry::instance();
        presets.ensureLoaded();
        const auto* preset = presets.getPreset(presetId);
        EXPECT_NE(preset, nullptr) << presetId;
        if (preset) ch->setAppearance(*preset);
    }
    if (!ch->loadModel(animFile)) return nullptr;
    return ch;
}

int findClipIndex(const AnimatedVoxelCharacter& ch, const std::string& name) {
    const auto& clips = ch.getAnimationClips();
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i].name == name) return (int)i;
    return -1;
}

// Strip scale from the global transform's 3x3 and return a normalized quat.
glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int c = 0; c < 3; ++c) {
        float len = glm::length(r[c]);
        if (len > 1e-8f) r[c] /= len;
    }
    glm::quat q = glm::normalize(glm::quat_cast(r));
    if (q.w < 0.0f) q = -q;   // canonical hemisphere so sign flips don't fail
    return q;
}

// ---------------------------------------------------------------------------
// Baseline generation + text serialization
// ---------------------------------------------------------------------------

struct GoldenDump {
    std::vector<std::string> lines;

    void add(const std::string& l) { lines.push_back(l); }

    static std::string fmt(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        return buf;
    }
};

GoldenDump captureGolden(AnimatedVoxelCharacter& ch) {
    GoldenDump d;

    {
        std::ostringstream s;
        s << "SCALARS " << GoldenDump::fmt(ch.getControllerHalfHeight())
          << " " << GoldenDump::fmt(ch.getControllerHalfWidth())
          << " " << GoldenDump::fmt(ch.getMaxStepHeight());
        d.add(s.str());
    }

    auto segs = ch.getSegmentBoxInfo();
    {
        std::ostringstream s;
        s << "SEGMENTS " << segs.size();
        d.add(s.str());
    }
    for (const auto& seg : segs) {
        std::ostringstream s;
        s << "SEGMENT " << seg.boneName
          << " " << GoldenDump::fmt(seg.halfExtents.x)
          << " " << GoldenDump::fmt(seg.halfExtents.y)
          << " " << GoldenDump::fmt(seg.halfExtents.z)
          << " " << (seg.isArm ? 1 : 0);
        d.add(s.str());
    }

    for (const char* clipName : kRepClips) {
        int idx = findClipIndex(ch, clipName);
        if (idx < 0) continue;   // baseline simply omits absent clips
        d.add(std::string("CLIP ") + clipName);
        for (float t : kSampleTimes) {
            ch.seekToClip(idx, t);
            {
                std::ostringstream s;
                s << "T " << GoldenDump::fmt(t);
                d.add(s.str());
            }
            const auto& skel = ch.getSkeleton();
            for (const auto& bone : skel.bones) {
                glm::vec3 p(bone.globalTransform[3]);
                glm::quat q = rotationOf(bone.globalTransform);
                std::ostringstream s;
                s << "B " << bone.name
                  << " " << GoldenDump::fmt(p.x) << " " << GoldenDump::fmt(p.y)
                  << " " << GoldenDump::fmt(p.z)
                  << " " << GoldenDump::fmt(q.w) << " " << GoldenDump::fmt(q.x)
                  << " " << GoldenDump::fmt(q.y) << " " << GoldenDump::fmt(q.z);
                d.add(s.str());
            }
        }
    }
    return d;
}

// ---------------------------------------------------------------------------
// Comparison: parse both dumps line-aligned; numeric fields within epsilon.
// Structural lines (SEGMENTS count, SEGMENT names/order, CLIP/T markers,
// bone names) must match exactly — a changed segment order or a vanished
// clip IS the regression, not noise.
// ---------------------------------------------------------------------------

void compareGolden(const GoldenDump& fresh, const std::vector<std::string>& baseline,
                   const std::string& label) {
    ASSERT_EQ(fresh.lines.size(), baseline.size())
        << label << ": line count changed (structure drift — segments/clips/bones "
        << "added or removed). Re-record ONLY if the change is intended.";

    for (size_t i = 0; i < baseline.size(); ++i) {
        std::istringstream a(baseline[i]), b(fresh.lines[i]);
        std::string tagA, tagB;
        a >> tagA; b >> tagB;
        ASSERT_EQ(tagA, tagB) << label << " line " << i + 1;

        auto compareNumeric = [&](int nameFields, int floatCount, float eps,
                                  bool isQuatTail) {
            for (int n = 0; n < nameFields; ++n) {
                std::string na, nb;
                a >> na; b >> nb;
                ASSERT_EQ(na, nb) << label << " line " << i + 1 << " (" << tagA << ")";
            }
            if (isQuatTail) {
                float pa[3], pb[3], qa[4], qb[4];
                for (float& v : pa) a >> v;
                for (float& v : qa) a >> v;
                for (float& v : pb) b >> v;
                for (float& v : qb) b >> v;
                for (int n = 0; n < 3; ++n)
                    ASSERT_NEAR(pa[n], pb[n], eps)
                        << label << " line " << i + 1 << " " << baseline[i];
                float dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
                ASSERT_GT(std::abs(dot), 1.0f - kQuatEps)
                    << label << " line " << i + 1 << " rotation drift: " << baseline[i];
            } else {
                for (int n = 0; n < floatCount; ++n) {
                    float va, vb;
                    a >> va; b >> vb;
                    ASSERT_NEAR(va, vb, eps)
                        << label << " line " << i + 1 << " " << baseline[i];
                }
            }
            // Trailing exact fields (e.g. isArm) — compare as strings.
            std::string ra, rb;
            while (a >> ra) {
                b >> rb;
                ASSERT_EQ(ra, rb) << label << " line " << i + 1;
            }
        };

        if (tagA == "B")            compareNumeric(1, 0, kPosEps, true);
        else if (tagA == "SEGMENT") compareNumeric(1, 3, kPosEps, false);
        else if (tagA == "SCALARS") compareNumeric(0, 3, kPosEps, false);
        else                        ASSERT_EQ(baseline[i], fresh.lines[i])
                                        << label << " line " << i + 1;
    }
}

void runGoldenCase(const std::string& animFile, const char* presetId,
                   const std::string& baselinePath, const std::string& label) {
    auto ch = makeCharacter(animFile, presetId);
    ASSERT_NE(ch, nullptr) << "failed to load " << animFile;

    GoldenDump fresh = captureGolden(*ch);
    ASSERT_GT(fresh.lines.size(), 100u) << label << ": suspiciously small capture";

    if (recordMode()) {
        std::ofstream out(baselinePath, std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "cannot write " << baselinePath;
        out << "# golden pose baseline — " << label << "\n";
        for (const auto& l : fresh.lines) out << l << "\n";
        GTEST_SKIP() << "recorded " << fresh.lines.size() << " lines to " << baselinePath;
    }

    std::ifstream in(baselinePath);
    ASSERT_TRUE(in.is_open())
        << "missing baseline " << baselinePath
        << " — record once with PHYXEL_GOLDEN_RECORD=1";
    std::vector<std::string> baseline;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        baseline.push_back(line);
    }
    compareGolden(fresh, baseline, label);
}

} // namespace

// ---------------------------------------------------------------------------

TEST(CharacterGoldenPose, HumanoidDefault) {
    runGoldenCase("resources/animated_characters/humanoid.anim", nullptr,
                  "tests/golden/character_poses/humanoid.golden.txt",
                  "humanoid/default");
}

TEST(CharacterGoldenPose, HumanoidHalflingPreset) {
    // Covers the proportioned path (applySkeletonProportions + root-bind
    // scaling + capsule resize) — exactly the code the body-plan refactor
    // walks through.
    runGoldenCase("resources/animated_characters/humanoid.anim", "halfling",
                  "tests/golden/character_poses/humanoid_halfling.golden.txt",
                  "humanoid/halfling");
}

TEST(CharacterGoldenPose, OgreVariantRig) {
    runGoldenCase("resources/animated_characters/ogre.anim", "ogre",
                  "tests/golden/character_poses/ogre.golden.txt",
                  "ogre/ogre-preset");
}

// ---------------------------------------------------------------------------
// Foot-IK identity: the legacy literals live HERE, in the test. After the
// body-plan refactor, plan-based resolution must produce the exact same ids.
// ---------------------------------------------------------------------------

TEST(CharacterGoldenPose, FootIKBoneIdentityHumanoid) {
    auto ch = makeCharacter("resources/animated_characters/humanoid.anim", nullptr);
    ASSERT_NE(ch, nullptr);

    const auto& boneMap = ch->getSkeleton().boneMap;
    auto legacyFind = [&](const char* name) -> int {
        auto it = boneMap.find(name);
        return it != boneMap.end() ? it->second : -1;
    };

    auto info = ch->resolveFootIKForTest();
    EXPECT_EQ(info.leftUpLeg,  legacyFind("mixamorig:LeftUpLeg"));
    EXPECT_EQ(info.leftLeg,    legacyFind("mixamorig:LeftLeg"));
    EXPECT_EQ(info.leftFoot,   legacyFind("mixamorig:LeftFoot"));
    EXPECT_EQ(info.rightUpLeg, legacyFind("mixamorig:RightUpLeg"));
    EXPECT_EQ(info.rightLeg,   legacyFind("mixamorig:RightLeg"));
    EXPECT_EQ(info.rightFoot,  legacyFind("mixamorig:RightFoot"));
    EXPECT_EQ(info.hipBoneId,  legacyFind("mixamorig:Hips"));
    EXPECT_TRUE(info.cacheReady);

    // All six must actually exist on the humanoid rig, and the root must be
    // bone 0 (the sit-anchor and clip-trajectory paths assume it; the
    // body-plan refactor replaces the literal 0 with a resolved id that must
    // equal it).
    EXPECT_GE(info.leftUpLeg, 0);
    EXPECT_GE(info.rightFoot, 0);
    EXPECT_EQ(info.hipBoneId, 0);
}

TEST(CharacterGoldenPose, FootIKBoneIdentityOgre) {
    // The variant rig shares the Mixamo topology — identical resolution.
    auto ch = makeCharacter("resources/animated_characters/ogre.anim", nullptr);
    ASSERT_NE(ch, nullptr);
    auto info = ch->resolveFootIKForTest();
    EXPECT_TRUE(info.cacheReady);
    EXPECT_EQ(info.hipBoneId, 0);
    EXPECT_GE(info.leftFoot, 0);
    EXPECT_GE(info.rightFoot, 0);
}
