// docs/VoxelFoundationPlan.md F5.3 -- POST /api/debug/voxfound_bodies: the live check of breaking + bodies drawn with
// one transform (prediction: docs/evidence/voxfound_f53_prediction.txt, G1-G5).
//
// A §8 lab rig (FractureLab, hand-built shapes -- not generator output) is placed on a drawn floor inside one chunk, the
// hit breaks it (F4.1 fractureReference), F5.1 extracts the loose pieces, and a solver moves them at a fixed 60 Hz:
// F5.5b (docs/evidence/voxfound_f55_prediction.txt): {"solver":"gpu"} (default, the shipped path: BodySimGpu on the
// async compute queue, one step in flight, poses one frame behind) or {"solver":"cpu"} (F5.2a's BodySimRef, the
// control). What stays in the world is drawn through today's preview path (FaceBuilderGpu, voxfound_face.vert); the
// bodies through voxfound_body.vert (one transform each), culled per body ({"cull":bool}, default on).
//
// Body: {"rig":"stone_wall"|"brick_wall"|"beam"|"glass"|"lintel", "energy": E/T, "hit":[x,y,z] (rig voxels, stone wall),
//        "impulse": N s, "at":[X,Y,Z] (world metres of the rig's voxel (0,0,0)), "pad": floor margin m (default 4),
//        "run": bool}
//    + "solver": "gpu" | "cpu" (F5.5b)
//    or  {"run":bool} {"ticks":N} {"shown":bool} {"cast":bool} {"cull":bool} {"as_world":bool} {"mutate":bool}
//        {"status":true} ("pen":false skips the CPU max-penetration measure: seconds at ~1,000 bodies, on the
//        main thread -- F5.5b live sampling)
//    or  {"clear":true}.
// F5.5c (voxfound_f55_prediction.txt H1-H5, L1-L3): with the rig {"preview":"region"|"area"} (region default: the
//    world kept as a RegionEdit on the engine's chunk grid, a bake edits it per body; area = F5.5b's whole-area build)
//    and {"gpu_edit":bool} (default true: BodySimGpu::edit after a bake; false = the full reload); later
//    {"preview":...} switches the path on the same bodies, {"gpu_edit":bool}; "rest" takes "cap" (bakes per pass);
//    status {"verify":true} re-builds every region chunk on the GPU and compares with the incremental faces.
// F5.5e: {"profile":bool} -- BodySimGpu's per-pass device profile on the synchronous {"ticks":N} (one tick per
//    submit while on; reset when turned on); the status carries "profile" (mean / min ms per group, ticks).
//    F5.5f: {"world_split":bool} adds the world pass's diagnostic copies to the profile (w_count / w_write / w_bucket /
//    w_spread, each its own group; bit-equal) and the per-body maxima in the stats (dg_*).
//    F5.5g: {"world_once":bool} -- the world pass tests each item once (staged, then copied; default on, bit-equal);
//    false = the F5.5f two-test pass (the A/B control). The stats carry world_redo (threads past the stage, re-tested).
//    F5.5h: {"world_spread":bool} -- a big body's world items over more than one workgroup (default on, bit-equal);
//    false = F5.5g's one workgroup per body. The stats carry spread_fallback (bodies that ran F5.5g's pass instead).
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
    lab.draw.halfM.clear();
    if (lab.sim)
        for (const SimBody& b : lab.sim->bodies()) {
            lab.draw.poses.push_back(worldPoseOf(b, lab.areaOriginM));
            lab.draw.halfM.push_back(bodyHalfM(b));
        }
}

// F5.5b: (re)load the GPU solver with the lab's world and bodies (made lazily on the async compute queue)
bool Application::voxFoundBodiesGpuLoad(std::string* err) {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    if (!lab.gpu) {
        lab.gpu = std::make_unique<BodySimGpu>();
        const std::string spv = Core::AssetManager::instance().resolveShader("voxfound_bodies.comp.spv");
        if (!lab.gpu->init(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice(),
                           vulkanDevice->getAsyncComputeQueue(), vulkanDevice->getAsyncComputeQueueFamily(),
                           std::filesystem::path(spv).parent_path().string(), err)) {
            lab.gpu.reset();
            return false;
        }
    }
    lab.gpuLoaded = false;
    if (lab.sim->bodies().empty()) return true;   // nothing to move (every body baked)
    if (!lab.gpu->load(lab.world, lab.mats, lab.sim->bodies(), lab.sim->params(), err)) return false;
    lab.gpuLoaded = true;
    return true;
}

// F5.5b: a GPU step of `ticks` landed in lab.sim's bodies: the rest policy once (the CPU path runs it after every tick),
// then the poses
void Application::voxFoundBodiesGpuDone(int ticks) {
    auto& lab = voxFoundBodiesLab;
    lab.tick += ticks;
    if (lab.gpu->lastGpuMs() >= 0.0) lab.gpuMsPerTick = lab.gpu->lastGpuMs() / ticks;
    std::string err;
    if (!voxFoundBodiesRest(false, nullptr, &err)) LOG_ERROR("VoxFound", "bodies rest pass: " + err);
    voxFoundBodiesPublish();
}

