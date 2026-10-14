#include "scene/SeatFit.h"
#include "scene/AnimatedVoxelCharacter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Phyxel::Scene::SeatFit {

nlohmann::json CharacterFitMetrics::toJson() const {
    return {{"total_height", total_height}, {"hip_height", hip_height}, {"eye_height", eye_height},
            {"leg_length", leg_length}, {"arm_reach", arm_reach}, {"shoulder_width", shoulder_width},
            {"hip_width", hip_width}, {"body_depth", body_depth}, {"sitting_height", sitting_height}};
}

nlohmann::json FitIssue::toJson() const {
    return {{"rule_id", ruleId}, {"message", message}, {"measured", measured},
            {"required", required}, {"severity", error ? "error" : "warn"}};
}

SeatFeatures SeatFeatures::fromJson(const nlohmann::json& f) {
    SeatFeatures s;
    if (!f.is_object()) return s;
    s.seat_top_y       = f.value("seat_top_y", 0.0f);
    s.seat_width_x     = f.value("seat_width_x", 0.0f);
    s.seat_depth_z     = f.value("seat_depth_z", 0.0f);
    s.front_edge_z     = f.value("front_edge_z", 0.0f);
    s.backrest_height  = f.value("backrest_height", 0.0f);
    s.backrest_present = f.value("backrest_present", s.backrest_height > 1e-3f);
    s.backrest_angle_deg = f.value("backrest_angle_deg", 0.0f);
    if (f.contains("armrests") && f["armrests"].is_array())
        for (const auto& a : f["armrests"]) {
            Armrest r;
            r.top_y = a.value("top_y", 0.0f); r.inner_x = a.value("inner_x", 0.0f);
            r.z_min = a.value("z_min", 0.0f); r.z_max = a.value("z_max", 0.0f);
            s.armrests.push_back(r);
        }
    if (f.contains("approach") && f["approach"].is_array() && f["approach"].size() == 3) {
        s.has_approach = true;
        s.approach = glm::vec3(f["approach"][0].get<float>(), f["approach"][1].get<float>(), f["approach"][2].get<float>());
    }
    return s;
}

namespace {
std::string fmt(const char* prefix, float a, const char* mid, float b, const char* suffix = "") {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3) << prefix << a << mid << b << suffix;
    return os.str();
}
} // namespace

std::vector<FitIssue> evaluate(const CharacterFitMetrics& c, const SeatFeatures& seat) {
    std::vector<FitIssue> issues;
    auto push = [&](const char* rule, std::string msg, float meas, float req, bool error) {
        issues.push_back({rule, std::move(msg), meas, req, error});
    };
    // Fit failures are HARD errors — accuracy over coverage.
    if (seat.seat_width_x > 0.0f && c.hip_width > 0.0f) {
        const float need = c.hip_width + kHipClearance;
        if (seat.seat_width_x < need)
            push("SEAT_TOO_NARROW", fmt("Seat width ", seat.seat_width_x, "m too narrow for hip width ", c.hip_width, "m"),
                 seat.seat_width_x, need, true);
    }
    if (seat.seat_depth_z > 0.0f && c.body_depth > 0.0f) {
        const float need = c.body_depth + kDepthClearance;
        if (seat.seat_depth_z < need)
            push("SEAT_TOO_SHALLOW", fmt("Seat depth ", seat.seat_depth_z, "m too shallow for body depth ", c.body_depth, "m"),
                 seat.seat_depth_z, need, true);
    }
    if (seat.seat_top_y > 0.0f && c.leg_length > 0.0f) {
        const float overhang = seat.seat_top_y - c.leg_length;
        if (overhang > kFootDropMax)
            push("SEAT_TOO_TALL", fmt("Seat ", seat.seat_top_y, "m above floor, legs only ", c.leg_length, "m"),
                 overhang, kFootDropMax, true);
        const float kneeRise = c.leg_length - seat.seat_top_y;   // big body on a tiny seat: knees rise into a squat
        if (kneeRise > kKneeRiseMax)
            push("SEAT_TOO_LOW", fmt("Seat ", seat.seat_top_y, "m too low for leg length ", c.leg_length, "m"),
                 kneeRise, kKneeRiseMax, true);
    }
    if (seat.backrest_height > 0.0f && c.sitting_height > 0.0f) {
        const float seatedEyeAboveSeat = std::max(0.0f, c.sitting_height - 0.1f);
        if (seat.backrest_height > seatedEyeAboveSeat + kBackrestHeadMax)
            push("BACKREST_BLOCKS_VIEW", fmt("Backrest ", seat.backrest_height, "m exceeds seated eye height ~", seatedEyeAboveSeat, "m"),
                 seat.backrest_height, seatedEyeAboveSeat + kBackrestHeadMax, false);
    }
    return issues;
}

