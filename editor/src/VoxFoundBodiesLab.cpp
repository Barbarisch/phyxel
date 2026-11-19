// docs/VoxelFoundationPlan.md F5.3 -- POST /api/debug/voxfound_bodies: the live check of breaking + bodies drawn with
// one transform (prediction: docs/evidence/voxfound_f53_prediction.txt, G1-G5).
//
// A §8 lab rig (FractureLab, hand-built shapes -- not generator output) is placed on a drawn floor inside one chunk, the
// hit breaks it (F4.1 fractureReference), F5.1 extracts the loose pieces, and the CPU REFERENCE solver (F5.2a
// BodySimRef) moves them at a fixed 60 Hz. RIG DIFFERENCE, stated: the shipped path is the GPU solver; F5.2b's dispatch
// floor (8-19 ms per tick) makes it unusable live until F5.5. What stays in the world is drawn through today's preview
// path (FaceBuilderGpu, voxfound_face.vert); the bodies through voxfound_body.vert (one transform each).
//
// Body: {"rig":"stone_wall"|"brick_wall"|"beam"|"glass"|"lintel", "energy": E/T, "hit":[x,y,z] (rig voxels, stone wall),
//        "impulse": N s, "at":[X,Y,Z] (world metres of the rig's voxel (0,0,0)), "pad": floor margin m (default 4),
//        "run": bool}
//    or  {"run":bool} {"ticks":N} {"shown":bool} {"cast":bool} {"as_world":bool} {"mutate":bool} {"status":true}
//    or  {"clear":true}.
// F5.4 (docs/evidence/voxfound_f54_prediction.txt L1-L6): {"rest":{"on":bool,"budget":N,"keep_m":M,"rest_ticks":T}}
//    sets the D6 policy (it runs after every tick while on; the player = the camera); {"bake_now":{...same keys}} runs
//    one pass now with those values. A bake writes the world grid, rebuilds the preview area and compacts the body
//    faces by copying ranges (no re-meshing). Responses carry "ledger": per material world + bodies + crumbs.

#include "Application.h"

#include <algorithm>
#include <chrono>

#include "core/AssetManager.h"
#include "core/MaterialRegistry.h"
#include "graphics/RenderCoordinator.h"