// F5.5b, the live GPU path, once per frame: take the step in flight if it finished, then submit the ticks owed (<= 4,
// as the CPU path; time beyond 4 ticks is dropped -- slower than real time if the solver cannot keep up)
void Application::voxFoundBodiesGpuStep(float dt) {
    auto& lab = voxFoundBodiesLab;
    if (!lab.gpu) return;
    const auto t0 = std::chrono::steady_clock::now();
    std::string err;
    bool active = lab.running;
    if (lab.gpu->stepPending()) {
        active = true;
        const int n = lab.gpu->pendingTicks();
        const int r = lab.gpu->stepPoll(&lab.sim->bodies(), false, &err);
        if (r < 0) {
            LOG_ERROR("VoxFound", "bodies GPU step: " + err);
            lab.running = false;
            return;
        }
        if (r == 1) {   // F5.5d X3: how long a step takes from submit to found done, and how often it is not done
            ++lab.pollsDone;
            const float ms = float(std::chrono::duration<double, std::milli>(t0 - lab.stepSubmitAt).count());
            if (lab.stepLatencyMs.size() < 600) lab.stepLatencyMs.push_back(ms);
            else lab.stepLatencyMs[lab.stepLatencyNext] = ms;
            lab.stepLatencyNext = (lab.stepLatencyNext + 1) % 600;
            voxFoundBodiesGpuDone(n);
        } else {
            ++lab.pollsWaiting;
        }
    }
    if (lab.running && lab.gpuLoaded) {
        const double h = lab.sim->params().dt;
        lab.accum += double(dt);
        if (!lab.gpu->stepPending()) {
            const int n = std::min(int(lab.accum / h), 4);
            if (n > 0) {
                if (!lab.gpu->stepBegin(n, &err)) {
                    LOG_ERROR("VoxFound", "bodies GPU stepBegin: " + err);
                    lab.running = false;
                    return;
                }
                lab.accum -= n * h;
                lab.stepSubmitAt = std::chrono::steady_clock::now();
            }
        }
        lab.accum = std::min(lab.accum, 4.0 * h);
    }
    if (active) {
        const float ms = float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (lab.simCpuMs.size() < 600) lab.simCpuMs.push_back(ms);
        else lab.simCpuMs[lab.simCpuNext] = ms;
        lab.simCpuNext = (lab.simCpuNext + 1) % 600;
    }
}

// F5.5c: the lab's world as a RegionEdit -- 256^3 chunks on the engine's 32 m chunk grid (lab voxel p is world 1/8 m
// voxel originVoxel + p), so the chunk cull and the occlusion walk see real chunk coordinates and the texture phase
// follows world position. A bake then edits it incrementally (voxFoundBodiesRest).
bool Application::voxFoundBodiesBuildRegion(const Core::VoxFound::VoxelGrid& g, std::string* err) {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    const glm::ivec3 cBase(glm::floor(glm::dvec3(lab.originVoxel) / double(kChunkM * kVoxelsPerM)));
    const glm::ivec3 off = lab.originVoxel - cBase * (kChunkM * kVoxelsPerM);
    glm::ivec3 hi;
    std::vector<RegionChunk> rcs = areaRegionChunks(g, off, &hi);
    if (rcs.empty()) {
        if (err) *err = "voxfound_bodies: the lab grid made no region chunks";
        return false;
    }
    voxFoundEdit.clear();
    voxFoundBuilder->dropResident();
    voxFoundEdit.begin(cBase, cBase + hi, 4, true, 0u);
    CardSet see;   // F2.10's sight set: transparent materials (no cards in the lab)
    for (size_t m = 1; m < sizeof(kLabTextures) / sizeof(kLabTextures[0]); ++m) {
        const auto* md = Core::MaterialRegistry::instance().getMaterial(kLabTextures[m]);
        if (md && md->alpha < 0.99f) see.set(uint8_t(m));
    }
    voxFoundEdit.setMaterialSets(CardSet{}, see);
    voxFoundPreview.faceTex.assign(kFaceTexWords, 0u);
    for (size_t m = 1; m < sizeof(kLabTextures) / sizeof(kLabTextures[0]); ++m)
        for (int f = 0; f < 6; ++f)
            voxFoundPreview.faceTex[m * 6 + size_t(f)] = Core::MaterialRegistry::instance().getTextureIndex(kLabTextures[m], f);
    voxFoundPreview.faces.clear();
    voxFoundPreview.sprigs.clear();
    voxFoundPreview.clusters.clear();
    voxFoundPreview.levels.clear();
    voxFoundPreview.levelsAlt.clear();
    voxFoundPreview.chunks.clear();
    voxFoundPreview.exitReach = 0;
    voxFoundPreview.lodSprigLevel = 0;
    voxFoundPreview.lodUseAlt = false;
    size_t faces = 0;
    for (RegionChunk& rc : rcs) {
        rc.chunk += cBase;
        rc.originVoxel += cBase * (kChunkM * kVoxelsPerM);
        std::vector<PackedFace> out;
        if (!voxFoundBuilder->build(rc.grid, Edges(rc.border), out, err)) return false;
        faces += out.size();
        voxFoundEdit.addChunk(rc, out);
        PreviewChunk pc;
        pc.originMetres = glm::dvec3(rc.chunk * kChunkM);
        pc.chunk = rc.chunk;
        voxFoundPreview.chunks.push_back(pc);
    }
    voxFoundPreview.anchorMetres = glm::dvec3(cBase * kChunkM);
    voxFoundPreview.originMetres = voxFoundPreview.anchorMetres;
    voxFoundPreview.clusterBlocks = 4;
    voxFoundPreview.shown = true;
    lab.leafTex.assign(sizeof(kLabTextures) / sizeof(kLabTextures[0]), 0);
    voxFoundEdit.assemble(voxFoundPreview, lab.leafTex);
    ++voxFoundPreview.revision;
    renderCoordinator->setVoxFoundPreview(&voxFoundPreview);
    lab.previewFaces = faces;
    lab.regionChunks = rcs.size();
    return true;
}

