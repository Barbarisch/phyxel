#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

namespace Phyxel {

// Emission shape for archetypes. Bursts use shapes to bias outward VELOCITY
// directions (nova/cone/ring); fields use shapes to place the shell POSITIONS
// (bubble/dome/cylinder/wall/line). Keeps the VFX layer's spatial vocabulary
// generic so spells (Layer 2) can map cones, walls, cylinders, etc.
enum class VfxShape {
    Sphere,    // full radial (default)
    Dome,      // upper hemisphere around `direction`
    Cone,      // within coneAngle of `direction`
    Ring,      // on a circle in the plane perpendicular to `direction`
    Cube,      // within/on a box of half-extent `extent`
    Cylinder,  // shell of a cylinder: radius `radius`, height `extent.y`, axis `direction`
    Wall,      // planar rectangle: normal `direction`, size `extent.x` x `extent.y`
    Line       // along a segment of length `extent.x` centered, along `direction`
};

// An anchor: fills outPos with the live position to track and returns true,
// or returns false if the anchor is no longer valid (e.g. its entity is gone,
// which ends the effect). A fixed-point anchor just returns a constant.
using VfxPositionProvider = std::function<bool(glm::vec3& outPos)>;

// Lifecycle events emitted by archetypes; the composition layer (VfxDirector)
// drains these to chain emissions (e.g. spawn a burst when a projectile impacts).
struct VfxEvent {
    enum class Type { Spawned, Impact, Expired, Tick };
    Type        type;
    std::string instanceId;   // id of the emitting effect (projectile/beam/field)
    glm::vec3   position{0.0f};
};

// One glowing voxel particle. Pure CPU data — integrated each frame and
// uploaded to the GPU as instanced cubes by Graphics::VfxRenderPipeline.
struct VfxParticle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 scale{0.1f};
    glm::vec4 color{1.0f};   // rgb = emissive color, a = current brightness (fades to 0)
    float lifetime = 0.0f;   // remaining seconds
    float maxLifetime = 1.0f;
    float gravity = 0.0f;    // m/s^2 applied to velocity.y
    float drag = 0.0f;       // velocity damping per second
    bool active = false;
};

// Parameters for a radial burst emitter (Phase 1's only emitter type).
struct VfxBurstParams {
    int   count       = 40;
    float speed       = 6.0f;     // base outward speed (m/s)
    float speedVar    = 2.5f;     // +/- random speed
    float upBias      = 0.35f;    // 0 = full sphere, 1 = straight up; biases the burst upward
    float gravity     = -5.0f;    // gravity applied to particles (m/s^2)
    float drag        = 1.2f;     // velocity damping per second
    float lifetime    = 0.9f;     // seconds
    float lifetimeVar = 0.35f;
    float size        = 0.18f;    // cube edge length
    float sizeVar     = 0.08f;
    float intensity   = 1.6f;     // emissive boost baked into color
    glm::vec3 color{1.0f, 0.45f, 0.12f}; // fire orange by default
    // Shape of the outward velocity spray. Sphere/Dome/Cone/Ring use `direction`
    // (and coneAngleDeg for Cone). Cube fills a box of half-extent `extent`.
    VfxShape  shape = VfxShape::Sphere;
    glm::vec3 direction{0.0f, 1.0f, 0.0f}; // axis for Dome/Cone/Ring
    float     coneAngleDeg = 45.0f;        // Cone half-angle
    glm::vec3 extent{1.0f};                // Cube half-extent (world units)
};

