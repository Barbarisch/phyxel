#include "core/ClickToMove.h"
#include "utils/VoxelRayMarch.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

namespace {
float horiz(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}
} // namespace

const char* ClickToMove::resultName(Result r) {
    switch (r) {
        case Result::Idle:      return "idle";
        case Result::Walking:   return "walking";
        case Result::Arrived:   return "arrived";
        case Result::NoPath:    return "no_path";
        case Result::Stalled:   return "stalled";
        case Result::Cancelled: return "cancelled";
    }
    return "?";
}

bool ClickToMove::requestWalkTo(const glm::vec3& goal, float standoff,
                                std::function<void()> onArrive) {
    ITurnActorBody* b = body();
    if (!b) { m_result = Result::NoPath; return false; }
    if (active()) cancel();

    const glm::vec3 from = b->position();
    std::vector<glm::vec3> wps;
    std::vector<float> radii;
    const NavGraph* graph = m_graph ? m_graph() : nullptr;
    if (graph) {
        auto res = graph->findPath(from, goal, m_agent);
        if (!res.found) { m_result = Result::NoPath; return false; }
        wps = res.waypoints.size() > 2 ? graph->smoothWaypoints(res.waypoints, m_agent)
                                       : res.waypoints;
        radii = graph->arrivalRadii(wps);
    } else {
        wps.push_back(goal);   // no navigation in this scene: straight line
    }
    if (wps.empty()) { m_result = Result::NoPath; return false; }
    // The graph's last waypoint is a cell CENTRE; the click was a point. Finish
    // on the point itself so the character stops where the player clicked.
    wps.back() = glm::vec3(goal.x, wps.back().y, goal.z);
    if (radii.size() != wps.size()) radii.assign(wps.size(), NavGraph::kArriveLoose);
    radii.back() = standoff > 0.0f ? standoff : kGoalRadius;

    m_goal = goal;
    m_standoff = standoff;
    m_onArrive = std::move(onArrive);
    m_waypoints = std::move(wps);
    m_arrive = std::move(radii);
    m_next = 0;
    m_walked = 0.0f;
    m_stallClock = 0.0f;
    m_result = Result::Walking;
    return true;
}

void ClickToMove::cancel() {
    if (!active()) return;
    finish(Result::Cancelled);
}

void ClickToMove::finish(Result r) {
    if (ITurnActorBody* b = body()) b->stop();
    m_result = r;
    if (r == Result::Arrived && m_onArrive) {
        auto cb = std::move(m_onArrive);
        m_onArrive = nullptr;
        cb();
    }
}

void ClickToMove::tick(float dt) {
    if (!active()) return;
    ITurnActorBody* b = body();
    if (!b) { finish(Result::Cancelled); return; }

    const glm::vec3 pos = b->position();
    // Arrived at the goal (standoff-aware) regardless of which waypoint is next:
    // a smoothed path can pass within reach of an NPC before its last waypoint.
    const float goalRadius = m_standoff > 0.0f ? m_standoff : kGoalRadius;
    if (horiz(pos, m_goal) <= goalRadius) { finish(Result::Arrived); return; }

    // Advance past waypoints reached; the last one is the goal itself.
    while (m_next + 1 < m_waypoints.size() &&
           horiz(pos, m_waypoints[m_next]) <= m_arrive[m_next])
        ++m_next;
    if (m_next + 1 >= m_waypoints.size() &&
        horiz(pos, m_waypoints.back()) <= goalRadius) { finish(Result::Arrived); return; }

    const float moved = b->stepToward(m_waypoints[m_next], dt);
    m_walked += moved;

    // Stall watchdog: kStallSeconds without progress (slower than
    // kStallProgress / kStallSeconds) ends the walk honestly, reported within
    // the window itself - a tick that moves resets the clock.
    if (moved >= (kStallProgress / kStallSeconds) * dt) m_stallClock = 0.0f;
    else                                                m_stallClock += dt;
    if (m_stallClock >= kStallSeconds) { finish(Result::Stalled); return; }
}

// --- helpers ------------------------------------------------------------------

bool ClickToMove::projectToScreen(const Graphics::Camera& cam, const glm::vec3& world,
                                  glm::vec2 viewportPx, glm::vec2& outPx) {
    if (viewportPx.x <= 0.0f || viewportPx.y <= 0.0f) return false;
    const glm::mat4 vp = cam.getProjectionMatrix(viewportPx.x / viewportPx.y, 0.1f, 1000.0f)
                       * cam.getViewMatrix();
    const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-6f) return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outPx = { (ndc.x * 0.5f + 0.5f) * viewportPx.x,
              (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportPx.y };
    return true;
}

bool ClickToMove::pickGround(const Graphics::Camera& cam, glm::vec2 screenPx, glm::vec2 viewportPx,
                             const std::function<bool(const glm::ivec3&)>& solid,
                             float maxDist, glm::vec3& outPoint, glm::ivec3* outCube) {
    if (viewportPx.x <= 0.0f || viewportPx.y <= 0.0f || !solid) return false;
    const glm::mat4 vp = cam.getProjectionMatrix(viewportPx.x / viewportPx.y, 0.1f, 1000.0f)
                       * cam.getViewMatrix();
    const glm::mat4 inv = glm::inverse(vp);
    const glm::vec2 ndc(screenPx.x / viewportPx.x * 2.0f - 1.0f,
                        -(screenPx.y / viewportPx.y * 2.0f - 1.0f));
    // The engine projects infinite reverse-Z (Camera.h): NDC z=1 is the near plane and
    // z=0 is INFINITY (w=0), so unproject two FINITE depths and take their direction.
    const glm::vec4 a = inv * glm::vec4(ndc, 1.0f, 1.0f);
    const glm::vec4 b = inv * glm::vec4(ndc, 0.5f, 1.0f);
    if (std::abs(a.w) < 1e-9f || std::abs(b.w) < 1e-9f) return false;
    const glm::vec3 p0 = glm::vec3(a) / a.w;
    const glm::vec3 p1 = glm::vec3(b) / b.w;
    glm::vec3 dir = p1 - p0;
    const float len = glm::length(dir);
    if (len < 1e-6f) return false;
    dir /= len;

    const Utils::VoxelRayHit h = Utils::marchVoxels(p0, dir, maxDist, solid);
    if (!h.hit) return false;
    const glm::vec3 hit = p0 + dir * h.t;
    outPoint = hit;
    // Standing point: on the cube's top. Entering through the top face keeps the
    // exact hit; a side hit is lifted onto the cube and nudged inside it so the
    // walk goal is the cell, not its edge.
    outPoint.y = static_cast<float>(h.cube.y + 1);
    if (h.enteredAxis == 0 || h.enteredAxis == 2) {
        outPoint.x = std::clamp(hit.x, h.cube.x + 0.05f, h.cube.x + 0.95f);
        outPoint.z = std::clamp(hit.z, h.cube.z + 0.05f, h.cube.z + 0.95f);
    }
    if (outCube) *outCube = h.cube;
    return true;
}

} // namespace Core
} // namespace Phyxel