namespace Phyxel {

namespace {

const char* kLabTextures[] = {"", "Stone", "Bricks", "Wood", "Glass", "Wood", "StoneTiles"};   // lab ids -> engine names
constexpr uint8_t kFloorMat = 6;                                                             // a copy of Stone

}  // namespace

void Application::voxFoundBodiesPublish() {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    lab.draw.poses.clear();
    if (lab.sim)
        for (const SimBody& b : lab.sim->bodies()) lab.draw.poses.push_back(worldPoseOf(b, lab.areaOriginM));
}

// The preview grid: what stays in the world, plus (as_world) the bodies' voxels written back at their rest place.
bool Application::voxFoundBodiesRebuildWorld(std::string* err) {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    VoxelGrid g = lab.world;
    if (lab.asWorld && lab.sim)
        for (const SimBody& b : lab.sim->bodies())
            for (size_t i = 0; i < b.src.grid.size(); ++i)
                if (b.src.grid.mat[i]) g.mat[g.idx(b.src.origin + b.src.grid.pos(i))] = b.src.grid.mat[i];
    std::vector<PackedFace> packed;
    if (!voxFoundBuilder->build(g, Outside::Air, packed, err)) return false;
    voxFoundPreview.faceTex.assign(kFaceTexWords, 0u);
    for (size_t m = 1; m < sizeof(kLabTextures) / sizeof(kLabTextures[0]); ++m)
        for (int f = 0; f < 6; ++f)
            voxFoundPreview.faceTex[m * 6 + size_t(f)] = Core::MaterialRegistry::instance().getTextureIndex(kLabTextures[m], f);
    voxFoundPreview.faces = packed;
    voxFoundPreview.sprigs.clear();
    voxFoundPreview.chunks.clear();
    voxFoundPreview.clusters.clear();
    voxFoundPreview.levels.clear();
    voxFoundEdit.clear();
    voxFoundPreview.originMetres = lab.areaOriginM;
    ++voxFoundPreview.revision;
    renderCoordinator->setVoxFoundPreview(&voxFoundPreview);
    lab.previewFaces = packed.size();
    return true;
}

// One D6 rest pass (F5.4): bake, then compact the body faces and rebuild the preview area if anything baked.
bool Application::voxFoundBodiesRest(bool force, const Core::VoxFound::RestParams* override, std::string* err) {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    if (!lab.sim || (!force && !lab.restOn)) return true;
    const RestParams& rp = override ? *override : lab.rest;
    const glm::dvec3 player = camera ? glm::dvec3(camera->getPosition()) - lab.areaOriginM : glm::dvec3(0.0);
    RestStats st;
    const size_t n0 = lab.sim->bodies().size();
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<size_t> baked = restPass(*lab.sim, lab.world, player, rp, &st);
    lab.restLast = st;
    ++lab.bakePasses;
    lab.restTotal.baked += st.baked;
    lab.restTotal.rigid += st.rigid;
    lab.restTotal.refused += st.refused;
    lab.restTotal.woken += st.woken;
    lab.restTotal.overwritten += st.overwritten;
    lab.restTotal.voxels += st.voxels;
    lab.restTotal.sumShiftM += st.sumShiftM;
    lab.restTotal.maxShiftM = std::max(lab.restTotal.maxShiftM, st.maxShiftM);
    if (baked.empty()) return true;
    // compact the face array: survivors' ranges copied in order, body index renumbered (faces are not rebuilt)
    const auto t1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> gone(n0, 0);
    for (size_t i : baked) gone[i] = 1;
    std::vector<BodyFace> faces;
    std::vector<uint32_t> first, count;
    faces.reserve(lab.draw.faces.size());
    for (size_t i = 0; i < n0; ++i) {
        if (gone[i]) continue;
        const uint32_t idx = uint32_t(first.size());
        first.push_back(uint32_t(faces.size()));
        count.push_back(lab.faceCount[i]);
        for (uint32_t k = lab.faceFirst[i]; k < lab.faceFirst[i] + lab.faceCount[i]; ++k) {
            BodyFace f = lab.draw.faces[k];
            f.body = idx;
            faces.push_back(f);
        }
    }
    lab.draw.faces.swap(faces);
    lab.faceFirst.swap(first);
    lab.faceCount.swap(count);
    ++lab.draw.facesRevision;
    const auto t2 = std::chrono::steady_clock::now();
    voxFoundBodiesPublish();
    const bool ok = voxFoundBodiesRebuildWorld(err);
    const auto t3 = std::chrono::steady_clock::now();
    lab.lastBakeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    lab.lastCompactMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    lab.lastRebuildMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    return ok;
}

void Application::stepVoxFoundBodies(float dt) {
    auto& lab = voxFoundBodiesLab;
    if (!lab.sim || !lab.running) return;
    lab.accum += double(dt);
    const double h = lab.sim->params().dt;
    int n = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (lab.accum >= h && n < 4) {   // at most 4 ticks per frame (stated; slower than real time if the frame is slow)
        lab.sim->step();
        lab.accum -= h;
        ++lab.tick;
        ++n;
        std::string err;
        voxFoundBodiesRest(false, nullptr, &err);
    }
    if (n == 4) lab.accum = 0.0;
    if (n) {
        lab.lastStepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        voxFoundBodiesPublish();
    }
}

void Application::handleVoxFoundBodies(const Core::APICommand& cmd, nlohmann::json& response) {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    const auto& p = cmd.params;
    if (!vulkanDevice || !renderCoordinator) {
        response = {{"error", "no Vulkan device / renderer"}};
        return;
    }
    if (p.value("clear", false)) {
        renderCoordinator->setVoxFoundBodies(nullptr);
        renderCoordinator->setVoxFoundPreview(nullptr);
        lab.sim.reset();
        lab.draw = {};
        voxFoundPreview.faces.clear();
        ++voxFoundPreview.revision;
        response = {{"cleared", true}};
        return;
    }
    if (!voxFoundBuilder) {
        voxFoundBuilder = std::make_unique<FaceBuilderGpu>();
        std::string gerr;
        const std::string spv = Core::AssetManager::instance().resolveShader("voxfound_faces.comp.spv");
        if (!voxFoundBuilder->init(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice(),
                                   vulkanDevice->getAsyncComputeQueue(), vulkanDevice->getAsyncComputeQueueFamily(),
                                   std::filesystem::path(spv).parent_path().string(), &gerr)) {
            voxFoundBuilder.reset();
            response = {{"error", "FaceBuilderGpu init: " + gerr}};
            return;
        }
    }
    auto vec3 = [](const nlohmann::json& j) { return glm::dvec3(j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()); };
    if (p.contains("rig")) {
        const std::string name = p["rig"].get<std::string>();
        const float e = p.value("energy", 80.0f);
        Rig rig;
        if (name == "stone_wall") {
            glm::vec3 hp(12, 8, 0);
            if (p.contains("hit")) hp = glm::vec3(vec3(p["hit"]));
            rig = rigStoneWall(hp, e);
        } else if (name == "brick_wall") rig = rigBrickWall(e);
        else if (name == "beam") rig = rigBeam(e);
        else if (name == "glass") rig = rigGlassPane(e);
        else if (name == "lintel") rig = rigColumnLintel(e);
        else if (name == "large_wall") {   // F5.1 / D5's 256 x 128 x 4 stone wall, base fixed, hit at the centre
            rig.name = "large_wall";
            rig.mats = labMaterials();
            rig.grid.voxelM = 0.125f;
            rig.grid.resize({256, 128, 4});
            for (int x = 0; x < 256; ++x)
                for (int y = 0; y < 128; ++y)
                    for (int z = 0; z < 4; ++z) rig.grid.set({x, y, z}, kStone, 0, y == 0);
            rig.hit = {{128, 64, 0}, {0, 0, 1}, e * rig.mats[kStone].toughness, 777, 200.0f};
        } else {
            response = {{"error", "unknown rig " + name + " (stone_wall, brick_wall, beam, glass, lintel, large_wall)"}};
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        // the display + sim grid: the rig on a 2-voxel floor (fixed, its own material id: a copy of Stone drawn as
        // StoneTiles), `pad` metres of floor around it, 2 m of air above
        const int pad = int(std::lround(p.value("pad", 4.0) / double(rig.grid.voxelM)));
        const glm::ivec3 off(pad, 2, pad);
        VoxelGrid g;
        g.voxelM = rig.grid.voxelM;
        g.resize(rig.grid.dim + glm::ivec3(2 * pad, 2 + 16, 2 * pad));
        for (int x = 0; x < g.dim.x; ++x)
            for (int z = 0; z < g.dim.z; ++z)
                for (int y = 0; y < 2; ++y) g.set({x, y, z}, kFloorMat, 0, true);
        const bool dmg = !rig.grid.damage.empty();
        if (dmg) g.damage.assign(g.size(), 0);
        for (size_t i = 0; i < rig.grid.size(); ++i) {
            if (!rig.grid.mat[i]) continue;
            const glm::ivec3 q = rig.grid.pos(i) + off;
            g.set(q, rig.grid.mat[i], rig.grid.part[i], rig.grid.fixed[i] != 0);
            if (dmg) g.damage[g.idx(q)] = rig.grid.damage[i];
        }
        std::vector<FractureMaterial> mats = rig.mats;
        mats.resize(kFloorMat + 1, mats[kStone]);
        mats[kFloorMat] = mats[kStone];
        Hit hit = rig.hit;
        hit.point += glm::vec3(off);
        hit.impulse = p.value("impulse", 20.0f);
        const FractureResult r = fractureReference(g, hit, mats);
        VoxelGrid world = g;
        const Extraction ex = extractBodies(world, r, hit, mats);
        lab.sim.reset();   // BodySimRef holds a reference to the world grid: build it on lab.world below
        lab.world = world;
        lab.mats = mats;
        lab.areaOriginM = (p.contains("at") ? vec3(p["at"]) : glm::dvec3(0.0)) - glm::dvec3(off) * double(g.voxelM);
        BodySimParams prm;
        prm.groundBelow = true;   // below the floor (never reached: the floor is voxels)
        lab.sim = std::make_unique<BodySimRef>(lab.world, lab.mats, prm);
        for (const Body& b : ex.bodies) lab.sim->add(b, b.vel, b.angVel);
        const BodyDrawSet set = buildBodyFaces(lab.sim->bodies());
        lab.draw.faces = set.faces;
        lab.faceFirst = set.first;
        lab.faceCount = set.count;
        lab.crumbMat.assign(mats.size(), 0);
        for (const Crumb& c : ex.crumbs) ++lab.crumbMat[c.mat];
        lab.restTotal = {};
        lab.restLast = {};
        lab.bakePasses = 0;
        lab.restOn = false;
        ++lab.draw.facesRevision;
        lab.refused = set.refused;
        lab.tick = 0;
        lab.accum = 0.0;
        lab.running = p.value("run", false);
        lab.asWorld = false;
        lab.crumbs = ex.crumbs.size();
        lab.rigName = name;
        lab.draw.shown = true;
        lab.draw.castShadows = true;
        lab.draw.mutate = false;
        voxFoundBodiesPublish();
        std::string err;
        if (!voxFoundBodiesRebuildWorld(&err)) {
            response = {{"error", err}};
            return;
        }
        renderCoordinator->setVoxFoundBodies(&lab.draw);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        response = {{"provenance", "hand-built lab rig FractureLab " + name + " (E/T " + std::to_string(e) +
                                       ") on a 2-voxel floor; F4.1 fractureReference + F5.1 extractBodies; "
                                       "motion by the CPU reference solver BodySimRef (rig difference: shipped = GPU)"},
                    {"bodies", ex.bodies.size()}, {"crumbs", ex.crumbs.size()}, {"body_faces", set.faces.size()},
                    {"refused", set.refused}, {"preview_faces", lab.previewFaces},
                    {"grid", {g.dim.x, g.dim.y, g.dim.z}},
                    {"rig_origin_m", {lab.areaOriginM.x + off.x * g.voxelM, lab.areaOriginM.y + off.y * g.voxelM,
                                      lab.areaOriginM.z + off.z * g.voxelM}},
                    {"setup_ms", ms}};
        return;
    }
    if (!lab.sim) {
        response = {{"error", "no rig (send {\"rig\":...} first)"}};
        return;
    }
    if (p.contains("run")) lab.running = p["run"].get<bool>();
    if (p.contains("shown")) lab.draw.shown = p["shown"].get<bool>();
    if (p.contains("cast")) lab.draw.castShadows = p["cast"].get<bool>();
    if (p.contains("mutate")) lab.draw.mutate = p["mutate"].get<bool>();
    auto restOf = [&](const nlohmann::json& j, RestParams r) {
        r.budget = j.value("budget", r.budget);
        r.keepRadiusM = j.value("keep_m", r.keepRadiusM);
        r.restTicks = j.value("rest_ticks", r.restTicks);
        return r;
    };
    if (p.contains("rest")) {
        lab.rest = restOf(p["rest"], lab.rest);
        lab.restOn = p["rest"].value("on", true);
    }
    if (p.contains("ticks")) {
        const int n = p["ticks"].get<int>();
        for (int i = 0; i < n; ++i) {
            lab.sim->step();
            ++lab.tick;
            std::string err;
            if (!voxFoundBodiesRest(false, nullptr, &err)) {
                response = {{"error", err}};
                return;
            }
        }
        voxFoundBodiesPublish();
    }
    if (p.contains("bake_now")) {
        const RestParams r = restOf(p["bake_now"], lab.rest);
        std::string err;
        if (!voxFoundBodiesRest(true, &r, &err)) {
            response = {{"error", err}};
            return;
        }
    }
    if (p.contains("as_world")) {
        lab.asWorld = p["as_world"].get<bool>();
        lab.draw.shown = !lab.asWorld;
        std::string err;
        if (!voxFoundBodiesRebuildWorld(&err)) {
            response = {{"error", err}};
            return;
        }
    }
    const auto st = renderCoordinator->voxFoundBodyStats();
    nlohmann::json centres = nlohmann::json::array();
    size_t awake = 0;
    for (const SimBody& b : lab.sim->bodies()) {
        awake += !b.asleep;
        const glm::dvec3 c = lab.areaOriginM + b.pos;
        centres.push_back({c.x, c.y, c.z});
    }
    response = {{"rig", lab.rigName}, {"tick", lab.tick}, {"t_s", lab.tick * lab.sim->params().dt},
                {"running", lab.running}, {"bodies", lab.sim->bodies().size()}, {"awake", awake},
                {"crumbs", lab.crumbs}, {"shown", lab.draw.shown}, {"cast", lab.draw.castShadows},
                {"as_world", lab.asWorld}, {"mutate", lab.draw.mutate}, {"body_faces", lab.draw.faces.size()},
                {"preview_faces", lab.previewFaces}, {"step_ms", lab.lastStepMs},
                {"max_penetration_mm", lab.sim->currentMaxPenetration() * 1000.0},
                {"render", {{"bodies", st.bodies}, {"faces", st.faces}, {"drawn_faces", st.drawnFaces},
                            {"cast_faces_mid", st.castFaces[0]}, {"cast_faces_near", st.castFaces[1]},
                            {"pose_ms", st.poseMs}, {"face_upload_ms", st.faceUploadMs},
                            {"face_uploads", st.faceUploads}}},
                {"centres_m", centres}};
    // F5.4: the conservation ledger (per material: world + bodies + crumbs) and the rest pass's numbers
    nlohmann::json ledger = nlohmann::json::object();
    std::vector<size_t> perMat(lab.mats.size(), 0);
    for (uint8_t m : lab.world.mat)
        if (m) ++perMat[m];
    for (const SimBody& b : lab.sim->bodies())
        for (uint8_t m : b.src.grid.mat)
            if (m) ++perMat[m];
    for (size_t m = 0; m < perMat.size() && m < lab.crumbMat.size(); ++m) perMat[m] += lab.crumbMat[m];
    for (size_t m = 1; m < perMat.size(); ++m) ledger[std::to_string(m)] = perMat[m];
    const RestStats& rt = lab.restTotal;
    response["ledger"] = ledger;
    response["rest"] = {{"on", lab.restOn}, {"budget", lab.rest.budget}, {"keep_m", lab.rest.keepRadiusM},
                        {"rest_ticks", lab.rest.restTicks}, {"passes", lab.bakePasses},
                        {"baked", rt.baked}, {"rigid", rt.rigid}, {"refused", rt.refused}, {"woken", rt.woken},
                        {"overwritten", rt.overwritten}, {"voxels", rt.voxels},
                        {"mean_shift_m", rt.voxels ? rt.sumShiftM / double(rt.voxels) : 0.0},
                        {"max_shift_m", rt.maxShiftM},
                        {"last", {{"considered", lab.restLast.considered}, {"over", lab.restLast.over},
                                  {"baked", lab.restLast.baked}, {"kept_near", lab.restLast.keptNear},
                                  {"kept_awake", lab.restLast.keptAwake}, {"kept_recent", lab.restLast.keptRecent},
                                  {"refused", lab.restLast.refused}, {"bake_ms", lab.lastBakeMs},
                                  {"compact_ms", lab.lastCompactMs}, {"rebuild_ms", lab.lastRebuildMs}}}};
}

}  // namespace Phyxel