// Parameters for a travelling projectile (Phase 2). The projectile's glowing
// core + trail are emitted into the same particle pool each frame, so no new
// render path is needed. On arrival it spawns a burst (a named preset).
struct VfxProjectileParams {
    glm::vec3 target{0.0f};          // arrival point (used when useTarget)
    bool  useTarget = true;          // travel to target; else along `direction` for maxDistance
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float speed = 12.0f;
    float maxDistance = 100.0f;
    bool  homing = false;            // steer velocity toward target over time
    float homingStrength = 5.0f;     // higher = turns toward target faster
    float spread = 0.0f;             // random initial velocity jitter (for multi-missile fans)
    float coreSize = 0.30f;          // bright leading cube
    float trailRate = 130.0f;        // trail particles per second
    float trailLifetime = 0.45f;
    float trailSize = 0.16f;
    float intensity = 1.9f;          // emissive boost baked into color
    glm::vec3 color{1.0f, 0.4f, 0.1f};
    bool  light = true;              // carry a transient point light (needs light callbacks set)
    float lightRadius = 8.0f;
    float lightIntensity = 2.5f;
    std::string arrivalEffect = "fireball"; // burst preset spawned on arrival ("" = none)
};

// Parameters for a sustained beam (Phase 2): a flickering line of glowing
// cubes from caster->target, re-emitted into the particle pool each frame.
struct VfxBeamParams {
    float duration = 0.35f;          // seconds the beam persists
    float thickness = 0.18f;         // cube size + radial jitter radius
    float density = 3.0f;            // particles per world-unit of length, per frame
    float intensity = 2.0f;          // emissive boost
    float particleLifetime = 0.08f;  // short — soft glow that re-emits each frame
    glm::vec3 color{0.3f, 0.95f, 0.55f}; // eldritch green
    bool  light = true;              // transient point light at the impact end
    float lightRadius = 7.0f;
    float lightIntensity = 2.2f;
    std::string impactEffect = "";   // optional burst preset at target on beam end ("" = none)
    bool  untilDismissed = false;    // ignore duration; persist until dismiss(id) (channeled beams)
    float tickInterval = 0.0f;       // >0 emits a Tick event every N seconds (0 = none)
    VfxPositionProvider anchorTarget;// optional: target end follows this (e.g. tracks a creature)
};

// Parameters for a sustained field/shell (Phase 2): a shimmering shell of glowing
// cubes on a sphere (or dome) around a center, re-emitted into the particle pool
// each frame. Used for Shield.
struct VfxFieldParams {
    float radius = 2.5f;
    float duration = 3.0f;           // fields persist (seconds)
    float thickness = 0.12f;         // shell radial jitter + cube size
    float density = 80.0f;           // particles spawned on the shell per frame
    float intensity = 1.6f;
    float particleLifetime = 0.12f;  // short — re-emitted each frame for a steady shell
    float pulseSpeed = 3.0f;         // shimmer/breathe rate (radians/sec)
    bool  hemisphere = false;        // (legacy) true forces Dome shape
    glm::vec3 color{0.3f, 0.6f, 1.0f}; // protective blue
    bool  light = true;              // transient point light at center
    float lightRadius = 6.0f;
    float lightIntensity = 1.8f;
    // Shell shape. Sphere/Dome use `radius`. Cylinder: radius + height `extent.y`,
    // axis `direction`. Wall: normal `direction`, size `extent.x` x `extent.y`.
    // Line: length `extent.x` along `direction`.
    VfxShape  shape = VfxShape::Sphere;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    glm::vec3 extent{4.0f, 3.0f, 0.0f}; // shape-specific dimensions (see above)
    bool  untilDismissed = false;      // ignore duration; persist until dismiss(id) (walls, auras)
    float tickInterval = 0.0f;         // >0 emits a Tick event every N seconds (0 = none)
    VfxPositionProvider anchor;        // optional: center follows this (e.g. an aura on a creature)
};

// Lightweight CPU particle VFX system for spell/effect bursts, projectiles, beams, and fields.
// Deliberately cheap: per-frame integrate + a single instanced draw, no
// physics collision and no GPU compute. See [[project-spell-vfx]].
class VfxSystem {
public:
    VfxSystem();

    // Integrate all active particles, fade them out, and recycle the dead.
    void update(float dt);

    // Spawn a radial burst of glowing voxel particles at a world position.
    void spawnBurst(const glm::vec3& position, const VfxBurstParams& params);