// The preview: what stays in the world, plus (as_world) the bodies' voxels written back at their rest place. A full
// build -- the region (F5.5c, default) or F5.5b's whole-area path ({"preview":"area"}, the control).
bool Application::voxFoundBodiesRebuildWorld(std::string* err) {
    using namespace Core::VoxFound;
    auto& lab = voxFoundBodiesLab;
    VoxelGrid g = lab.world;
    if (lab.asWorld && lab.sim)
        for (const SimBody& b : lab.sim->bodies())
            for (size_t i = 0; i < b.src.grid.size(); ++i)
                if (b.src.grid.mat[i]) g.mat[g.idx(b.src.origin + b.src.grid.pos(i))] = b.src.grid.mat[i];
    ++lab.fullRebuilds;
    if (lab.regionPreview) return voxFoundBodiesBuildRegion(g, err);
    std::vector<PackedFace> packed;
    if (!voxFoundBuilder->build(g, Outside::Air, packed, err)) return false;
    voxFoundEdit.clear();
    voxFoundPreview.faceTex.assign(kFaceTexWords, 0u);
    for (size_t m = 1; m < sizeof(kLabTextures) / sizeof(kLabTextures[0]); ++m)
        for (int f = 0; f < 6; ++f)
            voxFoundPreview.faceTex[m * 6 + size_t(f)] = Core::MaterialRegistry::instance().getTextureIndex(kLabTextures[m], f);
    voxFoundPreview.faces = packed;
    voxFoundPreview.sprigs.clear();
    voxFoundPreview.chunks.clear();
    voxFoundPreview.clusters.clear();
    voxFoundPreview.levels.clear();
    voxFoundPreview.levelsAlt.clear();
    voxFoundPreview.originMetres = lab.areaOriginM;
    ++voxFoundPreview.revision;
    renderCoordinator->setVoxFoundPreview(&voxFoundPreview);
    lab.previewFaces = packed.size();
    lab.regionChunks = 0;
    return true;
}

// F5.4 / F5.5d: the conservation ledger, per material: world + bodies + crumbs
std::vector<size_t> Application::voxFoundBodiesLedger() const {
    const auto& lab = voxFoundBodiesLab;
    std::vector<size_t> perMat(lab.mats.size(), 0);
    if (!lab.sim) return perMat;
    for (uint8_t m : lab.world.mat)
        if (m && m < perMat.size()) ++perMat[m];
    for (const Core::VoxFound::SimBody& b : lab.sim->bodies())
        for (uint8_t m : b.src.grid.mat)
            if (m && m < perMat.size()) ++perMat[m];
    for (size_t m = 0; m < perMat.size() && m < lab.crumbMat.size(); ++m) perMat[m] += lab.crumbMat[m];
    return perMat;
}

