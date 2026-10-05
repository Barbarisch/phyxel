#include "core/DebrisSettleAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include <glm/gtc/quaternion.hpp>

namespace Phyxel {
namespace Core {

namespace {
constexpr uint32_t kActive   = 1u;
constexpr uint32_t kSleeping = 2u;
constexpr uint32_t kAgeShift = 16u;
constexpr uint32_t kAgeMask  = 0x00FF0000u;
constexpr uint32_t kUncolored = 0xFFFFFFFFu;
} // namespace

void DebrisSettleAnalyzer::reset() {
    m_bodies.clear();
    m_ticks.clear();
    m_tick = 0;
    m_cumInjected = 0.0;
    m_totalMassG = 0.0;
    m_bodiesSeen = 0;
    m_sleptCleanTotal = m_sleptForcedTotal = m_wokeTotal = 0;
    m_creepClean.clear(); m_creepForced.clear(); m_vmaxClean.clear(); m_vmaxForced.clear();
}

void DebrisSettleAnalyzer::addTick(const std::vector<SettleBodySample>& bodies,
                                   const SettleSolverCounters& c) {
    const float dt = m_cfg.dt;
    const float g  = m_cfg.gravity;
    if (m_bodies.size() < bodies.size()) m_bodies.resize(bodies.size());

    SettleTickStats st;
    st.tick   = m_tick;
    st.t      = static_cast<float>(m_tick) * dt;
    st.solver = c;

    double sumDelta = 0.0;            // ΔE over bodies present in both ticks
    std::vector<float> awakeSpeeds;
    awakeSpeeds.reserve(bodies.size());

    for (size_t i = 0; i < m_bodies.size(); ++i) {
        BodyTrack& tr = m_bodies[i];
        if (i >= bodies.size() || !(bodies[i].flags & kActive)) {
            tr.present = false;
            continue;
        }
        const SettleBodySample& s = bodies[i];
        const uint32_t age   = (s.flags & kAgeMask) >> kAgeShift;
        const bool asleep    = (s.flags & kSleeping) != 0;
        const bool isNew     = !tr.present || age < tr.age;

        const glm::vec3 v   = (s.position - s.prevPosition) / dt;
        const float speed   = glm::length(v);
        const double inertia = 0.4 * double(s.mass) * double(s.radius) * double(s.radius);
        const double ke = 0.5 * double(s.mass) * double(glm::dot(v, v)) +
                          0.5 * inertia * double(glm::dot(s.angularVel, s.angularVel));
        const double energy = ke + double(s.mass) * double(g) * double(s.position.y);

        if (st.active == 0 || s.position.y < st.minY) st.minY = s.position.y;
        if (!std::isnan(m_cfg.floorY) && s.position.y < m_cfg.floorY - 1.0f) ++st.belowFloor;
        ++st.active;
        st.energy  += energy;
        st.kinetic += ke;

        if (isNew) {
            tr = BodyTrack{};
            tr.present   = true;
            tr.firstSeen = st.t;
            m_totalMassG += double(s.mass) * double(g);
            ++m_bodiesSeen;
        } else {
            sumDelta += energy - tr.energy;

            // Sleep transitions: clean (already still) vs force-frozen while moving — below.
            if (asleep && !tr.asleep) {
                float creep = 0.0f, vmax = 0.0f;
                if (tr.histN > 0) {
                    const uint32_t oldest = (tr.histHead + BodyTrack::kHist - tr.histN) % BodyTrack::kHist;
                    creep = glm::length(s.position - tr.histPos[oldest]);
                    for (uint32_t h = 0; h < tr.histN; ++h) vmax = std::max(vmax, tr.histSpeed[h]);
                }
                // CLEAN = the body was already strictly still (< strictSpeed) on the tick before
                // it froze — the GPU strict tier; the freeze is invisible. FORCED = frozen while
                // still moving faster than that — the lax tier's force-freeze, a visible "stop".
                // (The first definition demanded 30 consecutive strict ticks, stricter than the
                // engine's own rule, and counted normal deceleration-to-rest as forced.)
                if (glm::length(tr.vel) < m_cfg.strictSpeed) {
                    ++st.sleptClean; ++m_sleptCleanTotal;
                    m_creepClean.push_back(creep); m_vmaxClean.push_back(vmax);
                } else {
                    ++st.sleptForced; ++m_sleptForcedTotal;
                    m_creepForced.push_back(creep); m_vmaxForced.push_back(vmax);
                }
            } else if (!asleep && tr.asleep) {
                ++st.woke; ++m_wokeTotal;
            }

            if (!asleep && !tr.asleep) {
                const float prevSpeed = glm::length(tr.vel);
                if (tr.vel.y <= m_cfg.reboundPrevMax && v.y >= m_cfg.reboundUp) {
                    ++st.rebounds; ++tr.rebounds;
                    if (st.t >= m_cfg.settleWindow) ++tr.reboundsAfter;
                    if (v.y >= m_cfg.launchUp) ++st.launches;
                }
                if (prevSpeed < m_cfg.kickPrevMax && speed - prevSpeed > m_cfg.kickDelta) {
                    ++st.kicks; ++tr.kicks;
                }
            }
        }

        if (asleep) {
            ++st.asleep;
        } else {
            ++st.awake;
            awakeSpeeds.push_back(speed);
            st.maxSpeed = std::max(st.maxSpeed, speed);
            if (speed > m_cfg.churnSpeed) ++st.churning;
            if (s.constraintCount > 0 && (s.color >= c.maxColors || s.color == kUncolored)) {
                if (s.color == kUncolored && c.uncoloredSolved) ++st.uncoloredJacobi;
                else                                            ++st.colorSkipped;
            }
        }

        tr.strictRun = (!asleep && speed < m_cfg.strictSpeed) ? tr.strictRun + 1 : 0;
        tr.histPos[tr.histHead]   = s.position;
        tr.histSpeed[tr.histHead] = speed;
        tr.histHead = (tr.histHead + 1) % BodyTrack::kHist;
        tr.histN    = std::min(tr.histN + 1, BodyTrack::kHist);
        tr.age    = age;
        tr.vel    = v;
        tr.energy = energy;
        tr.asleep = asleep;
    }

    if (!awakeSpeeds.empty()) {
        size_t k = static_cast<size_t>(std::floor(0.95 * double(awakeSpeeds.size() - 1)));
        std::nth_element(awakeSpeeds.begin(), awakeSpeeds.begin() + k, awakeSpeeds.end());
        st.p95Speed = awakeSpeeds[k];
    }

    st.injected     = std::max(0.0, sumDelta);
    m_cumInjected  += st.injected;
    st.cumInjected  = m_cumInjected;

    m_ticks.push_back(st);
    m_lastSamples = bodies;
    ++m_tick;
}

nlohmann::json DebrisSettleAnalyzer::awakeBodies(uint32_t maxN) const {
    struct Row { size_t slot; float speed; };
    std::vector<Row> rows;
    for (size_t i = 0; i < m_lastSamples.size(); ++i) {
        const auto& s = m_lastSamples[i];
        if (!(s.flags & kActive) || (s.flags & kSleeping)) continue;
        rows.push_back({i, glm::length((s.position - s.prevPosition) / m_cfg.dt)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.speed > b.speed; });
    nlohmann::json out = nlohmann::json::array();
    for (size_t k = 0; k < rows.size() && k < maxN; ++k) {
        const auto& s = m_lastSamples[rows[k].slot];
        const glm::vec3 v = (s.position - s.prevPosition) / m_cfg.dt;
        const BodyTrack* tr = rows[k].slot < m_bodies.size() ? &m_bodies[rows[k].slot] : nullptr;
        // Tilt: angle between world up and the body's most-vertical local axis (0 = flat).
        const glm::quat q(s.rotation.w, s.rotation.x, s.rotation.y, s.rotation.z);
        float upDot = 0.0f;
        for (int a = 0; a < 3; ++a) {
            glm::vec3 e(0.0f); e[a] = 1.0f;
            upDot = std::max(upDot, std::abs((q * e).y));
        }
        const float tiltDeg = glm::degrees(std::acos(std::min(1.0f, upDot)));
        out.push_back({{"slot", rows[k].slot},
                       {"pos", {s.position.x, s.position.y, s.position.z}},
                       {"vel", {v.x, v.y, v.z}}, {"speed", rows[k].speed},
                       {"ang_speed", glm::length(s.angularVel)}, {"tilt_deg", tiltDeg},
                       {"color", s.color == kUncolored ? -1 : static_cast<int64_t>(s.color)},
                       {"constraints", s.constraintCount},
                       {"sleep_ctr", (s.flags >> 8) & 0xFFu}, {"age", (s.flags & kAgeMask) >> kAgeShift},
                       {"rebounds", tr ? tr->rebounds : 0u}, {"kicks", tr ? tr->kicks : 0u}});
    }
    return out;
}

nlohmann::json DebrisSettleAnalyzer::series(uint32_t lastN) const {
    nlohmann::json rows = nlohmann::json::array();
    size_t start = (lastN > 0 && m_ticks.size() > lastN) ? m_ticks.size() - lastN : 0;
    for (size_t i = start; i < m_ticks.size(); ++i) {
        const auto& s = m_ticks[i];
        rows.push_back({
            {"tick", s.tick}, {"t", s.t}, {"active", s.active}, {"awake", s.awake},
            {"asleep", s.asleep}, {"energy", s.energy}, {"kinetic", s.kinetic},
            {"injected", s.injected}, {"cum_injected", s.cumInjected},
            {"max_speed", s.maxSpeed}, {"p95_speed", s.p95Speed}, {"churning", s.churning},
            {"rebounds", s.rebounds}, {"kicks", s.kicks}, {"launches", s.launches},
            {"slept_clean", s.sleptClean}, {"slept_forced", s.sleptForced}, {"woke", s.woke},
            {"color_skipped", s.colorSkipped}, {"below_floor", s.belowFloor}, {"min_y", s.minY},
            {"constraints", s.solver.constraintsEmitted},
            {"hardcontact", s.solver.hardContactFires},
            {"hardcontact_max_depth", s.solver.hardContactMaxDepth},
            {"wake_requests", s.solver.wakeRequests},
            {"frozen_unknown", s.solver.frozenUnknown}});
    }
    return rows;
}

nlohmann::json DebrisSettleAnalyzer::summary() const {
    using nlohmann::json;
    json out;
    const uint32_t n = m_bodiesSeen;
    out["ticks"]        = m_ticks.size();
    out["duration_s"]   = m_ticks.empty() ? 0.0f : m_ticks.back().t + m_cfg.dt;
    out["bodies"]       = n;
    if (m_ticks.empty()) { out["verdict"] = "no data"; return out; }

    // ---- windows: [0,.5) [.5,1) [1,2) [2,4) [4,8) [8,16) [16,32) ----
    const float edges[] = {0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 1e9f};
    json windows = json::array();
    for (size_t w = 0; w + 1 < sizeof(edges) / sizeof(edges[0]); ++w) {
        uint64_t reb = 0, kick = 0, hc = 0, wakes = 0;
        uint32_t maxChurn = 0, maxSkip = 0, lastAwake = 0, ticksIn = 0;
        double inj = 0.0;
        float maxSpeed = 0.0f;
        for (const auto& s : m_ticks) {
            if (s.t < edges[w] || s.t >= edges[w + 1]) continue;
            ++ticksIn;
            reb += s.rebounds; kick += s.kicks; hc += s.solver.hardContactFires;
            wakes += s.woke; inj += s.injected;
            maxChurn = std::max(maxChurn, s.churning);
            maxSkip  = std::max(maxSkip, s.colorSkipped);
            maxSpeed = std::max(maxSpeed, s.maxSpeed);
            lastAwake = s.awake;
        }
        if (ticksIn == 0) continue;
        windows.push_back({
            {"from_s", edges[w]}, {"to_s", std::min(edges[w + 1], m_ticks.back().t + m_cfg.dt)},
            {"rebounds", reb}, {"kicks", kick}, {"woke", wakes},
            {"injected_J", inj},
            {"injected_lift_m", m_totalMassG > 0 ? inj / m_totalMassG : 0.0},
            {"awake_at_end", lastAwake}, {"max_churning", maxChurn}, {"max_speed", maxSpeed},
            {"hardcontact_fires", hc}, {"max_color_skipped", maxSkip}});
    }
    out["windows"] = windows;

    // ---- settle times ----
    auto settleTime = [&](double frac) -> json {
        // first tick from which awake ≤ (1-frac)·active holds for the rest of the run
        int idx = -1;
        for (int i = static_cast<int>(m_ticks.size()) - 1; i >= 0; --i) {
            const auto& s = m_ticks[i];
            if (s.active == 0) continue;
            if (double(s.awake) <= (1.0 - frac) * double(s.active) + 1e-9) idx = i;
            else break;
        }
        if (idx < 0) return nullptr;
        return m_ticks[idx].t;
    };
    out["time_95_asleep_s"]  = settleTime(0.95);
    out["time_all_asleep_s"] = settleTime(1.0);

    // ---- per-body rebound distribution ----
    uint64_t rebTotal = 0, rebAfter = 0;
    uint32_t rebMax = 0, over3 = 0;
    for (const auto& b : m_bodies) {
        rebTotal += b.rebounds; rebAfter += b.reboundsAfter;
        rebMax = std::max(rebMax, b.rebounds);
        if (b.rebounds > 3) ++over3;
    }
    // m_bodies only holds the latest occupant of each slot; with no slot reuse inside a
    // run (the rigs) that is every body. Totals from the tick series are authoritative.
    uint64_t rebSeries = 0, rebSeriesAfter = 0, kicksAfter = 0;
    double injAfter = 0.0;
    uint32_t hcAfter = 0, maxSkipped = 0, maxJacobi = 0, maxHcDepthTick = 0;
    float maxHcDepth = 0.0f;
    uint32_t maxConstraints = 0, droppedTicks = 0;
    uint64_t frozenUnknownTotal = 0;   // body-ticks held for unknown occupancy (1c)
    // The judged window starts once the IMPACT phase is over (docs/DebrisSettlingPlan.md §3:
    // "t > t0 + ..."): 0.5 s after the last tick where anything still moved faster than
    // impactSpeed (a collapsing tower or blast debris is still legitimately falling), and
    // never before settleWindow. Bubbling is motion AFTER that.
    float lastImpact = 0.0f;
    for (const auto& s : m_ticks)
        if (s.maxSpeed > m_cfg.impactSpeed) lastImpact = s.t;
    const float windowStart = std::max(m_cfg.settleWindow, lastImpact + 0.5f);
    for (const auto& s : m_ticks) {
        rebSeries += s.rebounds;
        if (s.t >= windowStart) {
            rebSeriesAfter += s.rebounds; kicksAfter += s.kicks;
            injAfter += s.injected; hcAfter += s.solver.hardContactFires;
        }
        frozenUnknownTotal += s.solver.frozenUnknown;
        maxSkipped = std::max(maxSkipped, s.colorSkipped);
        maxJacobi  = std::max(maxJacobi, s.uncoloredJacobi);
        if (s.solver.hardContactMaxDepth > maxHcDepth) {
            maxHcDepth = s.solver.hardContactMaxDepth; maxHcDepthTick = s.tick;
        }
        maxConstraints = std::max(maxConstraints, s.solver.constraintsEmitted);
        if (s.solver.constraintCap && s.solver.constraintsEmitted > s.solver.constraintCap)
            ++droppedTicks;
    }
    out["rebounds"] = {{"total", rebSeries}, {"after_settle_window", rebSeriesAfter},
                       {"per_body_mean", n ? double(rebSeries) / n : 0.0},
                       {"per_body_after_mean", n ? double(rebSeriesAfter) / n : 0.0},
                       {"per_body_max", rebMax}, {"bodies_over_3", over3}};
    out["kicks_after_settle_window"] = kicksAfter;
    uint32_t maxBelow = 0;
    float lowest = m_ticks.front().minY;
    for (const auto& s : m_ticks) {
        maxBelow = std::max(maxBelow, s.belowFloor);
        if (s.active) lowest = std::min(lowest, s.minY);
    }
    out["tunnelled"] = {{"floor_y", std::isnan(m_cfg.floorY) ? nlohmann::json(nullptr)
                                                              : nlohmann::json(m_cfg.floorY)},
                        {"max_bodies_below_floor", maxBelow},
                        {"bodies_below_floor_at_end", m_ticks.back().belowFloor},
                        {"lowest_y", lowest}};
    out["energy"] = {{"initial_J", m_ticks.front().energy}, {"final_J", m_ticks.back().energy},
                     {"injected_total_J", m_cumInjected},
                     {"injected_total_lift_m", m_totalMassG > 0 ? m_cumInjected / m_totalMassG : 0.0},
                     {"injected_after_window_J", injAfter},
                     {"injected_after_window_lift_m", m_totalMassG > 0 ? injAfter / m_totalMassG : 0.0}};
    out["sleep"] = {{"clean", m_sleptCleanTotal}, {"forced", m_sleptForcedTotal},
                    {"woke", m_wokeTotal},
                    {"forced_fraction", (m_sleptCleanTotal + m_sleptForcedTotal)
                        ? double(m_sleptForcedTotal) / double(m_sleptCleanTotal + m_sleptForcedTotal) : 0.0},
                    {"awake_at_end", m_ticks.back().awake}};
    // What a sleep LOOKED like: how far each body travelled in the 0.5 s before it froze, and
    // its peak speed in that window. A forced freeze with ~0 creep is invisible; one with cm
    // of creep is a visible "stop" of a still-sliding body.
    auto dist = [](std::vector<float> v, float scale) -> nlohmann::json {
        if (v.empty()) return nullptr;
        std::sort(v.begin(), v.end());
        auto at = [&](double q) { return v[static_cast<size_t>(q * double(v.size() - 1))] * scale; };
        return {{"n", v.size()}, {"median", at(0.5)}, {"p90", at(0.9)}, {"max", v.back() * scale}};
    };
    out["sleep"]["creep_before_sleep_mm"] = {{"clean", dist(m_creepClean, 1000.0f)},
                                             {"forced", dist(m_creepForced, 1000.0f)}};
    out["sleep"]["peak_speed_before_sleep"] = {{"clean", dist(m_vmaxClean, 1.0f)},
                                               {"forced", dist(m_vmaxForced, 1.0f)}};
    out["solver"] = {{"max_color_skipped", maxSkipped}, {"max_uncolored_jacobi", maxJacobi},
                     {"max_constraints", maxConstraints},
                     {"constraint_cap", m_ticks.back().solver.constraintCap},
                     {"ticks_dropping_constraints", droppedTicks},
                     {"hardcontact_fires_after_window", hcAfter},
                     {"hardcontact_max_depth_m", maxHcDepth},
                     {"hardcontact_max_depth_tick", maxHcDepthTick}};

    // ---- verdict (docs/DebrisSettlingPlan.md §3 A2 invariants) ----
    const json tAll = out["time_all_asleep_s"];
    const double rebAfterMean = n ? double(rebSeriesAfter) / n : 0.0;
    const double liftAfter = m_totalMassG > 0 ? injAfter / m_totalMassG : 0.0;
    json checks = {
        {"rebounds_after_window", {{"value", rebAfterMean}, {"limit", m_cfg.maxReboundsPerBodyAfter},
                                   {"pass", rebAfterMean <= m_cfg.maxReboundsPerBodyAfter}}},
        {"injected_lift_after_window_m", {{"value", liftAfter}, {"limit", m_cfg.maxInjectedLiftAfter},
                                          {"pass", liftAfter <= m_cfg.maxInjectedLiftAfter}}},
        // Plan §3: "100 % SLEEPING by t0 + 3 s" — within 3 s of the impact phase ending
        // (never later-judged than allAsleepBy for a scene with no impact phase).
        {"all_asleep_s", {{"value", tAll}, {"limit", std::max(m_cfg.allAsleepBy, lastImpact + 3.0f)},
                          {"pass", !tAll.is_null() &&
                                   tAll.get<double>() <= std::max(m_cfg.allAsleepBy, lastImpact + 3.0f)}}},
        {"forced_sleeps", {{"value", m_sleptForcedTotal}, {"limit", 0},
                           {"pass", m_sleptForcedTotal == 0}}},
        {"color_skipped", {{"value", maxSkipped}, {"limit", 0}, {"pass", maxSkipped == 0}}},
        {"constraints_dropped_ticks", {{"value", droppedTicks}, {"limit", 0}, {"pass", droppedTicks == 0}}},
        {"hardcontact_after_window", {{"value", hcAfter}, {"limit", 0}, {"pass", hcAfter == 0}}},
        {"tunnelled_through_floor", {{"value", maxBelow}, {"limit", 0}, {"pass", maxBelow == 0}}},
        // A body held because its contacts needed occupancy the shared pool does not have
        // (outside the box / chunk not resident). In a resident test world this must never fire.
        {"held_unknown_occupancy", {{"value", frozenUnknownTotal}, {"limit", 0},
                                    {"pass", frozenUnknownTotal == 0}}},
    };
    bool allPass = true;
    for (auto& [k, v] : checks.items()) allPass = allPass && v["pass"].get<bool>();
    out["checks"]  = checks;
    out["verdict"] = allPass ? "SETTLES" : "FAILS";
    out["criteria"] = {{"settle_window_min_s", m_cfg.settleWindow},
                       {"impact_phase_end_s", lastImpact},
                       {"judged_from_s", windowStart}};
    return out;
}

bool DebrisSettleAnalyzer::writeCsv(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "tick,t,active,awake,asleep,energy,kinetic,injected,cum_injected,max_speed,p95_speed,"
         "churning,rebounds,kicks,launches,slept_clean,slept_forced,woke,color_skipped,"
         "constraints,hardcontact,hardcontact_max_depth,wake_requests,frozen_unknown,below_floor,min_y\n";
    for (const auto& s : m_ticks) {
        f << s.tick << ',' << s.t << ',' << s.active << ',' << s.awake << ',' << s.asleep << ','
          << s.energy << ',' << s.kinetic << ',' << s.injected << ',' << s.cumInjected << ','
          << s.maxSpeed << ',' << s.p95Speed << ',' << s.churning << ',' << s.rebounds << ','
          << s.kicks << ',' << s.launches << ',' << s.sleptClean << ',' << s.sleptForced << ','
          << s.woke << ',' << s.colorSkipped << ',' << s.solver.constraintsEmitted << ','
          << s.solver.hardContactFires << ',' << s.solver.hardContactMaxDepth << ','
          << s.solver.wakeRequests << ',' << s.solver.frozenUnknown << ','
          << s.belowFloor << ',' << s.minY << '\n';
    }
    return true;
}

} // namespace Core
} // namespace Phyxel