    // Trigger a named preset ("fireball", "magic_missile", "eldritch_blast",
    // "shield", "heal"). Unknown names fall back to a generic spark burst.
    // Returns the number of particles actually spawned.
    int spawnEffect(const std::string& effect, const glm::vec3& position);

    // Launch a single travelling projectile from origin (Phase 2). Returns its instance id.
    std::string spawnProjectile(const glm::vec3& origin, const VfxProjectileParams& params);

    // Cast a named travelling spell from origin toward target. "fireball" =
    // one fiery projectile + light + arrival burst; "magic_missile" = a fan of
    // homing violet darts. Returns the number of projectiles launched.
    int castProjectile(const std::string& effect, const glm::vec3& origin, const glm::vec3& target);

    // Fire a sustained beam from origin to target (Phase 2). Returns its instance id.
    std::string spawnBeam(const glm::vec3& origin, const glm::vec3& target, const VfxBeamParams& params);

    // Cast a named beam ("eldritch_blast"). Returns 1 if a beam was created.
    int castBeam(const std::string& effect, const glm::vec3& origin, const glm::vec3& target);

    // Raise a sustained field/shell around a center (Phase 2). Returns its instance id.
    std::string spawnField(const glm::vec3& center, const VfxFieldParams& params);

    // Cast a named field ("shield"). Optional shapeOverride ("sphere","dome",
    // "cylinder","wall","line","cube") overrides the preset's shell shape for
    // testing/customization. Returns 1 if a field was created.
    int castField(const std::string& effect, const glm::vec3& center, const std::string& shapeOverride = "");

    // Parse a shape name to the enum (defaults to Sphere on unknown/empty).
    static VfxShape shapeFromString(const std::string& s);

    // End a sustained effect (beam/field) early by instance id. Returns true if found.
    bool dismiss(const std::string& instanceId);

    // Drain lifecycle events emitted since the last call (consumed by VfxDirector).
    std::vector<VfxEvent> drainEvents();

    // Wire transient point lights to the renderer's LightManager (optional —
    // projectiles skip lights if these aren't set). Keeps VfxSystem decoupled
    // from graphics. add returns a light id; -1 means "no light".
    using AddLightFn    = std::function<int(const glm::vec3& pos, const glm::vec3& color, float intensity, float radius)>;
    using MoveLightFn   = std::function<void(int id, const glm::vec3& pos)>;
    using RemoveLightFn = std::function<void(int id)>;
    void setLightCallbacks(AddLightFn add, MoveLightFn move, RemoveLightFn remove) {
        m_addLight = std::move(add); m_moveLight = std::move(move); m_removeLight = std::move(remove);
    }

    size_t getActiveProjectileCount() const { return m_activeProjectiles; }
    size_t getActiveBeamCount() const { return m_activeBeams; }
    size_t getActiveFieldCount() const { return m_activeFields; }

    // Rendering access (mirrors DebrisSystem's interface).
    const std::vector<VfxParticle>& getParticles() const { return m_particles; }
    size_t getActiveCount() const { return m_activeCount; }

    static const char* effectList() {
        return "fireball, magic_missile, eldritch_blast, shield, heal, spark";
    }

private:
    // A travelling projectile. Its core + trail are emitted into the particle
    // pool each frame; this struct only holds the mover's state.
    struct VfxProjectile {
        std::string id;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 target{0.0f};
        bool  useTarget = true;
        bool  homing = false;
        float homingStrength = 0.0f;
        float speed = 12.0f;
        float maxDistance = 100.0f;
        float traveled = 0.0f;
        float arrivalRadius = 1.0f;
        float coreSize = 0.30f;
        glm::vec4 coreColor{1.0f};   // intensity-scaled emissive
        float trailRate = 130.0f;
        float trailAccum = 0.0f;
        float trailLifetime = 0.45f;
        float trailSize = 0.16f;
        glm::vec3 trailColor{1.0f};  // intensity-scaled
        int   lightId = -1;
        std::string arrivalEffect;
        bool  active = false;
    };