bool Application::voxFoundBodiesRest(bool force, const Core::VoxFound::RestParams* override, std::string* err) {
    auto& lab = voxFoundBodiesLab;
    const bool ok = voxFoundBodiesRestPass(force, override, err);
    if (lab.sim && lab.ledgerEvery && (force || lab.restOn)) {   // F5.5d X1: after every rest call, timed apart
        const auto t0 = std::chrono::steady_clock::now();
        ++lab.ledgerChecks;
        if (voxFoundBodiesLedger() != lab.ledger0) {
            ++lab.ledgerBad;
            if (lab.ledgerFirstBadTick < 0) lab.ledgerFirstBadTick = lab.tick;
        }
        lab.ledgerMsMax = std::max(lab.ledgerMsMax,
                                   std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    return ok;
}

// One D6 rest pass (F5.4): bake, then compact the body faces and update the preview + the GPU solver if anything
// baked. F5.5c: the preview is edited per baked body (RegionEdit::applyCells, then one assemble) when the pass bakes at
// most kIncrementalMax bodies (incremental cost is per body; past that, one full build is cheaper); the GPU solver is
// edited (BodySimGpu::edit: changed cells + survivors) instead of reloaded. lastHitchMs = the whole call.
bool Application::voxFoundBodiesRestPass(bool force, const Core::VoxFound::RestParams* override, std::string* err) {
    using namespace Core::VoxFound;
    constexpr size_t kIncrementalMax = 32;
    auto& lab = voxFoundBodiesLab;
    if (!lab.sim || (!force && !lab.restOn)) return true;
    const RestParams& rp = override ? *override : lab.rest;
    const glm::dvec3 player = camera ? glm::dvec3(camera->getPosition()) - lab.areaOriginM : glm::dvec3(0.0);
    RestStats st;
    const size_t n0 = lab.sim->bodies().size();
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<BakePlan> plans;
    const std::vector<size_t> baked = restPass(*lab.sim, lab.world, player, rp, &st, &plans);
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
    if (baked.empty()) {   // F5.5d X4: the policy's cost on a frame that bakes nothing
        const float ms = float(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (lab.restIdleMs.size() < 3600) lab.restIdleMs.push_back(ms);
        else lab.restIdleMs[lab.restIdleNext] = ms;
        lab.restIdleNext = (lab.restIdleNext + 1) % 3600;
        return true;
    }
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
        count.push_back(lab.draw.count[i]);
        for (uint32_t k = lab.draw.first[i]; k < lab.draw.first[i] + lab.draw.count[i]; ++k) {
            BodyFace f = lab.draw.faces[k];
            f.body = idx;
            faces.push_back(f);
        }
    }
    lab.draw.faces.swap(faces);
    lab.draw.first.swap(first);
    lab.draw.count.swap(count);
    ++lab.draw.facesRevision;
    const auto t2 = std::chrono::steady_clock::now();
    voxFoundBodiesPublish();
    // the preview
    bool ok = true;
    const bool incremental = lab.regionPreview && !lab.asWorld && !voxFoundEdit.empty() && baked.size() <= kIncrementalMax;
    if (incremental) {
        std::vector<size_t> changed;
        for (const BakePlan& plan : plans) {
            if (!plan.ok) continue;
            std::vector<std::pair<glm::ivec3, uint8_t>> cells;
            cells.reserve(plan.cells.size());
            for (const BakeCell& c : plan.cells) cells.push_back({lab.originVoxel + c.cell, lab.world.mat[lab.world.idx(c.cell)]});
            const RegionEdit::Stats es = voxFoundEdit.applyCells(cells);
            for (size_t i : es.rebuilt) voxFoundBuilder->forgetResident(voxFoundEdit.residentKey(i));
            for (size_t i : es.changed)
                if (std::find(changed.begin(), changed.end(), i) == changed.end()) changed.push_back(i);
        }
        voxFoundEdit.assemble(voxFoundPreview, lab.leafTex, &changed);
        size_t pf = 0;
        for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) pf += voxFoundEdit.chunk(i).faces.size();
        lab.previewFaces = pf;
        ++lab.incrementalPasses;
    } else {
        ok = voxFoundBodiesRebuildWorld(err);
    }
    const auto t3 = std::chrono::steady_clock::now();
    lab.lastBakeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    lab.lastCompactMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    lab.lastRebuildMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    lab.lastRebuildIncremental = incremental;
    lab.maxEditMs = std::max(lab.maxEditMs, lab.lastRebuildMs);
    lab.lastGpuEditMs = 0.0;
    if (ok && lab.useGpu) {   // F5.5c: the GPU solver edited (changed cells + survivors), not reloaded
        const auto r0 = std::chrono::steady_clock::now();
        if (lab.sim->bodies().empty()) {
            lab.gpuLoaded = false;   // nothing left to move
        } else if (lab.gpuLoaded && lab.gpuEdit) {
            std::vector<std::pair<uint32_t, uint8_t>> cells;
            for (const BakePlan& plan : plans)
                if (plan.ok)
                    for (const BakeCell& c : plan.cells) {
                        const uint32_t wi = uint32_t(lab.world.idx(c.cell));
                        cells.push_back({wi, lab.world.mat[wi]});
                    }
            std::vector<size_t> removed = baked;
            std::sort(removed.begin(), removed.end());
            if (!lab.gpu->edit(cells, removed, lab.sim->bodies(), err)) return false;
            ++lab.gpuEdits;
        } else {
            if (!voxFoundBodiesGpuLoad(err)) return false;
            ++lab.reloads;
        }
        lab.lastGpuEditMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();
        lab.lastReloadMs = lab.lastGpuEditMs;
        lab.maxReloadMs = std::max(lab.maxReloadMs, lab.lastGpuEditMs);
    }
    lab.lastHitchMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    lab.maxHitchMs = std::max(lab.maxHitchMs, lab.lastHitchMs);
    lab.hitchMs.push_back(float(lab.lastHitchMs));
    return ok;
}

void Application::stepVoxFoundBodies(float dt) {
    auto& lab = voxFoundBodiesLab;
    if (!lab.sim) return;
    if (lab.running) {   // F5.5c: the frame interval while running (L3)
        if (lab.frameMs.size() < 3600) lab.frameMs.push_back(dt * 1000.0f);
        else lab.frameMs[lab.frameNext] = dt * 1000.0f;
        lab.frameNext = (lab.frameNext + 1) % 3600;
        if (dt * 1000.0 > lab.frameMaxMs) {   // F5.5d X8: when in the run the largest interval ended
            lab.frameMaxMs = dt * 1000.0;
            lab.frameMaxAtS = std::chrono::duration<double>(std::chrono::steady_clock::now() - lab.runStart).count();
        }
    }
    if (lab.useGpu) {
        voxFoundBodiesGpuStep(dt);
        return;
    }
    if (!lab.running) return;
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
        lab.gpu.reset();   // waits for a step in flight
        lab.gpuLoaded = false;
        renderCoordinator->setVoxFoundBodies(nullptr);
        renderCoordinator->setVoxFoundPreview(nullptr);
        lab.sim.reset();
        lab.draw = {};
        voxFoundEdit.clear();   // F5.5c
        voxFoundPreview.faces.clear();
        voxFoundPreview.chunks.clear();
        voxFoundPreview.clusters.clear();
        voxFoundPreview.levels.clear();
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
        const std::string solver = p.value("solver", std::string("gpu"));
        if (solver != "gpu" && solver != "cpu") {
            response = {{"error", "solver must be gpu or cpu"}};
            return;
        }
        if (lab.gpu && lab.gpu->stepPending()) lab.gpu->stepPoll(nullptr, true, nullptr);   // the old rig's step
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
        // F5.5b: the pad is clamped per axis so the preview's one-build limit (256 voxels a side) holds -- the large
        // wall (256 wide) gets no pad along x (pieces past its ends fall to the ground plane, below the floor)
        const int pad = int(std::lround(p.value("pad", 4.0) / double(rig.grid.voxelM)));
        const int padX = std::clamp((256 - rig.grid.dim.x) / 2, 0, pad), padZ = std::clamp((256 - rig.grid.dim.z) / 2, 0, pad);
        const glm::ivec3 off(padX, 2, padZ);
        VoxelGrid g;
        g.voxelM = rig.grid.voxelM;
        g.resize(rig.grid.dim + glm::ivec3(2 * padX, 2 + 16, 2 * padZ));
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
        // F5.5c: the origin on the 1/8 m voxel grid (the region preview is drawn per world voxel; [5,20,5] is on it)
        lab.originVoxel = glm::ivec3(glm::round(lab.areaOriginM / double(g.voxelM)));
        lab.areaOriginM = glm::dvec3(lab.originVoxel) * double(g.voxelM);
        const std::string preview = p.value("preview", std::string("region"));
        if (preview != "region" && preview != "area") {
            response = {{"error", "preview must be region or area"}};
            return;
        }
        lab.regionPreview = preview == "region";
        lab.gpuEdit = p.value("gpu_edit", true);
        lab.fullRebuilds = lab.incrementalPasses = lab.gpuEdits = 0;
        lab.lastHitchMs = lab.maxHitchMs = lab.maxEditMs = lab.lastGpuEditMs = 0.0;
        lab.hitchMs.clear();
        lab.frameMs.clear();
        lab.frameNext = 0;
        BodySimParams prm;
        prm.groundBelow = true;   // below the floor (never reached: the floor is voxels)
        lab.sim = std::make_unique<BodySimRef>(lab.world, lab.mats, prm);
        for (const Body& b : ex.bodies) lab.sim->add(b, b.vel, b.angVel);
        const BodyDrawSet set = buildBodyFaces(lab.sim->bodies());
        lab.draw.faces = set.faces;
        lab.draw.first = set.first;
        lab.draw.count = set.count;
        lab.useGpu = solver == "gpu";
        lab.reloads = 0;
        lab.lastReloadMs = lab.maxReloadMs = lab.gpuMsPerTick = 0.0;
        lab.simCpuMs.clear();
        lab.simCpuNext = 0;
        if (lab.useGpu) {
            std::string gerr;
            if (!voxFoundBodiesGpuLoad(&gerr)) {
                response = {{"error", "BodySimGpu: " + gerr}};
                return;
            }
        }
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
        lab.runStart = std::chrono::steady_clock::now();
        lab.runStartTick = 0;
        lab.asWorld = false;
        lab.crumbs = ex.crumbs.size();
        lab.ledger0 = voxFoundBodiesLedger();   // F5.5d X1: the rig's ledger, the reference for every later check
        lab.ledgerEvery = p.value("ledger_every", false);
        lab.ledgerChecks = lab.ledgerBad = lab.wakeAlls = 0;
        lab.ledgerFirstBadTick = -1;
        lab.ledgerMsMax = 0.0;
        lab.restIdleMs.clear();
        lab.restIdleNext = 0;
        lab.pollsDone = lab.pollsWaiting = 0;
        lab.stepLatencyMs.clear();
        lab.stepLatencyNext = 0;
        lab.frameMaxMs = 0.0;
        lab.frameMaxAtS = -1.0;
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
        lab.fullRebuilds = 0;   // the rig's own build is not a bake
        response = {{"provenance", "hand-built lab rig FractureLab " + name + " (E/T " + std::to_string(e) +
                                       ") on a 2-voxel floor; F4.1 fractureReference + F5.1 extractBodies; motion by " +
                                       (lab.useGpu ? std::string("the GPU solver BodySimGpu (the shipped path)")
                                                   : std::string("the CPU reference solver BodySimRef (the control)"))},
                    {"bodies", ex.bodies.size()}, {"crumbs", ex.crumbs.size()}, {"body_faces", set.faces.size()},
                    {"refused", set.refused}, {"preview_faces", lab.previewFaces},
                    {"preview", lab.regionPreview ? "region" : "area"}, {"region_chunks", lab.regionChunks},
                    {"origin_voxel", {lab.originVoxel.x, lab.originVoxel.y, lab.originVoxel.z}},
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
    if (p.contains("run")) {
        const bool on = p["run"].get<bool>();
        if (on && !lab.running) {
            lab.runStart = std::chrono::steady_clock::now();
            lab.runStartTick = lab.tick;
            lab.frameMs.clear();   // F5.5c: the frame intervals of this run
            lab.frameNext = 0;
            lab.frameMaxMs = 0.0;   // F5.5d: and the run's diagnostics
            lab.frameMaxAtS = -1.0;
            lab.pollsDone = lab.pollsWaiting = 0;
            lab.stepLatencyMs.clear();
            lab.stepLatencyNext = 0;
            lab.restIdleMs.clear();
            lab.restIdleNext = 0;
            lab.simCpuMs.clear();
            lab.simCpuNext = 0;
        }
        lab.running = on;
    }
    if (p.contains("cull")) lab.draw.cull = p["cull"].get<bool>();
    if (p.contains("gpu_edit")) lab.gpuEdit = p["gpu_edit"].get<bool>();   // F5.5c: false = reload (F5.5b, the control)
    if (p.contains("preview")) {   // F5.5c: switch the preview path on the same bodies (a full build of lab.world)
        const std::string pv = p["preview"].get<std::string>();
        if (pv != "region" && pv != "area") {
            response = {{"error", "preview must be region or area"}};
            return;
        }
        lab.regionPreview = pv == "region";
        std::string err;
        if (!voxFoundBodiesRebuildWorld(&err)) {
            response = {{"error", err}};
            return;
        }
    }
    if (p.contains("shown")) lab.draw.shown = p["shown"].get<bool>();
    if (p.contains("cast")) lab.draw.castShadows = p["cast"].get<bool>();
    if (p.contains("mutate")) lab.draw.mutate = p["mutate"].get<bool>();
    auto restOf = [&](const nlohmann::json& j, RestParams r) {
        r.budget = j.value("budget", r.budget);
        r.keepRadiusM = j.value("keep_m", r.keepRadiusM);
        r.restTicks = j.value("rest_ticks", r.restTicks);
        r.cap = j.value("cap", r.cap);   // F5.5c: bakes per pass (0 = no cap)
        return r;
    };
    if (p.contains("rest")) {
        lab.rest = restOf(p["rest"], lab.rest);
        lab.restOn = p["rest"].value("on", true);
    }
    if (p.contains("ledger_every")) lab.ledgerEvery = p["ledger_every"].get<bool>();   // F5.5d X1
    if (p.contains("ledger_plant") && lab.ledger0.size() > 1) {   // F5.5d: the check's red -- a wrong reference must
        lab.ledger0[1] = size_t(int64_t(lab.ledger0[1]) + p["ledger_plant"].get<int64_t>());   // count as bad
    }
    if (p.value("wake_all", false)) {   // F5.5d X2: every body awake on the pile (the budget's "every body awake")
        std::string err;
        if (lab.useGpu && lab.gpu && lab.gpu->stepPending()) {   // finish the step in flight first: the edit follows it
            const int k = lab.gpu->pendingTicks();
            if (lab.gpu->stepPoll(&lab.sim->bodies(), true, &err) == 1) voxFoundBodiesGpuDone(k);
        }
        for (SimBody& b : lab.sim->bodies()) {
            b.asleep = false;
            b.slowTicks = 0;
            b.asleepTicks = 0;
        }
        // GPU: the survivors' records through edit (no cells, no removals) -- bit-equal to a load (F5.5c H5), so the
        // warm start is dropped as a load drops it (stated)
        if (lab.useGpu && lab.gpuLoaded && !lab.sim->bodies().empty() && !lab.gpu->edit({}, {}, lab.sim->bodies(), &err)) {
            response = {{"error", err}};
            return;
        }
        ++lab.wakeAlls;
    }
    if (p.contains("profile") && lab.gpu) lab.gpu->setProfile(p["profile"].get<bool>());   // F5.5e
    if (p.contains("world_split") && lab.gpu) lab.gpu->setWorldSplit(p["world_split"].get<bool>());   // F5.5f
    if (p.contains("world_once") && lab.gpu) lab.gpu->setWorldOnce(p["world_once"].get<bool>());   // F5.5g
    if (p.contains("world_spread") && lab.gpu) lab.gpu->setWorldSpread(p["world_spread"].get<bool>());   // F5.5h
    if (p.contains("ticks") && lab.useGpu) {   // F5.5b: synchronous GPU ticks (the policy after every tick when on)
        const int n = p["ticks"].get<int>();
        std::string err;
        if (lab.gpu && lab.gpu->stepPending()) {
            const int k = lab.gpu->pendingTicks();
            if (lab.gpu->stepPoll(&lab.sim->bodies(), true, &err) == 1) voxFoundBodiesGpuDone(k);
        }
        for (int done = 0; done < n && lab.gpuLoaded;) {
            const int k = lab.restOn ? 1 : n - done;
            if (!lab.gpu->step(k, &err) || !lab.gpu->read(lab.sim->bodies(), &err)) {
                response = {{"error", err}};
                return;
            }
            lab.tick += k;
            done += k;
            if (lab.gpu->lastGpuMs() >= 0.0) lab.gpuMsPerTick = lab.gpu->lastGpuMs() / k;
            if (!voxFoundBodiesRest(false, nullptr, &err)) {
                response = {{"error", err}};
                return;
            }
        }
        voxFoundBodiesPublish();
    } else if (p.contains("ticks")) {
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
        lab.draw.castShadows = !lab.asWorld;   // F5.5d: drawn as world, the bodies must not cast as well
        std::string err;
        if (!voxFoundBodiesRebuildWorld(&err)) {
            response = {{"error", err}};
            return;
        }
    }
    // F5.5d: the CPU penetration measure (seconds at ~1,000 bodies, on the main thread) only when a STATUS asks for it --
    // before, every command's response ran it: the gate's own {"run":true} stalled one frame 0.7 s at 110 bodies and
    // ~60 s at 994 (F5.5c's "unexplained ~60 s interval at a run's second frame"). "pen":true forces it.
    const bool pen = p.value("pen", p.value("status", false));
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
                {"preview_faces", lab.previewFaces}, {"step_ms", lab.lastStepMs}, {"cull", lab.draw.cull},
                {"max_penetration_mm", pen ? lab.sim->currentMaxPenetration() * 1000.0 : -1.0},
                {"render", {{"bodies", st.bodies}, {"faces", st.faces}, {"drawn_faces", st.drawnFaces},
                            {"cast_faces_mid", st.castFaces[0]}, {"cast_faces_near", st.castFaces[1]},
                            {"pose_ms", st.poseMs}, {"face_upload_ms", st.faceUploadMs},
                            {"face_uploads", st.faceUploads}, {"culled", st.culled}, {"cull_ms", st.cullMs},
                            {"draws", st.draws}, {"cast_draws", {st.castDraws[0], st.castDraws[1]}},
                            {"kept_main", st.keptMain}, {"kept_cast", {st.keptCast[0], st.keptCast[1]}}}},
                {"centres_m", centres}};
    // F5.4: the conservation ledger (per material: world + bodies + crumbs) and the rest pass's numbers
    nlohmann::json ledger = nlohmann::json::object();
    const std::vector<size_t> perMat = voxFoundBodiesLedger();
    for (size_t m = 1; m < perMat.size(); ++m) ledger[std::to_string(m)] = perMat[m];
    const RestStats& rt = lab.restTotal;
    response["ledger"] = ledger;
    {   // F5.5d: the exit's measures
        auto qv = [](std::vector<float> v, double f) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return double(v[std::min(v.size() - 1, size_t(f * double(v.size())))]);
        };
        response["exit"] = {{"ledger_every", lab.ledgerEvery}, {"ledger_checks", lab.ledgerChecks},
                            {"ledger_bad", lab.ledgerBad}, {"ledger_first_bad_tick", lab.ledgerFirstBadTick},
                            {"ledger_check_ms_max", lab.ledgerMsMax}, {"ledger_now_ok", perMat == lab.ledger0},
                            {"wake_alls", lab.wakeAlls},
                            {"rest_idle_n", lab.restIdleMs.size()}, {"rest_idle_median_ms", qv(lab.restIdleMs, 0.5)},
                            {"rest_idle_p99_ms", qv(lab.restIdleMs, 0.99)}, {"rest_idle_max_ms", qv(lab.restIdleMs, 1.0)},
                            {"polls_done", lab.pollsDone}, {"polls_waiting", lab.pollsWaiting},
                            {"step_latency_n", lab.stepLatencyMs.size()},
                            {"step_latency_median_ms", qv(lab.stepLatencyMs, 0.5)},
                            {"step_latency_p99_ms", qv(lab.stepLatencyMs, 0.99)},
                            {"step_latency_max_ms", qv(lab.stepLatencyMs, 1.0)},
                            {"frame_max_ms", lab.frameMaxMs}, {"frame_max_at_s", lab.frameMaxAtS}};
    }
    if (pen) response["penetration_report"] = lab.sim->worstPenetrationReport();   // F5.5b
    // F5.5b: the solver path
    {
        std::vector<float> v = lab.simCpuMs;
        std::sort(v.begin(), v.end());
        auto q = [&](double f) { return v.empty() ? 0.0 : double(v[std::min(v.size() - 1, size_t(f * double(v.size())))]); };
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - lab.runStart).count();
        nlohmann::json g = {{"solver", lab.useGpu ? "gpu" : "cpu"}, {"loaded", lab.gpuLoaded},
                            {"ms_per_tick_device", lab.gpuMsPerTick}, {"reloads", lab.reloads},
                            {"last_reload_ms", lab.lastReloadMs}, {"max_reload_ms", lab.maxReloadMs},
                            {"cpu_ms_frames", v.size()}, {"cpu_ms_median", q(0.5)}, {"cpu_ms_p95", q(0.95)},
                            {"cpu_ms_max", v.empty() ? 0.0 : double(v.back())},
                            {"ticks_since_run", lab.tick - lab.runStartTick}, {"wall_s_since_run", wall},
                            {"ticks_per_s", wall > 0.0 ? double(lab.tick - lab.runStartTick) / wall : 0.0}};
        if (lab.gpu) {
            const auto& s = lab.gpu->stats();
            g["stats"] = {{"awake", s.awake}, {"contacts", s.contacts}, {"colours", s.colours},
                          {"overflow", s.scratchOverflow + s.constraintOverflow + s.pairOverflow},
                          {"uncoloured", s.uncoloured}, {"fused", lab.gpu->fusedThisStep()},
                          {"candidates", s.candidates}, {"capped", s.capped}, {"pairs_tested", s.pairsTested},
                          {"hard_fires", s.hardFires}, {"dg_max_cand", s.dgMaxCand},
                          {"dg_max_items", s.dgMaxItems}, {"dg_over_cap", s.dgOverCap}, {"dg_sum_cand", s.dgSumCand},
                          {"world_redo", s.worldRedo}, {"spread_fallback", s.spreadFallback}};
            const auto& pr = lab.gpu->profile();   // F5.5e
            if (pr.ticks > 0) {
                nlohmann::json groups = nlohmann::json::object(), mins = nlohmann::json::object();
                for (int k = 0; k < BodySimGpu::kProfGroups; ++k) {
                    groups[BodySimGpu::profileName(k)] = pr.sumMs[k] / pr.ticks;
                    mins[BodySimGpu::profileName(k)] = pr.minMs[k];
                }
                g["profile"] = {{"ticks", pr.ticks}, {"mean_ms", groups}, {"min_ms", mins}};
            }
        }
        response["solver"] = g;
    }
    // F5.5c: {"verify":true} rebuilds every region chunk from its store with FaceBuilderGpu and compares the faces with
    // the incrementally kept ones (as a set)
    if (p.value("verify", false) && !voxFoundEdit.empty()) {
        size_t bad = 0, checked = 0;
        std::string verr;
        for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
            const auto& c = voxFoundEdit.chunk(i);
            std::vector<PackedFace> out;
            if (!voxFoundBuilder->build(voxFoundEdit.dense(i), Edges(c.border), out, &verr)) break;
            std::vector<uint64_t> a, b;
            for (const PackedFace& f : out) a.push_back(f.key());
            for (const PackedFace& f : c.faces) b.push_back(f.key());
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            checked += a.size();
            bad += a != b;
        }
        response["verify"] = {{"chunks", voxFoundEdit.chunkCount()}, {"mismatched_chunks", bad},
                              {"faces_checked", checked}, {"error", verr}};
    }
    {
        std::vector<float> f = lab.frameMs, h = lab.hitchMs;
        std::sort(f.begin(), f.end());
        std::sort(h.begin(), h.end());
        auto q = [](const std::vector<float>& v, double x) {
            return v.empty() ? 0.0 : double(v[std::min(v.size() - 1, size_t(x * double(v.size())))]);
        };
        size_t argmax = 0;   // where the longest interval sits in the run (the ring in order while < 3,600)
        for (size_t i = 1; i < lab.frameMs.size(); ++i)
            if (lab.frameMs[i] > lab.frameMs[argmax]) argmax = i;
        std::vector<float> firstMs(lab.frameMs.begin(), lab.frameMs.begin() + std::min<size_t>(8, lab.frameMs.size()));
        response["frames"] = {{"n", f.size()}, {"median_ms", q(f, 0.5)}, {"p99_ms", q(f, 0.99)},
                              {"max_ms", f.empty() ? 0.0 : double(f.back())}, {"max_at", argmax},
                              {"first_ms", firstMs}};
        response["bake_levers"] = {{"preview", lab.regionPreview ? "region" : "area"}, {"gpu_edit", lab.gpuEdit},
                                   {"region_chunks", lab.regionChunks}, {"full_rebuilds", lab.fullRebuilds},
                                   {"incremental_passes", lab.incrementalPasses}, {"gpu_edits", lab.gpuEdits},
                                   {"hitch_passes", h.size()}, {"hitch_median_ms", q(h, 0.5)},
                                   {"hitch_p99_ms", q(h, 0.99)}, {"hitch_max_ms", lab.maxHitchMs},
                                   {"last_hitch_ms", lab.lastHitchMs}, {"max_preview_edit_ms", lab.maxEditMs},
                                   {"last_gpu_edit_ms", lab.lastGpuEditMs}, {"cap", lab.rest.cap},
                                   {"last_incremental", lab.lastRebuildIncremental},
                                   {"last_parts_ms", {{"choose", lab.restLast.chooseMs}, {"grid", lab.restLast.gridMs},
                                                      {"plan", lab.restLast.planMs}, {"apply", lab.restLast.applyMs},
                                                      {"wake", lab.restLast.wakeMs}, {"remove", lab.restLast.removeMs}}}};
    }
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
