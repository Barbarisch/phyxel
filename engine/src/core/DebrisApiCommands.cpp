// DebrisInteractionPlan Phase 5b: the debris API handlers, moved VERBATIM out of the editor's
// Application.cpp (2026-10-07) so the editor and shipped games (GameApiService) register the same
// code. Only the host members they used are rebound to DebrisApiContext. See DebrisApiCommands.h.
#include "core/DebrisApiCommands.h"
#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/CoherentFragmentManager.h"
#include "core/DamageSystem.h"
#include "core/DebrisRuntime.h"
#include "core/GpuParticlePhysics.h"
#include "core/KinematicVoxelManager.h"
#include "core/Microcube.h"
#include "core/Subcube.h"
#include "graphics/RenderCoordinator.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "solver_shared.h"
#include "utils/Logger.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <limits>
#include <random>
#include <string>

namespace Phyxel {

using json = nlohmann::json;

namespace {

// Helper: handle debug dynamic spawn commands (Bullet cubes / GPU particles)
bool handleDebugDynamicSpawnCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    ChunkManager* chunkManager,
    GpuParticlePhysics* gpuParticles,
    const std::string& gpuDisabledReason)
{
    if (cmd.action == "spawn_gpu_particle") {
        float x = cmd.params.value("x", 0.0f);
        float y = cmd.params.value("y", 20.0f);
        float z = cmd.params.value("z", 0.0f);
        std::string material = cmd.params.value("material", "Stone");
        float scale = cmd.params.value("scale", 1.0f);
        float lifetime = cmd.params.value("lifetime", 30.0f);
        int count = std::clamp(cmd.params.value("count", 1), 1, 2000);
        glm::vec3 vel(0.0f);
        if (cmd.params.contains("velocity")) {
            vel.x = cmd.params["velocity"].value("x", 0.0f);
            vel.y = cmd.params["velocity"].value("y", 0.0f);
            vel.z = cmd.params["velocity"].value("z", 0.0f);
        }
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GPU particle physics not available"}};
            return true;
        }
        float spacing = scale * 1.1f;
        int gridSize = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(count))));
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            int gx = i % gridSize;
            int gy = (i / gridSize) % gridSize;
            int gz = i / (gridSize * gridSize);
            GpuParticlePhysics::SpawnParams sp;
            sp.position = glm::vec3(x + gx * spacing, y + gy * spacing, z + gz * spacing);
            sp.velocity = vel;
            sp.angularVel = glm::vec3(
                ((rand() % 1000) / 500.f - 1.f) * 3.0f,
                ((rand() % 1000) / 500.f - 1.f) * 3.0f,
                ((rand() % 1000) / 500.f - 1.f) * 3.0f);
            sp.materialName = material;
            sp.scale = glm::vec3(scale);
            sp.lifetime = lifetime;
            gpuParticles->queueSpawn(sp);
            ++spawned;
        }
        response = {{"success", true}, {"spawned", spawned}, {"system", "gpu"},
                    {"position", {{"x", x}, {"y", y}, {"z", z}}}};
        return true;
    }
    // Settle-probe rigs (docs/DebrisSettlingPlan.md §3): a deterministic lattice of GPU
    // debris. gap=0 → faces exactly touching (the packed / crater case); gap>0 → separated
    // (the control). (x,y,z) = min corner of the lattice; y is the BOTTOM face of layer 0.
    if (cmd.action == "spawn_gpu_lattice") {
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GPU particle physics not available"}};
            return true;
        }
        const float x = cmd.params.value("x", 0.0f);
        const float y = cmd.params.value("y", 20.0f);
        const float z = cmd.params.value("z", 0.0f);
        const int nx = std::clamp(cmd.params.value("nx", 4), 1, 64);
        const int ny = std::clamp(cmd.params.value("ny", 4), 1, 64);
        const int nz = std::clamp(cmd.params.value("nz", 4), 1, 64);
        const float scale    = cmd.params.value("scale", 1.0f);
        const float gap      = cmd.params.value("gap", 0.0f);
        const float spin     = cmd.params.value("spin", 0.0f);     // max |ω| per axis, rad/s
        const float jitter   = cmd.params.value("jitter", 0.0f);   // max |v| per axis, m/s
        const float lifetime = cmd.params.value("lifetime", 120.0f);
        const std::string material = cmd.params.value("material", "Stone");
        const uint32_t seed  = cmd.params.value("seed", 1u);
        glm::vec3 vel(0.0f);
        if (cmd.params.contains("velocity")) {
            vel.x = cmd.params["velocity"].value("x", 0.0f);
            vel.y = cmd.params["velocity"].value("y", 0.0f);
            vel.z = cmd.params["velocity"].value("z", 0.0f);
        }
        if (nx * ny * nz > 9000) {
            response = {{"error", "lattice too large"}, {"bodies", nx * ny * nz}, {"max", 9000}};
            return true;
        }
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const float pitch = scale + gap;
        int spawned = 0;
        for (int iy = 0; iy < ny; ++iy)
            for (int iz = 0; iz < nz; ++iz)
                for (int ix = 0; ix < nx; ++ix) {
                    GpuParticlePhysics::SpawnParams sp;
                    sp.position = glm::vec3(x + (ix + 0.5f) * pitch - 0.5f * gap,
                                            y + (iy + 0.5f) * pitch - 0.5f * gap,
                                            z + (iz + 0.5f) * pitch - 0.5f * gap);
                    sp.velocity   = vel + jitter * glm::vec3(u(rng), u(rng), u(rng));
                    sp.angularVel = spin * glm::vec3(u(rng), u(rng), u(rng));
                    sp.scale      = glm::vec3(scale);
                    sp.materialName = material;
                    sp.lifetime   = lifetime;
                    gpuParticles->queueSpawn(sp);
                    ++spawned;
                }
        response = {{"success", true}, {"spawned", spawned}, {"pitch", pitch},
                    {"extent", {{"x", nx * pitch}, {"y", ny * pitch}, {"z", nz * pitch}}}};
        return true;
    }
    // Position log, on the MAIN thread: the main thread writes the stream every frame, so
    // opening/closing it anywhere else races that write (it hung the engine, 2026-10-05).
    if (cmd.action == "particle_log") {
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GpuParticlePhysics not available"}};
            return true;
        }
        const std::string action = cmd.params.value("action", "");
        const std::string file   = cmd.params.value("file", "particle_positions.csv");
        if (action == "start") {
            response = {{"success", gpuParticles->startPositionLog(file)}, {"action", "start"}, {"file", file}};
        } else if (action == "stop") {
            gpuParticles->stopPositionLog();
            response = {{"success", true}, {"action", "stop"}};
        } else if (action == "status") {
            response = {{"logging", gpuParticles->isPositionLogging()}};
        } else {
            response = {{"error", "Unknown action. Use 'start', 'stop', or 'status'."}};
        }
        return true;
    }
    if (cmd.action == "gpu_physics") {
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GPU particle physics not available"},
                        {"enabled", false},
                        {"disabled_reason", gpuDisabledReason},
                        {"refused_spawns", Phyxel::DamageSystem::refusedDebrisTotal()}};
            return true;
        }
        if (cmd.params.contains("frozen")) gpuParticles->setFrozen(cmd.params.value("frozen", false));
        if (cmd.params.contains("step"))   gpuParticles->stepTicks(cmd.params.value("step", 0u));
        if (cmd.params.contains("flags"))  gpuParticles->setSolverFlags(cmd.params.value("flags", DebrisShared::SOLVER_FLAGS_DEFAULT));
        if (cmd.params.contains("cold_scale"))
            gpuParticles->setColdPenaltyScale(cmd.params.value("cold_scale", 1.0f));
        response = {{"success", true}, {"enabled", true},
                    {"refused_spawns", Phyxel::DamageSystem::refusedDebrisTotal()},
                    {"frozen", gpuParticles->isFrozen()},
                    {"solver_flags", gpuParticles->solverFlags()},
                    {"cold_scale", gpuParticles->coldPenaltyScale()},
                    {"pending_steps", gpuParticles->pendingStepTicks()},
                    {"total_ticks", gpuParticles->totalTicks()},
                    {"active", gpuParticles->getActiveParticleCount()},
                    {"kinematic_boxes", gpuParticles->kinematicBoxes().size()},
                    {"character_mover_boxes", gpuParticles->moverCount()},
                    {"object_mover_boxes", gpuParticles->objectMoverCount()},
                    {"impulses_pending", gpuParticles->pendingImpulses()},
                    {"impulses_submitted", gpuParticles->impulsesSubmitted()},
                    {"impulse_overflow", gpuParticles->impulseOverflow()},
                    {"body_mover_boxes", gpuParticles->bodyMoverCount()},
                    {"kinematic_overflow", gpuParticles->kinematicOverflow()}};
        return true;
    }
    if (cmd.action == "gpu_kinematic_box") {
        // DebrisInteractionPlan 1f: a scripted mover for solver tests (no NPC AI). Omitted fields
        // keep the stored box's value; `remove:true` deletes it. The box is a kinematic AVBD body
        // (Phase 2), axis-aligned for now - the echo says so.
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GPU particle physics not available"}};
            return true;
        }
        const std::string id = cmd.params.value("id", std::string("box"));
        if (cmd.params.value("remove", false)) {
            response = {{"removed", gpuParticles->removeKinematicBox(id)}, {"id", id},
                        {"kinematic_boxes", gpuParticles->kinematicBoxes().size()}};
            return true;
        }
        auto vec3Of = [&](const char* k, glm::vec3 def) {
            if (!cmd.params.contains(k)) return def;
            const auto& a = cmd.params[k];
            if (a.is_array() && a.size() == 3) return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
            return glm::vec3(a.value("x", def.x), a.value("y", def.y), a.value("z", def.z));
        };
        GpuParticlePhysics::KinematicBox b;
        if (auto it = gpuParticles->kinematicBoxes().find(id); it != gpuParticles->kinematicBoxes().end())
            b = it->second;
        b.center   = vec3Of("center", b.center);
        b.half     = vec3Of("half", b.half);
        b.velocity = vec3Of("velocity", b.velocity);
        b.ttl      = cmd.params.value("ttl", b.ttl);
        gpuParticles->setKinematicBox(id, b);
        const auto& stored = gpuParticles->kinematicBoxes().at(id);
        response = {{"id", id},
                    {"center", {stored.center.x, stored.center.y, stored.center.z}},
                    {"half", {stored.half.x, stored.half.y, stored.half.z}},
                    {"velocity", {stored.velocity.x, stored.velocity.y, stored.velocity.z}},
                    {"ttl", stored.ttl},
                    {"kinematic_boxes", gpuParticles->kinematicBoxes().size()},
                    {"overflow", gpuParticles->kinematicOverflow()},
                    {"backend", "kinematic AVBD body (axis-aligned)"}};
        if (cmd.params.contains("rotation") || cmd.params.contains("angular_velocity"))
            response["ignored"] = {"rotation", "angular_velocity"};
        return true;
    }
    if (cmd.action == "settle_probe") {
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GPU particle physics not available"}};
            return true;
        }
        const std::string op = cmd.params.value("op", "status");
        if (op == "start") {
            Core::DebrisSettleAnalyzer::Config c;
            c.settleWindow = cmd.params.value("settle_window", c.settleWindow);
            c.allAsleepBy  = cmd.params.value("all_asleep_by", c.allAsleepBy);
            c.maxReboundsPerBodyAfter = cmd.params.value("max_rebounds_after", c.maxReboundsPerBodyAfter);
            c.maxInjectedLiftAfter    = cmd.params.value("max_injected_lift_after", c.maxInjectedLiftAfter);
            if (cmd.params.contains("floor_y")) c.floorY = cmd.params.value("floor_y", 0.0f);
            gpuParticles->startSettleProbe(c);
        } else if (op == "stop") {
            gpuParticles->stopSettleProbe();
        } else if (op != "status") {
            response = {{"error", "op must be start|stop|status"}};
            return true;
        }
        const auto& an = gpuParticles->settleAnalyzer();
        response = {{"success", true}, {"running", gpuParticles->isSettleProbing()},
                    {"ticks", an.tickCount()}};
        if (op != "start") {
            response["summary"] = an.summary();
            const uint32_t lastN = cmd.params.value("series_last", 0u);
            if (cmd.params.value("series", false) || lastN > 0)
                response["series"] = an.series(lastN);
            if (cmd.params.contains("bodies"))
                response["awake_bodies"] = an.awakeBodies(cmd.params.value("bodies", 16u));
            if (cmd.params.contains("csv")) {
                const std::string path = cmd.params["csv"].get<std::string>();
                response["csv_written"] = an.writeCsv(path);
                response["csv"] = path;
            }
        }
        return true;
    }
    if (cmd.action == "spawn_voxel_body") {
        float x = cmd.params.value("x", 0.0f);
        float y = cmd.params.value("y", 20.0f);
        float z = cmd.params.value("z", 0.0f);
        float scale = cmd.params.value("scale", 1.0f);
        float mass = cmd.params.value("mass", 1.0f);
        float restitution = cmd.params.value("restitution", 0.2f);
        float friction = cmd.params.value("friction", 0.6f);
        float lifetime = cmd.params.value("lifetime", std::numeric_limits<float>::max());
        int count = std::clamp(cmd.params.value("count", 1), 1, 5000);
        glm::vec3 vel(0.0f);
        if (cmd.params.contains("velocity")) {
            vel.x = cmd.params["velocity"].value("x", 0.0f);
            vel.y = cmd.params["velocity"].value("y", 0.0f);
            vel.z = cmd.params["velocity"].value("z", 0.0f);
        }
        if (!chunkManager || !chunkManager->physicsWorld ||
            !chunkManager->physicsWorld->getVoxelWorld()) {
            response = {{"error", "VoxelDynamicsWorld not available"}};
            return true;
        }
        auto* voxelWorld = chunkManager->physicsWorld->getVoxelWorld();
        glm::vec3 halfExtents(scale * 0.5f);
        float spacing = scale * 1.1f;
        int gridSize = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(count))));
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            int gx = i % gridSize;
            int gy = (i / gridSize) % gridSize;
            int gz = i / (gridSize * gridSize);
            glm::vec3 pos(x + gx * spacing, y + gy * spacing, z + gz * spacing);
            auto* body = voxelWorld->createVoxelBody(pos, halfExtents, mass, restitution, friction);
            if (body) {
                body->linearVelocity = vel;
                body->lifetime = lifetime;
                ++spawned;
            }
        }
        response = {{"success", true}, {"spawned", spawned}, {"system", "voxel"},
                    {"body_count", static_cast<int>(voxelWorld->getBodyCount())},
                    {"position", {{"x", x}, {"y", y}, {"z", z}}}};
        return true;
    }
    if (cmd.action == "clear_voxel_bodies") {
        if (!chunkManager || !chunkManager->physicsWorld ||
            !chunkManager->physicsWorld->getVoxelWorld()) {
            response = {{"error", "VoxelDynamicsWorld not available"}};
            return true;
        }
        auto* voxelWorld = chunkManager->physicsWorld->getVoxelWorld();
        size_t cleared = voxelWorld->getBodyCount();
        voxelWorld->removeAllBodies();
        response = {{"success", true}, {"cleared", cleared}};
        return true;
    }
    if (cmd.action == "clear_dynamics") {
        uint32_t gpuCleared = 0;
        if (gpuParticles && gpuParticles->isInitialized()) {
            gpuCleared = gpuParticles->getActiveParticleCount();
            gpuParticles->despawnAll();
        }
        response = {{"success", true}, {"gpu_cleared", gpuCleared}};
        return true;
    }
    return false;
}
}  // namespace

