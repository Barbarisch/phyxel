#pragma once

// ============================================================================
// ClickToMove — BG3-style "click the ground, the character walks there"
// (Ravenmere G-75 / CombatUiBg3 increment 5, 2026-09-15).
//
// Outside turn-based combat the player has only had WASD. This walker routes a
// click through the SAME NavGraph the NPCs walk (no second pathing model), feeds
// the character's normal locomotion through an ITurnActorBody (the adapter the
// TurnActor already uses in combat), and reports every outcome honestly:
// Arrived / NoPath / Stalled / Cancelled. A walk that makes no progress ends
// itself instead of pushing into a wall forever (the harness's steering wedges).
//
// Optional targets: `standoff` stops the walk short (interact with an NPC, stop
// at a door) and fires `onArrive` once, in the tick that arrives.
//
// Two helpers live here because both the walker and the host's click handling
// need them: `pickGround` (camera ray -> first solid cube, top face point) and
// `projectToScreen` (world -> pixels; the same math PlayerTurnController uses).
// ============================================================================

#include "core/NavGraph.h"
#include "core/TurnActor.h"
#include "graphics/Camera.h"

#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

class ClickToMove {
public:
    enum class Result { Idle, Walking, Arrived, NoPath, Stalled, Cancelled };
    static const char* resultName(Result r);

    using BodyProvider  = std::function<ITurnActorBody*()>;
    using GraphProvider = std::function<const NavGraph*()>;

    /// The body may be rebuilt across scenes (the shell's player character is),
    /// so it is resolved fresh on every request/tick.
    void setBodyProvider(BodyProvider p)   { m_body = std::move(p); }
    /// Null graph = straight-line walk (a scene with no navigation built).
    void setGraphProvider(GraphProvider p) { m_graph = std::move(p); }
    void setAgent(const NavAgentProfile& a) { m_agent = a; }

    /// Start walking to `goal` (world position; y is the feet height near the
    /// goal — used only to pick the surface level). Returns false (and leaves
    /// the body untouched) when the graph has no route. `standoff` > 0 stops the
    /// walk within that horizontal distance of the goal; `onArrive` fires once
    /// on arrival (not on NoPath/Stalled/Cancelled).
    bool requestWalkTo(const glm::vec3& goal, float standoff = 0.0f,
                       std::function<void()> onArrive = {});
    /// Stop now (WASD pressed, combat began, scene changed). No-op when idle.
    void cancel();

    bool active() const { return m_result == Result::Walking; }
    Result lastResult() const { return m_result; }
    const glm::vec3& goal() const { return m_goal; }
    const std::vector<glm::vec3>& waypoints() const { return m_waypoints; }
    size_t nextWaypoint() const { return m_next; }
    /// Distance walked on the current request (metres, XZ, as the body reports it).
    float walked() const { return m_walked; }

    /// Drive one frame: step toward the current waypoint, advance on its arrival
    /// radius, finish on the goal. No-op unless walking.
    void tick(float dt);

    /// A walk slower than kStallProgress/kStallSeconds for kStallSeconds in a row
    /// ends as Stalled (the body is stopped). Long enough for a door funnel to
    /// resolve, short enough that a wedge is reported within the same breath.
    static constexpr float kStallSeconds  = 2.0f;
    static constexpr float kStallProgress = 0.05f;
    /// Arrival tolerance at the final goal when no standoff is given (metres).
    static constexpr float kGoalRadius = 0.25f;

    // --- helpers shared with the host's click handling ------------------------

    /// World -> screen pixels (viewport-relative, y down). False when behind the
    /// camera. Same projection PlayerTurnController::screenOf uses.
    static bool projectToScreen(const Graphics::Camera& cam, const glm::vec3& world,
                                glm::vec2 viewportPx, glm::vec2& outPx);

    /// Camera ray through a pixel -> the first solid cube along it (3D DDA over
    /// `solid`, at most `maxDist` metres). `outPoint` is the hit point on the
    /// cube's entry face with y raised to the cube's TOP (a standing point), so a
    /// click on the side of a step still targets somewhere walkable. False when
    /// nothing solid is within range.
    static bool pickGround(const Graphics::Camera& cam, glm::vec2 screenPx, glm::vec2 viewportPx,
                           const std::function<bool(const glm::ivec3&)>& solid,
                           float maxDist, glm::vec3& outPoint, glm::ivec3* outCube = nullptr);

private:
    ITurnActorBody* body() const { return m_body ? m_body() : nullptr; }
    void finish(Result r);

    BodyProvider  m_body;
    GraphProvider m_graph;
    NavAgentProfile m_agent;

    Result m_result = Result::Idle;
    glm::vec3 m_goal{0.0f};
    float m_standoff = 0.0f;
    std::function<void()> m_onArrive;
    std::vector<glm::vec3> m_waypoints;
    std::vector<float>     m_arrive;
    size_t m_next = 0;
    float  m_walked = 0.0f;
    float  m_stallClock = 0.0f;
};

} // namespace Core
} // namespace Phyxel