bool refused(const std::vector<FitIssue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const FitIssue& i) { return i.error; });
}

namespace {
const AnimatedVoxelCharacter::BoneAABB* byId(const std::vector<AnimatedVoxelCharacter::BoneAABB>& v, int id) {
    if (id < 0) return nullptr;
    for (const auto& b : v) if (b.boneId == id) return &b;
    return nullptr;
}
} // namespace

CharacterFitMetrics measureCharacter(AnimatedVoxelCharacter& ch) {
    CharacterFitMetrics m;
    const auto live = ch.getBoneAABBs();
    if (live.empty()) return m;
    float yMin = std::numeric_limits<float>::infinity(), yMax = -std::numeric_limits<float>::infinity();
    for (const auto& b : live) {
        yMin = std::min(yMin, b.center.y - b.halfExtents.y);
        yMax = std::max(yMax, b.center.y + b.halfExtents.y);
    }
    m.total_height = (std::isfinite(yMin) && std::isfinite(yMax)) ? (yMax - yMin) : 0.0f;

    const auto& plan = ch.bodyPlanResolved();
    const auto& sk   = ch.getSkeleton();
    const auto* hips = byId(live, plan.rootBoneId);
    const auto* head = byId(live, ch.headBoneId());
    m.hip_height = hips ? hips->center.y - yMin : 0.0f;
    m.eye_height = head ? head->center.y - yMin : (m.total_height > 0.0f ? m.total_height - 0.1f : 0.0f);

    // legs: hip width between the two upper-leg boxes; leg length hips -> foot bottom (else shin)
    if (plan.legs.size() >= 2) {
        const auto* lUp = byId(live, plan.legs[0].upperId);
        const auto* rUp = byId(live, plan.legs[1].upperId);
        if (lUp && rUp) m.hip_width = glm::distance(lUp->center, rUp->center);
    }
    if (hips && !plan.legs.empty()) {
        const auto* foot = byId(live, plan.legs[0].footId);
        const auto* shin = byId(live, plan.legs[0].midId);
        if (foot)      m.leg_length = hips->center.y - (foot->center.y - foot->halfExtents.y);
        else if (shin) m.leg_length = hips->center.y - (shin->center.y - shin->halfExtents.y);
    }
    if (m.leg_length <= 0.0f) m.leg_length = m.hip_height;

    // arms: topmost arm segments (parent not an arm segment) give shoulder width; reach = the
    // off-hand chain's bone lengths hand -> upper (the same chain the two-handed pin uses)
    std::vector<int> armIds;
    for (const auto& [bid, isArm] : plan.segments) if (isArm && bid >= 0) armIds.push_back(bid);
    std::vector<const AnimatedVoxelCharacter::BoneAABB*> shoulders;
    for (int bid : armIds) {
        const int p = sk.bones[bid].parentId;
        if (std::find(armIds.begin(), armIds.end(), p) == armIds.end())
            if (const auto* b = byId(live, bid)) shoulders.push_back(b);
    }
    if (shoulders.size() >= 2) m.shoulder_width = glm::distance(shoulders[0]->center, shoulders[1]->center);
    const auto chain = ch.offHandChain();
    if (chain[0] >= 0 && chain[2] >= 0) {
        float total = 0.0f;
        for (int cur = chain[2], guard = 0; cur >= 0 && cur != chain[0] && guard < 64; cur = sk.bones[cur].parentId, ++guard)
            total += glm::length(sk.bones[cur].localPosition);
        m.arm_reach = total;
    }

    // torso depth from the top of the spine chain (Spine2 on Mixamo)
    if (!ch.spineChain().empty())
        if (const auto* sp = byId(live, ch.spineChain().back())) m.body_depth = sp->halfExtents.z * 2.0f;

    // seated height from the SittingIdle clip the plan resolves for this rig
    const std::string sitIdle = ch.clipForState(AnimatedCharacterState::SittingIdle, false);
    if (!sitIdle.empty()) {
        const auto seated = ch.sampleBoneAABBsAtTime(sitIdle, 0.5f, glm::vec3(0.0f));
        const auto* sHead = byId(seated, ch.headBoneId());
        const auto* sHips = byId(seated, plan.rootBoneId);
        if (sHead && sHips)
            m.sitting_height = (sHead->center.y + sHead->halfExtents.y) - (sHips->center.y - sHips->halfExtents.y);
    }
    return m;
}

} // namespace Phyxel::Scene::SeatFit