void registerDebrisCommands(Core::CommandRegistry& reg, std::function<DebrisApiContext()> context) {
    // The debug debris set: one shared dispatcher (moved from Application's legacy if-chain).
    for (const char* action : {"spawn_gpu_particle", "spawn_gpu_lattice", "particle_log", "gpu_physics",
                               "gpu_kinematic_box", "settle_probe", "spawn_voxel_body",
                               "clear_voxel_bodies", "clear_dynamics"}) {
        reg.on(action, [context](const Core::APICommand& cmd, json& response) {
            const DebrisApiContext c = context();
            GpuParticlePhysics* gpu = c.debris ? c.debris->gpu() : nullptr;
            static const std::string kNoRuntime = "no debris runtime";
            const std::string& reason = c.debris ? c.debris->disabledReason() : kNoRuntime;
            if (!handleDebugDynamicSpawnCommand(cmd, response, c.chunks, gpu, reason))
                response = {{"error", "unhandled debris action: " + cmd.action}};
        });
    }

    // Phase 6b: what the GPU reported (sleep / wake / impact) and what the runtime made of it.
    // {"recent": N} also returns the last N events (newest last, N <= 512).
    reg.on("debris_events", [context](const Core::APICommand& cmd, json& r) {
        const DebrisApiContext c = context();
        if (!c.debris || !c.debris->gpu()) { r = {{"error", "GPU debris not available"}}; return; }
        const auto& st = c.debris->eventStats();
        auto* gpu = c.debris->gpu();
        r = {{"success", true},
             {"sleep", st.sleep}, {"wake", st.wake}, {"impact", st.impact},
             {"sounds_impact", st.soundsImpact}, {"sounds_settle", st.soundsSettle},
             {"gpu_events_total", gpu->eventsTotal()}, {"gpu_events_dropped", gpu->eventsDropped()},
             {"settled", c.debris->settledCount()}};
        // Phase 6c water tiles (the water directory covers the occupancy window's XZ).
        const auto& ws = c.debris->waterStats();
        r["water"] = {{"ready", ws.ready}, {"tiles_uploaded", ws.tilesUploaded}, {"tiles_cached", ws.tilesCached},
                      {"tiles_pending", ws.tilesPending}, {"overflow", ws.overflow},
                      {"min_chunk", {ws.minChunk.x, ws.minChunk.y}}};
        const int n = std::clamp(cmd.params.value("recent", 0), 0, 512);
        if (n > 0) {
            json list = json::array();
            const auto& rec = c.debris->recentEvents();
            const size_t first = rec.size() > static_cast<size_t>(n) ? rec.size() - n : 0;
            for (size_t k = first; k < rec.size(); ++k) {
                const auto& e = rec[k];
                list.push_back({{"slot", e.slot}, {"type", e.type == DebrisShared::DEBRIS_EVENT_SLEEP ? "sleep" :
                                                           e.type == DebrisShared::DEBRIS_EVENT_WAKE  ? "wake" : "impact"},
                                {"pos", {e.position.x, e.position.y, e.position.z}}, {"scale", e.scale},
                                {"speed", e.speed}, {"material", GpuParticlePhysics::materialNameOf(e.materialIndex)}});
            }
            r["recent"] = list;
        }
    });

    // Phase 6b: gather settled rubble near a point (finite physical items: credited by VOLUME, a
    // 1/3 piece is 1/27 of a cube). {x,y,z, radius (m, <= 16, default 2.5), max (pieces, <= 256,
    // default 64), inventory (bool, default true: credit the host inventory)}.
    reg.on("debris_gather", [context](const Core::APICommand& cmd, json& r) {
        const DebrisApiContext c = context();
        if (!c.debris || !c.debris->gpu()) { r = {{"error", "GPU debris not available"}}; return; }
        const glm::vec3 at(cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f), cmd.params.value("z", 0.0f));
        const float radius = std::clamp(cmd.params.value("radius", 2.5f), 0.0f, 16.0f);
        const int   maxP   = std::clamp(cmd.params.value("max", 64), 0, 256);
        const auto g = c.debris->gather(at, radius, maxP);
        const bool credit = cmd.params.value("inventory", true) && static_cast<bool>(c.addToInventory);
        if (credit) for (const auto& [mat, n] : g.items) c.addToInventory(mat, n);
        r = {{"success", true}, {"pieces", g.pieces}, {"items", g.items}, {"carried", g.carried},
             {"radius", radius}, {"credited_inventory", credit}};
    });

    reg.on("apply_damage", [context](const Core::APICommand& cmd, nlohmann::json& r) {
    const DebrisApiContext ctx = context();
    ChunkManager* chunkManager = ctx.chunks;
    GpuParticlePhysics* gpuParticlePhysics = ctx.debris ? ctx.debris->gpu() : nullptr;
    Physics::PhysicsWorld* physicsWorld = ctx.physics;
    Graphics::RenderCoordinator* renderCoordinator = ctx.renderer;
    Core::KinematicVoxelManager* kinematicVoxelManager = ctx.kvm;
    Core::CoherentFragmentManager* coherentFragmentManager = ctx.fragments;
    (void)chunkManager; (void)gpuParticlePhysics; (void)physicsWorld; (void)renderCoordinator;
    (void)kinematicVoxelManager; (void)coherentFragmentManager;
        if (!chunkManager) { r = {{"error", "ChunkManager not available"}}; return; }
        glm::vec3 center(cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f), cmd.params.value("z", 0.0f));
        float radius = cmd.params.value("radius", 4.0f);
        float energy = cmd.params.value("energy", 400.0f);
        std::string type = cmd.params.value("type", std::string("force"));
        glm::vec3 dir(0.0f);
        if (cmd.params.contains("direction")) {
            auto d = cmd.params["direction"];
            dir = glm::vec3(d.value("x", 0.0f), d.value("y", 0.0f), d.value("z", 0.0f));
        }
        float supportY = cmd.params.value("support_y", Phyxel::DamageSystem::NO_SUPPORT);
        bool collapse = cmd.params.value("collapse", true);
        // Coherent topple is OPT-IN (default false) until the DamageSystem-level glue has
        // automated coverage — until then the shipped scatter path stays the default.
        bool coherent = cmd.params.value("coherent", false);
        // Blast SHAPE (§15.6 B). Explicit `radii:{x,y,z}` wins; else a `shape` convenience
        // expands from `radius` + `thickness`: "disc" = thin horizontal slab (the clean
        // tree-fell), "wall" = thin in Z / tall in Y, "line" = thin on both lateral axes.
        // Omitted -> {0,0,0} -> spherical `radius` (back-compat).
        glm::vec3 radii(0.0f);
        if (cmd.params.contains("radii")) {
            auto rr = cmd.params["radii"];
            radii = glm::vec3(rr.value("x", 0.0f), rr.value("y", 0.0f), rr.value("z", 0.0f));
        } else if (cmd.params.contains("shape")) {
            const std::string shape = cmd.params.value("shape", std::string("sphere"));
            const float th = cmd.params.value("thickness", 0.7f);
            if      (shape == "disc") radii = glm::vec3(radius, th,     radius);
            else if (shape == "wall") radii = glm::vec3(radius, radius, th);
            else if (shape == "line") radii = glm::vec3(radius, th,     th);
            // "sphere" (or anything else) leaves radii {0,0,0} -> scalar radius.
        }
        Phyxel::DamageSystem dmg(chunkManager, gpuParticlePhysics);
        // Coherent collapse: a severed component topples as ONE rigid slab via the
        // persistent CoherentFragmentManager (docs/DestructionSystemV2.md P1.2b). Wire
        // its deps lazily — the voxel world is live by the time damage is applied.
        if (coherent && coherentFragmentManager && physicsWorld && physicsWorld->getVoxelWorld() && kinematicVoxelManager) {
            coherentFragmentManager->setDeps(physicsWorld->getVoxelWorld(), kinematicVoxelManager);
            dmg.setFragmentManager(coherentFragmentManager);
        }
        dmg.setPushExisting(cmd.params.value("push", true));   // test control: false = pre-Phase-4 blast
        auto dmgResult = dmg.applyDamage(center, radius, energy, type, dir, supportY, collapse, coherent, radii);
        // stage_changed: grazed voxels whose damage crossed a VISIBLE stage boundary. Echoed
        // so a caller can assert a pure graze actually moved something -- `grazed` alone says
        // only that a hit landed, never that the surface now looks different (3.7).
        // `debris` counts pieces actually queued on the GPU; without a solver they are refused.
        const bool gpuDebris = gpuParticlePhysics && gpuParticlePhysics->isInitialized();
        r = {{"success", true}, {"broken", dmgResult.voxelsBroken},
             {"grazed", dmgResult.voxelsGrazed},
             {"debris", gpuDebris ? dmgResult.debrisSpawned : 0},
             {"debris_refused", gpuDebris ? 0 : dmgResult.debrisSpawned},
             {"stage_changed", dmgResult.voxelsStageChanged},
             {"coherent_bodies", (coherentFragmentManager ? coherentFragmentManager->count() : 0)},
             // Phase 4: the blast also pushed what already moves.
             {"push", {{"impulse", dmgResult.impulse}, {"radius", dmgResult.impulseRadius},
                       {"gpu_queued", dmgResult.impulseQueued},
                       {"cpu_bodies", dmgResult.cpuBodiesPushed}}}};
    });

    reg.on("physics_impulse", [context](const Core::APICommand& cmd, nlohmann::json& r) {
    const DebrisApiContext ctx = context();
    ChunkManager* chunkManager = ctx.chunks;
    GpuParticlePhysics* gpuParticlePhysics = ctx.debris ? ctx.debris->gpu() : nullptr;
    Physics::PhysicsWorld* physicsWorld = ctx.physics;
    Graphics::RenderCoordinator* renderCoordinator = ctx.renderer;
    Core::KinematicVoxelManager* kinematicVoxelManager = ctx.kvm;
    Core::CoherentFragmentManager* coherentFragmentManager = ctx.fragments;
    (void)chunkManager; (void)gpuParticlePhysics; (void)physicsWorld; (void)renderCoordinator;
    (void)kinematicVoxelManager; (void)coherentFragmentManager;
        const glm::vec3 c(cmd.params.value("x", 0.0f), cmd.params.value("y", 0.0f), cmd.params.value("z", 0.0f));
        const float radius = cmd.params.value("radius", 4.0f);
        const float J      = cmd.params.value("impulse", 0.0f);
        const float upBias = cmd.params.value("up_bias", 0.0f);
        const std::string worlds = cmd.params.value("worlds", std::string("both"));
        if (worlds != "gpu" && worlds != "cpu" && worlds != "both") {
            r = {{"error", "worlds must be gpu, cpu or both"}};
            return;
        }
        const bool cone = cmd.params.contains("direction");
        glm::vec3 dir(0.0f, 0.0f, -1.0f);
        if (cone) {
            const auto& d = cmd.params["direction"];
            dir = glm::vec3(d.value("x", 0.0f), d.value("y", 0.0f), d.value("z", 0.0f));
        }
        const float halfDeg = cmd.params.value("half_angle_deg", 30.0f);
        GpuParticlePhysics::ImpulseQueued q;
        const bool gpuOk = gpuParticlePhysics && gpuParticlePhysics->isInitialized();
        if (worlds != "cpu" && gpuOk)
            q = cone ? gpuParticlePhysics->applyConeImpulse(c, dir, halfDeg, radius, J, upBias)
                     : gpuParticlePhysics->applyRadialImpulse(c, radius, J, upBias);
        // The CPU half uses the same clamped values the GPU half reports (or clamps alike).
        const float cr  = std::clamp(radius, 0.1f, DebrisShared::IMPULSE_MAX_RADIUS);
        const float cj  = std::max(J, 0.0f);
        const float cub = std::clamp(upBias, 0.0f, 1.0f);
        const float ch  = std::clamp(halfDeg, 0.0f, 90.0f);
        const glm::vec3 axis = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec3(0.0f, 0.0f, -1.0f);
        int cpu = 0;
        if (worlds != "gpu" && physicsWorld && physicsWorld->getVoxelWorld())
            cpu = physicsWorld->getVoxelWorld()->applyImpulse(c, cr, cj, cub, axis,
                      cone ? std::cos(glm::radians(ch)) : DebrisShared::IMPULSE_RADIAL);
        r = {{"success", true}, {"queued", q.queued}, {"gpu_available", gpuOk},
             {"radius", cr}, {"impulse", cj}, {"up_bias", cub}, {"worlds", worlds},
             {"cone", cone}, {"half_angle_deg", cone ? ch : 0.0f},
             {"cpu_bodies_pushed", cpu},
             {"gpu_pending", gpuOk ? gpuParticlePhysics->pendingImpulses() : 0u},
             {"gpu_overflow", gpuOk ? gpuParticlePhysics->impulseOverflow() : 0u}};
    });

    reg.on("occupancy_diff", [context](const Core::APICommand& cmd, nlohmann::json& r) {
    const DebrisApiContext ctx = context();
    ChunkManager* chunkManager = ctx.chunks;
    GpuParticlePhysics* gpuParticlePhysics = ctx.debris ? ctx.debris->gpu() : nullptr;
    Physics::PhysicsWorld* physicsWorld = ctx.physics;
    Graphics::RenderCoordinator* renderCoordinator = ctx.renderer;
    Core::KinematicVoxelManager* kinematicVoxelManager = ctx.kvm;
    Core::CoherentFragmentManager* coherentFragmentManager = ctx.fragments;
    (void)chunkManager; (void)gpuParticlePhysics; (void)physicsWorld; (void)renderCoordinator;
    (void)kinematicVoxelManager; (void)coherentFragmentManager;
        // DebrisInteractionPlan 1c step 6: the three copies of static occupancy, compared over a
        // region at micro resolution — the chunk STORE (what was placed), the physics GRID (what
        // characters/furniture collide with) and the packed POOL (what lighting and GPU debris
        // see). Every settle-bench scenario asserts 0 mismatches. Region in world cubes, <= 64^3.
        // The store side counts what the grid's full rebuild counts: visible cubes, and sub-voxels
        // that are neither broken nor invisible.
        if (!chunkManager || !renderCoordinator) { r = {{"error", "no world"}}; return; }
        const glm::ivec3 a(cmd.params.value("x1", 0), cmd.params.value("y1", 0), cmd.params.value("z1", 0));
        const glm::ivec3 b(cmd.params.value("x2", 0), cmd.params.value("y2", 0), cmd.params.value("z2", 0));
        const glm::ivec3 lo = glm::min(a, b), hi = glm::max(a, b);
        const glm::ivec3 ext = hi - lo + 1;
        if (static_cast<int64_t>(ext.x) * ext.y * ext.z > 64LL * 64 * 64) {
            r = {{"error", "region larger than 64^3 cubes"}, {"cubes", static_cast<int64_t>(ext.x) * ext.y * ext.z}};
            return;
        }
        using Bits = std::bitset<729>;
        auto microIdx = [](const glm::ivec3& sp, const glm::ivec3& mp) {
            const glm::ivec3 m = sp * 3 + mp;
            return m.x * 81 + m.y * 9 + m.z;
        };
        int cells = 0, absent = 0, unknown = 0, cellMis = 0, subMis = 0, gridMis = 0;
        json bad = json::array();
        for (int x = lo.x; x <= hi.x; ++x) for (int y = lo.y; y <= hi.y; ++y) for (int z = lo.z; z <= hi.z; ++z) {
            const glm::ivec3 wc(x, y, z);
            Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord(wc));
            if (!ch) { ++absent; continue; }
            ++cells;
            const glm::ivec3 lp = ChunkManager::worldToLocalCoord(wc);
            // STORE.
            Bits store;
            if (ch->visibleSolidCubeAt(lp)) store.set();
            else {
                for (Subcube* sc : ch->getStaticSubcubesAt(lp)) {
                    if (!sc || sc->isBroken() || !sc->isVisible()) continue;
                    for (int m = 0; m < 27; ++m)
                        store.set(microIdx(sc->getLocalPosition(), {m / 9, (m / 3) % 3, m % 3}));
                }
                for (int s = 0; s < 27; ++s) {
                    const glm::ivec3 sp(s / 9, (s / 3) % 3, s % 3);
                    for (Microcube* mc : ch->getMicrocubesAt(lp, sp))
                        if (mc && !mc->isBroken() && mc->isVisible())
                            store.set(microIdx(sp, mc->getMicrocubeLocalPosition()));
                }
            }
            // GRID (the same walk buildLightOccupancy and the cell probe use).
            const auto& grid = ch->getOccupancyGrid();
            Bits gridBits;
            if (grid.isCubeFilled(lp)) {
                if (!grid.isSubdivided(lp)) gridBits.set();
                else for (int m = 0; m < 729; ++m) {
                    const int mx = m / 81, my = (m / 9) % 9, mz = m % 9;
                    const glm::ivec3 s(mx / 3, my / 3, mz / 3), mm(mx % 3, my % 3, mz % 3);
                    if (grid.isSubcubeFilled(lp, s) &&
                        (!grid.isSubcubeSubdivided(lp, s) || grid.isMicrocubeFilled(lp, s, mm)))
                        gridBits.set(m);
                }
            }
            // POOL.
            if (!renderCoordinator->lightOccupancyKnownAt(wc * 9)) { ++unknown; continue; }
            Bits pool;
            const auto pc = renderCoordinator->lightOccupancyCubeAt(wc);
            if (pc == Graphics::CubeOccupancy::Solid) pool.set();
            else if (pc == Graphics::CubeOccupancy::Mixed)
                for (int m = 0; m < 729; ++m)
                    if (renderCoordinator->lightOccupancySolidAt(wc * 9 + glm::ivec3(m / 81, (m / 9) % 9, m % 9)))
                        pool.set(m);

            const bool poolOk = pool == store, gridOk = gridBits == store;
            if (!gridOk) ++gridMis;
            if (poolOk && gridOk) continue;
            if (!poolOk) {
                ++cellMis;
                const Bits diff = pool ^ store;
                for (int s = 0; s < 27; ++s) {
                    const glm::ivec3 sp(s / 9, (s / 3) % 3, s % 3);
                    bool any = false;
                    for (int m = 0; m < 27 && !any; ++m) any = diff.test(microIdx(sp, {m / 9, (m / 3) % 3, m % 3}));
                    subMis += any;
                }
            }
            if (bad.size() < 20)
                bad.push_back({{"cell", {x, y, z}}, {"store", store.count()}, {"grid", gridBits.count()},
                               {"pool", pool.count()}});
        }
        r = {{"region", {{"min", {lo.x, lo.y, lo.z}}, {"max", {hi.x, hi.y, hi.z}}}},
                    {"cells_checked", cells}, {"cells_no_chunk", absent},
                    {"cells_pool_unknown", unknown},       // not resident in the pool: debris HOLDS there
                    {"cell_mismatches", cellMis},          // store vs pool, cube cells
                    {"subcube_mismatches", subMis},        // store vs pool, 1/3 cells
                    {"grid_mismatches", gridMis},          // store vs physics grid, cube cells
                    // A loaded chunk not yet in the pool is NOT agreement (debris holds there).
                    {"agrees", cellMis == 0 && gridMis == 0 && unknown == 0},
                    {"first_mismatches", bad}};             // micro counts out of 729 per copy
        return;

    });
}

}  // namespace Phyxel