    // A sustained beam. Its line of cubes is emitted into the particle pool
    // each frame; this struct only holds the beam's state.
    struct VfxBeam {
        std::string id;
        glm::vec3 origin{0.0f};
        glm::vec3 target{0.0f};
        float duration = 0.35f;
        float elapsed = 0.0f;
        float thickness = 0.18f;
        float density = 3.0f;
        float intensity = 2.0f;
        float particleLifetime = 0.08f;
        glm::vec3 color{1.0f};
        int   lightId = -1;
        std::string impactEffect;
        bool  untilDismissed = false;
        float tickInterval = 0.0f;
        float tickAccum = 0.0f;
        VfxPositionProvider anchorTarget;
        bool  active = false;
    };

    // A sustained field/shell. Its shell of cubes is emitted into the particle
    // pool each frame; this struct only holds the field's state.
    struct VfxField {
        std::string id;
        glm::vec3 center{0.0f};
        float radius = 2.5f;
        float duration = 3.0f;
        float elapsed = 0.0f;
        float thickness = 0.12f;
        float density = 80.0f;
        float intensity = 1.6f;
        float particleLifetime = 0.12f;
        float pulseSpeed = 3.0f;
        bool  hemisphere = false;
        glm::vec3 color{1.0f};
        int   lightId = -1;
        VfxShape  shape = VfxShape::Sphere;
        glm::vec3 direction{0.0f, 1.0f, 0.0f};
        glm::vec3 extent{4.0f, 3.0f, 0.0f};
        bool  untilDismissed = false;
        float tickInterval = 0.0f;
        float tickAccum = 0.0f;
        VfxPositionProvider anchor;
        bool  active = false;
    };

    int allocParticle();          // find/grow a free slot; -1 if at capacity
    // Emit one particle into the pool (used by trails/cores/beams/fields); returns idx or -1.
    int emitParticle(const glm::vec3& pos, const glm::vec3& vel, float size,
                     const glm::vec4& color, float lifetime, float gravity, float drag);
    int allocProjectile();
    void updateProjectiles(float dt);
    int allocBeam();
    void updateBeams(float dt);
    int allocField();
    void updateFields(float dt);
    glm::vec3 randomUnitVector();
    // Random direction within a cone of half-angle (radians) around `axis`.
    glm::vec3 randomConeDir(const glm::vec3& axis, float halfAngleRad);
    // A random point on the given shell shape around `center`.
    glm::vec3 randomShellPoint(VfxShape shape, const glm::vec3& center, float radius,
                               const glm::vec3& dir, const glm::vec3& extent);
    // Two unit vectors perpendicular to `dir` (and to each other) — a basis for shapes.
    void buildBasis(const glm::vec3& dir, glm::vec3& outU, glm::vec3& outV);
    float frand();                // [0, 1)
    float frand(float lo, float hi);

    std::vector<VfxParticle> m_particles;
    size_t m_activeCount = 0;
    size_t m_searchHint = 0;      // rotating start index for slot allocation

    std::vector<VfxProjectile> m_projectiles;
    size_t m_activeProjectiles = 0;
    std::vector<VfxBeam> m_beams;
    size_t m_activeBeams = 0;
    std::vector<VfxField> m_fields;
    size_t m_activeFields = 0;

    std::vector<VfxEvent> m_events;          // lifecycle events drained by VfxDirector
    uint32_t m_instanceCounter = 0;          // for unique effect ids
    std::string nextId(const char* prefix);
    void emitEvent(VfxEvent::Type type, const std::string& id, const glm::vec3& pos);

    AddLightFn    m_addLight;
    MoveLightFn   m_moveLight;
    RemoveLightFn m_removeLight;

    uint32_t m_rngState = 0x9E3779B9u;

    static const size_t MAX_PARTICLES = 10000;
    static const size_t MAX_PROJECTILES = 256;
    static const size_t MAX_BEAMS = 64;
    static const size_t MAX_FIELDS = 32;
};

} // namespace Phyxel
