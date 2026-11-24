#ifndef NOMINMAX
#define NOMINMAX
#endif
// docs/VoxelFoundationPlan.md step 7 D3-core -- moved verbatim from editor/src/Application.cpp (the switched world).
#include "core/voxfound/VoxFoundWorld.h"
#include <unordered_map>
#include "core/voxfound/WorldReads.h"
#include "core/voxfound/OccupancyFromStore.h"
#include "core/NavGraph.h"
#include "core/PerfCapture.h"
#include "core/water/WaterSpanWriteBack.h"
#include "core/water/WaterSpanEdit.h"
#include "core/water/WaterCoreGpu.h"
#include "core/DebrisApiCommands.h"
#include <cmath>
#include <cstdlib>
#include "graphics/FarTerrainManager.h"
#include "graphics/FaceCoverage.h"
#include "graphics/GrassRenderPipeline.h"
#include "graphics/WaterSurfaceRenderPipeline.h"
#include "graphics/WaterRenderPipeline.h"
#include "graphics/ChunkUpdatePerf.h"
#include "graphics/DeferredBufferReclaim.h"
#include "graphics/ChunkArenaSystem.h"
#include "core/MaterialRegistry.h"
#include "core/PerfApi.h"
#include "core/TraversalProbe.h"
#include "core/GameSettings.h"
#include "core/AtlasManager.h"
#include "core/VfxSystem.h"
#include "core/VfxDirector.h"
#include "core/SpellVfxMapper.h"
#include "core/SpellDefinition.h"
#include "core/SpellAnimMapper.h"
#include "core/MeleeAnimMapper.h"
#include "core/RpgItem.h"
#include "core/DamageSystem.h"
#include "core/DamageStage.h"
#include "utils/GpuProfiler.h"
#include "scene/VoxelInteractionSystem.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/SeatFit.h"
#include "scene/motion/MotionBricksSystem.h"
#include "scene/AppearancePresetRegistry.h"
#include "graphics/AnimationSystem.h"
#include "scene/NPCEntity.h"
#include "scene/CharacterTurnBody.h"
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "scene/behaviors/CombatBehavior.h"
#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "ai/Schedule.h"
#include "ai/NeedsSystem.h"
#include "ai/RelationshipManager.h"
#include "ai/WorldView.h"
#include "core/EntityRegistry.h"
#include "core/APICommandQueue.h"
#include "core/EngineAPIServer.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include "ai/AIEnhancer.h"
#include "ai/AIConversationService.h"
#include "ai/LLMClient.h"
#include "ai/TTSService.h"
#include "core/WorldStorage.h"
#include "core/Chunk.h"
#include "core/LodChunkMesh.h"
#include "core/WorldGenerator.h"
#include "core/WorldForgeBuildService.h"
#include "core/WorldForgeLedger.h"
#include "core/WorldForgePlan.h"
#include "core/WorldRecipe.h"
#include "core/HydrologyMap.h"
#include "core/WaterProfile.h"
#include "core/WaterOccupancy.h"
#include "core/FlowField.h"
#include "core/VoxelTemplate.h"
#include "physics/Material.h"
#include "story/StoryWorldLoader.h"
#include "story/StoryDirectorTypes.h"
#include "core/EngineConfig.h"
#include "core/CrashHandler.h"
#include "core/AssetManager.h"
#include "core/voxfound/StorageLab.h"
#include <array>
#include "core/GameDefinitionLoader.h"
#include "core/CharacterVisualResolver.h"
#include "core/MonsterDefinition.h"
#include "core/MonsterVisualRegistry.h"
#include "core/StructureGenerator.h"
#include "core/StructureBuildService.h"
#include "core/SettlementBuildService.h"
#include "core/WaterSpansForChunk.h"
#include "core/ChunkBlobCodec.h"
#include "core/SpawnGate.h"
#include "core/StructureRealizer.h"
#include "core/RoomLayout.h"
#include "core/SettlementLayout.h"
#include "core/KeepClear.h"
#include "core/ResidentPlanner.h"
#include "core/PathPlanner.h"
#include "core/StreetPaver.h"
#include "core/FurnitureCatalog.h"
#include "core/FenceBuilder.h"
#include "core/DimensionCanon.h"
#include "core/BuildingProgramValidator.h"
#include "core/RoomProgram.h"
#include "core/FurniturePlacer.h"
#include "core/FurnitureCatalog.h"
#include "core/RealizedStructureValidator.h"
#include "core/RealizedWorldValidator.h"
#include "core/ProjectInfo.h"
#include "core/LauncherState.h"
#include "core/ItemRegistry.h"
#include "core/SpawnGrounding.h"
#include "core/Uuid.h"
#include "core/EquipmentSystem.h"
#include "core/CombatSystem.h"
#include "core/Cube.h"
#include "physics/VoxelRigidBody.h"
#include "ui/MenuDefinition.h"
#include "ui/UISystem.h"
#include "ui/GameMenus.h"
#include "ui/GameMenuRenderer.h"
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include "core/WaterSpanGridKey.h"
#include <set>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <limits>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <direct.h>
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <sys/stat.h>

namespace Phyxel {
std::unique_ptr<IWorldEditor> makeRegionWorldEditor(VoxFoundWorld& app);   // F7.4 (defined with RegionWorldEditor)


// docs/VoxelFoundationPlan.md F2.2 -- POST /api/debug/voxfound_preview (registered in registerMatSimCommands).
void VoxFoundWorld::handleVoxFoundPreview(const Core::APICommand& cmd, nlohmann::json& response) {
        // docs/VoxelFoundationPlan.md F2.2 Ã¢â‚¬â€ draw a 1/8 m area: the ENGINE's terrain generator (and flora
        // planner + the tree_forge --grid 8 library when "flora") sampled at 1/8 m by
        // VoxFound::buildTerrainArea, faces built on the GPU by FaceBuilderGpu, drawn through voxel.frag.
        // Body: {type:"Perlin"|"Mountains"|"Flat", seed, x, z (area origin, metres), size (<= 32 m),
        //        flora, outside:"air"|"skip", at:[X,Y,Z] (world metres for the area's min corner;
        //        default = its true position)}  or  {clear:true}.
        using namespace Core::VoxFound;
        if (cmd.params.value("clear", false)) {
            if (renderCoordinator) renderCoordinator->setVoxFoundPreview(nullptr);
            voxFoundPreview.faces.clear();
            voxFoundPreview.sprigs.clear();
            voxFoundPreview.chunks.clear();
            voxFoundPreview.clusters.clear();
            if (chunkManager && voxFoundWorldReads) chunkManager->setVoxFoundReads(nullptr);   // F7.3: unroute first
            voxFoundWorldReads.reset();
            voxFoundEdit.clear();   // F2.16
            ++voxFoundPreview.revision;
            response = {{"cleared", true}};
            return;
        }
        if (!vulkanDevice || !renderCoordinator) {
            response = {{"error", "no Vulkan device / renderer"}};
            return;
        }
        // {"pick":{"origin":[x,y,z] metres,"dir":[x,y,z],"max_m":m,"size":s}}: F3.2b -- the first solid voxel of the
        // editable region along the ray (RegionEdit::pick), its entry face, and the brush cells of size s to break
        // (holding the hit) and to place (holding the voxel in front of the hit face).
        if (cmd.params.contains("pick")) {
            using namespace Core::VoxFound;
            const auto& pj = cmd.params["pick"];
            if (voxFoundEdit.empty()) {
                response = {{"error", "no editable region preview"}};
                return;
            }
            auto vec3 = [](const nlohmann::json& j) { return glm::dvec3(j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()); };
            const glm::dvec3 o = vec3(pj.at("origin")) * double(kVoxelsPerM), d = vec3(pj.at("dir"));
            const int size = pj.value("size", 8);
            const auto t0 = std::chrono::steady_clock::now();
            const RegionEdit::PickHit h = voxFoundEdit.pick(o, d, pj.value("max_m", 64.0) * double(kVoxelsPerM));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            response = {{"hit", h.hit}, {"pick_ms", ms}, {"steps", h.steps}};
            if (h.hit) {
                const glm::ivec3 front = h.voxel + h.normal;
                response["voxel"] = {h.voxel.x, h.voxel.y, h.voxel.z};
                response["normal"] = {h.normal.x, h.normal.y, h.normal.z};
                response["distance_m"] = h.distance / double(kVoxelsPerM);
                response["material"] = h.material < voxFoundPalette.size() ? voxFoundPalette[h.material] : std::string("?");
                response["front_material"] = voxFoundEdit.voxelAt(front);
                if (brushSizeOk(size)) {
                    const glm::ivec3 b = brushCell(h.voxel, size), pl = brushCell(front, size);
                    response["break_cell"] = {b.x, b.y, b.z};
                    response["place_cell"] = {pl.x, pl.y, pl.z};
                }
            }
            return;
        }
        if (cmd.params.value("save", false)) {   // F3.3: the edited chunks, codec v3, into world.db
            handleVoxFoundSave(response);
            return;
        }
        // F3.3 gate helpers: {"saved_clear":true} deletes the saved rows of the region's chunks; {"store_hash":true}
        // returns each chunk's voxel hash (FNV-1a over its dense materials) and edited flag.
        if (cmd.params.value("saved_clear", false) || cmd.params.value("store_hash", false)) {
            auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
            nlohmann::json rows = nlohmann::json::array();
            size_t cleared = 0;
            for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
                const auto& c = voxFoundEdit.chunk(i);
                if (cmd.params.value("saved_clear", false) && ws && ws->deleteVoxFoundBlob(c.chunk)) ++cleared;
                if (cmd.params.value("store_hash", false)) {
                    const Core::VoxFound::VoxelGrid g = voxFoundEdit.dense(i);
                    uint64_t h = 1469598103934665603ull;
                    for (uint8_t v : g.mat) h = (h ^ v) * 1099511628211ull;
                    // F7.9b: the same voxels by material NAME (palette order differs between region builds)
                    std::vector<uint64_t> nameKey(256, 0);   // FNV-1a of each palette name (air 0): order-free
                    for (size_t m = 1; m < voxFoundPalette.size() && m < 256; ++m) {
                        uint64_t k = 1469598103934665603ull;
                        for (char ch : voxFoundPalette[m]) k = (k ^ uint8_t(ch)) * 1099511628211ull;
                        nameKey[m] = k;
                    }
                    uint64_t hn = 1469598103934665603ull;
                    for (uint8_t v : g.mat) hn = (hn ^ nameKey[v]) * 1099511628211ull;
                    rows.push_back({{"chunk", {c.chunk.x, c.chunk.y, c.chunk.z}}, {"hash", std::to_string(h)},
                                    {"hash_by_name", std::to_string(hn)}, {"edited", c.edited},
                                    {"faces", c.faces.size()}, {"sprigs", c.sprigs.size()}});
                }
            }
            response = {{"cleared", cleared}, {"chunks", rows},
                        {"saved_rows_total", ws ? ws->getVoxFoundBlobCoords().size() : size_t(0)}};
            return;
        }
        // {"edit":{...}}: F2.16 (Ã‚Â§4.6 item 9) -- edit an editable region preview, rebuilt incrementally
        if (cmd.params.contains("edit")) {
            handleVoxFoundEdit(cmd.params["edit"], response);
            return;
        }
        if (cmd.params.contains("cast")) {   // {"cast":{"faces":bool,"sprigs":bool}}: shadow casters on/off (diagnosis)
            const auto& c = cmd.params["cast"];
            voxFoundPreview.castFaces = c.value("faces", voxFoundPreview.castFaces);
            voxFoundPreview.castSprigs = c.value("sprigs", voxFoundPreview.castSprigs);
            if (!cmd.params.contains("type") && !cmd.params.contains("templates")) {
                response = {{"cast_faces", voxFoundPreview.castFaces}, {"cast_sprigs", voxFoundPreview.castSprigs}};
                return;
            }
        }
        // F2.8 A/B switches: {"hide_world":bool} stops drawing today's world; {"show_preview":bool} keeps the preview
        // on the device but draws it or not -- so one session can alternate today / new store without rebuilding.
        // {"chunk_cull":bool}: F2.9 per-chunk distance + frustum culling of the preview's main-view draws (default on).
        // {"occlusion":bool}: F2.10 today's occlusion walk over the preview's chunks (default on).
        // {"mdi":bool}: F2.11 GPU draw-building pass + one indirect multidraw for the faces (default on).
        if (cmd.params.contains("occlusion")) renderCoordinator->setVoxFoundOcclusion(cmd.params.value("occlusion", true));
        if (cmd.params.contains("mdi")) renderCoordinator->setVoxFoundMdi(cmd.params.value("mdi", true));
        // {"shadow_cull":bool}: F2.12 today's per-chunk caster test + the light frustum (faces mid/near, sprigs mid).
        // {"shadow_mdi":bool}: F2.12 per-cluster light-frustum cull + one indirect multidraw per cascade (faces);
        // default OFF (measured slower).
        if (cmd.params.contains("shadow_cull"))
            renderCoordinator->setVoxFoundShadowCull(cmd.params.value("shadow_cull", true));
        if (cmd.params.contains("shadow_mdi"))
            renderCoordinator->setVoxFoundShadowMdi(cmd.params.value("shadow_mdi", true));
        // {"face_pool":bool}: F2.17 (Ã‚Â§4.6 item 10) device-local face pool, edits copy only their spans (default on)
        if (cmd.params.contains("face_pool")) renderCoordinator->setVoxFoundFacePool(cmd.params.value("face_pool", true));
        // {"tjunction":0|1|2, "tj_px":f}: F2.18 T-junction cracks (0 today, 1 exact sums, 2 + in-plane margin of f px)
        if (cmd.params.contains("tjunction") || cmd.params.contains("tj_px"))
            renderCoordinator->setVoxFoundTJunction(cmd.params.value("tjunction", renderCoordinator->voxFoundTJunction()),
                                                    cmd.params.value("tj_px", 0.0f));
        // {"lod_auto":bool, "lod_px":P}: F2.14 the main view picks each fragment's level by size on screen, P pixels per
        // voxel (the tier knob, default 1); off = level 0 everywhere. Needs {"lod_levels":n} at build time.
        // {"lod_casters":bool}: F2.14 with lod_auto, the face shadow casters draw the levels too (each caster fragment
        // at the level the camera sees it at, floored by the cascade's texel). Default off.
        // {"lod_sprig":bool}: F2.14c A/B -- with a build's lod_sprig_level, true (default) draws the coarse leaf mass and
        // hands the sprigs off to it; false drops the mass from the draw lists and keeps sprigs everywhere (the F2.14
        // state). Re-selects the levels (a face re-upload).
        if ((cmd.params.contains("lod_auto") || cmd.params.contains("lod_px") || cmd.params.contains("lod_casters") ||
             cmd.params.contains("lod_sprig") || cmd.params.contains("lod_max") ||
             cmd.params.contains("lod_leaf_look") || cmd.params.contains("lod_cards_split") ||
             cmd.params.contains("lod_alt_on")) &&
            !cmd.params.contains("type") && !cmd.params.contains("region")) {
            bool ok = true;
            if (cmd.params.contains("lod_casters")) voxFoundPreview.lodCasters = cmd.params.value("lod_casters", false);
            // {"lod_leaf_look":bool}: F2.14f A/B -- true (default) draws the leaf mass with its own pipeline, shaded as
            // leaf cards (voxfound_leafmass.frag); false = through voxel.frag (the F2.14c look). No rebuild.
            if (cmd.params.contains("lod_leaf_look")) {
                voxFoundPreview.lodLeafLook = cmd.params.value("lod_leaf_look", true);
                ++voxFoundPreview.revision;   // the draw lists are re-ordered (leaf mass last)
            }
            // {"lod_cards_split":bool}: F2.14g A/B -- draw the coarse levels meshed from two pyramids (true) or one
            // (false); the other way must have been built too ({"lod_card_ab":true}). Re-selects; no rebuild.
            if (cmd.params.contains("lod_cards_split")) {
                const bool want = cmd.params.value("lod_cards_split", true);
                const bool alt = want != voxFoundPreview.lodSplit;
                if (alt && voxFoundPreview.levelsAlt.empty()) ok = false;
                else if (alt != voxFoundPreview.lodUseAlt) {
                    voxFoundPreview.lodUseAlt = alt;
                    if (!voxFoundPreview.lodFirst.empty()) ok = voxFoundPreview.selectLod();
                }
            }
            // {"lod_alt_on":bool}: F2.14h -- draw the alternate variant's coarse levels (built with lod_alt/lod_card_ab)
            if (cmd.params.contains("lod_alt_on")) {
                const bool alt = cmd.params.value("lod_alt_on", false);
                if (alt && voxFoundPreview.levelsAlt.empty()) ok = false;
                else if (alt != voxFoundPreview.lodUseAlt) {
                    voxFoundPreview.lodUseAlt = alt;
                    if (!voxFoundPreview.lodFirst.empty()) ok = voxFoundPreview.selectLod();
                }
            }
            // {"lod_max":n}: F2.14d A/B -- the highest built level the selection uses (re-selects; no rebuild)
            if (cmd.params.contains("lod_max")) {
                const int cap = std::clamp(cmd.params.value("lod_max", 7), 0, 7);
                if (cap != voxFoundPreview.lodLevelCap) {
                    voxFoundPreview.lodLevelCap = cap;
                    if (!voxFoundPreview.lodFirst.empty()) ok = voxFoundPreview.selectLod();
                }
            }
            if (cmd.params.contains("lod_sprig")) {
                const bool on = cmd.params.value("lod_sprig", true);
                if (on != voxFoundPreview.lodSprigOn) {
                    voxFoundPreview.lodSprigOn = on;
                    if (!voxFoundPreview.lodFirst.empty()) ok = voxFoundPreview.selectLod();
                }
            }
            if (cmd.params.contains("lod_px"))
                voxFoundPreview.lodPxPerVoxel = std::clamp(cmd.params.value("lod_px", 1.0f), 0.125f, 64.0f);
            if (cmd.params.contains("lod_auto")) {
                const bool on = cmd.params.value("lod_auto", true);
                if (on && voxFoundPreview.lodFirst.empty()) ok = voxFoundPreview.levels.size() > 1 && voxFoundPreview.selectLod();
                voxFoundPreview.lodOn = on && ok;
            }
            response = {{"lod_auto", voxFoundPreview.lodOn}, {"lod_px", voxFoundPreview.lodPxPerVoxel},
                        {"lod_casters", voxFoundPreview.lodCasters}, {"lod_sprig", voxFoundPreview.lodSprigOn},
                        {"lod_sprig_level", voxFoundPreview.lodSprigLevel}, {"ok", ok},
                        {"lod_max", voxFoundPreview.lodLevelCap}, {"lod_max_level", voxFoundPreview.lodMaxLevel},
                        {"lod_leaf_look", voxFoundPreview.lodLeafLook},
                        {"lod_cards_split", voxFoundPreview.lodSplit != voxFoundPreview.lodUseAlt},
                        {"levels_built", voxFoundPreview.levels.size()}, {"faces_on_device", voxFoundPreview.faces.size()}};
            if (!ok) response["error"] = "coarser levels not built (lod_levels)";
            return;
        }
        // {"dir_clusters":bool, "dir_cull":bool}: F2.15 (Ã‚Â§4.6 item 8) -- the multidraw uses the per-direction clusters,
        // and the cull pass drops direction groups that cannot face the camera (both ON by default, F2.15)
        // {"shadow_dir_cull":bool}: F2.15b -- the shadow multidraw (shadow_mdi) drops direction groups facing away from
        // the light
        // {"mdi_compact":bool}: F2.15f -- the cull pass appends + merges the visible clusters (indirect count)
        if (cmd.params.contains("mdi_compact") && !cmd.params.contains("type") && !cmd.params.contains("region")) {
            voxFoundPreview.mdiCompact = cmd.params.value("mdi_compact", false);
            response = {{"mdi_compact", voxFoundPreview.mdiCompact},
                        {"draw_indirect_count", vulkanDevice && vulkanDevice->supportsDrawIndirectCount()}};
            return;
        }
        if ((cmd.params.contains("dir_clusters") || cmd.params.contains("dir_cull") ||
             cmd.params.contains("shadow_dir_cull") || cmd.params.contains("shadow_closed") ||
             cmd.params.contains("shadow_seam")) &&
            !cmd.params.contains("type") && !cmd.params.contains("region")) {
            if (cmd.params.contains("dir_clusters")) voxFoundPreview.dirClusters = cmd.params.value("dir_clusters", false);
            if (cmd.params.contains("dir_cull")) voxFoundPreview.dirCull = cmd.params.value("dir_cull", false);
            if (cmd.params.contains("shadow_dir_cull"))
                voxFoundPreview.shadowDirCull = cmd.params.value("shadow_dir_cull", false);
            // {"shadow_closed":bool}: F2.15b -- the shadow multidraw draws the closed casters (leaf voxels as air)
            if (cmd.params.contains("shadow_closed"))
                voxFoundPreview.shadowClosed = cmd.params.value("shadow_closed", true);
            // {"shadow_seam":bool}: F2.15d -- with the light test, also draw the seam (leaf-hidden faces near air)
            if (cmd.params.contains("shadow_seam")) voxFoundPreview.shadowSeam = cmd.params.value("shadow_seam", true);
            ++voxFoundPreview.revision;   // re-upload the cluster table
            response = {{"dir_clusters", voxFoundPreview.dirClusters}, {"dir_cull", voxFoundPreview.dirCull},
                        {"shadow_dir_cull", voxFoundPreview.shadowDirCull},
                        {"shadow_closed", voxFoundPreview.shadowClosed},
                        {"caster_faces", voxFoundPreview.casterFaces.size()},
                        {"shadow_seam", voxFoundPreview.shadowSeam}, {"seam_faces", voxFoundPreview.seamFaces.size()},
                        {"seam_depth", voxFoundPreview.seamDepth}, {"exit_reach", voxFoundPreview.exitReach},
                        {"clusters", voxFoundPreview.clusters.size()}, {"clusters_dir", voxFoundPreview.clustersDir.size()},
                        {"lod_clusters", voxFoundPreview.lodClusters.size()},
                        {"lod_clusters_dir", voxFoundPreview.lodClustersDir.size()}};
            return;
        }
        // {"lod_draw":k}: F2.13 draw pyramid level k (built with {"lod_levels":n}) for the whole region, no rebuild.
        if (cmd.params.contains("lod_draw") && !cmd.params.contains("type") && !cmd.params.contains("region")) {
            const int k = cmd.params.value("lod_draw", 0);
            const bool ok = voxFoundPreview.selectLevel(k);
            response = {{"lod_draw", voxFoundPreview.drawLevel}, {"ok", ok},
                        {"levels_built", voxFoundPreview.levels.size()}, {"faces", voxFoundPreview.faces.size()}};
            if (!ok) response["error"] = "level " + std::to_string(k) + " not built (lod_levels)";
            return;
        }
        // F7.3 (docs/VoxelFoundationPlan.md section 7 step 3): {"world_switch":bool} -- on: today's world hidden, the
        // editable region drawn, AND ChunkManager's four reads routed to the region's own stores (what is drawn is what
        // the engine reads); off: both back. {"world_switch_stub":bool} (tests): hide + draw without routing (the red).
        // {"reads_check":{"n":N}}: N sampled region cubes -- ChunkManager::getVoxelTypeAt vs the region's rule.
        // {"nav_check":{"from":[x,z],"to":[x,z]}}: NPCManager::buildNavGrid + a path between the two ground points.
        // F7.4: {"edit_stub":bool} (tests: the next switch-on leaves the editor unset -- the red);
        // {"edit_action":{"action":"break"|"place","mode":"cube"|"subcube"|"micro"}}: a hover update on the camera's
        // forward ray, then the SAME VoxelInteractionSystem call the B / C keys make for that mode.
        if (cmd.params.contains("struct_stub")) {   // F7.11 (tests): build_structure not routed (the red)
            voxFoundStructStub = cmd.params["struct_stub"].get<bool>();
            response = {{"struct_stub", voxFoundStructStub}};
            return;
        }
        if (cmd.params.value("today_census", false) && chunkManager) {   // F7.27 (tests): today's volume per material
            std::map<std::string, double> vol;
            for (const auto& [cc, ch] : chunkManager->chunkMap) {
                if (!ch) continue;
                const auto& st = ch->getVoxelStore();
                for (size_t i = 0; i < 32768; ++i) if (st.solid(i)) vol[st.material(i)] += 512.0;
                for (const auto& sp : ch->getStaticSubcubes()) if (sp) vol[sp->getMaterialName()] += 512.0 / 27.0;
                for (const auto& mp : ch->getStaticMicrocubes()) if (mp) vol[mp->getMaterialName()] += 512.0 / 729.0;
            }
            response = {{"today_voxels_by_material", vol}, {"chunks", chunkManager->chunkMap.size()}};
            return;
        }
        if (cmd.params.contains("grass_stub")) {   // F7.29 (tests): no grass on the switched world (the red)
            voxFoundGrassStub = cmd.params["grass_stub"].get<bool>();
            if (voxFoundWorldReads) voxFoundGrassFill();
            response = {{"grass_stub", voxFoundGrassStub}, {"grass_instances", voxFoundGrassCount}};
            return;
        }
        if (cmd.params.value("stored_check", false) && chunkManager) {   // D1b (tests): overlaid chunks == their blobs
            auto* ws = chunkManager->m_streamingManager.getWorldStorage();
            size_t checked = 0, bad = 0, notStored = 0;
            nlohmann::json firstBad = nlohmann::json::array();
            for (size_t i = 0; i < voxFoundEdit.chunkCount() && ws; ++i) {
                const glm::ivec3 cc = voxFoundEdit.chunk(i).chunk;
                std::vector<uint8_t> blob;
                if (!ws->readChunkBlob(cc, blob) || blob.empty()) { ++notStored; continue; }
                Core::VoxFound::BlockStore s;
                std::vector<std::string> names;
                if (!Core::VoxFound::convertPxb2(blob.data(), blob.size(), s, names, nullptr, nullptr)) { ++bad; continue; }
                ++checked;
                const auto& rs = voxFoundEdit.chunk(i).store;
                size_t diff = 0;
                for (int x = 0; x < 256 && diff == 0; ++x)
                    for (int y = 0; y < 256 && diff == 0; ++y)
                        for (int z = 0; z < 256; ++z) {
                            const uint8_t a = s.at({x, y, z}), b = rs.at({x, y, z});
                            std::string an = a < names.size() ? names[a] : "?";
                            if (a != 0 && !Core::MaterialRegistry::instance().getMaterial(an)) an = "Default";   // retired
                            const std::string bn = b == 0 ? "" : (b < voxFoundPalette.size() ? voxFoundPalette[b] : "?");
                            if ((a == 0) != (b == 0) || (a != 0 && an != bn)) { ++diff; break; }
                        }
                if (diff) {
                    ++bad;
                    if (firstBad.size() < 5) firstBad.push_back({cc.x, cc.y, cc.z});
                }
            }
            response = {{"checked", checked}, {"mismatching_chunks", bad}, {"not_stored", notStored},
                        {"first_bad", firstBad}, {"overlaid_total", voxFoundOverlaidTotal}};
            return;
        }
        if (cmd.params.value("scene_status", false)) {   // D3-scenes (tests)
            response = {{"multi_scene", voxFoundMultiScene}, {"log", voxFoundSceneLog},
                        {"switched", voxFoundWorldReads != nullptr}, {"region_chunks", voxFoundEdit.chunkCount()}};
            return;
        }
        if (cmd.params.value("today_status", false) && chunkManager) {   // D2 (tests)
            response = {{"today_world_disabled", ChunkStreamingManager::todayWorldDisabled()},
                        {"today_chunks_resident", chunkManager->chunkMap.size()}};
            return;
        }
        if (cmd.params.contains("grass_refresh_stub")) {   // F7.30 (tests): edits leave the grass alone (the red)
            voxFoundGrassRefreshStub = cmd.params["grass_refresh_stub"].get<bool>();
            response = {{"grass_refresh_stub", voxFoundGrassRefreshStub}};
            return;
        }
        if (cmd.params.value("grass_status", false)) {
            response = {{"grass_instances", voxFoundGrassCount}, {"refreshes", voxFoundGrassRefreshes},
                        {"dirty_chunks", voxFoundGrassDirty.size()},
                        {"renderer_instances", renderCoordinator ? renderCoordinator->voxFoundGrassInstances() : 0}};
            return;
        }
        if (cmd.params.contains("item_snap_stub")) {   // F7.25 (tests): items snap by today's cube scan (the red)
            if (itemPropManager) itemPropManager->setVoxFoundSnapStub(cmd.params["item_snap_stub"].get<bool>());
            response = {{"item_snap_stub", cmd.params["item_snap_stub"]}};
            return;
        }
        if (cmd.params.contains("box_voxels")) {   // F7.24 (tests): the solid 1/8 m voxels of a box [lo, hi] (voxel units)
            const auto& bj = cmd.params["box_voxels"];
            const glm::ivec3 lo(bj["lo"][0].get<int>(), bj["lo"][1].get<int>(), bj["lo"][2].get<int>());
            const glm::ivec3 hi(bj["hi"][0].get<int>(), bj["hi"][1].get<int>(), bj["hi"][2].get<int>());
            nlohmann::json v = nlohmann::json::array();
            size_t n = 0;
            for (int x = lo.x; x <= hi.x; ++x)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int z = lo.z; z <= hi.z; ++z)
                        if (const uint8_t m = voxFoundEdit.voxelAt({x, y, z}))
                            if (++n <= 200000)
                                v.push_back({x - lo.x, y - lo.y, z - lo.z, m < voxFoundPalette.size() ? voxFoundPalette[m] : "?"});
            response = {{"solid", n}, {"voxels", v}, {"truncated", n > 200000}};
            return;
        }
        if (cmd.params.contains("today_stream_off")) {   // F7.18: the step-7 gate -- today's streaming off while switched
            voxFoundTodayStreamOff = cmd.params["today_stream_off"].get<bool>();
            response = {{"today_stream_off", voxFoundTodayStreamOff}};
            return;
        }
        if (cmd.params.contains("span_edit_stub")) {   // F7.19 (tests): region edits leave the spans alone (the red)
            voxFoundSpanEditStub = cmd.params["span_edit_stub"].get<bool>();
            response = {{"span_edit_stub", voxFoundSpanEditStub}};
            return;
        }
        if (cmd.params.contains("span_store_stub")) {   // F7.18 (tests): the span store not used (the red)
            voxFoundSpanStoreStub = cmd.params["span_store_stub"].get<bool>();
            if (voxFoundWorldReads) voxFoundSpansFill();
            response = {{"span_store_stub", voxFoundSpanStoreStub}};
            return;
        }
        if (cmd.params.value("span_check", false)) {   // F7.18: S1 store vs today's chunks, S2 store vs the generator
            size_t storeChunks = 0, both = 0, vsChunks = 0, vsGen = 0, spans = 0, withSpans = 0;
            nlohmann::json firstBad = nlohmann::json::array();
            WorldGenerator* g = chunkManager ? chunkManager->getStreamingGenerator() : nullptr;
            auto same = [](const ChunkManager::WaterSpanList& a, const ChunkManager::WaterSpanList& b) {
                if (a.size() != b.size()) return false;
                for (size_t k = 0; k < a.size(); ++k)
                    if (a[k].x != b[k].x || a[k].z != b[k].z || a[k].bottom != b[k].bottom || a[k].top != b[k].top) return false;
                return true;
            };
            if (chunkManager && chunkManager->voxFoundSpansOn())
                for (const auto& [cc, l] : chunkManager->voxFoundSpanStore()) {
                    ++storeChunks;
                    spans += l.size();
                    withSpans += !l.empty();
                    if (g && !same(l, Phyxel::waterSpansForChunk(*g, cc))) {
                        ++vsGen;
                        if (firstBad.size() < 5) firstBad.push_back({{"chunk", {cc.x, cc.y, cc.z}}, {"vs", "generator"}});
                    }
                    if (const Chunk* ch = chunkManager->getChunkAtCoord(cc)) {
                        ++both;
                        if (!same(l, ch->getWaterSpans())) {
                            ++vsChunks;
                            if (firstBad.size() < 5) firstBad.push_back({{"chunk", {cc.x, cc.y, cc.z}}, {"vs", "chunk"}});
                        }
                    }
                }
            response = {{"store_on", chunkManager && chunkManager->voxFoundSpansOn()}, {"store_chunks", storeChunks},
                        {"chunks_with_spans", withSpans}, {"spans", spans}, {"resident_in_both", both},
                        {"mismatch_vs_chunks", vsChunks}, {"mismatch_vs_generator", vsGen}, {"first_bad", firstBad},
                        {"today_chunks_resident", chunkManager ? chunkManager->chunkMap.size() : 0},
                        {"edited_chunks", chunkManager ? chunkManager->voxFoundEditedSpans().size() : 0},
                        {"loaded_edited", voxFoundSpansLoaded}, {"span_edit_cells", voxFoundSpanEditCells}};
            return;
        }
        if (cmd.params.contains("overlap_stub")) {   // F8.4 (tests): overlapping structures not cleared (the red)
            voxFoundOverlapStub = cmd.params["overlap_stub"].get<bool>();
            response = {{"overlap_stub", voxFoundOverlapStub}};
            return;
        }
        if (cmd.params.value("struct_registry", false)) {   // F8.4
            response = voxFoundStructuresJson();
            response["count"] = voxFoundStructures.size();
            return;
        }
        if (cmd.params.contains("bounds_stub")) {   // F8.3b (tests): the switched world's play bounds not set (the red)
            voxFoundBoundsStub = cmd.params["bounds_stub"].get<bool>();
            if (voxFoundBoundsStub && chunkManager) chunkManager->clearVoxFoundPlayBounds();
            response = {{"bounds_stub", voxFoundBoundsStub}};
            return;
        }
        if (cmd.params.contains("rapid_stub")) {   // F8.2 (tests): a new rig does not bake the previous one (the red)
            voxFoundRapidStub = cmd.params["rapid_stub"].get<bool>();
            response = {{"rapid_stub", voxFoundRapidStub}};
            return;
        }
        if (cmd.params.value("settle_rubble", false)) {   // F8.2: the current world rig's rubble into whole voxels
            response = voxFoundSettleRubble();
            return;
        }
        if (cmd.params.value("last_break", false)) {   // F8.2: the last switched-world break's report
            response = voxFoundLastBreak;
            return;
        }
        if (cmd.params.value("census", false)) {   // F8.2: every material's voxel count over the region's stores
            std::vector<uint64_t> n(256, 0);
            for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {   // F7.27: by block (was a dense copy per chunk:
                const auto& st = voxFoundEdit.chunk(i).store;            // a 252-chunk region took over 30 min)
                const glm::ivec3 nb = st.dim() / 8;
                for (int bx = 0; bx < nb.x; ++bx)
                    for (int by = 0; by < nb.y; ++by)
                        for (int bz = 0; bz < nb.z; ++bz) {
                            uint8_t um = 0;
                            if (st.uniformBlock({bx, by, bz}, um)) { n[um] += 512; continue; }
                            for (int x = 0; x < 8; ++x)
                                for (int y = 0; y < 8; ++y)
                                    for (int z = 0; z < 8; ++z) ++n[st.at({bx * 8 + x, by * 8 + y, bz * 8 + z})];
                        }
            }
            nlohmann::json byName = nlohmann::json::object();
            for (size_t m = 1; m < 256; ++m)
                if (n[m]) byName[m < voxFoundPalette.size() ? voxFoundPalette[m] : "id" + std::to_string(m)] = n[m];
            response = {{"census", byName}, {"chunks", voxFoundEdit.chunkCount()}};
            return;
        }
        if (cmd.params.value("boot_status", false)) {   // F7.16
            response = {{"pending", voxFoundBootPending}, {"report", voxFoundBootReport},
                        {"configured", !voxFoundBootConfig.is_null()}};
            return;
        }
        if (cmd.params.contains("water_sync_stub")) {   // F7.17 (tests): the water sim's resyncs skipped (the red)
            voxFoundWaterSyncStub = cmd.params["water_sync_stub"].get<bool>();
            response = {{"water_sync_stub", voxFoundWaterSyncStub}};
            return;
        }
        if (cmd.params.contains("tree_stub")) {   // F7.14 (tests): tree-matter reads not routed (the red)
            voxFoundTreeStub = cmd.params["tree_stub"].get<bool>();
            response = {{"tree_stub", voxFoundTreeStub}};
            return;
        }
        if (cmd.params.contains("writer_stub")) {   // F7.13 (tests): the old-grid writers not routed (the red)
            voxFoundWriterStub = cmd.params["writer_stub"].get<bool>();
            response = {{"writer_stub", voxFoundWriterStub}};
            return;
        }
        if (cmd.params.value("writer_log", false)) {   // F7.13: the hook's writes by label
            nlohmann::json byLabel = nlohmann::json::object();
            for (const auto& [label, e] : voxFoundWriterLog) byLabel[label] = {{"calls", e.first}, {"voxels", e.second}};
            response = {{"by_label", byLabel}};
            return;
        }
        if (cmd.params.value("struct_log", false)) {   // F7.12: the new-store placements so far ({"log_clear":true} empties)
            response = {{"placements", voxFoundCanvasLog}};
            if (cmd.params.value("log_clear", false)) voxFoundCanvasLog = nlohmann::json::array();
            return;
        }
        if (cmd.params.contains("struct_check")) {   // F7.11 W3: solid voxels above the floor in a box (no canvas cells)
            const auto& sc = cmd.params["struct_check"];
            const glm::ivec3 lo(sc["lo"][0].get<int>(), sc["lo"][1].get<int>(), sc["lo"][2].get<int>());
            const glm::ivec3 hi(sc["hi"][0].get<int>(), sc["hi"][1].get<int>(), sc["hi"][2].get<int>());
            const int floor = sc["floor"].get<int>();
            size_t solid = 0, stray = 0;
            for (int x = lo.x; x <= hi.x; ++x)
                for (int y = floor; y <= hi.y; ++y)
                    for (int z = lo.z; z <= hi.z; ++z) {
                        const uint8_t m = voxFoundEdit.voxelAt({x, y, z});
                        if (!m) continue;
                        ++solid;
                        stray += !voxFoundLastCanvasCells.count({x, y, z});
                    }
            response = {{"solid_above_floor", solid}, {"not_from_canvas", stray},
                        {"canvas_cells", voxFoundLastCanvasCells.size()}};
            return;
        }
        if (cmd.params.contains("legacy_template_stub")) {   // D3a (tests): no C/S/M conversion (the red)
            voxFoundLegacyTemplateStub = cmd.params["legacy_template_stub"].get<bool>();
            response = {{"legacy_template_stub", voxFoundLegacyTemplateStub}};
            return;
        }
        if (cmd.params.contains("template_stub")) {   // F7.10 (tests): spawn_template not routed (the red)
            voxFoundTemplateStub = cmd.params["template_stub"].get<bool>();
            response = {{"template_stub", voxFoundTemplateStub}};
            return;
        }
        if (cmd.params.contains("stream") || cmd.params.contains("stream_stub") || cmd.params.value("stream_status", false) ||
            cmd.params.contains("stream_shift")) {   // F7.9b
            if (cmd.params.contains("stream")) {
                const bool want = cmd.params["stream"].get<bool>();
                // F7.9b: "bounded" planting depends on the region box (trees rooted outside it are dropped), so a moving
                // region would change the world under the player -- streaming needs the open window
                if (want && voxFoundRegionRequest.value("flora", false) &&
                    voxFoundRegionRequest.value("flora_window", std::string("bounded")) != "open") {
                    response = {{"error", "stream needs a region built with flora_window:\"open\" (bounded planting "
                                          "depends on the region box)"}};
                    return;
                }
                voxFoundStreamOn = want;
                if (chunkManager && voxFoundWorldReads && !voxFoundBoundsStub) {   // F8.3b: streaming = unbounded
                    const glm::ivec3 rlo = voxFoundEdit.chunkLo(), rhi = voxFoundEdit.chunkHi();
                    chunkManager->setVoxFoundPlayBounds(want ? Core::PlayBounds{}
                        : Core::PlayBounds::make(float(rlo.x * 32), float(rlo.z * 32), float((rhi.x + 1) * 32), float((rhi.z + 1) * 32)));
                }
            }
            if (cmd.params.contains("stream_stub")) voxFoundStreamStub = cmd.params["stream_stub"].get<bool>();
            if (cmd.params.contains("stream_bg")) voxFoundStreamBg = cmd.params["stream_bg"].get<bool>();   // F7.9c
            nlohmann::json shift;
            if (cmd.params.contains("stream_shift")) {
                const auto& s = cmd.params["stream_shift"];
                shift = voxFoundStreamShift(glm::ivec3(s[0].get<int>(), s[1].get<int>(), s[2].get<int>()));
            }
            const auto& rg = voxFoundRegionRequest.is_null() ? nlohmann::json() : voxFoundRegionRequest["region"];
            response = {{"stream", voxFoundStreamOn}, {"stream_stub", voxFoundStreamStub}, {"shifts", voxFoundStreamShifts},
                        {"last_shift_ms", voxFoundStreamLastMs}, {"max_shift_ms", voxFoundStreamMaxMs},
                        {"cached_chunks", voxFoundStreamCache.size()}, {"region", rg},
                        {"switched", voxFoundWorldReads != nullptr}, {"stream_bg", voxFoundStreamBg},
                        {"job_pending", voxFoundStreamJob != nullptr}, {"max_slice_ms", voxFoundStreamMaxSliceMs},
                        {"last_bg", voxFoundStreamLastBg}, {"faces", voxFoundPreview.liveFaces()},
                        {"play_bounds", chunkManager ? chunkManager->playBoundsSource() : ""}};
            if (!shift.is_null()) response["shift"] = shift;
            return;
        }
        if (cmd.params.contains("phys_stub")) {   // F7.8 (tests): the switch sets no physics overrides (the red)
            voxFoundPhysStub = cmd.params["phys_stub"].get<bool>();
            response = {{"phys_stub", voxFoundPhysStub}};
            return;
        }
        if (cmd.params.contains("phys_probe")) {   // F7.8: VoxelDynamicsWorld::overlapsTerrain at points vs the region
            auto* vw = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr;
            const float he = cmd.params.value("half_m", 0.02f);
            size_t agree = 0, n = 0, solidRegion = 0;
            nlohmann::json bad = nlohmann::json::array();
            for (const auto& pj : cmd.params["phys_probe"]) {
                const glm::vec3 p(pj[0].get<float>(), pj[1].get<float>(), pj[2].get<float>());
                const bool phys = vw && vw->overlapsTerrain(p, glm::vec3(he));
                const bool reg = voxFoundEdit.voxelAt(glm::ivec3(glm::floor(p * 8.0f))) != 0;
                ++n;
                solidRegion += reg;
                if (phys == reg) ++agree;
                else if (bad.size() < 20) bad.push_back({{"p", {p.x, p.y, p.z}}, {"physics", phys}, {"region", reg}});
            }
            response = {{"probes", n}, {"agree", agree}, {"region_solid", solidRegion}, {"disagree", bad},
                        {"overrides", vw ? vw->gridOverrideCount() : 0}};
            return;
        }
        if (cmd.params.contains("lod_stub")) {   // F7.6d (tests): edits mark no chunk's coarse levels dirty (the red)
            voxFoundLodStub = cmd.params["lod_stub"].get<bool>();
            response = {{"lod_stub", voxFoundLodStub}};
            return;
        }
        if (cmd.params.contains("lod_refresh")) {   // F7.6d: refresh n dirty chunks now (0 = all)
            response = voxFoundLodRefresh(size_t(std::max(0, cmd.params.value("lod_refresh", 0))));
            return;
        }
        if (cmd.params.value("lod_check", false)) {   // F7.6d: every chunk's coarse levels vs a fresh refresh
            response = voxFoundLodCheck();
            return;
        }
        if (cmd.params.value("lod_status", false)) {
            response = {{"levels", voxFoundLodEditLevels}, {"dirty", voxFoundLodDirty.size()},
                        {"refreshed", voxFoundLodRefreshed}, {"max_chunk_ms", voxFoundLodMaxChunkMs},
                        {"mean_chunk_ms", voxFoundLodRefreshed ? voxFoundLodSumChunkMs / double(voxFoundLodRefreshed) : 0.0},
                        {"last_select_ms", voxFoundLodLastSelectMs}, {"draw_level", voxFoundPreview.drawLevel},
                        {"lod_on", voxFoundPreview.lodOn}, {"lod_selected", !voxFoundPreview.lodFirst.empty()},
                        {"lod_stub", voxFoundLodStub}};
            return;
        }
        if (cmd.params.contains("api_stub")) {   // F7.6c (tests): the voxel API not routed (the red)
            voxFoundApiStub = cmd.params["api_stub"].get<bool>();
            response = {{"api_stub", voxFoundApiStub}};
            return;
        }
        if (cmd.params.contains("undo_stub")) {   // F7.6b (tests): /api/undo ignores the history (the red)
            voxFoundUndoStub = cmd.params["undo_stub"].get<bool>();
            response = {{"undo_stub", voxFoundUndoStub}};
            return;
        }
        if (cmd.params.contains("save_stub")) {   // F7.6a (tests): save_world skips the switched region (the red)
            voxFoundSaveStub = cmd.params["save_stub"].get<bool>();
            response = {{"save_stub", voxFoundSaveStub}};
            return;
        }
        if (cmd.params.contains("damage_stub")) {   // F7.5 (tests): the next switch-on sets the override but it is ignored
            voxFoundDamageStub = cmd.params["damage_stub"].get<bool>();
            response = {{"damage_stub", voxFoundDamageStub}};
            return;
        }
        if (cmd.params.contains("occ_stub")) {   // F7.3b (tests): the next switch-on sets the source but the pass ignores it
            voxFoundOccStub = cmd.params["occ_stub"].get<bool>();
            response = {{"occ_stub", voxFoundOccStub}};
            return;
        }
        if (cmd.params.value("occupancy_stats", false)) {   // F7.3b: what the light occupancy pool holds
            const auto st = renderCoordinator->lightOccupancyStats();
            response["occupancy"] = {{"from_new_store", renderCoordinator->voxFoundOccupancyActive()},
                                     {"resident_chunks", st.residentChunks}, {"mixed_cubes", st.mixedCubes},
                                     {"loaded_chunks", renderCoordinator->lightOccupancyLoadedChunks()},
                                     {"out_of_box", renderCoordinator->lightOccupancyOutOfBoxChunks()},
                                     {"residency_backlog", renderCoordinator->lightOccupancyResidencyBacklog()},
                                     {"edit_backlog", renderCoordinator->lightOccupancyEditBacklog()},
                                     {"dropped", st.droppedChunks}, {"pack_ms", st.lastPackMs}};
            return;
        }
        if (cmd.params.contains("edit_stub") || cmd.params.contains("edit_verify")) {
            if (cmd.params.contains("edit_stub")) voxFoundEditStub = cmd.params["edit_stub"].get<bool>();
            if (cmd.params.contains("edit_verify")) voxFoundEditVerify = cmd.params["edit_verify"].get<bool>();
            response = {{"edit_stub", voxFoundEditStub}, {"edit_verify", voxFoundEditVerify}};
            return;
        }
        if (cmd.params.contains("edit_action")) {
            if (!voxelInteractionSystem || !camera) {
                response = {{"error", "edit_action: no interaction system / camera"}};
                return;
            }
            const auto& ea = cmd.params["edit_action"];
            const std::string action = ea.value("action", std::string("break")), mode = ea.value("mode", std::string("cube"));
            const glm::vec3 camPos = camera->getPosition(), camFront = camera->getFront();
            const auto t0 = std::chrono::steady_clock::now();
            voxelInteractionSystem->updateHoverFromRay(camPos, camFront);
            const WorldEditHit hov = voxelInteractionSystem->worldEditHover();
            voxFoundLastEdit = nlohmann::json::object();
            if (action == "break") {
                if (mode == "cube") voxelInteractionSystem->breakHoveredCube(camPos);
                else if (mode == "subcube") voxelInteractionSystem->breakHoveredSubcube();
                else voxelInteractionSystem->breakHoveredMicrocube();
            } else {
                if (mode == "cube") voxelInteractionSystem->placeVoxelAtHover();
                else if (mode == "subcube") voxelInteractionSystem->placeSubcubeAtHover();
                else voxelInteractionSystem->placeMicrocubeAtHover();
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            response = {{"action", action}, {"mode", mode}, {"editor_set", voxelInteractionSystem->worldEditor() != nullptr},
                        {"hover_hit", hov.hit}, {"hover_voxel", {hov.voxel.x, hov.voxel.y, hov.voxel.z}},
                        {"hover_normal", {hov.normal.x, hov.normal.y, hov.normal.z}}, {"hover_material", hov.material},
                        {"voxels_changed", voxelInteractionSystem->lastEditVoxels()}, {"action_ms", ms},
                        {"edit", voxFoundLastEdit}};
            return;
        }
        if (cmd.params.contains("world_switch_stub")) {
            voxFoundWorldSwitchStub = cmd.params["world_switch_stub"].get<bool>();
            if (!cmd.params.contains("world_switch") && !cmd.params.contains("reads_check") &&
                !cmd.params.contains("nav_check")) {   // alone: must not fall through to a default preview build
                response = {{"world_switch_stub", voxFoundWorldSwitchStub}};
                return;
            }
        }
        if (cmd.params.contains("world_switch")) {
            const bool on = cmd.params["world_switch"].get<bool>();
            if (on && voxFoundEdit.empty()) {
                response = {{"error", "world_switch: no editable region (build one: region + editable:true)"}};
                return;
            }
            if (chunkManager) chunkManager->setVoxFoundReads(nullptr);
            voxFoundWorldReads.reset();
            if (voxelInteractionSystem) voxelInteractionSystem->setWorldEditor(nullptr);   // F7.4
            voxFoundWorldEditor.reset();
            renderCoordinator->setVoxFoundOccupancy(nullptr);   // F7.3b
            DamageSystem::setDestructionOverride(nullptr);       // F7.5
            nlohmann::json loadReport;
            if (on) voxFoundSwitchLoadSaved = cmd.params.value("load_saved", true);   // F7.9b: a shift reuses it
            if (on && (voxFoundSwitchLoadSaved || !voxFoundStreamCache.empty())) {   // F7.6a / F7.9b
                const auto t0 = std::chrono::steady_clock::now();
                loadReport = loadVoxFoundSaved(voxFoundSwitchLoadSaved);   // false: the stream cache only
                if (voxFoundSwitchLoadSaved) voxFoundStructuresLoad();   // F8.4
                voxFoundSpansLoadPending = voxFoundSwitchLoadSaved && !voxFoundSpansRestored;   // F7.19: once per switch-on (not per stream shift)
                loadReport["wall_ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            }
            voxFoundPhysSwitch(on);   // F7.8: the physics occupancy follows the switch
            if (!on && waterManager && !voxFoundWaterSyncStub) waterManager->syncSolidsFromChunks();   // F7.17: back to today
            if (!on && chunkManager) chunkManager->clearVoxFoundPlayBounds();   // F8.3b
            if (!on && renderCoordinator && !voxFoundStreamSwapping) renderCoordinator->setVoxFoundGrass({});   // F7.29
            if (!on && chunkManager && !voxFoundStreamSwapping) {   // F7.18: today's chunks hold the spans again
                chunkManager->setVoxFoundSpansOn(false);
                chunkManager->bumpWaterSpanRevision();
                voxFoundSpansRestored = false;   // F7.19: the next switch-on reads the save again
            }
            if (chunkManager) {       // F7.13: the old-grid writers write the switched world
                if (on && !voxFoundWriterStub)
                    chunkManager->setVoxFoundWrite([this](const std::vector<std::pair<glm::ivec3, std::string>>& vox,
                                                          const std::string& label) -> size_t {
                        std::vector<std::pair<glm::ivec3, uint8_t>> cells;
                        cells.reserve(vox.size());
                        std::map<std::string, uint8_t> ids;
                        for (const auto& [p, name] : vox) {
                            auto it = ids.find(name);
                            if (it == ids.end()) {
                                std::string merr;
                                const int m = voxFoundMaterial(name, &merr);
                                it = ids.emplace(name, uint8_t(m < 0 ? 0 : m)).first;   // unknown material: air (logged)
                                if (m < 0) LOG_WARN("VoxFound", "writer: unknown material '" + name + "' written as air");
                            }
                            cells.push_back({p, it->second});
                        }
                        const nlohmann::json r = voxFoundPaintCells(label, cells);
                        const size_t changed = r.value("voxels_changed", size_t(0));
                        auto& e = voxFoundWriterLog[label];
                        ++e.first;
                        e.second += changed;
                        return changed;
                    });
                else
                    chunkManager->setVoxFoundWrite(nullptr);
                if (on && !voxFoundWriterStub)   // F7.13: whole-cube clears as boxes (RegionEdit::applyBox), one assemble
                    chunkManager->setVoxFoundClearBoxes([this](const std::vector<std::pair<glm::ivec3, glm::ivec3>>& boxes,
                                                               const std::string& label) -> size_t {
                        using namespace Core::VoxFound;
                        if (boxes.empty()) return 0;
                        glm::ivec3 lo(std::numeric_limits<int>::max()), hi(std::numeric_limits<int>::min());
                        for (const auto& [a, b] : boxes) lo = glm::min(lo, a), hi = glm::max(hi, b);
                        const auto read = [this](glm::ivec3 p) { return voxFoundEdit.voxelAt(p); };
                        const bool pending = voxFoundHistory.begin(label, lo, hi, read);   // over 16 M voxels: not recorded
                        std::set<size_t> rebuilt, changed;
                        size_t voxels = 0;
                        RegionEdit::Stats all;
                        {   // ONE masked edit over the boxes' bound: a single incremental rebuild (per-box applyBox
                            // calls each paid a rebuild: ~1,900 of them for a village's clearing)
                            std::set<std::tuple<int, int, int>> cubes;
                            for (const auto& [a, b] : boxes)
                                for (int y = a.y >> 3; y <= b.y >> 3; ++y) cubes.insert({a.x >> 3, y, a.z >> 3});
                            const auto inside = [&cubes](glm::ivec3 p) {
                                return cubes.count({p.x >> 3, p.y >> 3, p.z >> 3}) != 0;
                            };
                            std::string err;
                            const RegionEdit::Stats st = voxFoundEdit.apply(lo, hi, inside, 0, nullptr, &err);
                            voxels += st.voxelsChanged;
                            rebuilt.insert(st.rebuilt.begin(), st.rebuilt.end());
                            changed.insert(st.changed.begin(), st.changed.end());
                            all.boxLo = st.boxLo;
                            all.boxHi = st.boxHi;
                        }
                        all.rebuilt.assign(rebuilt.begin(), rebuilt.end());
                        all.changed.assign(changed.begin(), changed.end());
                        voxFoundOccBump(all.rebuilt);
                        voxFoundLodMark(all);
                        if (pending) voxFoundHistory.commit(read);
                        if (voxFoundBuilder)
                            for (size_t i : all.rebuilt) voxFoundBuilder->forgetResident(voxFoundEdit.residentKey(i));
                        voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex, &all.changed);
                        auto& e = voxFoundWriterLog[label];
                        ++e.first;
                        e.second += voxels;
                        return voxels;
                    });
                else
                    chunkManager->setVoxFoundClearBoxes(nullptr);
                if (on && !voxFoundTreeStub)   // F7.14: tree matter = a Leaf* / Log* voxel in the cube's block
                    chunkManager->setVoxFoundTreeCell([this](const glm::ivec3& cube) -> bool {
                        const glm::ivec3 c(cube.x >> 5, cube.y >> 5, cube.z >> 5);
                        const Core::VoxFound::BlockStore* st = voxFoundEdit.chunkStore(c);
                        if (!st) return false;
                        auto veg = [this](uint8_t m) {
                            return m && m < voxFoundPalette.size() &&
                                   (voxFoundPalette[m].rfind("Leaf", 0) == 0 || voxFoundPalette[m].rfind("Log", 0) == 0);
                        };
                        const glm::ivec3 b(cube.x & 31, cube.y & 31, cube.z & 31);   // the cube's block in the chunk
                        uint8_t u = 0;
                        if (st->uniformBlock(b, u)) return veg(u);
                        for (int k = 0; k < 512; ++k)
                            if (veg(st->get(b * 8 + glm::ivec3(k >> 6, (k >> 3) & 7, k & 7)))) return true;
                        return false;
                    });
                else
                    chunkManager->setVoxFoundTreeCell(nullptr);
            }
            if (on) {
                voxFoundWorldEditor = makeRegionWorldEditor(*this);   // F7.4
                if (voxelInteractionSystem && !voxFoundEditStub) voxelInteractionSystem->setWorldEditor(voxFoundWorldEditor.get());
                voxFoundOccSource.chunks = [this] { return voxFoundEdit.chunkCoords(); };   // F7.3b
                voxFoundOccSource.storeOf = [this](glm::ivec3 c) { return voxFoundEdit.chunkStore(c); };
                voxFoundOccSource.revisionOf = [this](glm::ivec3 c) {
                    auto it = voxFoundOccRevision.find({c.x, c.y, c.z});
                    return 1u + (it == voxFoundOccRevision.end() ? 0u : it->second);   // never 0 (today's revisions start there)
                };
                renderCoordinator->setVoxFoundOccupancy(&voxFoundOccSource, voxFoundOccStub);
                DamageSystem::setDestructionOverride(   // F7.5: blasts / spells / chops break the switched world
                    [this](const glm::vec3& c, float radius, float energy, const glm::vec3& dir, DamageResult& res) {
                        nlohmann::json rep;
                        if (!voxFoundWorldBreak(c, radius, energy, dir, &rep)) return false;
                        res.voxelsBroken = int(voxFoundBodiesLab.worldRemoved);
                        res.debrisSpawned = rep.value("bodies", 0);
                        return true;
                    },
                    voxFoundDamageStub);
                voxFoundWorldReads = std::make_unique<WorldReads>(
                    [this](glm::ivec3 c) { return voxFoundEdit.chunkStore(c); }, voxFoundEdit.chunkCoords());
                if (chunkManager && !voxFoundWorldSwitchStub) chunkManager->setVoxFoundReads(voxFoundWorldReads.get());
                if (waterManager && !voxFoundWaterSyncStub) waterManager->syncSolidsFromChunks();   // F7.17: the region's solids
                if (chunkManager && !voxFoundBoundsStub) {   // F8.3b: the switched world's play bounds
                    const glm::ivec3 rlo = voxFoundEdit.chunkLo(), rhi = voxFoundEdit.chunkHi();
                    chunkManager->setVoxFoundPlayBounds(voxFoundStreamOn ? Core::PlayBounds{}
                        : Core::PlayBounds::make(float(rlo.x * 32), float(rlo.z * 32), float((rhi.x + 1) * 32), float((rhi.z + 1) * 32)));
                }
                voxFoundSpansFill();   // F7.18: the water spans of the region's chunks (no Chunk object needed)
                voxFoundGrassFill();   // F7.29: the grass blades of the region's ground
            }
            renderCoordinator->setHideWorld(on);
            voxFoundPreview.shown = on;
            response = {{"world_switch", on}, {"reads_routed", chunkManager && chunkManager->voxFoundReads() != nullptr},
                        {"editor_set", voxelInteractionSystem && voxelInteractionSystem->worldEditor() != nullptr},
                        {"region_chunks", voxFoundEdit.chunkCount()}, {"stub", voxFoundWorldSwitchStub}};
            if (!loadReport.is_null()) response["loaded"] = loadReport;   // F7.6a
            if (!cmd.params.contains("reads_check") && !cmd.params.contains("nav_check")) return;
        }
        if (cmd.params.contains("reads_check") && chunkManager) {
            const int n = cmd.params["reads_check"].value("n", 4096);
            std::vector<glm::ivec3> cs = voxFoundEdit.chunkCoords();
            size_t sameRegion = 0, checked = 0, kinds[3] = {0, 0, 0};
            for (int i = 0; i < n && !cs.empty(); ++i) {
                uint32_t h = uint32_t(i) * 2654435761u ^ 0x9e3779b9u;
                h ^= h >> 15;
                h *= 0x85ebca6bu;
                h ^= h >> 13;
                const glm::ivec3 c = cs[h % cs.size()];
                const glm::ivec3 cube = c * 32 + glm::ivec3(int((h >> 8) % 32), int((h >> 13) % 32), int((h >> 18) % 32));
                int solid = 0;   // the region's rule: 512 voxels of the cube's block
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 8; ++y)
                        for (int z = 0; z < 8; ++z) solid += voxFoundEdit.voxelAt(cube * 8 + glm::ivec3(x, y, z)) != 0;
                const auto rule = solid == 0 ? VoxelLocation::EMPTY : solid == 512 ? VoxelLocation::CUBE
                                                                                    : VoxelLocation::SUBDIVIDED;
                ++kinds[rule];
                sameRegion += chunkManager->getVoxelTypeAt(cube) == rule;
                ++checked;
            }
            response["reads_check"] = {{"checked", checked}, {"equal_to_region_rule", sameRegion},
                                       {"region_kinds", {kinds[0], kinds[1], kinds[2]}},
                                       {"routed", chunkManager->voxFoundReads() != nullptr}};
        }
        if (cmd.params.contains("nav_check") && chunkManager && npcManager) {
            const auto& nj = cmd.params["nav_check"];
            auto ground = [&](const nlohmann::json& xz) {   // [x,y,z] = the feet as given; [x,z] = on the top solid cube
                if (xz.size() == 3) return glm::vec3(xz.at(0).get<float>(), xz.at(1).get<float>(), xz.at(2).get<float>());
                const int x = xz.at(0).get<int>(), z = xz.at(1).get<int>();
                for (int y = 63; y >= 0; --y)
                    if (chunkManager->hasVoxelAt({x, y, z})) return glm::vec3(x + 0.5f, float(y + 1), z + 0.5f);
                return glm::vec3(x + 0.5f, -1.0f, z + 0.5f);
            };
            const glm::vec3 from = ground(nj.at("from")), to = ground(nj.at("to"));
            const auto t0 = std::chrono::steady_clock::now();
            npcManager->buildNavGrid();
            const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            nlohmann::json nr = {{"from", {from.x, from.y, from.z}}, {"to", {to.x, to.y, to.z}}, {"build_ms", buildMs},
                                 {"routed", chunkManager->voxFoundReads() != nullptr}};
            if (const auto* g = npcManager->getNavGraph()) {
                Core::NavAgentProfile agent;
                const auto p = g->findPath(from, to, agent);
                nr["found"] = p.found;
                nr["nodes"] = p.nodes.size();
                nr["cells_per_m"] = g->cellsPerM();   // F7.22: 8 while routed to the switched world
            } else {
                nr["found"] = false;
                nr["error"] = "no nav graph";
            }
            response["nav_check"] = nr;
        }
        if (cmd.params.contains("column_top") && chunkManager) {   // F7.3: [[x,z],...] -> the top solid cube per column
            nlohmann::json tops = nlohmann::json::array();
            for (const auto& xz : cmd.params["column_top"]) {
                const int x = xz.at(0).get<int>(), z = xz.at(1).get<int>();
                int top = -1;
                for (int y = 63; y >= 0 && top < 0; --y)
                    if (chunkManager->hasVoxelAt({x, y, z})) top = y;
                tops.push_back({x, top, z});
            }
            response["column_top"] = tops;
        }
        if (cmd.params.contains("world_switch") || cmd.params.contains("reads_check") || cmd.params.contains("nav_check") ||
            cmd.params.contains("column_top"))
            return;
        if (cmd.params.contains("hide_world") || cmd.params.contains("show_preview") || cmd.params.contains("chunk_cull") ||
            cmd.params.contains("occlusion") || cmd.params.contains("mdi") || cmd.params.contains("shadow_cull") ||
            cmd.params.contains("shadow_mdi") || cmd.params.contains("face_pool") || cmd.params.contains("tjunction") ||
            cmd.params.contains("tj_px")) {
            if (cmd.params.contains("hide_world")) renderCoordinator->setHideWorld(cmd.params.value("hide_world", false));
            if (cmd.params.contains("show_preview")) voxFoundPreview.shown = cmd.params.value("show_preview", true);
            if (cmd.params.contains("chunk_cull")) renderCoordinator->setVoxFoundChunkCull(cmd.params.value("chunk_cull", true));
            if (!cmd.params.contains("type") && !cmd.params.contains("region")) {
                response = {{"hide_world", renderCoordinator->hideWorld()}, {"show_preview", voxFoundPreview.shown},
                            {"chunk_cull", renderCoordinator->voxFoundChunkCull()},
                            {"occlusion", renderCoordinator->voxFoundOcclusion()},
                            {"mdi", renderCoordinator->voxFoundMdi()},
                            {"shadow_cull", renderCoordinator->voxFoundShadowCull()},
                            {"shadow_mdi", renderCoordinator->voxFoundShadowMdi()},
                            {"face_pool", renderCoordinator->voxFoundFacePool()},
                            {"tjunction", renderCoordinator->voxFoundTJunction()},
                            {"tj_px", renderCoordinator->voxFoundTjPx()}};
                return;
            }
        }
        if (cmd.params.value("stats", false)) {   // what the last frame actually drew (main + shadow passes)
            const auto sc = renderCoordinator->voxFoundShadowCasters();
            response = {{"faces_drawn", renderCoordinator->voxFoundPreviewFacesDrawn()},
                        {"sprigs_drawn", renderCoordinator->voxFoundPreviewSprigsDrawn()},
                        {"shadow_faces_mid", sc[0]}, {"shadow_faces_near", sc[1]}, {"shadow_sprigs_mid", sc[2]},
                        {"ranges_drawn", renderCoordinator->voxFoundRangesDrawn()},
                        {"ranges_culled", renderCoordinator->voxFoundRangesCulled()},
                        {"chunk_cull", renderCoordinator->voxFoundChunkCull()},
                        {"ranges_occluded", renderCoordinator->voxFoundRangesOccluded()},
                        {"occlusion", renderCoordinator->voxFoundOcclusion()},
                        {"mdi", renderCoordinator->voxFoundMdi()},
                        {"clusters", renderCoordinator->voxFoundCullStats()[0]},
                        {"clusters_drawn", renderCoordinator->voxFoundCullStats()[1]},
                        {"mdi_compact", voxFoundPreview.mdiCompact},   // F2.15f: clusters_drawn = appended commands
                        {"upload_ms_last", {renderCoordinator->voxFoundUploadMs()[0], renderCoordinator->voxFoundUploadMs()[1],
                                            renderCoordinator->voxFoundUploadMs()[2]}},   // F2.16: faces, sprigs, clusters
                        {"face_pool", [&] {   // F2.17
                             const auto ps = renderCoordinator->voxFoundPoolStats();
                             return nlohmann::json{{"on", renderCoordinator->voxFoundFacePool()},
                                                   {"device_local", ps.deviceLocal},
                                                   {"uploads_partial", ps.partial}, {"uploads_full", ps.full},
                                                   {"last_kind", ps.lastKind == 2 ? "partial" : ps.lastKind == 1 ? "full" : "none"},
                                                   {"last_face_bytes", ps.lastFaceBytes},
                                                   {"last_cluster_slots", ps.lastClusterSlots},
                                                   {"last_record_ms", ps.lastMs},
                                                   {"live_faces", voxFoundPreview.liveFaces()},
                                                   {"image_faces", voxFoundPreview.faces.size()},
                                                   {"pooled", voxFoundPreview.pooled}};
                         }()},
                        {"cluster_faces_drawn", renderCoordinator->voxFoundCullStats()[2]},
                        {"faces_by_level", renderCoordinator->voxFoundFacesByLevel()},   // F2.14, last measured
                        {"lod_auto", voxFoundPreview.lodOn}, {"lod_px", voxFoundPreview.lodPxPerVoxel},
                        {"lod_focal_px", renderCoordinator->voxFoundFocalPx()},
                        {"lod_code", renderCoordinator->voxFoundLodCode()},
                        {"lod_casters", voxFoundPreview.lodCasters},
                        {"lod_sprig", voxFoundPreview.lodSprigOn}, {"lod_sprig_level", voxFoundPreview.lodSprigLevel},
                        {"sprig_chunks_handed_off", renderCoordinator->voxFoundSprigChunksHandedOff()},
                        {"shadow_faces_by_level_mid", renderCoordinator->voxFoundShadowFacesByLevel()[0]},
                        {"shadow_faces_by_level_near", renderCoordinator->voxFoundShadowFacesByLevel()[1]},
                        {"shadow_lod_floor", renderCoordinator->voxFoundShadowFloor()},
                        {"cluster_blocks", voxFoundPreview.clusterBlocks},
                        {"shadow_cull", renderCoordinator->voxFoundShadowCull()},
                        {"shadow_mdi", renderCoordinator->voxFoundShadowMdi()},
                        {"shadow_ranges_mid", renderCoordinator->voxFoundShadowCullStats(0)[0]},
                        {"shadow_ranges_near", renderCoordinator->voxFoundShadowCullStats(1)[0]},
                        {"shadow_clusters_mid", renderCoordinator->voxFoundShadowCullStats(0)[1]},
                        {"shadow_clusters_near", renderCoordinator->voxFoundShadowCullStats(1)[1]},
                        {"shadow_cluster_faces_mid", renderCoordinator->voxFoundShadowCullStats(0)[2]},
                        {"shadow_cluster_faces_near", renderCoordinator->voxFoundShadowCullStats(1)[2]},
                        {"preview_faces", voxFoundPreview.faces.size()},
                        {"preview_sprigs", voxFoundPreview.liveSprigs()}};
            return;
        }
        if (!voxFoundBuilder) {
            voxFoundBuilder = std::make_unique<FaceBuilderGpu>();
            std::string gerr;
            const std::string spv = Core::AssetManager::instance().resolveShader("voxfound_faces.comp.spv");
            if (!voxFoundBuilder->init(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice(),
                                       // F2.16: its own queue, so an edit's fence does not wait behind queued frames
                                       vulkanDevice->getAsyncComputeQueue(), vulkanDevice->getAsyncComputeQueueFamily(),
                                       std::filesystem::path(spv).parent_path().string(), &gerr)) {
                voxFoundBuilder.reset();
                response = {{"error", "FaceBuilderGpu init: " + gerr}};
                return;
            }
        }
        const std::string typeName = cmd.params.value("type", std::string("Perlin"));
        WorldGenerator::GenerationType type = WorldGenerator::GenerationType::Perlin;
        if (typeName == "Mountains") type = WorldGenerator::GenerationType::Mountains;
        else if (typeName == "Flat") type = WorldGenerator::GenerationType::Flat;
        else if (typeName != "Perlin" && cmd.params.value("source", std::string("generator")) == "generator") {
            // F7.27: a converted source (today / world_db) uses no generator -- its type does not matter
            response = {{"error", "type must be Perlin, Mountains or Flat"}};
            return;
        }
        const uint32_t seed = cmd.params.value("seed", 7u);
        if (cmd.params.contains("region")) {
            handleVoxFoundRegion(cmd, int(type), typeName, seed, response);
            return;
        }
        const int x0 = cmd.params.value("x", 0), z0 = cmd.params.value("z", 0);
        const int size = cmd.params.value("size", 32);
        const bool flora = cmd.params.value("flora", false);
        const Outside outside = cmd.params.value("outside", std::string("air")) == "skip" ? Outside::Skip : Outside::Air;
        if (size < 1 || size > kMaxChunkVoxels / kVoxelsPerM) {
            response = {{"error", "size must be 1..32 metres (one chunk)"}};
            return;
        }
        WorldGenerator gen(type, seed);
        gen.loadBiomes("resources/biomes.json");
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (int x = x0 - 1; x <= x0 + size; ++x)
            for (int z = z0 - 1; z <= z0 + size; ++z) {
                const float h = gen.sampleSurface(x, z).bedHeight;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        // One chunk is 32 m tall: 2 m under the lowest ground, up to 26 m of headroom for trees.
        const int y0 = int(std::floor(lo)) - 2;
        const bool trees = flora || (cmd.params.contains("templates") && cmd.params["templates"].is_array());
        const int top = int(std::ceil(hi)) + 2 + (trees ? 26 : 0);
        const int ySize = std::min(32, top - y0);
        if (int(std::ceil(hi)) + 1 > y0 + 32) {
            response = {{"error", "ground relief over this area exceeds one 32 m chunk"}, {"relief_m", hi - lo}};
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        VoxelArea area = buildTerrainArea(gen, {x0, y0, z0}, {size, ySize, size});
        FloraStampStats fs;
        if (flora) fs = stampFlora(area, gen, "resources/templates_g8/nature");
        // "templates":[{"name":"forge_oak_m","x":8,"z":8}] Ã¢â‚¬â€ stamp named 1/8 m library templates (tree_forge
        // --grid 8 output) with stampFlora's rule: footprint centred on metre column (x, z) of the area, base on
        // the ground there. For side-by-side A/B against today's grid-3 template spawned at the same spot.
        nlohmann::json stampedTemplates = nlohmann::json::array();
        if (cmd.params.contains("templates") && cmd.params["templates"].is_array()) {
            for (const auto& tj : cmd.params["templates"]) {
                const std::string name = tj.value("name", std::string());
                const GridTemplate t = loadGridTemplate("resources/templates_g8/nature/" + name + ".voxel");
                if (!t.error.empty()) {
                    stampedTemplates.push_back({{"name", name}, {"error", t.error}});
                    continue;
                }
                const int cx = tj.value("x", 0) * kVoxelsPerM + kVoxelsPerM / 2;
                const int cz = tj.value("z", 0) * kVoxelsPerM + kVoxelsPerM / 2;
                int gy = 0;
                for (int vy = area.grid.dim.y - 1; vy >= 0; --vy)
                    if (area.grid.in({cx, vy, cz}) && area.grid.mat[area.grid.idx({cx, vy, cz})] != 0) { gy = vy + 1; break; }
                const glm::ivec3 base(cx - t.bounds.x / 2, gy, cz - t.bounds.z / 2);
                size_t written = 0, clipped = 0;
                for (const auto& b : t.boxes) {
                    uint8_t m = 0;
                    for (size_t i = 1; i < area.palette.size(); ++i)
                        if (area.palette[i] == b.material) m = uint8_t(i);
                    if (m == 0) { m = uint8_t(area.palette.size()); area.palette.push_back(b.material); }
                    for (int x = 0; x < b.size.x; ++x)
                        for (int y = 0; y < b.size.y; ++y)
                            for (int z = 0; z < b.size.z; ++z) {
                                const glm::ivec3 q = base + b.pos + glm::ivec3(x, y, z);
                                if (!area.grid.in(q)) { ++clipped; continue; }
                                area.grid.mat[area.grid.idx(q)] = m;
                                ++written;
                            }
                }
                stampedTemplates.push_back({{"name", name}, {"cells", written}, {"clipped", clipped},
                                            {"bounds_cells", {t.bounds.x, t.bounds.y, t.bounds.z}},
                                            {"base_voxel", {base.x, base.y, base.z}}});
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        // F2.3 leaves: materials the engine draws as leaf cards (MaterialDef::billboarded) emit no solid faces;
        // their exposed cells become sprigs at kSprigsPerM2 (Mesher.h). "leaves":"voxels" draws them solid (A/B).
        const bool leafCards = cmd.params.value("leaves", std::string("cards")) != "voxels";
        SprigParams sprigParams;
        std::vector<std::string> cardMaterials;
        for (size_t m = 1; m < area.palette.size(); ++m) {
            const auto* md = Core::MaterialRegistry::instance().getMaterial(area.palette[m]);
            if (md && md->billboarded) {
                sprigParams.cards.set(uint8_t(m));
                cardMaterials.push_back(area.palette[m]);
            }
        }
        sprigParams.worldOriginVoxel = area.originVoxel;
        sprigParams.threshold24 =
            sprigThreshold24(cmd.params.value("sprigs_per_m2", kSprigsPerM2), kVoxelsPerM);
        std::vector<PackedFace> packed;
        std::string berr;
        if (!voxFoundBuilder->build(area.grid, outside, packed, &berr, leafCards ? &sprigParams : nullptr)) {
            response = {{"error", berr}};
            return;
        }
        const auto t2 = std::chrono::steady_clock::now();
        std::vector<PackedFace> sprigRecs;
        packed.erase(std::remove_if(packed.begin(), packed.end(),
                                    [&](const PackedFace& f) {
                                        if (!isSprig(f)) return false;
                                        sprigRecs.push_back(f);
                                        return true;
                                    }),
                     packed.end());
        // Texture per (material, face): MaterialRegistry by palette name, faceID 0 +Z 1 -Z 2 +X 3 -X 4 +Y 5 -Y.
        std::vector<std::array<uint16_t, 6>> tex(area.palette.size());
        for (size_t m = 0; m < area.palette.size(); ++m)
            for (int f = 0; f < 6; ++f)
                tex[m][f] = m == 0 ? 0 : Core::MaterialRegistry::instance().getTextureIndex(area.palette[m], f);
        // F2.5: the 8 B record is drawn as-is; the texture comes from this device table (Preview.h).
        voxFoundPreview.faceTex.assign(kFaceTexWords, 0u);
        for (size_t m = 0; m < tex.size() && m < 256; ++m)
            for (int f = 0; f < 6; ++f) voxFoundPreview.faceTex[m * 6 + f] = tex[m][f];
        voxFoundPreview.faces = packed;
        voxFoundPreview.chunks.clear();   // one area: drawn at originMetres
        voxFoundEdit.clear();             // F2.16: edits are for region previews
        voxFoundPreview.clusters.clear();   // F2.11 MDI is for region previews only
        // Sprig instance: today's leaf texture (face 0, as ChunkRenderManager), full sky, no block light.
        voxFoundPreview.sprigs.resize(sprigRecs.size());
        for (size_t i = 0; i < sprigRecs.size(); ++i) {
            const uint32_t m = (sprigRecs[i].b >> 16) & 0xFFu;
            voxFoundPreview.sprigs[i] = {(sprigRecs[i].a & 0xFFFFFFu) | (15u << 24),
                                         m < tex.size() ? uint32_t(tex[m][0]) : 0u};
        }
        glm::dvec3 at(x0, y0, z0);
        if (cmd.params.contains("at") && cmd.params["at"].is_array() && cmd.params["at"].size() >= 3)
            at = {cmd.params["at"][0].get<double>(), cmd.params["at"][1].get<double>(), cmd.params["at"][2].get<double>()};
        voxFoundPreview.originMetres = at;
        ++voxFoundPreview.revision;
        renderCoordinator->setVoxFoundPreview(&voxFoundPreview);
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        response = {{"provenance", "engine WorldGenerator (" + typeName + " seed " + std::to_string(seed) +
                                       ") sampled at 1/8 m by VoxFound::buildTerrainArea" +
                                       (flora ? " + engine flora plan stamped from resources/templates_g8/nature" : "") +
                                       "; faces by FaceBuilderGpu"},
                    {"voxels", {area.grid.dim.x, area.grid.dim.y, area.grid.dim.z}},
                    {"area_origin_m", {x0, y0, z0}},
                    {"drawn_at_m", {at.x, at.y, at.z}},
                    {"solid_voxels", area.grid.solidCount()},
                    {"faces", packed.size()},
                    {"leaves", leafCards ? "cards" : "voxels"},
                    {"card_materials", cardMaterials},
                    {"sprigs", sprigRecs.size()},
                    {"sprigs_per_m2", cmd.params.value("sprigs_per_m2", kSprigsPerM2)},
                    {"sprig_instance_bytes", sprigRecs.size() * sizeof(PreviewSprig)},
                    {"face_record_bytes", packed.size() * sizeof(PackedFace)},
                    {"preview_instance_bytes", packed.size() * sizeof(PackedFace)},
                    {"face_tex_table_bytes", kFaceTexWords * sizeof(uint32_t)},
                    {"gpu_face_build_ms", voxFoundBuilder->lastGpuMs()},
                    {"generate_ms", ms(t0, t1)},
                    {"build_wall_ms", ms(t1, t2)},
                    {"palette", area.palette},
                    {"templates_stamped", stampedTemplates},
                    {"flora_stamped", fs.stamped},
                    {"flora_missing", fs.missing}};
        return;
}

int VoxFoundWorld::voxFoundMaterial(const std::string& name, std::string* err) {
    using namespace Core::VoxFound;
    if (name.empty() || name == "air") return 0;
    const auto* md = Core::MaterialRegistry::instance().getMaterial(name);
    if (!md) {
        if (err) *err = "unknown material " + name;
        return -1;
    }
    size_t m = 1;
    while (m < voxFoundPalette.size() && voxFoundPalette[m] != name) ++m;
    if (m == voxFoundPalette.size()) {
        if (m >= 256) {
            if (err) *err = "palette full (256)";
            return -1;
        }
        voxFoundPalette.push_back(name);
        voxFoundLeafTex.resize(voxFoundPalette.size(), 0);
        for (int f = 0; f < 6; ++f) {
            const uint16_t t = Core::MaterialRegistry::instance().getTextureIndex(name, f);
            voxFoundPreview.faceTex[m * 6 + size_t(f)] = t;
            if (f == 0) voxFoundLeafTex[m] = t;
        }
        CardSet cards = voxFoundEdit.cards(), see = voxFoundEdit.seeThrough();
        if (md->billboarded) cards.set(uint8_t(m));
        if (md->billboarded || md->alpha < 0.99f) see.set(uint8_t(m));
        voxFoundEdit.setMaterialSets(cards, see);
    }
    return int(m);
}

// docs/VoxelFoundationPlan.md F3.3 -- POST /api/debug/voxfound_preview {"save":true}: every chunk of the editable region
// that an edit (or a load) changed is encoded with codec v3 (BlockStore::encodeV3) and written to world.db
// (voxfound_chunk_blobs); chunks the generator still produces are not saved (Ã‚Â§3.5a point 7).
void VoxFoundWorld::handleVoxFoundSave(nlohmann::json& response) {
    using namespace Core::VoxFound;
    auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
    if (!ws || !ws->getDb()) {
        response = {{"error", "no world storage (world.db) open"}};
        return;
    }
    if (voxFoundEdit.empty()) {
        response = {{"error", "no editable region preview"}};
        return;
    }
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    size_t saved = 0, bytes = 0;
    double encodeMs = 0.0, writeMs = 0.0;
    nlohmann::json rows = nlohmann::json::array();
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        const auto& c = voxFoundEdit.chunk(i);
        if (!c.edited) continue;
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<uint8_t> blob = c.store.encodeV3(voxFoundPalette, &err);
        const auto t1 = std::chrono::steady_clock::now();
        if (blob.empty()) {
            response = {{"error", err}, {"chunk", {c.chunk.x, c.chunk.y, c.chunk.z}}};
            return;
        }
        if (!ws->saveVoxFoundBlob(c.chunk, blob)) {
            response = {{"error", "world.db write failed"}, {"chunk", {c.chunk.x, c.chunk.y, c.chunk.z}}};
            return;
        }
        const auto t2 = std::chrono::steady_clock::now();
        encodeMs += ms(t0, t1);
        writeMs += ms(t1, t2);
        ++saved;
        bytes += blob.size();
        uint64_t bh = 1469598103934665603ull;   // FNV-1a of the blob: a re-save of a reload must be byte-identical
        for (uint8_t v : blob) bh = (bh ^ v) * 1099511628211ull;
        rows.push_back({{"chunk", {c.chunk.x, c.chunk.y, c.chunk.z}}, {"bytes", blob.size()}, {"blob_hash", std::to_string(bh)},
                        {"store_bytes", c.store.memoryBytes()}, {"encode_ms", ms(t0, t1)}});
    }
    ws->setMeta("voxfound_structures", voxFoundStructuresJson().dump());   // F8.4: the structure registry
    if (chunkManager && chunkManager->voxFoundSpansOn()) {   // F7.19: the store's edited water spans
        nlohmann::json sp = nlohmann::json::array();
        for (const auto& [cc, l] : chunkManager->voxFoundEditedSpans()) {
            nlohmann::json e = nlohmann::json::array();
            for (const auto& s : l) e.push_back({s.x, s.z, s.bottom, s.top});
            sp.push_back({{"c", {cc.x, cc.y, cc.z}}, {"s", e}});
        }
        ws->setMeta("voxfound_spans", sp.dump());
    }
    size_t savedCache = 0;   // F7.9b: edited chunks outside the region (they left it while streaming)
    for (const auto& [k, blob] : voxFoundStreamCache) {
        const glm::ivec3 c(std::get<0>(k), std::get<1>(k), std::get<2>(k));
        if (!ws->saveVoxFoundBlob(c, blob)) {
            response = {{"error", "world.db write failed"}, {"chunk", {c.x, c.y, c.z}}};
            return;
        }
        ++savedCache;
        bytes += blob.size();
    }
    response = {{"saved_chunks", saved}, {"saved_from_stream_cache", savedCache}, {"region_chunks", voxFoundEdit.chunkCount()}, {"bytes", bytes},
                {"encode_ms", encodeMs}, {"write_ms", writeMs}, {"chunks", rows}, {"codec", "PXF3 v1"}};
}

// F3.3: every saved chunk (world.db voxfound_chunk_blobs) inside the editable region wins over the generator: decoded,
// its materials mapped to the region palette by name, and written through RegionEdit::loadChunk (the exact incremental
// rebuild; the neighbours' border planes follow). Then the drawn lists are assembled from the store.
nlohmann::json VoxFoundWorld::loadVoxFoundSaved(bool fromDb) {
    using namespace Core::VoxFound;
    auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
    if ((!ws || !ws->getDb()) && voxFoundStreamCache.empty()) return {{"error", "no world storage (world.db) open"}};
    size_t fromCache = 0;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    size_t loaded = 0, voxels = 0, bytes = 0;
    double decodeMs = 0.0, writeMs = 0.0;
    nlohmann::json errors = nlohmann::json::array();
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        const glm::ivec3 cc = voxFoundEdit.chunk(i).chunk;
        std::vector<uint8_t> blob;
        auto cit = voxFoundStreamCache.find({cc.x, cc.y, cc.z});   // F7.9b: a chunk that left the region edited wins
        if (cit != voxFoundStreamCache.end()) {
            blob = std::move(cit->second);
            voxFoundStreamCache.erase(cit);
            ++fromCache;
        } else if (!fromDb || !ws || !ws->getDb() || !ws->loadVoxFoundBlob(cc, blob)) {
            continue;
        }
        BlockStore saved;
        std::vector<std::string> names;
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        if (!BlockStore::decodeV3(blob.data(), blob.size(), saved, names, &err)) {
            errors.push_back({{"chunk", {cc.x, cc.y, cc.z}}, {"error", err}});
            continue;
        }
        std::vector<uint8_t> map(names.size(), 0);
        bool ok = true;
        for (size_t k = 1; k < names.size() && ok; ++k) {
            const int m = voxFoundMaterial(names[k], &err);
            ok = m >= 0;
            if (ok) map[k] = uint8_t(m);
        }
        if (!ok) {
            errors.push_back({{"chunk", {cc.x, cc.y, cc.z}}, {"error", err}});
            continue;
        }
        saved.remapMaterials(map);
        const auto t1 = std::chrono::steady_clock::now();
        const RegionEdit::Stats st = voxFoundEdit.loadChunk(i, saved);
        voxFoundOccBump(std::vector<size_t>{i});   // F7.6a: the switched light occupancy repacks a loaded chunk
        voxFoundLodMark(st);                       // F7.6d
        const auto t2 = std::chrono::steady_clock::now();
        decodeMs += ms(t0, t1);
        writeMs += ms(t1, t2);
        ++loaded;
        voxels += st.voxelsChanged;
        bytes += blob.size();
    }
    if (loaded) voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex);
    return {{"loaded_chunks", loaded}, {"from_stream_cache", fromCache}, {"bytes", bytes}, {"voxels_written", voxels}, {"decode_ms", decodeMs},
            {"load_rebuild_ms", writeMs}, {"errors", errors}};
}

// docs/VoxelFoundationPlan.md F2.16 (Ã‚Â§4.6 item 9) -- POST /api/debug/voxfound_preview {"edit":{...}}: edit a region
// preview built with {"editable":true}, rebuilt INCREMENTALLY (RegionEdit: only the rows, sprig cells and blocks the
// brush can change). Body: {"material":"air"|name, "lo":[x,y,z], "hi":[x,y,z]} (world 1/8 m voxels, inclusive) or
// {"material", "sphere":{"c":[x,y,z], "r":voxels}}; "verify":true also rebuilds every touched chunk fully with
// FaceBuilderGpu and compares faces, sprigs and connectivity. The drawn lists are re-uploaded whole (item 10).
// F7.4 (docs/VoxelFoundationPlan.md section 7 step 4): the editable region as the player's IWorldEditor -- picks are
// RegionEdit::pick (metres -> 1/8 m voxels), brushes the preview's own edit path (handleVoxFoundEdit: RegionEdit::brush +
// the incremental assemble), so the switched world is edited exactly as F2.16 / F3.1 measured.
class RegionWorldEditor : public IWorldEditor {
public:
    explicit RegionWorldEditor(VoxFoundWorld& app) : m_app(app) {}
    WorldEditHit pick(const glm::vec3& origin, const glm::vec3& dir, float maxMetres) const override {
        return m_app.voxFoundPick(origin, dir, maxMetres);
    }
    size_t brush(const glm::ivec3& voxel, int size, const std::string& material) override {
        return m_app.voxFoundBrush(voxel, size, material);
    }
private:
    VoxFoundWorld& m_app;
};

WorldEditHit VoxFoundWorld::voxFoundPick(const glm::vec3& origin, const glm::vec3& dir, float maxMetres) const {
    using namespace Core::VoxFound;
    WorldEditHit out;
    if (voxFoundEdit.empty() || glm::length(dir) <= 0.0f) return out;
    const RegionEdit::PickHit h = voxFoundEdit.pick(glm::dvec3(origin) * double(kVoxelsPerM), glm::dvec3(dir),
                                                    double(maxMetres) * double(kVoxelsPerM));
    out.hit = h.hit;
    out.voxel = h.voxel;
    out.normal = h.normal;
    out.material = h.material < voxFoundPalette.size() ? voxFoundPalette[h.material] : std::string();
    return out;
}

size_t VoxFoundWorld::voxFoundBrush(const glm::ivec3& voxel, int size, const std::string& material) {
    nlohmann::json ej = {{"brush", {{"at", {voxel.x, voxel.y, voxel.z}}, {"size", size}}},
                         {"material", material.empty() ? std::string("air") : material}, {"verify", voxFoundEditVerify}};
    nlohmann::json r;
    handleVoxFoundEdit(ej, r);
    voxFoundLastEdit = r;
    return r.value("voxels_changed", size_t(0));
}

std::unique_ptr<IWorldEditor> makeRegionWorldEditor(VoxFoundWorld& app) { return std::make_unique<RegionWorldEditor>(app); }

// F7.6d (docs/VoxelFoundationPlan.md section 7 step 6, "LOD tiers 2/3"): the coarse levels of an editable region kept
// current. An edit marks the chunks whose 1 m-margin area meets its box; each is refreshed from the store (its area =
// the store + the 1 m margin, beyond the region the nearest region voxel; LodBuilderGpu levels; FaceBuilderGpu meshes
// each level's cut as the build does) and spliced into Preview::levels[k]; when none are left the drawn selection is
// re-made.
void VoxFoundWorld::voxFoundLodMark(const Core::VoxFound::RegionEdit::Stats& st) {
    // F7.17: every region edit reaches here with its box -- the water sim re-samples the cubes under it
    if (waterManager && voxFoundWorldReads && !voxFoundWaterSyncStub && st.boxLo.x <= st.boxHi.x) {
        auto fd = [](int v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); };
        waterManager->syncSolidsBox({fd(st.boxLo.x), fd(st.boxLo.y), fd(st.boxLo.z)},
                                    {fd(st.boxHi.x), fd(st.boxHi.y), fd(st.boxHi.z)});
    }
    voxFoundSpanEdit(st);   // F7.19: and the water spans (today's voxel-edit callback, which region edits never fire)
    if (voxFoundWorldReads && st.boxLo.x <= st.boxHi.x) {   // F7.30: and the grass of the chunks it touched
        for (size_t i : voxFoundEdit.chunksNear(st.boxLo, st.boxHi, 8)) voxFoundGrassDirty.insert(i);
        voxFoundGrassLastEdit = std::chrono::steady_clock::now();
    }
    if (voxFoundLodEditLevels <= 0 || voxFoundLodStub) return;
    for (size_t i : voxFoundEdit.chunksNear(st.boxLo, st.boxHi, Core::VoxFound::kVoxelsPerM)) voxFoundLodDirty.insert(i);
}

bool VoxFoundWorld::voxFoundLodChunkFaces(size_t i, std::vector<std::vector<Core::VoxFound::PackedFace>>& perLevel,
                                        std::string* err) {
    using namespace Core::VoxFound;
    const int n = voxFoundLodEditLevels;
    if (n <= 0 || i >= voxFoundEdit.chunkCount() || !voxFoundBuilder) {
        if (err) *err = "no coarse levels / builder";
        return false;
    }
    const int M = kVoxelsPerM, N = kChunkM * kVoxelsPerM, A = N + 2 * M;
    const glm::ivec3 origin = voxFoundEdit.chunk(i).originVoxel;
    const glm::ivec3 rlo = voxFoundEdit.voxelLo(), rhi = voxFoundEdit.voxelHi();
    const VoxelGrid inner = voxFoundEdit.dense(i);
    VoxelGrid area;
    area.resize(glm::ivec3(A));
    auto outer = [&](int x, int y, int z) {   // the margin: the region's voxel, clamped into the region
        return voxFoundEdit.voxelAt(glm::clamp(origin - M + glm::ivec3(x, y, z), rlo, rhi));
    };
    for (int x = 0; x < A; ++x)
        for (int y = 0; y < A; ++y) {
            const bool rowIn = x >= M && x < M + N && y >= M && y < M + N;
            uint8_t* row = &area.mat[area.idx(glm::ivec3(x, y, 0))];
            if (rowIn) {
                std::copy_n(&inner.mat[inner.idx(glm::ivec3(x - M, y - M, 0))], N, row + M);
                for (int z = 0; z < M; ++z) row[z] = outer(x, y, z);
                for (int z = M + N; z < A; ++z) row[z] = outer(x, y, z);
            } else {
                for (int z = 0; z < A; ++z) row[z] = outer(x, y, z);
            }
        }
    std::vector<LodGrid> lv;
    if (voxFoundLodBuilder) {
        if (!voxFoundLodBuilder->build(area, n, lv, err)) return false;
    } else {
        LodGrid l = lodBase(area);
        for (int k = 1; k <= n; ++k) lv.push_back(l = lodDown(l));
    }
    SprigParams noSprigs = voxFoundEdit.sprigParams(i);
    noSprigs.threshold24 = 0;   // leaves stay cards (no faces), as the build's default variant
    perLevel.assign(size_t(n) + 1, {});
    for (int k = 1; k <= n; ++k) {
        const int sc = 1 << k;
        const glm::ivec3 off(M / sc), innerDim(N / sc);
        const VoxelGrid& g = lv[size_t(k - 1)].grid;
        std::vector<PackedFace> coarse;
        if (!voxFoundBuilder->build(subGrid(g, off, innerDim), Edges(neighboursOf(g, off, innerDim, Outside::Air)),
                                    coarse, err, &noSprigs))
            return false;
        for (const PackedFace& f : coarse)
            if (!isSprig(f)) perLevel[size_t(k)].push_back(lodScaleFace(f, k));
    }
    return true;
}

// F7.6d: chunk ci's range of level L replaced by fs (clusters as the build's addLevel: cells + direction groups)
static void voxFoundSpliceLevel(Core::VoxFound::Preview::Level& L, size_t ci, std::vector<Core::VoxFound::PackedFace> fs,
                                glm::ivec3 chunk, glm::ivec3 regionLo, int clusterBlocks, bool dirOctant) {
    using namespace Core::VoxFound;
    const size_t nc = L.firstFace.size();
    std::vector<std::vector<PreviewCluster>> cl(nc), cd(nc);
    for (const PreviewCluster& c : L.clusters) if (c.chunk < nc) cl[c.chunk].push_back(c);
    for (const PreviewCluster& c : L.clustersDir) if (c.chunk < nc) cd[c.chunk].push_back(c);
    const std::vector<FaceCluster> cells = clusterFaces(fs, clusterBlocks, dirOctant);   // reorders fs
    const std::vector<FaceCluster> groups = splitByDirection(fs, cells);
    Preview::Level out;
    out.firstFace.resize(nc);
    out.faceCount.resize(nc);
    out.leafCount.resize(nc);
    for (size_t j = 0; j < nc; ++j) {
        const uint32_t first = uint32_t(out.faces.size());
        out.firstFace[j] = first;
        if (j == ci) {
            out.faces.insert(out.faces.end(), fs.begin(), fs.end());
            for (int d = 0; d < 2; ++d)
                for (const FaceCluster& c : d ? groups : cells) {
                    PreviewCluster pcl;
                    const glm::vec3 base = glm::vec3((chunk - regionLo) * kChunkM);
                    pcl.lo = base + glm::vec3(c.lo) / float(kVoxelsPerM);
                    pcl.hi = base + glm::vec3(c.hi) / float(kVoxelsPerM);
                    pcl.firstFace = first + c.first;
                    pcl.count = c.count;
                    pcl.chunk = uint32_t(ci);
                    pcl.leafMass = false;
                    pcl.dir = c.dir;
                    (d ? out.clustersDir : out.clusters).push_back(pcl);
                }
            out.leafCount[j] = 0;
        } else {
            const uint32_t f0 = L.firstFace[j];
            out.faces.insert(out.faces.end(), L.faces.begin() + f0, L.faces.begin() + f0 + L.faceCount[j]);
            for (int d = 0; d < 2; ++d)
                for (PreviewCluster c : d ? cd[j] : cl[j]) {
                    c.firstFace = c.firstFace - f0 + first;
                    (d ? out.clustersDir : out.clusters).push_back(c);
                }
            out.leafCount[j] = j < L.leafCount.size() ? L.leafCount[j] : 0u;
        }
        out.faceCount[j] = uint32_t(out.faces.size()) - first;
    }
    L = std::move(out);
}

nlohmann::json VoxFoundWorld::voxFoundLodRefresh(size_t maxChunks) {
    using namespace Core::VoxFound;
    size_t done = 0;
    std::string err;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    double wall = 0.0;
    while (!voxFoundLodDirty.empty() && (maxChunks == 0 || done < maxChunks)) {
        const size_t i = *voxFoundLodDirty.begin();
        voxFoundLodDirty.erase(voxFoundLodDirty.begin());
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::vector<PackedFace>> perLevel;
        if (!voxFoundLodChunkFaces(i, perLevel, &err)) break;
        for (int k = 1; k <= voxFoundLodEditLevels && size_t(k) < voxFoundPreview.levels.size(); ++k)
            voxFoundSpliceLevel(voxFoundPreview.levels[size_t(k)], i, std::move(perLevel[size_t(k)]),
                                voxFoundEdit.chunk(i).chunk, voxFoundLodRegionLo, voxFoundLodClusterBlocks,
                                voxFoundLodDirOctant);
        const double t = ms(t0, std::chrono::steady_clock::now());
        voxFoundLodMaxChunkMs = std::max(voxFoundLodMaxChunkMs, t);
        voxFoundLodSumChunkMs += t;
        wall += t;
        ++voxFoundLodRefreshed;
        ++done;
    }
    bool reselected = false;
    if (done && voxFoundLodDirty.empty()) {   // the drawn copy re-made once the levels are all current
        const auto t0 = std::chrono::steady_clock::now();
        if (!voxFoundPreview.lodFirst.empty()) reselected = voxFoundPreview.selectLod();
        else if (voxFoundPreview.drawLevel > 0) reselected = voxFoundPreview.selectLevel(voxFoundPreview.drawLevel);
        voxFoundLodLastSelectMs = ms(t0, std::chrono::steady_clock::now());
    }
    return {{"refreshed", done}, {"dirty_left", voxFoundLodDirty.size()}, {"wall_ms", wall}, {"error", err},
            {"reselected", reselected}, {"select_ms", voxFoundLodLastSelectMs}, {"max_chunk_ms", voxFoundLodMaxChunkMs}};
}

nlohmann::json VoxFoundWorld::voxFoundLodCheck() {
    using namespace Core::VoxFound;
    auto key = [](const PackedFace& f) { return (uint64_t(f.a) << 32) | f.b; };
    nlohmann::json chunks = nlohmann::json::array();
    std::vector<size_t> bad(size_t(voxFoundLodEditLevels) + 1, 0), total(size_t(voxFoundLodEditLevels) + 1, 0);
    std::string err;
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        std::vector<std::vector<PackedFace>> perLevel;
        if (!voxFoundLodChunkFaces(i, perLevel, &err)) return {{"error", err}};
        nlohmann::json row = {{"chunk", {voxFoundEdit.chunk(i).chunk.x, voxFoundEdit.chunk(i).chunk.y,
                                         voxFoundEdit.chunk(i).chunk.z}}};
        nlohmann::json lv = nlohmann::json::array();
        for (int k = 1; k <= voxFoundLodEditLevels && size_t(k) < voxFoundPreview.levels.size(); ++k) {
            const Preview::Level& L = voxFoundPreview.levels[size_t(k)];
            std::vector<uint64_t> a, b;
            for (uint32_t f = L.firstFace[i]; f < L.firstFace[i] + L.faceCount[i]; ++f) a.push_back(key(L.faces[f]));
            for (const PackedFace& f : perLevel[size_t(k)]) b.push_back(key(f));
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::vector<uint64_t> d;
            std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
            bad[size_t(k)] += d.size();
            total[size_t(k)] += a.size();
            lv.push_back({{"level", k}, {"faces", a.size()}, {"fresh", b.size()}, {"mismatched", d.size()}});
        }
        row["levels"] = lv;
        chunks.push_back(row);
    }
    return {{"chunks", chunks}, {"mismatched_by_level", bad}, {"faces_by_level", total},
            {"dirty", voxFoundLodDirty.size()}};
}

// F7.10: listed cells written as ONE edit -- recorded for undo over their box (EditHistory), applyCells (the exact
// incremental path), then the light occupancy, coarse levels, resident device copies and drawn lists as an edit.
nlohmann::json VoxFoundWorld::voxFoundPaintCells(const std::string& label,
                                               const std::vector<std::pair<glm::ivec3, uint8_t>>& cells) {
    using namespace Core::VoxFound;
    if (cells.empty()) return {{"voxels_changed", 0}, {"undo", "no_change"}};
    glm::ivec3 lo(std::numeric_limits<int>::max()), hi(std::numeric_limits<int>::min());
    for (const auto& [p, m] : cells) lo = glm::min(lo, p), hi = glm::max(hi, p);
    const auto read = [this](glm::ivec3 p) { return voxFoundEdit.voxelAt(p); };
    const auto t0 = std::chrono::steady_clock::now();
    const bool pending = voxFoundHistory.begin(label, lo, hi, read);
    std::string err;
    RegionEdit::Stats st = voxFoundEdit.applyCells(cells, nullptr, &err);
    voxFoundOccBump(st.rebuilt);
    voxFoundLodMark(st);
    const bool recorded = pending && voxFoundHistory.commit(read);
    if (voxFoundBuilder)
        for (size_t i : st.rebuilt) voxFoundBuilder->forgetResident(voxFoundEdit.residentKey(i));
    voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex, &st.changed);
    size_t bad = 0, clipped = 0;   // read back: every listed cell inside the region holds its material
    nlohmann::json badSamples = nlohmann::json::array();
    const glm::ivec3 rlo = voxFoundEdit.voxelLo(), rhi = voxFoundEdit.voxelHi();
    for (const auto& [p, m] : cells) {
        if (glm::any(glm::lessThan(p, rlo)) || glm::any(glm::greaterThan(p, rhi))) {
            ++clipped;   // outside the region: not written (applyCells clips), reported
            continue;
        }
        const uint8_t got = voxFoundEdit.voxelAt(p);
        if (got == m) continue;
        ++bad;
        if (badSamples.size() < 6)
            badSamples.push_back({{"p", {p.x, p.y, p.z}}, {"want", m < voxFoundPalette.size() ? voxFoundPalette[m] : "?"},
                                  {"got", got < voxFoundPalette.size() ? voxFoundPalette[got] : "?"}});
    }
    return {{"voxels_changed", st.voxelsChanged}, {"lo", {lo.x, lo.y, lo.z}}, {"hi", {hi.x, hi.y, hi.z}},
            {"undo", recorded ? "recorded" : (pending ? "no_change" : "too_large")}, {"verify_bad", bad},
            {"verify_samples", badSamples}, {"cells", cells.size()}, {"clipped_outside_region", clipped},
            {"error", err},
            {"wall_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()}};
}

// F7.10 (docs/VoxelFoundationPlan.md F7's "other placers"): a static template on the switched world -- its 1/8 m version
// (resources/templates_g8, by stem or relative path), min corner at floor(position * 8), rotation = quarter turns about
// +Y, snap (default) = its lowest layer on the region's ground (the highest solid voxel under its footprint + 1).
nlohmann::json VoxFoundWorld::voxFoundSpawnTemplate(const nlohmann::json& params) {
    using namespace Core::VoxFound;
    namespace fs = std::filesystem;
    const std::string name = params.value("name", "");
    if (name.empty()) return {{"error", "Template name required"}};
    std::string path;
    const fs::path root = "resources/templates_g8";
    if (fs::exists(root / (name + ".voxel"))) path = (root / (name + ".voxel")).string();
    else if (fs::exists(root))
        for (const auto& e : fs::recursive_directory_iterator(root))
            if (e.is_regular_file() && e.path().stem().string() == name && e.path().extension() == ".voxel") {
                path = e.path().string();
                break;
            }
    // step 7 D3a: no 1/8 m version -> today's C / S / M template, converted (the PXB2 resampler)
    std::string legacyPath;
    if (path.empty() && !voxFoundLegacyTemplateStub) {
        const fs::path lroot = "resources/templates";
        if (fs::exists(lroot / (name + ".voxel"))) legacyPath = (lroot / (name + ".voxel")).string();
        else if (fs::exists(lroot))
            for (const auto& e : fs::recursive_directory_iterator(lroot))
                if (e.is_regular_file() && e.path().stem().string() == name && e.path().extension() == ".voxel") {
                    legacyPath = e.path().string();
                    break;
                }
    }
    if (params.contains("path")) {   // D3-core part 3 (the asset editor): this file, whatever library it is in
        const std::string pth = params["path"].get<std::string>();
        std::ifstream pin(pth);
        std::string head((std::istreambuf_iterator<char>(pin)), {});
        path.clear();
        legacyPath.clear();
        (head.find("# grid: 8") != std::string::npos ? path : legacyPath) = pth;
    }
    if (path.empty() && legacyPath.empty())
        return {{"success", false}, {"world", "voxfound"}, {"error", "no 1/8 m template " + name}};
    CsmTemplateStats csm;
    const GridTemplate t = path.empty() ? loadCsmTemplate(legacyPath, &csm) : loadGridTemplate(path);
    if (!t.error.empty()) return {{"success", false}, {"world", "voxfound"}, {"error", t.error}};
    if (t.cellsPerM != kVoxelsPerM)
        return {{"success", false}, {"world", "voxfound"}, {"error", "template grid is not 8 per metre"}};
    const int rot = ((params.value("rotation", 0) % 4) + 4) % 4;
    const glm::ivec3 B = t.bounds;
    auto rotate = [&](glm::ivec3 p) {   // quarter turns about +Y inside the bounds
        for (int r = 0; r < rot; ++r) p = glm::ivec3((r % 2 == 0 ? B.z : B.x) - 1 - p.z, p.y, p.x);
        return p;
    };
    std::vector<std::pair<glm::ivec3, uint8_t>> local;
    for (const auto& b : t.boxes) {
        std::string err;
        const int m = voxFoundMaterial(b.material, &err);
        if (m < 0) return {{"success", false}, {"world", "voxfound"}, {"error", err}};
        for (int x = 0; x < b.size.x; ++x)
            for (int y = 0; y < b.size.y; ++y)
                for (int z = 0; z < b.size.z; ++z) local.push_back({rotate(b.pos + glm::ivec3(x, y, z)), uint8_t(m)});
    }
    glm::vec3 pos(0.0f);
    if (params.contains("position"))
        pos = {params["position"].value("x", 0.0f), params["position"].value("y", 0.0f), params["position"].value("z", 0.0f)};
    glm::ivec3 origin(glm::floor(pos * float(kVoxelsPerM)));
    int groundTop = std::numeric_limits<int>::min();
    if (params.value("snap", true)) {   // the highest solid voxel under the footprint, searched down from origin + 16 m
        int minY = std::numeric_limits<int>::max();
        for (const auto& [p, m] : local) minY = std::min(minY, p.y);
        std::set<std::pair<int, int>> cols;
        for (const auto& [p, m] : local) cols.insert({p.x, p.z});
        for (const auto& [x, z] : cols)
            for (int y = origin.y + 16 * kVoxelsPerM; y > origin.y - 64 * kVoxelsPerM; --y)
                if (voxFoundEdit.voxelAt({origin.x + x, y, origin.z + z})) {
                    groundTop = std::max(groundTop, y);
                    break;
                }
        if (groundTop != std::numeric_limits<int>::min()) origin.y = groundTop + 1 - minY;
    }
    std::vector<std::pair<glm::ivec3, uint8_t>> cells;
    cells.reserve(local.size());
    for (const auto& [p, m] : local) cells.push_back({origin + p, m});
    nlohmann::json r = voxFoundPaintCells("spawn_template:" + name, cells);
    r["success"] = r.value("error", std::string()).empty();
    r["world"] = "voxfound";
    r["template"] = path.empty() ? legacyPath : path;
    if (path.empty())
        r["converted"] = {{"from", "C/S/M (loadCsmTemplate)"}, {"cubes", csm.cubes}, {"subcubes", csm.subcubes},
                          {"microcubes", csm.microcubes}, {"tints_dropped", csm.tintsDropped},
                          {"other_lines", csm.otherLines}, {"rescued", csm.rescued}, {"voxels", t.cells}};
    r["template_cells"] = local.size();
    r["origin"] = {origin.x, origin.y, origin.z};
    r["rotation"] = rot;
    if (groundTop != std::numeric_limits<int>::min()) r["snap_ground_top"] = groundTop;
    return r;
}

// F7.11: a realized 1/8 m shell on the switched world. Seated: story 0's walkable floor surface = the highest solid
// region voxel under the footprint + 1; excavated: the canvas box's air cells above the floor that are solid in the
// region become air (terrain bumps, trees); painted with the canvas's solid cells -- all ONE voxFoundPaintCells edit.
nlohmann::json VoxFoundWorld::voxFoundPlaceCanvas(const Core::MicroCanvas& canvas, int floorTopCell, int ox, int oz) {
    using namespace Core::VoxFound;
    if (canvas.cellsPerM() != kVoxelsPerM) return {{"success", false}, {"error", "canvas is not 1/8 m"}};
    const std::vector<glm::ivec3> occ = canvas.occupiedCells();
    if (occ.empty()) return {{"success", false}, {"error", "empty canvas"}};
    glm::ivec3 clo(std::numeric_limits<int>::max()), chi(std::numeric_limits<int>::min());
    for (const glm::ivec3& c : occ) clo = glm::min(clo, c), chi = glm::max(chi, c);
    const int ox8 = ox * kVoxelsPerM, oz8 = oz * kVoxelsPerM;
    // tree matter (the flora's Leaf* / Log* voxels) is not ground: the region's trees are voxels, not the placed
    // objects the old vegetation gate removes (found live: the first seat landed on a canopy 20 m up)
    std::vector<char> vegetation(256, 0);
    for (size_t m = 1; m < voxFoundPalette.size() && m < 256; ++m)
        vegetation[m] = voxFoundPalette[m].rfind("Leaf", 0) == 0 || voxFoundPalette[m].rfind("Log", 0) == 0;
    // F8.4: registered structures this one's footprint overlaps are cleared first (no stacking -- the old pipeline's
    // rule): air over each one's box from its floor surface up (its slab / foundation stay)
    nlohmann::json overlapsCleared = nlohmann::json::array();
    if (!voxFoundOverlapStub) {
        const int fx0 = ox8 + clo.x, fx1 = ox8 + chi.x, fz0 = oz8 + clo.z, fz1 = oz8 + chi.z;
        std::vector<VoxFoundStructure> keep;
        for (const auto& st : voxFoundStructures) {
            const bool overlap = st.lo.x <= fx1 && st.hi.x >= fx0 && st.lo.z <= fz1 && st.hi.z >= fz0;
            if (!overlap) {
                keep.push_back(st);
                continue;
            }
            std::vector<std::pair<glm::ivec3, uint8_t>> air;
            for (int x = st.lo.x; x <= st.hi.x; ++x)
                for (int y = st.floor; y <= st.hi.y; ++y)
                    for (int z = st.lo.z; z <= st.hi.z; ++z)
                        if (voxFoundEdit.voxelAt({x, y, z})) air.push_back({{x, y, z}, uint8_t(0)});
            const nlohmann::json cr = voxFoundPaintCells("clear_overlapping_structure", air);
            size_t lightsRemoved = 0;   // F7.21c: its lights go with it
            if (renderCoordinator)
                for (const auto& l : st.lights)
                    if (l.value("id", -1) >= 0 && renderCoordinator->getLightManager().removeLight(l["id"].get<int>()))
                        ++lightsRemoved;
            size_t propsRemoved = 0;   // F7.21d: and its item props
            if (itemPropManager)
                for (const auto& p : st.props)
                    if (itemPropManager->removeProp(p.get<std::string>())) ++propsRemoved;
            size_t fixturesRemoved = 0;   // F7.21e: and its fixture records (they own no voxels)
            if (placedObjectManager)
                for (const auto& f : st.fixtures)
                    if (placedObjectManager->remove(f.get<std::string>())) ++fixturesRemoved;
            overlapsCleared.push_back({{"id", st.id}, {"voxels", cr.value("voxels_changed", size_t(0))},
                                       {"lights_removed", lightsRemoved}, {"props_removed", propsRemoved},
                                       {"fixtures_removed", fixturesRemoved}});
        }
        voxFoundStructures.swap(keep);
    }
    // F8.3: the floor seats at the MEDIAN ground top under the footprint (the old pipeline's pad level,
    // StructureGenerator::planPadLevel) -- the highest top floated houses 10 m over a slope; each column is then filled
    // up to the shell's lowest cell (a foundation) and cut above the floor (the excavation below)
    std::map<std::pair<int, int>, int> colTop;
    std::set<std::pair<int, int>> cols;
    for (const glm::ivec3& c : occ) cols.insert({c.x, c.z});
    std::vector<int> tops;
    for (const auto& [x, z] : cols)
        for (int y = 160 * kVoxelsPerM; y > -64 * kVoxelsPerM; --y) {   // the region's column, from 160 m down
            const uint8_t m = voxFoundEdit.voxelAt({ox8 + x, y, oz8 + z});
            if (m && !vegetation[m]) {
                colTop[{x, z}] = y;
                tops.push_back(y);
                break;
            }
        }
    if (tops.empty()) return {{"success", false}, {"error", "no ground under the footprint"}};
    std::nth_element(tops.begin(), tops.begin() + tops.size() / 2, tops.end());
    const int groundTop = tops[tops.size() / 2];
    const glm::ivec3 origin(ox8, groundTop + 1 - floorTopCell, oz8);
    std::vector<std::pair<glm::ivec3, uint8_t>> cells;
    voxFoundLastCanvasCells.clear();
    std::map<std::string, uint8_t> ids;
    for (const glm::ivec3& c : occ) {
        const std::string name = canvas.materialAt(c.x, c.y, c.z);
        auto it = ids.find(name);
        if (it == ids.end()) {
            std::string err;
            const int m = voxFoundMaterial(name, &err);
            if (m < 0) return {{"success", false}, {"error", err}};
            it = ids.emplace(name, uint8_t(m)).first;
        }
        cells.push_back({origin + c, it->second});
        voxFoundLastCanvasCells.insert({origin.x + c.x, origin.y + c.y, origin.z + c.z});
    }
    size_t foundation = 0;   // F8.3: each footprint column filled from its ground to the shell's lowest cell
    {
        std::string ferr;
        const int dirt = voxFoundMaterial("Dirt", &ferr);
        std::map<std::pair<int, int>, int> colLow;   // the shell's lowest cell per column (world voxel)
        for (const glm::ivec3& c : occ) {
            auto it = colLow.find({c.x, c.z});
            const int y = origin.y + c.y;
            if (it == colLow.end() || y < it->second) colLow[{c.x, c.z}] = y;
        }
        for (const auto& [xz, low] : colLow) {
            auto t = colTop.find(xz);
            if (t == colTop.end() || dirt < 0) continue;
            for (int y = t->second + 1; y < low; ++y) {
                const glm::ivec3 w(ox8 + xz.first, y, oz8 + xz.second);
                if (voxFoundLastCanvasCells.count({w.x, w.y, w.z})) continue;
                cells.push_back({w, uint8_t(dirt)});
                ++foundation;
            }
        }
    }
    size_t clearedTrees = 0;   // tree matter in the footprint columns from the floor up (above the canvas box too)
    for (int x = clo.x; x <= chi.x; ++x)
        for (int z = clo.z; z <= chi.z; ++z)
            for (int y = groundTop + 1; y < 160 * kVoxelsPerM; ++y) {
                const glm::ivec3 w(ox8 + x, y, oz8 + z);
                const uint8_t m = voxFoundEdit.voxelAt(w);
                if (!m || !vegetation[m] || voxFoundLastCanvasCells.count({w.x, w.y, w.z})) continue;
                if (y - origin.y >= std::max(clo.y, floorTopCell) && y - origin.y <= chi.y) continue;   // the box pass
                cells.push_back({w, uint8_t(0)});
                ++clearedTrees;
            }
    size_t excavated = 0;   // the canvas box above the floor: region solids the canvas does not paint
    for (int x = clo.x; x <= chi.x; ++x)
        for (int y = std::max(clo.y, floorTopCell); y <= chi.y; ++y)
            for (int z = clo.z; z <= chi.z; ++z) {
                const glm::ivec3 w = origin + glm::ivec3(x, y, z);
                if (voxFoundLastCanvasCells.count({w.x, w.y, w.z}) || !voxFoundEdit.voxelAt(w)) continue;
                cells.push_back({w, uint8_t(0)});
                ++excavated;
            }
    {   // F8.3: one decision per cell, the first wins (the shell, then the foundation, then the clears)
        std::set<std::tuple<int, int, int>> seen;
        std::vector<std::pair<glm::ivec3, uint8_t>> once;
        once.reserve(cells.size());
        for (const auto& c : cells)
            if (seen.insert({c.first.x, c.first.y, c.first.z}).second) once.push_back(c);
        cells.swap(once);
    }
    nlohmann::json r = voxFoundPaintCells("build_structure", cells);
    r["success"] = r.value("error", std::string()).empty() && r.value("verify_bad", size_t(1)) == 0;
    if (r.value("error", std::string()).empty()) r.erase("error");   // F7.12: callers read any "error" key as a refusal
    r["world"] = "voxfound";
    r["canvas_cells"] = occ.size();
    r["excavated"] = excavated;
    r["cleared_tree_voxels"] = clearedTrees;
    r["foundation_voxels"] = foundation;   // F8.3
    r["overlaps_cleared"] = overlapsCleared;   // F8.4
    if (r.value("success", false)) {   // F8.4: registered
        VoxFoundStructure st;
        st.lo = origin + clo;
        st.hi = origin + chi;
        st.floor = groundTop + 1;
        st.id = voxFoundStructureNext++;
        voxFoundStructures.push_back(st);
        r["structure_id"] = st.id;
    }
    r["floor_y_voxel"] = groundTop + 1;
    r["origin"] = {origin.x, origin.y, origin.z};
    nlohmann::json logged = r;   // F7.12: kept for struct_log (without the bulky samples)
    logged.erase("verify_samples");
    voxFoundCanvasLog.push_back(logged);
    return r;
}

// F7.16: game.json world.voxfound = the switched world as a boot mode (read at project load, startup or runtime)
void VoxFoundWorld::voxFoundReadBootConfig(const std::string& gameJsonPath) {
    try {
        std::ifstream gin(gameJsonPath, std::ios::binary);
        if (!gin) return;
        std::string text((std::istreambuf_iterator<char>(gin)), std::istreambuf_iterator<char>());
        if (text.size() >= 3 && uint8_t(text[0]) == 0xEF && uint8_t(text[1]) == 0xBB && uint8_t(text[2]) == 0xBF)
            text.erase(0, 3);   // a UTF-8 BOM (PowerShell writes one)
        const nlohmann::json g = nlohmann::json::parse(text);
        // F7.27: the switched world is the DEFAULT -- a project world with no "voxfound" key boots switched (a
        // streaming world around the camera from the generator, as VoxFoundBoot; a fixed world from TODAY's resident
        // chunks through the converter); "voxfound": {"enabled": false} opts out
        // (a game.json with no "world" object -- a world that lives only in its DB -- is a fixed world: source today)
        const bool noWorld = !g.contains("world") || !g["world"].is_object();
        const bool topOptOut = g.contains("voxfound") && g["voxfound"].is_object() &&
                               !g["voxfound"].value("enabled", true);   // the opt-out for a project with no world object
        if (!topOptOut && g.contains("scenes") && g["scenes"].is_array()) {   // D3-scenes: every scene switched
            voxFoundMultiScene = true;
            LOG_INFO("VoxFound", "boot mode: MULTI-SCENE (each scene's region built at its load) for " + gameJsonPath);
            return;
        }
        if (!topOptOut && !g.contains("scenes") && (noWorld || !g["world"].contains("voxfound"))) {
            nlohmann::json w = defaultBootConfig(noWorld ? nlohmann::json() : g["world"]);
            voxFoundBootConfig = w;
            voxFoundTodayKeep = w["voxfound"].value("today_world", false);
            voxFoundBootPending = true;
            voxFoundBootFrames = 0;
            LOG_INFO("VoxFound", "boot mode: the DEFAULT (no world.voxfound) for " + gameJsonPath);
        } else if (g.contains("world") && g["world"].is_object() && g["world"].contains("voxfound") &&
            g["world"]["voxfound"].value("enabled", false)) {
            voxFoundBootConfig = g["world"];
            voxFoundBootPending = true;
            voxFoundBootFrames = 0;
            LOG_INFO("VoxFound", "boot mode configured from " + gameJsonPath);
        }
    } catch (const std::exception& e) {
        LOG_WARN("VoxFound", std::string("boot config: game.json not read: ") + e.what());
    }
}

// F7.16: the switched world as a boot mode -- the same sequence the tests drive by hand: the editable region around
// the camera (the world's type / seed / flora; flora_window open, which streaming needs), switch on, stream.
void VoxFoundWorld::voxFoundBoot() {
    const auto t0 = std::chrono::steady_clock::now();
    const nlohmann::json& w = voxFoundBootConfig;
    const nlohmann::json& v = w["voxfound"];
    std::vector<int> size = v.value("size", std::vector<int>{4, 2, 4});
    if (size.size() != 3) size = {4, 2, 4};
    const glm::vec3 cam = camera ? camera->getPosition() : glm::vec3(0.0f);
    const int cx = int(std::floor(cam.x / 32.0f)), cz = int(std::floor(cam.z / 32.0f));
    const int y0 = v.value("y_from", 0);
    glm::ivec3 lo(cx - size[0] / 2, y0, cz - size[2] / 2), hi = lo + glm::ivec3(size[0], size[1], size[2]) - 1;
    bool fromDb = v.value("source", std::string()) == "world_db";
    std::string dbFallback;
    if (fromDb) {   // D1: the box of the DB's STORED chunks (no Chunk object read)
        auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
        const std::vector<glm::ivec3> stored = ws ? ws->getAllChunkCoordinates() : std::vector<glm::ivec3>{};
        if (stored.empty()) {
            fromDb = false;
            dbFallback = "the DB holds no chunks: today's resident world converted instead";
        } else {
            lo = glm::ivec3(std::numeric_limits<int>::max());
            hi = glm::ivec3(std::numeric_limits<int>::min());
            for (const auto& c : stored) lo = glm::min(lo, c), hi = glm::max(hi, c);
        }
    }
    const bool fromToday = v.value("source", std::string()) == "today" || !dbFallback.empty();
    if (fromToday && chunkManager && !chunkManager->chunkMap.empty()) {   // F7.27: the box of today's resident chunks
        lo = glm::ivec3(std::numeric_limits<int>::max());
        hi = glm::ivec3(std::numeric_limits<int>::min());
        for (const auto& [cc, ch] : chunkManager->chunkMap)
            if (ch) lo = glm::min(lo, cc), hi = glm::max(hi, cc);
    }
    nlohmann::json req = {{"type", w.value("type", std::string("Perlin"))}, {"seed", w.value("seed", 0)},
                          {"flora", w.value("flora", true)}, {"editable", true}, {"hide_world", false},
                          {"flora_window", "open"},
                          {"region", {{"from", {lo.x, lo.y, lo.z}}, {"to", {hi.x, hi.y, hi.z}}}}};
    if (fromToday || fromDb) {
        req["source"] = fromDb ? "world_db" : "today";
        req["max_chunks"] = v.value("max_chunks", 4096);   // F7.28: a fixed world is converted whole
    }
    Core::APICommand c;
    nlohmann::json r = nlohmann::json::object(), on = nlohmann::json::object(), st = nlohmann::json::object();   // F7.27: never null
    c.params = req;
    handleVoxFoundPreview(c, r);
    const auto t1 = std::chrono::steady_clock::now();
    nlohmann::json overlay;
    if (!r.contains("error") && v.value("overlay_world_db", false)) {   // D1b: the DB's stored chunks over the generated
        voxFoundOverlayDb = true;
        std::vector<glm::ivec3> coords;
        for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) coords.push_back(voxFoundEdit.chunk(i).chunk);
        overlay = voxFoundOverlayStored(coords);
    }
    if (!r.contains("error")) {
        c.params = {{"world_switch", true}, {"load_saved", v.value("load_saved", true)}};
        handleVoxFoundPreview(c, on);
        if (v.value("stream", true)) {
            c.params = {{"stream", true}};
            handleVoxFoundPreview(c, st);
        }
        voxFoundTodayStreamOff = v.value("today_stream_off", false);   // F7.26: the step-7 gate from the boot
    }
    const auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    voxFoundBootReport = {{"booted", !r.contains("error") && !on.contains("error")}, {"region_request", req},
                          {"build_error", r.value("error", std::string())}, {"build_ms", ms(t0, t1)},
                          {"switch_ms", ms(t1, t2)}, {"total_ms", ms(t0, t2)}, {"loaded", on.value("loaded", nlohmann::json())},
                          {"stream", st.value("stream", false)}, {"camera", {cam.x, cam.y, cam.z}},
                          {"today_stream_off", voxFoundTodayStreamOff},
                          {"source", fromDb ? "world_db" : fromToday ? "today" : "generator"},
                          {"source_fallback", dbFallback}, {"default", v.value("default", false)}, {"overlay", overlay},
                          {"build_timing", {{"generate_ms", r.value("generate_ms", 0.0)}, {"build_wall_ms", r.value("build_wall_ms", 0.0)},
                                            {"gpu_face_build_ms_sum", r.value("gpu_face_build_ms_sum", 0.0)},
                                            {"region_lod_ms", r.value("region_lod_ms", 0.0)},
                                            {"convert_ms", r.contains("converted") && r["converted"].is_object()
                                                               ? r["converted"].value("convert_ms", 0.0) : 0.0},
                                            {"chunks_stored", r.contains("converted") && r["converted"].is_object()
                                                               ? r["converted"].value("chunks_stored", 0) : 0},
                                            {"chunks_missing", r.contains("converted") && r["converted"].is_object()
                                                               ? r["converted"].value("chunks_missing", 0) : 0}}}};   // F7.31 / D1
    LOG_INFO("VoxFound", "boot mode: " + voxFoundBootReport.dump());
}

// F7.18 (step-7 port 1): the switched world's water spans -- every region chunk's generation-time spans from
// WorldGenerator::waterSpansForChunk (what generateChunk stores on today's Chunk), into ChunkManager's span store, which
// is then the authority for every span reader / writer. Edited entries are kept; entries outside the region that were
// never edited are dropped.
void VoxFoundWorld::voxFoundSpansFill() {
    if (!chunkManager || voxFoundEdit.empty()) return;
    if (voxFoundSpanStoreStub) {   // the red: today's chunks stay the holders
        chunkManager->setVoxFoundSpansOn(false);
        return;
    }
    WorldGenerator* g = chunkManager->getStreamingGenerator();
    chunkManager->setVoxFoundSpansOn(true);
    if (voxFoundSpansLoadPending) {   // F7.19: the saved edited spans first (the fill never overwrites an edited entry)
        voxFoundSpansLoadPending = false;
        voxFoundSpansRestored = true;
        auto* ws = chunkManager->m_streamingManager.getWorldStorage();
        if (ws && ws->hasMeta("voxfound_spans")) {
            try {
                for (const auto& e : nlohmann::json::parse(ws->getMeta("voxfound_spans"))) {
                    ChunkManager::WaterSpanList l;
                    for (const auto& s : e.at("s"))
                        l.push_back({s[0].get<uint8_t>(), s[1].get<uint8_t>(), s[2].get<float>(), s[3].get<float>()});
                    chunkManager->restoreVoxFoundEditedSpans({e["c"][0].get<int>(), e["c"][1].get<int>(), e["c"][2].get<int>()},
                                                             std::move(l));
                    ++voxFoundSpansLoaded;
                }
            } catch (const std::exception& ex) {
                LOG_WARN("VoxFound", std::string("voxfound_spans not read: ") + ex.what());
            }
        }
    }
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        const glm::ivec3 cc = voxFoundEdit.chunk(i).chunk;
        chunkManager->fillVoxFoundSpans(cc, g ? Phyxel::waterSpansForChunk(*g, cc) : ChunkManager::WaterSpanList{});
    }
    chunkManager->retainVoxFoundSpans(voxFoundEdit.chunkLo(), voxFoundEdit.chunkHi());
    chunkManager->bumpWaterSpanRevision();
}

// F7.19 (W2): a region edit's span edit -- today's voxel-edit callback runs applyWaterSpanEdit per edited cube; region
// edits never fire it. Over the edit box's WET columns (a span in the store there), each 1 m cell is solid when >= 256
// of its 512 voxels are (the half rule) and goes through applyWaterSpanEdit (a no-op where nothing changes).
void VoxFoundWorld::voxFoundSpanEdit(const Core::VoxFound::RegionEdit::Stats& st) {
    if (!chunkManager || !chunkManager->voxFoundSpansOn() || !voxFoundWorldReads || voxFoundSpanEditStub) return;
    if (st.boxLo.x > st.boxHi.x) return;
    auto fd = [](int v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); };
    const glm::ivec3 lo(fd(st.boxLo.x), fd(st.boxLo.y), fd(st.boxLo.z)), hi(fd(st.boxHi.x), fd(st.boxHi.y), fd(st.boxHi.z));
    std::set<std::pair<int, int>> wet;
    chunkManager->forEachWaterSpans([&](const glm::ivec3& cc, const ChunkManager::WaterSpanList& l) {
        if (cc.x * 32 + 31 < lo.x || cc.x * 32 > hi.x || cc.z * 32 + 31 < lo.z || cc.z * 32 > hi.z) return;
        for (const auto& s : l) {
            const int x = cc.x * 32 + s.x, z = cc.z * 32 + s.z;
            if (x >= lo.x && x <= hi.x && z >= lo.z && z <= hi.z) wet.insert({x, z});
        }
    });
    // the cells a span edit can move: the box's y range, one cube below it (a dug bed lets the water down)
    for (const auto& [x, z] : wet)
        for (int y = lo.y - 1; y <= hi.y; ++y) {
            int n = 0;
            for (int a = 0; a < 8; ++a)
                for (int b = 0; b < 8; ++b)
                    for (int c = 0; c < 8; ++c) n += voxFoundEdit.voxelAt({x * 8 + a, y * 8 + b, z * 8 + c}) != 0;
            applyWaterSpanEdit(x, y, z, n >= 256);
            ++voxFoundSpanEditCells;
        }
}

// F7.21c: a furnished structure's lights (the forge registered them after the sink) belong to its registry entry
void VoxFoundWorld::voxFoundStructureAttachLights(int structureId, const nlohmann::json& lights) {
    for (auto& s : voxFoundStructures)
        if (s.id == structureId) {
            for (const auto& l : lights) s.lights.push_back(l);
            return;
        }
}

// F7.29: the switched world's grass -- today's rule (a grass-topped surface with air above sprouts one blade clump per
// metre cell, today's grass.vert fans it out) on the 1/8 m ground: per metre column, the top solid voxel of its centre
// voxel column; a Grass / GrassForest / GrassSavanna top with air above gets an instance at that metre, its sink below
// the cube top (7 - v % 8 eighths) in tex bits 20-22. Stated simplifications: sky light 15, no block light, edge-ness 8
// (no lawn-edge taper), the topmost surface only, rebuilt at switch-on (edits do not refresh it yet).
void VoxFoundWorld::voxFoundGrassFill() {
    if (!renderCoordinator || voxFoundEdit.empty() || voxFoundGrassStub) {
        if (renderCoordinator) renderCoordinator->setVoxFoundGrass({});
        voxFoundGrassCount = 0;
        return;
    }
    const std::vector<int16_t> grassTex = voxFoundGrassTex();
    voxFoundGrassPerChunk.assign(voxFoundEdit.chunkCount(), {});
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) voxFoundGrassPerChunk[i] = voxFoundGrassOf(i, grassTex);
    voxFoundGrassDirty.clear();
    voxFoundGrassUpload();
}

// F7.30: one region chunk's grass instances (F7.29's rule)
std::vector<GrassInstanceData> VoxFoundWorld::voxFoundGrassOf(size_t i, const std::vector<int16_t>& grassTex) const {
    std::vector<GrassInstanceData> v;
    {
        const auto& ch = voxFoundEdit.chunk(i);
        const auto& st = ch.store;
        const int H = st.dim().y;
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) {
                const int vx = x * 8 + 4, vz = z * 8 + 4;
                int y = H - 1;
                while (y >= 0 && !st.at({vx, y, vz})) --y;
                if (y < 0 || y == H - 1) continue;   // no ground in this chunk, or covered by the chunk above (unknown)
                const uint8_t m = st.at({vx, y, vz});
                if (m >= grassTex.size() || grassTex[m] < 0) continue;
                GrassInstanceData gi;
                const uint32_t cy = uint32_t(y / 8), sink = uint32_t(7 - y % 8);
                gi.packed = (uint32_t(x) & 0x1F) | ((cy & 0x1F) << 5) | ((uint32_t(z) & 0x1F) << 10) | (15u << 15);
                gi.tex = uint32_t(uint16_t(grassTex[m])) | (8u << 16) | (sink << 20);
                v.push_back(gi);
            }
    }
    return v;
}

std::vector<int16_t> VoxFoundWorld::voxFoundGrassTex() const {
    auto& reg = Core::MaterialRegistry::instance();
    std::vector<int16_t> grassTex(voxFoundPalette.size(), -1);
    for (size_t m = 1; m < voxFoundPalette.size(); ++m) {
        const std::string& n = voxFoundPalette[m];
        if (n == "Grass" || n == "GrassForest" || n == "GrassSavanna") grassTex[m] = int16_t(reg.getTextureIndex(n, 4));
    }
    return grassTex;
}

void VoxFoundWorld::voxFoundGrassUpload() {
    if (!renderCoordinator) return;
    std::vector<std::pair<glm::ivec3, std::vector<GrassInstanceData>>> chunks;
    size_t total = 0;
    for (size_t i = 0; i < voxFoundGrassPerChunk.size() && i < voxFoundEdit.chunkCount(); ++i) {
        total += voxFoundGrassPerChunk[i].size();
        if (!voxFoundGrassPerChunk[i].empty())
            chunks.push_back({voxFoundEdit.chunk(i).originVoxel / 8, voxFoundGrassPerChunk[i]});
    }
    renderCoordinator->setVoxFoundGrass(chunks);
    voxFoundGrassCount = total;
}

// F7.30: the edited chunks' grass, once edits have paused (one device wait per burst)
void VoxFoundWorld::voxFoundGrassRefreshTick() {
    if (voxFoundGrassDirty.empty() || voxFoundGrassRefreshStub || !voxFoundWorldReads) return;
    if (std::chrono::steady_clock::now() - voxFoundGrassLastEdit < std::chrono::milliseconds(250)) return;
    if (voxFoundGrassPerChunk.size() != voxFoundEdit.chunkCount()) voxFoundGrassPerChunk.assign(voxFoundEdit.chunkCount(), {});
    const auto grassTex = voxFoundGrassTex();
    for (size_t i : voxFoundGrassDirty)
        if (i < voxFoundEdit.chunkCount()) voxFoundGrassPerChunk[i] = voxFoundGrassOf(i, grassTex);
    voxFoundGrassDirty.clear();
    voxFoundGrassUpload();
    ++voxFoundGrassRefreshes;
}

// D1b (step 7, stage D1): the DB's STORED chunks (today's PXB2 blobs) over the region -- each through the converter
// (convertPxb2: cubes exact, thirds / ninths by volume), mapped to the region palette and loaded whole. No Chunk object
// is read. The switched world's own saves (loaded at the switch-on, after this) still win.
nlohmann::json VoxFoundWorld::voxFoundOverlayStored(const std::vector<glm::ivec3>& coords) {
    using namespace Core::VoxFound;
    auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
    if (!ws || !ws->getDb()) return {{"overlaid", 0}, {"note", "no world storage"}};
    size_t overlaid = 0;
    std::map<std::string, size_t> unknownMapped;   // retired materials -> Default (chunks)
    nlohmann::json errors = nlohmann::json::array();
    for (const glm::ivec3& cc : coords) {
        const int i = voxFoundEdit.indexOf(cc);
        if (i < 0) continue;
        std::vector<uint8_t> blob;
        if (!ws->readChunkBlob(cc, blob) || blob.empty()) continue;
        BlockStore s;
        std::vector<std::string> names;
        std::string err;
        if (!convertPxb2(blob.data(), blob.size(), s, names, nullptr, &err)) {
            errors.push_back({{"chunk", {cc.x, cc.y, cc.z}}, {"error", err}});
            continue;
        }
        std::vector<uint8_t> map(names.size(), 0);
        bool ok = true;
        for (size_t k = 1; k < names.size() && ok; ++k) {
            int m = voxFoundMaterial(names[k], &err);
            if (m < 0 && Core::MaterialRegistry::instance().getMaterial(names[k]) == nullptr) {
                // a RETIRED material (e.g. Cork): today draws it as the missing-texture fallback; here it is Default
                m = voxFoundMaterial("Default", &err);
                ++unknownMapped[names[k]];
            }
            ok = m >= 0;
            if (ok) map[k] = uint8_t(m);
        }
        if (!ok) {
            errors.push_back({{"chunk", {cc.x, cc.y, cc.z}}, {"error", err}});
            continue;
        }
        s.remapMaterials(map);
        const RegionEdit::Stats st = voxFoundEdit.loadChunk(size_t(i), s);
        voxFoundOccBump(std::vector<size_t>{size_t(i)});
        voxFoundLodMark(st);
        ++overlaid;
    }
    if (overlaid) voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex);
    voxFoundOverlaidTotal += overlaid;
    return {{"overlaid", overlaid}, {"errors", errors}, {"retired_materials_as_default", unknownMapped}};
}

// F8.4: the structure registry as JSON (world_meta "voxfound_structures") and back
nlohmann::json VoxFoundWorld::voxFoundStructuresJson() const {
    nlohmann::json a = nlohmann::json::array();
    for (const auto& s : voxFoundStructures)
        a.push_back({{"id", s.id}, {"lo", {s.lo.x, s.lo.y, s.lo.z}}, {"hi", {s.hi.x, s.hi.y, s.hi.z}}, {"floor", s.floor},
                     {"lights", s.lights}, {"props", s.props}, {"fixtures", s.fixtures}});
    return {{"next", voxFoundStructureNext}, {"structures", a}};
}
void VoxFoundWorld::voxFoundStructuresLoad() {
    auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
    if (!ws || !ws->hasMeta("voxfound_structures")) return;
    try {
        const nlohmann::json j = nlohmann::json::parse(ws->getMeta("voxfound_structures"));
        voxFoundStructures.clear();
        for (const auto& e : j.at("structures")) {
            VoxFoundStructure s;
            s.id = e.at("id").get<int>();
            s.lo = {e["lo"][0].get<int>(), e["lo"][1].get<int>(), e["lo"][2].get<int>()};
            s.hi = {e["hi"][0].get<int>(), e["hi"][1].get<int>(), e["hi"][2].get<int>()};
            s.floor = e.at("floor").get<int>();
            if (e.contains("lights") && renderCoordinator)   // F7.21c: re-registered (light ids are per session)
                for (const auto& l : e["lights"]) {
                    nlohmann::json r = l;
                    const auto& p = l["pos"];
                    const auto& c = l["color"];
                    r["id"] = renderCoordinator->getLightManager().addPointLight(
                        Graphics::LightSource::Fixture, glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>()),
                        glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>()), l.value("intensity", 1.0f),
                        l.value("radius", 1.0f));
                    s.lights.push_back(r);
                }
            if (e.contains("props")) s.props = e["props"];   // F7.21d: placed-object ids (the props persist themselves)
            if (e.contains("fixtures")) s.fixtures = e["fixtures"];   // F7.21e
            voxFoundStructures.push_back(s);
        }
        voxFoundStructureNext = j.value("next", int(voxFoundStructures.size()) + 1);
    } catch (const std::exception& e) {
        LOG_WARN("VoxFound", std::string("voxfound_structures not read: ") + e.what());
    }
}

// F7.9b: the switched world streams. A shift keeps every edited region chunk (codec v3, F3.3) in voxFoundStreamCache,
// switches off, rebuilds the region at the new box with the same request (F7.9a's worker generation), and switches on
// -- which loads the cache, then world.db, through F7.6a's exact path. Broad strokes: the whole region is regenerated
// (kept chunks included) and the shift is synchronous (one hitch); the levers are a kept-chunk reuse and a background
// build. Bodies in flight (the F7.5 world rig) are not carried across a shift.
nlohmann::json VoxFoundWorld::voxFoundStreamShift(glm::ivec3 newLo) {
    using namespace Core::VoxFound;
    if (voxFoundRegionRequest.is_null() || !voxFoundRegionRequest.contains("region"))
        return {{"error", "no editable region request to shift"}};
    const auto t0 = std::chrono::steady_clock::now();
    const auto& rg = voxFoundRegionRequest["region"];
    const glm::ivec3 a(rg["from"][0].get<int>(), rg["from"][1].get<int>(), rg["from"][2].get<int>());
    const glm::ivec3 b(rg["to"][0].get<int>(), rg["to"][1].get<int>(), rg["to"][2].get<int>());
    const glm::ivec3 size = glm::max(a, b) - glm::min(a, b);
    size_t cached = 0;
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        const auto& c = voxFoundEdit.chunk(i);
        if (!c.edited) continue;
        std::string err;
        std::vector<uint8_t> blob = c.store.encodeV3(voxFoundPalette, &err);
        if (blob.empty()) return {{"error", "encode: " + err}};
        voxFoundStreamCache[{c.chunk.x, c.chunk.y, c.chunk.z}] = std::move(blob);
        ++cached;
    }
    const bool wasOn = voxFoundWorldReads != nullptr;
    Core::APICommand c;
    nlohmann::json r;
    if (wasOn) {
        c.params = {{"world_switch", false}};
        handleVoxFoundPreview(c, r);
    }
    const auto t1 = std::chrono::steady_clock::now();
    nlohmann::json req = voxFoundRegionRequest;
    const glm::ivec3 hi = newLo + size;
    req["region"] = {{"from", {newLo.x, newLo.y, newLo.z}}, {"to", {hi.x, hi.y, hi.z}}};
    c.params = req;
    nlohmann::json built;
    handleVoxFoundPreview(c, built);
    if (built.contains("error")) return {{"error", built["error"]}, {"stage", "build"}};
    const auto t2 = std::chrono::steady_clock::now();
    nlohmann::json on;
    if (wasOn) {
        c.params = {{"world_switch", true}, {"load_saved", voxFoundSwitchLoadSaved}};
        handleVoxFoundPreview(c, on);
    }
    const auto t3 = std::chrono::steady_clock::now();
    auto ms = [](auto x, auto y) { return std::chrono::duration<double, std::milli>(y - x).count(); };
    voxFoundStreamLastMs = ms(t0, t3);
    voxFoundStreamMaxMs = std::max(voxFoundStreamMaxMs, voxFoundStreamLastMs);
    ++voxFoundStreamShifts;
    return {{"to", {newLo.x, newLo.y, newLo.z}}, {"cached", cached}, {"off_ms", ms(t0, t1)}, {"build_ms", ms(t1, t2)},
            {"generate_ms", built.value("generate_ms", 0.0)}, {"switch_on_ms", ms(t2, t3)}, {"wall_ms", ms(t0, t3)},
            {"loaded", on.value("loaded", nlohmann::json())}};
}

void VoxFoundWorld::voxFoundStreamTick() {
    if (!voxFoundStreamOn || voxFoundStreamStub || !voxFoundWorldReads || !camera || voxFoundRegionRequest.is_null()) return;
    if (voxFoundStreamJob) {   // F7.9c: a background shift in flight finishes first
        voxFoundStreamBgTick(voxFoundStreamJob->lo);
        return;
    }
    const auto& rg = voxFoundRegionRequest["region"];
    const glm::ivec3 a(rg["from"][0].get<int>(), rg["from"][1].get<int>(), rg["from"][2].get<int>());
    const glm::ivec3 b(rg["to"][0].get<int>(), rg["to"][1].get<int>(), rg["to"][2].get<int>());
    const glm::ivec3 lo = glm::min(a, b), hi = glm::max(a, b);
    const glm::vec3 p = camera->getPosition();
    const glm::ivec3 fc(int(std::floor(p.x / 32.0f)), 0, int(std::floor(p.z / 32.0f)));
    // the focus must stay on the region's inner chunks (one chunk of margin each side in x and z)
    const bool inside = fc.x >= lo.x + 1 && fc.x <= hi.x - 1 && fc.z >= lo.z + 1 && fc.z <= hi.z - 1;
    if (inside) return;
    const glm::ivec3 size = hi - lo + 1;
    // F7.9c: recentre only the axis the camera left the inner chunks on (one slab enters, not two)
    const bool inX = fc.x >= lo.x + 1 && fc.x <= hi.x - 1, inZ = fc.z >= lo.z + 1 && fc.z <= hi.z - 1;
    const glm::ivec3 newLo(inX ? lo.x : fc.x - size.x / 2, lo.y, inZ ? lo.z : fc.z - size.z / 2);
    if (newLo == lo) return;
    if (voxFoundStreamBg) voxFoundStreamBgTick(newLo);   // F7.9c
    else voxFoundStreamShift(newLo);
}

// F7.9c: one step of a background shift per call (from the frame tick; a pending job keeps stepping even when the
// camera's chunk is back inside).
void VoxFoundWorld::voxFoundStreamBgTick(glm::ivec3 newLo) {
    using namespace Core::VoxFound;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    if (!voxFoundStreamJob) {
        if (!voxFoundRegionGen) return;
        auto job = std::make_unique<VoxFoundStreamJob>();
        const glm::ivec3 size = voxFoundEdit.chunkHi() - voxFoundEdit.chunkLo();
        job->lo = newLo;
        job->hi = newLo + size;
        for (int x = job->lo.x; x <= job->hi.x; ++x)
            for (int y = job->lo.y; y <= job->hi.y; ++y)
                for (int z = job->lo.z; z <= job->hi.z; ++z)
                    if (voxFoundEdit.indexOf({x, y, z}) < 0) job->entering.push_back({x, y, z});
        job->areas.resize(job->entering.size());
        job->start = std::chrono::steady_clock::now();
        const bool flora = voxFoundRegionRequest.value("flora", false);
        auto* raw = job.get();
        job->worker = std::thread([raw, flora, base = std::make_shared<WorldGenerator>(*voxFoundRegionGen)]() {
            const auto t0 = std::chrono::steady_clock::now();
            // F7.9c: 4 generators in parallel (F7.9a's measured best), each a copy
            std::atomic<size_t> next{0};
            std::mutex errMu;
            auto work = [&]() {
                WorldGenerator g(*base);
                try {
                    const std::string dir = "resources/templates_g8/nature";
                    for (size_t k; (k = next.fetch_add(1)) < raw->entering.size();) {
                        raw->areas[k] = buildTerrainArea(g, raw->entering[k] * kChunkM - 1, glm::ivec3(kChunkM + 2));
                        if (flora) stampFlora(raw->areas[k], g, dir, 24, nullptr);
                    }
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lk(errMu);
                    raw->error = e.what();
                }
            };
            std::vector<std::thread> ts;
            for (int t = 0; t < 4; ++t) ts.emplace_back(work);
            for (auto& t : ts) t.join();
            raw->genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            raw->done = true;
        });
        voxFoundStreamJob = std::move(job);
        return;
    }
    VoxFoundStreamJob& job = *voxFoundStreamJob;
    if (!job.done) return;
    if (job.worker.joinable()) job.worker.join();
    if (!job.error.empty()) {
        voxFoundStreamLastBg = {{"error", job.error}};
        voxFoundStreamJob.reset();
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (job.prepared < job.entering.size()) {   // one entering chunk per frame
        const size_t k = job.prepared++;
        VoxelArea& a = job.areas[k];
        uint8_t remap[256] = {};
        for (size_t m = 1; m < a.palette.size() && m < 256; ++m) {
            std::string err;
            const int id = voxFoundMaterial(a.palette[m], &err);
            if (id < 0) {
                voxFoundStreamLastBg = {{"error", "palette: " + err}};
                voxFoundStreamJob.reset();
                return;
            }
            remap[m] = uint8_t(id);
        }
        for (uint8_t& v : a.grid.mat) v = remap[v];
        // the 1 m margin inside edited region chunks of the new box: their voxels, not the generator's
        const glm::ivec3 c = job.entering[k], o = a.originVoxel, d = a.grid.dim;
        const int N = kChunkM * kVoxelsPerM;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    const glm::ivec3 nc = c + glm::ivec3(dx, dy, dz);
                    if ((dx | dy | dz) == 0 || glm::any(glm::lessThan(nc, job.lo)) || glm::any(glm::greaterThan(nc, job.hi)))
                        continue;
                    const int ni = voxFoundEdit.indexOf(nc);
                    if (ni < 0 || !voxFoundEdit.chunk(size_t(ni)).edited) continue;
                    const glm::ivec3 nlo = glm::max(nc * N - o, glm::ivec3(0)), nhi = glm::min((nc + 1) * N - o, d);
                    for (int x = nlo.x; x < nhi.x; ++x)
                        for (int y = nlo.y; y < nhi.y; ++y)
                            for (int z = nlo.z; z < nhi.z; ++z)
                                a.grid.mat[a.grid.idx({x, y, z})] = voxFoundEdit.voxelAt(o + glm::ivec3(x, y, z));
                }
        RegionChunk rc;
        rc.chunk = c;
        rc.originVoxel = c * N;
        const glm::ivec3 off(kVoxelsPerM), inner(N);
        rc.grid = subGrid(a.grid, off, inner);
        rc.border = neighboursOf(a.grid, off, inner, Outside::Air);
        rc.area = &a.grid;
        rc.areaMarginVoxels = kVoxelsPerM;
        std::vector<PackedFace> out;
        std::string err;
        const bool leafCards = voxFoundRegionRequest.value("leaves", std::string("cards")) != "voxels";
        SprigParams sp = voxFoundEdit.sprigParamsFor(rc.originVoxel);
        if (!voxFoundBuilder || !voxFoundBuilder->build(rc.grid, Edges(rc.border), out, &err, leafCards ? &sp : nullptr)) {
            voxFoundStreamLastBg = {{"error", "faces: " + err}};
            voxFoundStreamJob.reset();
            return;
        }
        job.ready.push_back(voxFoundEdit.makeChunk(rc, out));
        a = VoxelArea{};   // free the area
        job.maxSliceMs = std::max(job.maxSliceMs, ms(t0, std::chrono::steady_clock::now()));
        voxFoundStreamMaxSliceMs = std::max(voxFoundStreamMaxSliceMs, job.maxSliceMs);
        return;
    }
    voxFoundStreamLastBg = voxFoundStreamSwap();
    voxFoundStreamJob.reset();
}

// F7.9c: the swap frame -- leaving edited chunks kept, the box moved, entering chunks inserted, the drawn lists
// assembled whole, the switch rebound.
nlohmann::json VoxFoundWorld::voxFoundStreamSwap() {
    using namespace Core::VoxFound;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    VoxFoundStreamJob& job = *voxFoundStreamJob;
    const auto t0 = std::chrono::steady_clock::now();
    size_t kept = 0;
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        const auto& c = voxFoundEdit.chunk(i);
        const bool leaving = glm::any(glm::lessThan(c.chunk, job.lo)) || glm::any(glm::greaterThan(c.chunk, job.hi));
        if (!leaving || !c.edited) continue;
        std::string err;
        std::vector<uint8_t> blob = c.store.encodeV3(voxFoundPalette, &err);
        if (!blob.empty()) voxFoundStreamCache[{c.chunk.x, c.chunk.y, c.chunk.z}] = std::move(blob), ++kept;
    }
    Core::APICommand cmd;
    nlohmann::json r;
    cmd.params = {{"world_switch", false}};
    voxFoundStreamSwapping = true;   // F7.18: the span store (and its edited entries) survives the shift
    handleVoxFoundPreview(cmd, r);
    voxFoundStreamSwapping = false;
    voxFoundEdit.rebox(job.lo, job.hi);
    for (auto& c : job.ready) voxFoundEdit.insertChunk(std::move(c));
    if (voxFoundOverlayDb) voxFoundOverlayStored(job.entering);   // D1b: stored chunks entering the region
    if (voxFoundBuilder) voxFoundBuilder->dropResident();
    // the preview: one entry per chunk in the store's order; level 0 only (coarse levels dropped: stated)
    voxFoundPreview.chunks.assign(voxFoundEdit.chunkCount(), PreviewChunk{});
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        voxFoundPreview.chunks[i].originMetres = glm::dvec3(voxFoundEdit.chunk(i).originVoxel) / double(kVoxelsPerM);
        voxFoundPreview.chunks[i].chunk = voxFoundEdit.chunk(i).chunk;
    }
    voxFoundPreview.levels.resize(1);
    voxFoundPreview.levelsAlt.clear();
    voxFoundPreview.lodFirst.clear();
    voxFoundPreview.lodCount.clear();
    voxFoundPreview.lodClusters.clear();
    voxFoundPreview.lodClustersDir.clear();
    voxFoundPreview.lodOn = false;
    voxFoundLodEditLevels = 0;
    voxFoundLodDirty.clear();
    voxFoundPreview.anchorMetres = glm::dvec3(job.lo * kChunkM);
    voxFoundPreview.originMetres = glm::dvec3(job.lo * kChunkM);
    voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex);
    for (const glm::ivec3& c : job.entering) ++voxFoundOccRevision[{c.x, c.y, c.z}];
    voxFoundRegionRequest["region"] = {{"from", {job.lo.x, job.lo.y, job.lo.z}}, {"to", {job.hi.x, job.hi.y, job.hi.z}}};
    nlohmann::json on;
    cmd.params = {{"world_switch", true}, {"load_saved", voxFoundSwitchLoadSaved}};
    handleVoxFoundPreview(cmd, on);
    const auto t1 = std::chrono::steady_clock::now();
    job.swapMs = ms(t0, t1);
    voxFoundStreamMaxSliceMs = std::max(voxFoundStreamMaxSliceMs, job.swapMs);
    voxFoundStreamLastMs = ms(job.start, t1);
    voxFoundStreamMaxMs = std::max(voxFoundStreamMaxMs, voxFoundStreamLastMs);
    ++voxFoundStreamShifts;
    return {{"to", {job.lo.x, job.lo.y, job.lo.z}}, {"entering", job.entering.size()}, {"kept_leaving", kept},
            {"gen_ms", job.genMs}, {"max_prepare_ms", job.maxSliceMs}, {"swap_ms", job.swapMs},
            {"latency_ms", voxFoundStreamLastMs}, {"loaded", on.value("loaded", nlohmann::json())}};
}

// F7.8 (docs/VoxelFoundationPlan.md section 7 step 1, queryAABB): while switched, every region chunk's static terrain
// in VoxelDynamicsWorld is a grid whose queryAABB is answered from the region's store (VoxFound::storeBoxes) -- the
// character's grounding, CPU bodies and overlap tests then read what is drawn.
void VoxFoundWorld::voxFoundPhysSwitch(bool on) {
    auto* vw = physicsWorld ? physicsWorld->getVoxelWorld() : nullptr;
    if (vw)
        for (const auto& g : voxFoundPhysGrids) vw->setGridOverride(g->getChunkOrigin() / 32, nullptr);
    voxFoundPhysGrids.clear();
    if (!on || !vw || voxFoundPhysStub) return;
    for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
        auto g = std::make_unique<Physics::VoxelOccupancyGrid>();
        const glm::ivec3 c = voxFoundEdit.chunk(i).chunk;
        g->setChunkOrigin(c * 32);
        g->setExternalQuery([this, i](const glm::vec3& mn, const glm::vec3& mx, std::vector<Physics::OccupiedBox>& out) {
            if (i >= voxFoundEdit.chunkCount()) return;
            const auto& ch = voxFoundEdit.chunk(i);
            Core::VoxFound::storeBoxes(ch.store, ch.originVoxel, mn, mx, [&](glm::vec3 lo, glm::vec3 hi) {
                out.push_back({(lo + hi) * 0.5f, (hi - lo) * 0.5f});
            });
        });
        vw->setGridOverride(c, g.get());
        voxFoundPhysGrids.push_back(std::move(g));
    }
}

// F7.6c (docs/VoxelFoundationPlan.md section 7 step 6, section 12): the voxel API on the switched world. The JSON keeps
// its shape: x/y/z are metres (today's cube coordinates) -> a cube is the size-8 brush, a subcube (sx, sy, sz thirds)
// the size-2 brush at x*8 + floor((s + 0.5) * 8 / 3) (F7.4's mapping); "units":"voxel" = 1/8 m voxels (+ "size").
// F7.24 (docs/VoxelFoundationPlan.md section 7 step 6, the voxel API's units): the rest of the region API on the switched
// world. Coordinates are cubes (metres) by default -- a box x1..x2 covers voxels x1*8 .. x2*8+7 -- or 1/8 m voxels with
// "units":"voxel". copy / snapshots capture the box's 1/8 m voxels exactly (sub-cube detail kept); paste writes the
// clip's SOLID voxels (as today's paste places only its entries); move = clear the source + paste, one edit; restore
// writes the box exactly (air included). Rotation: 90 degree steps about +Y, SnapshotManager::rotateY90's convention
// (x' = size.z - 1 - z, z' = x). Every call is ONE region edit (one undo entry). place_voxels_batch: each entry a
// cube (512 voxels) or, in voxel units, one voxel.
bool VoxFoundWorld::voxFoundApiRegion(const std::string& action, const nlohmann::json& p, nlohmann::json& response) {
    if (!voxFoundWorldReads || voxFoundApiStub || voxFoundEdit.empty()) return false;
    const bool vu = p.value("units", std::string("cube")) == "voxel";
    const int k = vu ? 1 : 8;
    auto boxOf = [&](const char* a1, const char* a2) {
        const glm::ivec3 a(p.value(std::string("x") + a1, 0), p.value(std::string("y") + a1, 0), p.value(std::string("z") + a1, 0));
        const glm::ivec3 b(p.value(std::string("x") + a2, 0), p.value(std::string("y") + a2, 0), p.value(std::string("z") + a2, 0));
        const glm::ivec3 lo = glm::min(a, b) * k, hi = glm::max(a, b) * k + glm::ivec3(k - 1);
        return std::make_pair(lo, hi - lo + glm::ivec3(1));
    };
    auto capture = [&](glm::ivec3 lo, glm::ivec3 size) {
        VoxFoundBox bx;
        bx.lo = lo;
        bx.size = size;
        bx.mats.resize(size_t(size.x) * size.y * size.z);
        size_t i = 0;
        for (int x = 0; x < size.x; ++x)
            for (int y = 0; y < size.y; ++y)
                for (int z = 0; z < size.z; ++z) bx.mats[i++] = voxFoundEdit.voxelAt(lo + glm::ivec3(x, y, z));
        return bx;
    };
    auto rotated = [](const VoxFoundBox& b, int steps, glm::ivec3& sizeOut) {
        std::vector<std::pair<glm::ivec3, uint8_t>> out;   // (offset, material) of the solid voxels
        glm::ivec3 size = b.size;
        size_t i = 0;
        for (int x = 0; x < b.size.x; ++x)
            for (int y = 0; y < b.size.y; ++y)
                for (int z = 0; z < b.size.z; ++z, ++i) {
                    if (!b.mats[i]) continue;
                    glm::ivec3 o(x, y, z), s = b.size;
                    for (int r = 0; r < steps; ++r) {
                        o = glm::ivec3(s.z - 1 - o.z, o.y, o.x);
                        s = glm::ivec3(s.z, s.y, s.x);
                    }
                    out.push_back({o, b.mats[i]});
                }
        for (int r = 0; r < steps; ++r) size = glm::ivec3(size.z, size.y, size.x);
        sizeOut = size;
        return out;
    };
    const size_t kMaxVoxels = size_t(64) * 1024 * 1024;   // 1/8 m voxels per box (= 131,072 cubes)
    auto tooBig = [&](glm::ivec3 size) { return size_t(size.x) * size.y * size.z > kMaxVoxels; };
    std::vector<std::pair<glm::ivec3, uint8_t>> cells;
    nlohmann::json extra = nlohmann::json::object();
    std::string label = "api_" + action;
    if (action == "place_voxels_batch") {
        if (!p.contains("voxels")) { response = {{"error", "Missing 'voxels' array"}, {"world", "voxfound"}}; return true; }
        size_t refused = 0;
        for (const auto& v : p["voxels"]) {
            const int m = voxFoundMaterial(v.value("material", std::string("Default")), nullptr);
            if (m < 0) { ++refused; continue; }
            const glm::ivec3 at = glm::ivec3(v.value("x", 0), v.value("y", 0), v.value("z", 0)) * k;
            for (int x = 0; x < k; ++x)
                for (int y = 0; y < k; ++y)
                    for (int z = 0; z < k; ++z) cells.push_back({at + glm::ivec3(x, y, z), uint8_t(m)});
        }
        extra = {{"entries", p["voxels"].size()}, {"refused_unknown_material", refused}};
    } else if (action == "copy_region" || action == "create_snapshot") {
        const auto [lo, size] = boxOf("1", "2");
        if (tooBig(size)) { response = {{"error", "Region too large"}, {"world", "voxfound"}}; return true; }
        VoxFoundBox bx = capture(lo, size);
        size_t solid = 0;
        for (uint8_t m : bx.mats) solid += m != 0;
        if (action == "copy_region") {
            voxFoundClip = std::move(bx);
            voxFoundClipSet = true;
        } else {
            const std::string name = p.value("name", std::string());
            if (name.empty()) { response = {{"error", "Missing 'name'"}, {"world", "voxfound"}}; return true; }
            voxFoundSnapshots[name] = std::move(bx);
        }
        response = {{"success", true}, {"world", "voxfound"}, {"voxel_count", solid},
                    {"size_voxels", {size.x, size.y, size.z}}, {"units", vu ? "voxel" : "cube"}};
        return true;
    } else if (action == "paste_region") {
        if (!voxFoundClipSet) { response = {{"error", "Clipboard is empty  --  use copy_region first"}, {"world", "voxfound"}}; return true; }
        const glm::ivec3 at = glm::ivec3(p.value("x", 0), p.value("y", 0), p.value("z", 0)) * k;
        glm::ivec3 size;
        for (const auto& [o, m] : rotated(voxFoundClip, ((p.value("rotate", 0) % 360) + 360) % 360 / 90, size))
            cells.push_back({at + o, m});
        extra = {{"size_voxels", {size.x, size.y, size.z}}, {"rotation", p.value("rotate", 0)}};
    } else if (action == "move_region") {
        const auto [lo, size] = boxOf("1", "2");
        if (tooBig(size)) { response = {{"error", "Region too large"}, {"world", "voxfound"}}; return true; }
        const VoxFoundBox src = capture(lo, size);
        const glm::ivec3 dst = glm::ivec3(p.value("dx", lo.x / k), p.value("dy", lo.y / k), p.value("dz", lo.z / k)) * k;
        std::map<std::tuple<int, int, int>, uint8_t> m;   // the source cleared, then the destination (wins)
        for (int x = 0; x < size.x; ++x)
            for (int y = 0; y < size.y; ++y)
                for (int z = 0; z < size.z; ++z) m[{lo.x + x, lo.y + y, lo.z + z}] = 0;
        glm::ivec3 rs;
        for (const auto& [o, mat] : rotated(src, ((p.value("rotate", 0) % 360) + 360) % 360 / 90, rs))
            m[{dst.x + o.x, dst.y + o.y, dst.z + o.z}] = mat;
        for (const auto& [q, mat] : m) cells.push_back({{std::get<0>(q), std::get<1>(q), std::get<2>(q)}, mat});
        extra = {{"moved_voxels", std::count_if(src.mats.begin(), src.mats.end(), [](uint8_t v) { return v != 0; })}};
    } else if (action == "restore_snapshot") {
        const std::string name = p.value("name", std::string());
        auto it = voxFoundSnapshots.find(name);
        if (it == voxFoundSnapshots.end()) { response = {{"error", "Snapshot not found"}, {"name", name}, {"world", "voxfound"}}; return true; }
        const VoxFoundBox& bx = it->second;
        size_t i = 0;
        for (int x = 0; x < bx.size.x; ++x)
            for (int y = 0; y < bx.size.y; ++y)
                for (int z = 0; z < bx.size.z; ++z) cells.push_back({bx.lo + glm::ivec3(x, y, z), bx.mats[i++]});
        label = "api_restore_snapshot:" + name;
    } else {
        return false;
    }
    nlohmann::json r = voxFoundPaintCells(label, cells);
    response = r;
    response["world"] = "voxfound";
    const bool ok = r.value("error", std::string()).empty();   // an EMPTY error key is not an error (drop it)
    if (ok) response.erase("error");
    response["success"] = ok;
    for (auto it = extra.begin(); it != extra.end(); ++it) response[it.key()] = it.value();
    return true;
}

bool VoxFoundWorld::voxFoundApiEdit(const std::string& action, const nlohmann::json& p, nlohmann::json& response) {
    if (!voxFoundWorldReads || voxFoundApiStub || voxFoundEdit.empty()) return false;
    const bool vu = p.value("units", std::string("cube")) == "voxel";
    const glm::ivec3 at(p.value("x", 0), p.value("y", 0), p.value("z", 0));
    const std::string material = p.value("material", std::string("Default"));
    nlohmann::json ej;
    if (action == "place_voxel" || action == "remove_voxel") {
        const int size = vu ? p.value("size", 1) : 8;
        const glm::ivec3 v = vu ? at : at * 8;
        ej = {{"brush", {{"at", {v.x, v.y, v.z}}, {"size", size}}},
              {"material", action == "remove_voxel" ? std::string("air") : material}};
    } else if (action == "place_subcube") {
        const glm::vec3 s(p.value("sx", 0), p.value("sy", 0), p.value("sz", 0));
        const glm::ivec3 v = at * 8 + glm::ivec3(glm::floor((s + 0.5f) * 8.0f / 3.0f));
        ej = {{"brush", {{"at", {v.x, v.y, v.z}}, {"size", 2}}}, {"material", material}};
    } else {   // fill_region / clear_region
        const glm::ivec3 a(p.value("x1", 0), p.value("y1", 0), p.value("z1", 0));
        const glm::ivec3 b(p.value("x2", 0), p.value("y2", 0), p.value("z2", 0));
        glm::ivec3 lo = glm::min(a, b), hi = glm::max(a, b);
        if (!vu) lo *= 8, hi = hi * 8 + 7;
        ej = {{"lo", {lo.x, lo.y, lo.z}}, {"hi", {hi.x, hi.y, hi.z}},
              {"material", action == "clear_region" ? std::string("air") : material}};
        if (action == "fill_region") {
            if (p.value("hollow", false)) ej["hollow"] = vu ? 1 : 8;
            if (!p.value("replace", false)) ej["only_air"] = true;
        }
    }
    nlohmann::json r;
    try {
        handleVoxFoundEdit(ej, r);
    } catch (const std::exception& e) {
        r = {{"error", e.what()}};
    }
    if (r.contains("error")) {
        response = {{"success", false}, {"world", "voxfound"}, {"error", r["error"]}};
        return true;
    }
    const size_t changed = r.value("voxels_changed", size_t(0));
    response = {{"success", true}, {"world", "voxfound"}, {"units", vu ? "voxel" : "cube"},
                {"position", {{"x", at.x}, {"y", at.y}, {"z", at.z}}}, {"voxels_changed", changed},
                {"lo", r["lo"]}, {"hi", r["hi"]}, {"undo", r["undo"]}, {"brush", r["brush"]},
                {"wall_ms", r.value("rebuild_ms", 0.0) + r.value("assemble_ms", 0.0)}};
    if (action == "fill_region") response["placed"] = changed;
    if (action == "clear_region") response["removed"] = changed;
    if (changed && npcManager && action != "fill_region" && action != "clear_region") {
        auto fd = [](int c) { return c >= 0 ? c / 8 : -((-c + 7) / 8); };
        npcManager->onVoxelChanged(vu ? glm::ivec3(fd(at.x), fd(at.y), fd(at.z)) : at);
    }
    return true;
}

// F7.6c: the cube (x, y, z) on the switched world -- its 8^3 block's solid voxels and their most common material.
// NOTE: called on the HTTP thread like today's handler (which reads ChunkManager there too); not synchronised with
// edits on the main thread -- the same pre-existing pattern, stated.
nlohmann::json VoxFoundWorld::voxFoundQuery(int x, int y, int z) const {
    std::vector<size_t> count(256, 0);
    size_t solid = 0;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 8; ++k) {
                const uint8_t m = voxFoundEdit.voxelAt(glm::ivec3(x * 8 + i, y * 8 + j, z * 8 + k));
                if (m) ++count[m], ++solid;
            }
    nlohmann::json r = {{"position", {{"x", x}, {"y", y}, {"z", z}}}, {"world", "voxfound"}, {"exists", solid > 0}};
    nlohmann::json vf = {{"solid_voxels", solid}};
    if (solid) {
        const size_t best = size_t(std::max_element(count.begin() + 1, count.end()) - count.begin());
        const std::string name = best < voxFoundPalette.size() ? voxFoundPalette[best] : std::string();
        r["material"] = name;
        vf["material"] = name;
    }
    r["voxfound"] = vf;
    return r;
}

// F7.6b: undo / redo on the switched world -- the history's differing cells written back through applyCells (the exact
// incremental path), then the light occupancy, the resident device copies and the drawn lists as an edit does.
bool VoxFoundWorld::voxFoundUndoRedo(bool undo, nlohmann::json& r) {
    using namespace Core::VoxFound;
    if (!voxFoundWorldReads || voxFoundUndoStub || voxFoundEdit.empty()) return false;
    if (undo ? !voxFoundHistory.canUndo() : !voxFoundHistory.canRedo()) return false;   // today's stack answers
    const auto t0 = std::chrono::steady_clock::now();
    const auto read = [this](glm::ivec3 p) { return voxFoundEdit.voxelAt(p); };
    std::string label;
    const EditHistory::Cells cells = undo ? voxFoundHistory.undo(read, &label) : voxFoundHistory.redo(read, &label);
    RegionEdit::Stats st;
    std::string err;
    if (!cells.empty()) st = voxFoundEdit.applyCells(cells, nullptr, &err);
    voxFoundOccBump(st.rebuilt);
    voxFoundLodMark(st);   // F7.6d
    if (voxFoundBuilder)
        for (size_t i : st.rebuilt) voxFoundBuilder->forgetResident(voxFoundEdit.residentKey(i));
    const auto t1 = std::chrono::steady_clock::now();
    voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex, &st.changed);
    const auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    r = {{"success", err.empty()}, {"world", "voxfound"}, {"operation", label}, {"voxels_restored", cells.size()},
         {"voxels_changed", st.voxelsChanged}, {"chunks_rebuilt", st.rebuilt.size()}, {"error", err},
         {"rebuild_ms", ms(t0, t1)}, {"assemble_ms", ms(t1, t2)}, {"wall_ms", ms(t0, t2)},
         {"undo_depth", voxFoundHistory.undoDepth()}, {"redo_depth", voxFoundHistory.redoDepth()}};
    return true;
}

void VoxFoundWorld::voxFoundOccBump(const std::vector<size_t>& rebuiltChunks) {   // F7.3b / F7.5
    for (size_t i : rebuiltChunks) {
        const auto& rc = voxFoundEdit.chunk(i).chunk;
        ++voxFoundOccRevision[{rc.x, rc.y, rc.z}];
    }
}

// F7.5: DamageSystem's override while the world is switched -- the event runs the bodies lab's "world" rig on a window
// of the region (fracture + extraction + bodies + rubble), running; today's carving does not run.
bool VoxFoundWorld::voxFoundWorldBreak(const glm::vec3& centre, float radius, float energy, const glm::vec3& dir,
                                     nlohmann::json* report) {
    if (!voxFoundWorldReads) return false;
    // F8.2: a new world rig resets the body sim -- the previous rig's bodies (their voxels left the region at that break)
    // are first stopped and baked back into the region, all of them (found: 100 rapid blasts lost 3.6 M voxels)
    if (voxFoundBodiesLab.worldMode && !voxFoundRapidStub) {
        Core::APICommand stop, bake;
        nlohmann::json ignore;
        stop.params = {{"run", false}};
        handleVoxFoundBodies(stop, ignore);
        bake.params = {{"bake_now", {{"budget", 0}, {"keep_m", 0.0}, {"rest_ticks", 0}, {"awake", true}}},
                       {"status", true}, {"pen", false}};
        handleVoxFoundBodies(bake, ignore);
        ignore["settle"] = voxFoundSettleRubble();   // F8.2: and its rubble as whole voxels
        voxFoundLastPrevBake = ignore;   // F8.2: what the pre-bake did (reported with the break)
    }
    const glm::ivec3 hw(glm::floor(centre * float(Core::VoxFound::kVoxelsPerM)));
    const int side = std::clamp(int(std::ceil(std::max(8.0f, 4.0f * radius) * Core::VoxFound::kVoxelsPerM)), 16, 256);
    nlohmann::json body = {{"rig", "world"}, {"hit_world", {hw.x, hw.y, hw.z}}, {"window", {side, side, side}},
                           {"energy", energy}, {"run", true}, {"ledger_every", true}};
    if (glm::length(dir) > 0.0f) body["dir"] = {dir.x, dir.y, dir.z};
    Core::APICommand cmd;
    cmd.params = body;
    nlohmann::json r;
    handleVoxFoundBodies(cmd, r);
    if (!voxFoundLastPrevBake.is_null()) {   // F8.2
        r["prev_bake"] = {{"bodies", voxFoundLastPrevBake.value("bodies", -1)},
                          {"rest", voxFoundLastPrevBake.value("rest", nlohmann::json())},
                          {"error", voxFoundLastPrevBake.value("error", std::string())}};
        voxFoundLastPrevBake = nullptr;
    }
    if (report) *report = r;
    voxFoundLastBreak = {{"prev_bake", r.value("prev_bake", nlohmann::json())}, {"bodies", r.value("bodies", -1)},
                         {"error", r.value("error", std::string())}};   // F8.2
    if (r.contains("error")) return false;
    Core::APICommand rest;   // rest on, as the lab's live gates run it
    rest.params = {{"rest", {{"on", true}}}};
    nlohmann::json rr;
    handleVoxFoundBodies(rest, rr);
    return true;
}

void VoxFoundWorld::handleVoxFoundEdit(const nlohmann::json& ej, nlohmann::json& response) {
    using namespace Core::VoxFound;
    if (voxFoundEdit.empty()) {
        response = {{"error", "no editable region preview (build one with {\"region\":...,\"editable\":true})"}};
        return;
    }
    // {"replace_all":true}: every chunk's lists from a FULL FaceBuilderGpu rebuild of its current voxels (the pixel A/B
    // reference for the incremental lists; no voxel changes)
    if (ej.value("replace_all", false) && voxFoundBuilder) {
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < voxFoundEdit.chunkCount(); ++i) {
            const auto& c = voxFoundEdit.chunk(i);
            const SprigParams sp = voxFoundEdit.sprigParams(i);
            std::vector<PackedFace> out;
            std::string err;
            const VoxelGrid g = voxFoundEdit.dense(i);   // F3.2a: the store expanded for the full rebuild
            if (!voxFoundBuilder->build(g, Edges(c.border), out, &err, &sp)) {
                response = {{"error", err}};
                return;
            }
            voxFoundEdit.replaceFaces(i, out);
        }
        voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex);
        response = {{"replaced_chunks", voxFoundEdit.chunkCount()}, {"faces", voxFoundPreview.liveFaces()},
                    {"wall_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()}};
        return;
    }
    auto ivec = [&](const nlohmann::json& j) {
        if (!j.is_array() || j.size() != 3) throw std::runtime_error("expected [x,y,z] (world 1/8 m voxels)");
        return glm::ivec3(j[0].get<int>(), j[1].get<int>(), j[2].get<int>());
    };
    glm::ivec3 lo, hi;
    std::function<bool(glm::ivec3)> inside = [](glm::ivec3) { return true; };
    std::string brush = "box";
    int brushSize = 0;   // F3.2b: {"brush":{"at":[x,y,z],"size":1|2|4|8}} (world voxels; the brush cell holding "at")
    if (ej.contains("brush")) {
        brushSize = ej["brush"].value("size", 8);
        if (!brushSizeOk(brushSize)) {
            response = {{"error", "brush size must be 1, 2, 4 or 8 voxels"}};
            return;
        }
        lo = brushCell(ivec(ej["brush"].at("at")), brushSize);
        hi = lo + (brushSize - 1);
        brush = "brush";
    } else if (ej.contains("sphere")) {
        const glm::ivec3 c = ivec(ej["sphere"].at("c"));
        const double r = ej["sphere"].value("r", 4.0);
        const int ri = int(std::ceil(r));
        lo = c - ri;
        hi = c + ri;
        inside = [c, r](glm::ivec3 p) { return glm::length(glm::dvec3(p - c)) <= r; };
        brush = "sphere";
    } else {
        const glm::ivec3 a = ivec(ej.at("lo")), b = ivec(ej.at("hi"));
        lo = glm::min(a, b);
        hi = glm::max(a, b);
        // F7.6c: {"hollow":t} a shell t voxels thick; {"only_air":true} only air voxels change (fill_region's
        // replace:false) -- both through the masked path (apply with inside)
        const int shell = ej.value("hollow", 0);
        const bool onlyAir = ej.value("only_air", false);
        if (shell > 0 || onlyAir) {
            const glm::ivec3 ilo = lo + shell, ihi = hi - shell;
            inside = [this, ilo, ihi, shell, onlyAir](glm::ivec3 p) {
                if (shell > 0 && glm::all(glm::greaterThanEqual(p, ilo)) && glm::all(glm::lessThanEqual(p, ihi)))
                    return false;
                return !onlyAir || voxFoundEdit.voxelAt(p) == 0;
            };
            brush = "masked_box";
        }
    }
    // the material: air, a palette entry, or a new palette entry (its textures and card / see-through sets added)
    const std::string matName = ej.value("material", std::string("air"));
    std::string matErr;
    const int matIndex = voxFoundMaterial(matName, &matErr);
    if (matIndex < 0) {
        response = {{"error", matErr}};
        return;
    }
    const uint8_t mat = uint8_t(matIndex);
    const auto t0 = std::chrono::steady_clock::now();
    // Rows executor: the CPU (meshRunsPlan) by default; {"device":true} re-meshes the planned rows on the device
    // (FaceBuilderGpu::rowsDelta). Measured 2026-11-15 (docs/evidence/voxfound_f216_prediction.txt): the device kernel
    // is ~0.15 ms but its synchronous readback waits for a GPU timeslice -- 0.5-0.7 ms with GPU headroom, 10-13 ms at
    // the uncapped operating point -- vs 3.6-4.4 ms on the CPU for a 1 m edit. The device path pays off once item 10
    // records the rows into the frame and writes the face pool on the device (no readback, no wait).
    std::string rowsErr;
    voxFoundEdit.setLegacyF31(ej.value("legacy", false));   // F3.1 A/B: F2.17's edit path
    voxFoundEdit.setLegacyBorder(ej.value("legacy_border", false));   // F3.5a A/B: F3.5's border handling
    voxFoundEdit.setLegacyConnect(ej.value("legacy_connect", false));   // F3.5b A/B: F3.5a's flood
    FaceBuilderGpu* rowsGpu = ej.value("device", false) ? voxFoundBuilder.get() : nullptr;
    // F7.6b: on the switched world every edit is recorded (the box before / after) for undo
    const auto readVoxel = [this](glm::ivec3 p) { return voxFoundEdit.voxelAt(p); };
    const bool recording = voxFoundWorldReads && !ej.value("no_undo", false);
    bool recordedPending = false;
    if (recording) recordedPending = voxFoundHistory.begin(brush + ":" + matName, lo, hi, readVoxel);
    RegionEdit::Stats st;
    if (brush == "brush")
        voxFoundEdit.brush(lo, brushSize, mat, st, rowsGpu, &rowsErr);   // F3.2b (size checked above)
    else if (brush == "sphere" || brush == "masked_box")   // F7.6c: the masked box
        st = voxFoundEdit.apply(lo, hi, inside, mat, rowsGpu, &rowsErr);
    else
        st = voxFoundEdit.applyBox(lo, hi, mat, rowsGpu, &rowsErr);   // F3.2b: covered blocks written whole
    voxFoundOccBump(st.rebuilt);
    voxFoundLodMark(st);   // F7.6d   // F7.3b: the switched light occupancy repacks these chunks
    const bool recorded = recordedPending && voxFoundHistory.commit(readVoxel);   // F7.6b
    // the CPU executor changed these chunks' voxels behind any resident device copy: forget them (a later device edit
    // would otherwise re-mesh stale voxels next to its own box -- 12 face mismatches, live, before this)
    if (!ej.value("device", false) && voxFoundBuilder)
        for (size_t i : st.rebuilt) voxFoundBuilder->forgetResident(voxFoundEdit.residentKey(i));
    const auto t1 = std::chrono::steady_clock::now();
    // F2.17: only the rebuilt chunks' spans; F3.5a: only those whose lists changed
    voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex, &st.changed);
    const auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    nlohmann::json rebuilt = nlohmann::json::array();
    for (size_t i : st.rebuilt) {
        const auto& c = voxFoundEdit.chunk(i);
        rebuilt.push_back({c.chunk.x, c.chunk.y, c.chunk.z});
    }
    response = {{"brush", brush}, {"material", matName}, {"lo", {lo.x, lo.y, lo.z}}, {"hi", {hi.x, hi.y, hi.z}},
                {"voxels_changed", st.voxelsChanged}, {"chunks_edited", st.chunksEdited},
                {"chunks_rebuilt", rebuilt}, {"faces_removed", st.facesRemoved}, {"faces_added", st.facesAdded},
                {"rows_meshed", st.rowsMeshed}, {"rows_full_chunk", 6u * 256u * 256u},
                {"sprigs_added", st.sprigsAdded}, {"rows_on_device", st.onDevice}, {"rows_error", rowsErr},
                {"plan_ms", st.planMs}, {"rows_wall_ms", st.rowsWallMs}, {"rows_gpu_ms", st.rowsGpuMs},
                {"upload_bytes", st.uploadBytes}, {"resident_misses", st.residentMisses},
                {"set_ms", st.setMs}, {"collapse_ms", st.collapseMs}, {"edit_store_bytes", voxFoundEdit.bytes()}, {"faces_ms", st.facesMs}, {"cluster_ms", st.clusterMs},
                {"sprig_ms", st.sprigMs}, {"connect_ms", st.connectMs}, {"rebuild_ms", ms(t0, t1)},
                {"assemble_ms", ms(t1, t2)}, {"faces", voxFoundPreview.liveFaces()},
                {"connect_floods", st.connectFloods}, {"blocks_filled", st.blocksFilled}, {"brush_size", brushSize}, {"assemble_bytes", voxFoundPreview.lastCopyBytes},
                {"undo", recording ? (recorded ? "recorded" : (recordedPending ? "no_change" : "too_large")) : "off"},
                {"legacy", voxFoundEdit.legacyF31()}, {"legacy_border", voxFoundEdit.legacyBorder()},
                {"chunks_changed", st.changed.size()}, {"legacy_connect", voxFoundEdit.legacyConnect()},
                {"connect_bytes", voxFoundEdit.connectBytes()},
                {"sprigs", voxFoundPreview.liveSprigs()}};
    if (ej.value("verify", false) && voxFoundBuilder) {
        // every rebuilt chunk, fully, on the device: the same voxels + border planes through FaceBuilderGpu
        size_t faceMiss = 0, sprigMiss = 0, connMiss = 0;
        double gpuMs = 0.0;
        for (size_t i : st.rebuilt) {
            const auto& c = voxFoundEdit.chunk(i);
            const SprigParams sp = voxFoundEdit.sprigParams(i);
            std::vector<PackedFace> out;
            std::string err;
            const VoxelGrid g = voxFoundEdit.dense(i);   // F3.2a: the store expanded for the full rebuild
            if (!voxFoundBuilder->build(g, Edges(c.border), out, &err, &sp)) {
                response["verify_error"] = err;
                break;
            }
            gpuMs += voxFoundBuilder->lastGpuMs();
            std::vector<uint64_t> fa, fb, sa, sb;
            for (const PackedFace& f : out) (isSprig(f) ? sb : fb).push_back(f.key());
            for (const PackedFace& f : c.faces) fa.push_back(f.key());
            for (const PackedFace& f : c.sprigs) sa.push_back(f.key());
            for (auto* v : {&fa, &fb, &sa, &sb}) std::sort(v->begin(), v->end());
            std::vector<uint64_t> diff;
            std::set_symmetric_difference(fa.begin(), fa.end(), fb.begin(), fb.end(), std::back_inserter(diff));
            faceMiss += diff.size();
            diff.clear();
            std::set_symmetric_difference(sa.begin(), sa.end(), sb.begin(), sb.end(), std::back_inserter(diff));
            sprigMiss += diff.size();
            connMiss += c.connect != blockFaceConnectivity(g, voxFoundEdit.seeThrough()) ? 1u : 0u;
            // "replace_with_full": draw the full rebuild's lists instead (the pixel A/B reference)
            if (ej.value("replace_with_full", false)) voxFoundEdit.replaceFaces(i, out);
        }
        if (ej.value("replace_with_full", false)) voxFoundEdit.assemble(voxFoundPreview, voxFoundLeafTex);
        response["verify"] = {{"face_mismatches", faceMiss}, {"sprig_mismatches", sprigMiss},
                              {"connectivity_mismatches", connMiss}, {"chunks", st.rebuilt.size()},
                              {"gpu_full_rebuild_ms_sum", gpuMs}};
    }
}

// docs/VoxelFoundationPlan.md F2.8 -- the preview of a REGION of whole engine chunks, built as chunks: each 32 m
// chunk is generated alone (VoxFound::forEachRegionChunk: terrain + flora over the chunk and a 1 m margin), its
// faces built on the GPU with its real border (Neighbours), and drawn at its true world position. Body:
// {region:{from:[cx,cy,cz], to:[cx,cy,cz]}, type, seed, flora, leaves, sprigs_per_m2, hide_world}. Blocking
// (a debug route): generation is CPU, one chunk at a time.
void VoxFoundWorld::handleVoxFoundRegion(const Core::APICommand& cmd, int generationType, const std::string& typeName,
                                       uint32_t seed, nlohmann::json& response) {
    using namespace Core::VoxFound;
    const auto& rj = cmd.params["region"];
    auto vec3Of = [&](const char* k) -> glm::ivec3 {
        if (!rj.contains(k) || !rj[k].is_array() || rj[k].size() != 3)
            throw std::runtime_error(std::string("region.") + k + " must be [cx,cy,cz] (engine chunk coordinates)");
        return {rj[k][0].get<int>(), rj[k][1].get<int>(), rj[k][2].get<int>()};
    };
    const glm::ivec3 a = vec3Of("from"), b = vec3Of("to");
    const glm::ivec3 lo = glm::min(a, b), hi = glm::max(a, b);
    const glm::ivec3 nC = hi - lo + 1;
    if (cmd.params.value("editable", false)) voxFoundRegionRequest = cmd.params;   // F7.9b: what a shift rebuilds
    const int count = nC.x * nC.y * nC.z;
    const int maxChunks = std::clamp(cmd.params.value("max_chunks", 256), 1, 8192);   // F7.28: measured, not assumed
    if (count > maxChunks) {
        response = {{"error", "region over " + std::to_string(maxChunks) + " chunks"}, {"chunks", count}};
        return;
    }
    WorldGenerator gen(static_cast<WorldGenerator::GenerationType>(generationType), seed);
    gen.loadBiomes("resources/biomes.json");
    // The SAME generator as the loaded world: GameDefinitionLoader applies the world DB's stored recipe (biome and
    // flora tuning) when it generates, so the preview applies it too ("recipe":"none" opts out).
    std::string recipeUsed = "biomes.json";
    if (cmd.params.value("recipe", std::string("world")) == "world" && chunkManager) {
        if (WorldStorage* st = chunkManager->getWorldStorage(); st && st->hasMeta("recipe")) {
            gen.applyRecipe(WorldRecipe::fromJson(st->getMeta("recipe")));
            recipeUsed = "world.db";
        }
    }
    const bool flora = cmd.params.value("flora", false);
    const std::string floraDir = "resources/templates_g8/nature";
    // "flora_window":"bounded" (default) plants what today's bounded static region plants (decorateFlora over the
    // region's columns, 8-column inset); "open" plants everything (a streaming world).
    const bool bounded = cmd.params.value("flora_window", std::string("bounded")) == "bounded";
    const glm::ivec4 window(lo.x * kChunkM + 8, lo.z * kChunkM + 8, (hi.x + 1) * kChunkM - 1 - 8,
                            (hi.z + 1) * kChunkM - 1 - 8);
    const bool leafCards = cmd.params.value("leaves", std::string("cards")) != "voxels";
    const double sprigsPerM2 = cmd.params.value("sprigs_per_m2", kSprigsPerM2);
    if (cmd.params.value("editable", false)) voxFoundRegionGen = std::make_shared<WorldGenerator>(gen);   // F7.9c

    std::vector<PackedFace> sprigRecs;
    std::vector<PreviewChunk> chunks;
    // F2.15b: {"closed_casters":true} builds closed shadow casters (each chunk meshed with its leaf voxels as air) for
    // the shadow light test; off by default -- measured: x2.31 caster faces, slower than today's casters
    const bool closedCasters = cmd.params.value("closed_casters", false);
    std::vector<PackedFace> casterFaces;          // F2.15b: closed casters (leaf voxels as air), per chunk
    std::vector<PreviewCluster> casterClusters;   // F2.15b: their direction clusters
    // F2.15d: {"caster_seam_depth":n} -- the leaf-hidden solid faces whose leaf is within n leaf voxels of air (Mesher.h
    // casterSeamGrid), drawn on top of the main faces by the light-tested shadow pass; 0 = not built (default: measured,
    // it does not close the leaks -- a canopy's inside has no cards, so leaf depth does not bound a light ray)
    const int seamDepth = std::clamp(cmd.params.value("caster_seam_depth", 0), 0, 64);
    // F2.15d-2: {"caster_exit_reach":r} -- the main faces of solids within r solid steps of a leaf-hidden face (Mesher.h
    // casterExitGrid), drawn by the light-tested shadow pass as AWAY groups (cast only facing away from the light).
    // Up to the area margin - 1 (7) the set is a function of world position; farther it depends on the chunk cut (a
    // diagnostic only, reported as exit_chunk_exact:false); -1 = unbounded inside each chunk's area; 0 = not built (default:
    // the A/B at reach 24 saved only 0.1-0.5 ms and still leaked, and its only consumer, shadow_dir_cull, is off)
    const int exitReach = std::clamp(cmd.params.value("caster_exit_reach", 0), -1, 64);
    std::vector<PackedFace> seamFaces;
    std::vector<PreviewCluster> seamClusters;
    // F2.11 measured (VoxelFoundationPlan.md): 1-block clusters (26k indirect commands) cost +0.2-1.0 ms whole frame,
    // 2 blocks +0.0-0.26, 4 blocks neutral -> 4 is the default; 1 stays available to measure.
    const int clusterBlocks = std::clamp(cmd.params.value("cluster_blocks", 4), 1, 32);
    // F2.15f: {"dir_order":"axis"} keeps the F2.15 direction order within a cell (-x, +x, -y, ...); default "octant"
    // (-x, -y, -z, +x, +y, +z), which the compacting cull pass merges into fewer commands
    const bool dirOctant = cmd.params.value("dir_order", std::string("octant")) != "axis";
    // F2.13 (Ã‚Â§3.5a point 3, Ã‚Â§4.6 item 7): {"lod_levels":n} also builds pyramid levels 1..n of every chunk (1 = 1/4 m,
    // 2 = 1/2, 3 = 1 m; the 1 m margin of each chunk's area gives a coarse level its real neighbours, so 3 is the
    // deepest here) and keeps them all; {"lod_draw":k} (handleVoxFoundPreview) swaps level k in for the whole region
    // without a rebuild. A debug view to see and measure each level; per-region selection is F2.14. Leaf sprigs and
    // occlusion connectivity stay at level 0.
    // F2.14d: levels 4..7 (2, 4, 8, 16 m) come from the REGION pyramid (Mesher.h lodPaste): each chunk's exact 1 m
    // level is pasted into one region level and lodDown runs on the region (outside it = the residency edge). 7 is
    // the deepest the 3-bit maxLevel of the flags holds (32 m and above need a re-layout).
    const int lodLevels = std::clamp(cmd.params.value("lod_levels", 0), 0, 7);
    const int chunkLevels = std::min(lodLevels, 3);   // built per chunk with its 1 m margin (F2.13)
    // F2.14e: {"lod_gpu":bool} (default true) builds a chunk's levels 1..3 on the device (LodBuilderGpu, exact vs the
    // CPU lodBase + lodDown, VoxFoundLodBuilderGpu); false = the CPU reference. lod_level_ms = the time spent on them.
    const bool lodGpu = cmd.params.value("lod_gpu", true);
    double lodLevelMs = 0.0, lodGpuDeviceMs = 0.0;
    if (lodGpu && chunkLevels > 0 && !voxFoundLodBuilder) {
        voxFoundLodBuilder = std::make_unique<LodBuilderGpu>();
        std::string gerr;
        const std::string spv = Core::AssetManager::instance().resolveShader("voxfound_lod.comp.spv");
        if (!voxFoundLodBuilder->init(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice(),
                                      vulkanDevice->getGraphicsQueue(), vulkanDevice->getGraphicsQueueFamily(),
                                      std::filesystem::path(spv).parent_path().string(), &gerr)) {
            voxFoundLodBuilder.reset();
            response = {{"error", "LodBuilderGpu init: " + gerr}};
            return;
        }
    }
    // F2.14g: {"lod_split_cards":bool} (default FALSE) builds the coarse levels from TWO pyramids, solid voxels and
    // leaf voxels (Mesher.h lodSplitCards): leaves never hide ground or trunk faces at a coarse level, and the leaf
    // pyramid is the leaf mass where it is drawn. Measured against the level-0 reference (VoxelFoundationPlan F2.14g):
    // it removes the holes under coarse canopy cells at P=32 but shows branches level 0 hides under its leaves, farther
    // from level 0 at P=8 -- so it is off. false = one pyramid, cards hide their neighbours (F2.13-F2.14f).
    // {"lod_card_ab":true} also builds the other way as an alternate level set ({"lod_cards_split":bool} swaps it).
    const bool splitCards = cmd.params.value("lod_split_cards", false);
    // F2.14h: {"lod_leaf_fill":t} (0 = off, the default) -- the leaf mass (levels >= lod_sprig_level) comes from the
    // LEAF pyramid, a cell being mass when it covers >= t x 8^k leaf voxels (Mesher.h lodLeafMass); the solid geometry
    // keeps its meshing. {"lod_alt":{"split_cards":bool,"leaf_fill":t}} builds a second variant ({"lod_alt_on":bool}
    // swaps it; {"lod_card_ab":true} = the alt with split flipped, F2.14g).
    const double leafFill = std::clamp(cmd.params.value("lod_leaf_fill", 0.0), 0.0, 1.0);
    bool vSplit[2] = {splitCards, !splitCards};
    double vFill[2] = {leafFill, leafFill};
    int nVariants = cmd.params.value("lod_card_ab", false) ? 2 : 1;
    if (cmd.params.contains("lod_alt") && cmd.params["lod_alt"].is_object()) {
        nVariants = 2;
        vSplit[1] = cmd.params["lod_alt"].value("split_cards", splitCards);
        vFill[1] = std::clamp(cmd.params["lod_alt"].value("leaf_fill", leafFill), 0.0, 1.0);
    }
    auto variantSplit = [&](int v) { return leafCards && vSplit[v]; };
    auto variantLeaf = [&](int v) { return leafCards && (vSplit[v] || vFill[v] > 0.0); };   // needs the leaf pyramid
    // per variant: the region's level 3 (lodLevels > 3), of the main pyramid and of the leaf pyramid (if needed)
    std::vector<LodGrid> regionMain(static_cast<size_t>(nVariants)), regionLeaf(static_cast<size_t>(nVariants));
    if (lodLevels > 3)
        for (int v = 0; v < nVariants; ++v) {
            regionMain[size_t(v)] = lodEmpty(nC * kChunkM, 3);
            if (variantLeaf(v)) regionLeaf[size_t(v)] = lodEmpty(nC * kChunkM, 3);
        }
    // F2.14c: {"lod_sprig_level":h} (1..lod_levels; 0 = off, the default) -- levels >= h build leaves as SOLID coarse
    // leaf mass (today's chunk-LOD handoff of cards); the sprigs then belong to the levels below h and dissolve by
    // size on screen with lod_auto (voxfound_foliage.vert, VoxFound::lodSprigFade).
    const int sprigLevel = std::clamp(cmd.params.value("lod_sprig_level", 0), 0, lodLevels);
    // F2.16 (Ã‚Â§4.6 item 9): {"editable":true} keeps each chunk's voxels, border planes, faces, sprigs and block bits
    // (RegionEdit, ~17 MB per chunk) so {"edit":...} rebuilds incrementally. Level 0 only: refused with coarse levels,
    // closed casters, seam / exit faces or an alternate level set (each would need its own update: owed).
    const bool editable = cmd.params.value("editable", false);
    // F7.6d: coarse levels 1..3 of the default variant are kept current with edits; the rest stays refused (owed)
    if (editable && (lodLevels > 3 || closedCasters || seamDepth > 0 || exitReach != 0 || nVariants > 1 ||
                     (lodLevels > 0 && (splitCards || leafFill > 0.0 || sprigLevel > 0)))) {
        response = {{"error", "editable: lod_levels up to 3 of the default variant only -- no lod_levels > 3, "
                              "closed_casters, caster_seam_depth, caster_exit_reach, lod alternates, lod_split_cards, "
                              "lod_leaf_fill or lod_sprig_level"}};
        return;
    }
    voxFoundLodEditLevels = editable ? chunkLevels : 0;   // F7.6d
    voxFoundLodClusterBlocks = clusterBlocks;
    voxFoundLodDirOctant = dirOctant;
    voxFoundLodRegionLo = lo;
    voxFoundLodDirty.clear();
    voxFoundLodMaxChunkMs = voxFoundLodLastSelectMs = voxFoundLodSumChunkMs = 0.0;
    voxFoundLodRefreshed = 0;
    voxFoundEdit.clear();
    if (voxFoundBuilder) voxFoundBuilder->dropResident();   // F2.16: resident grids of the old region
    if (editable) voxFoundEdit.begin(lo, hi, clusterBlocks, dirOctant, sprigThreshold24(sprigsPerM2, kVoxelsPerM));
    std::vector<std::vector<Preview::Level>> sets(size_t(nVariants), std::vector<Preview::Level>(size_t(lodLevels) + 1));
    std::vector<Preview::Level>& levels = sets[0];
    std::vector<size_t> levelSolid(levels.size(), 0);
    std::vector<std::string> palette;
    double gpuMs = 0.0, genMs = 0.0, buildMs = 0.0;
    size_t solid = 0;
    std::string err;
    // one chunk's faces of level k into that level: F2.11 clusters of clusterBlocks^3 blocks (one indirect command
    // each), solid faces first, then the F2.14c leaf mass (its own clusters, tagged)
    auto addLevel = [&](std::vector<Preview::Level>& into, int k, uint32_t chunkIndex, glm::ivec3 chunk,
                        std::vector<PackedFace>& solidFaces, std::vector<PackedFace>& leafFaces) {
        Preview::Level& L = into[size_t(k)];
        const uint32_t first = uint32_t(L.faces.size());
        for (int part = 0; part < 2; ++part) {
            std::vector<PackedFace>& fs = part ? leafFaces : solidFaces;   // clusterFaces reorders in place
            const uint32_t partFirst = first + (part ? uint32_t(solidFaces.size()) : 0u);
            const std::vector<FaceCluster> cells = clusterFaces(fs, clusterBlocks, dirOctant);
            // F2.15: the same faces cut into direction groups (contiguous within each cell)
            const std::vector<FaceCluster> groups = splitByDirection(fs, cells);
            for (int d = 0; d < 2; ++d)
                for (const FaceCluster& c : d ? groups : cells) {
                    PreviewCluster pcl;
                    const glm::vec3 base = glm::vec3((chunk - lo) * kChunkM);
                    pcl.lo = base + glm::vec3(c.lo) / float(kVoxelsPerM);
                    pcl.hi = base + glm::vec3(c.hi) / float(kVoxelsPerM);
                    pcl.firstFace = partFirst + c.first;
                    pcl.count = c.count;
                    pcl.chunk = chunkIndex;
                    pcl.leafMass = part == 1;
                    pcl.dir = c.dir;
                    (d ? L.clustersDir : L.clusters).push_back(pcl);
                }
        }
        L.faces.insert(L.faces.end(), solidFaces.begin(), solidFaces.end());
        L.faces.insert(L.faces.end(), leafFaces.begin(), leafFaces.end());
        L.firstFace.push_back(first);
        L.faceCount.push_back(uint32_t(L.faces.size()) - first);
        L.leafCount.push_back(uint32_t(leafFaces.size()));
    };
    // one chunk's cut [off, off + inner) of coarse level k of variant v, meshed with its real neighbours and scaled
    // to level-0 units. `main` = that level's grid (the chunk's own area, or the region's above 1 m). One pyramid:
    // leaf-mass levels (F2.14c) draw leaves as solid faces, the others keep them as cards (no faces, hiding their
    // neighbours). Two pyramids (F2.14g): `main` holds no leaves and is meshed alone; `leaf` is meshed (as solid faces)
    // only at leaf-mass levels, in its own grid.
    auto meshCoarse = [&](int v, int k, const VoxelGrid& main, const LodGrid* leafLod, glm::ivec3 off, glm::ivec3 inner,
                          const SprigParams& noSprigs, uint32_t chunkIndex, glm::ivec3 chunk) -> bool {
        const bool leafMass = sprigLevel > 0 && k >= sprigLevel;   // F2.14c: no card set = leaves are solid
        std::vector<PackedFace> solidFaces, leafFaces;
        // F2.14h: the leaf grid to mesh -- thresholded at the variant's fill, or the leaf pyramid level as built
        VoxelGrid leafGrid;
        if (leafLod) {
            uint8_t fallback = 0;
            for (int m = 1; m < 256 && !fallback; ++m)
                if (noSprigs.cards.has(uint8_t(m))) fallback = uint8_t(m);
            leafGrid = vFill[v] > 0.0 ? lodLeafMass(*leafLod, vFill[v], fallback) : leafLod->grid;
        }
        const VoxelGrid* leaf = leafLod ? &leafGrid : nullptr;
        auto meshOne = [&](const VoxelGrid& g, const SprigParams* cardsArg, bool allLeaf) -> bool {
            const VoxelGrid cg = subGrid(g, off, inner);
            const Neighbours cb = neighboursOf(g, off, inner, Outside::Air);
            std::vector<PackedFace> coarse;
            if (!voxFoundBuilder->build(cg, Edges(cb), coarse, &err, cardsArg)) return false;
            gpuMs += voxFoundBuilder->lastGpuMs();
            if (v == 0) levelSolid[size_t(k)] += cg.solidCount();
            for (const PackedFace& f : coarse)
                if (!isSprig(f))
                    ((allLeaf || (leafMass && noSprigs.cards.has(uint8_t((f.b >> 16) & 0xFFu)))) ? leafFaces
                                                                                               : solidFaces)
                        .push_back(lodScaleFace(f, k));
            return true;
        };
        if (leaf && !vSplit[v]) {   // F2.14h: one pyramid for the solid geometry, the leaf mass from the leaf pyramid
            if (!meshOne(main, leafCards ? &noSprigs : nullptr, false)) return false;   // cards: no faces, hide
            if (leafMass && !meshOne(*leaf, nullptr, true)) return false;
        } else if (leaf) {   // two pyramids
            if (!meshOne(main, nullptr, false)) return false;
            if (leafMass) {
                if (!meshOne(*leaf, nullptr, true)) return false;
            } else if (v == 0) {
                levelSolid[size_t(k)] += subGrid(*leaf, off, inner).solidCount();   // the volume metric counts leaves
            }
        } else if (!meshOne(main, (leafCards && !leafMass) ? &noSprigs : nullptr, false)) {
            return false;
        }
        addLevel(sets[size_t(v)], k, chunkIndex, chunk, solidFaces, leafFaces);
        return true;
    };
    // levels 1..n of one grid: on the device (F2.14e) or the CPU reference
    auto buildLevels = [&](const VoxelGrid& g, int n, std::vector<LodGrid>& out) -> bool {
        const auto tl = std::chrono::steady_clock::now();
        out.clear();
        if (lodGpu) {
            if (!voxFoundLodBuilder->build(g, n, out, &err)) return false;
            lodGpuDeviceMs += voxFoundLodBuilder->lastGpuMs();
        } else {
            LodGrid l = lodBase(g);
            for (int k = 1; k <= n; ++k) out.push_back(l = lodDown(l));
        }
        lodLevelMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tl).count();
        return true;
    };
    auto tGen = std::chrono::steady_clock::now();
    auto onChunk = [&](const RegionChunk& rc, const std::vector<std::string>& pal) {
        if (!err.empty()) return;
        const auto t1 = std::chrono::steady_clock::now();
        genMs += std::chrono::duration<double, std::milli>(t1 - tGen).count();
        SprigParams sp;
        CardSet seeThrough;   // F2.10 occlusion: leaf cards and transparent materials pass sight (today: alpha < 0.99)
        for (size_t m = 1; m < pal.size(); ++m) {
            const auto* md = Core::MaterialRegistry::instance().getMaterial(pal[m]);
            if (md && md->billboarded) sp.cards.set(uint8_t(m));
            if (md && (md->billboarded || md->alpha < 0.99f)) seeThrough.set(uint8_t(m));
        }
        sp.worldOriginVoxel = rc.originVoxel;
        sp.threshold24 = sprigThreshold24(sprigsPerM2, kVoxelsPerM);
        std::vector<PackedFace> out;
        if (!voxFoundBuilder->build(rc.grid, Edges(rc.border), out, &err, leafCards ? &sp : nullptr)) return;
        gpuMs += voxFoundBuilder->lastGpuMs();
        if (editable) {   // F2.16: the palette only grows, so the sets of the last chunk cover every earlier one
            voxFoundEdit.setMaterialSets(leafCards ? sp.cards : CardSet{}, seeThrough);
            voxFoundEdit.addChunk(rc, out);
        }
        solid += rc.grid.solidCount();
        PreviewChunk pc;
        pc.originMetres = glm::dvec3(rc.originVoxel) / double(kVoxelsPerM);
        pc.firstSprig = uint32_t(sprigRecs.size());
        {   // level 0: today's rule (cards hide their neighbours), the same faces in every variant
            std::vector<PackedFace> solidFaces, leafFaces;
            for (const PackedFace& f : out) (isSprig(f) ? sprigRecs : solidFaces).push_back(f);
            levelSolid[0] += rc.grid.solidCount();
            for (int v = 0; v < nVariants; ++v) {
                std::vector<PackedFace> sf = solidFaces, lf = leafFaces;   // addLevel reorders in place
                addLevel(sets[size_t(v)], 0, uint32_t(chunks.size()), rc.chunk, sf, lf);
            }
        }
        if (chunkLevels > 0 && rc.area) {
            SprigParams noSprigs = sp;
            noSprigs.threshold24 = 0;   // leaves stay cards (no faces); their sprigs come from level 0
            for (int v = 0; v < nVariants; ++v) {
                // levels 1..chunkLevels of the main pyramid and (split) of the leaf pyramid
                std::vector<LodGrid> mainLv, leafLv;
                if (variantLeaf(v)) {
                    VoxelGrid aSolid, aLeaf;
                    lodSplitCards(*rc.area, sp.cards, aSolid, aLeaf);
                    if (!buildLevels(variantSplit(v) ? aSolid : *rc.area, chunkLevels, mainLv) ||
                        !buildLevels(aLeaf, chunkLevels, leafLv))
                        return;
                } else if (!buildLevels(*rc.area, chunkLevels, mainLv)) {
                    return;
                }
                for (int k = 1; k <= chunkLevels; ++k) {
                    const int sc = 1 << k;
                    const glm::ivec3 off(rc.areaMarginVoxels / sc), inner(kChunkM * kVoxelsPerM / sc);
                    if (!meshCoarse(v, k, mainLv[size_t(k - 1)].grid, leafLv.empty() ? nullptr : &leafLv[size_t(k - 1)],
                                    off, inner, noSprigs, uint32_t(chunks.size()), rc.chunk))
                        return;
                }
                if (lodLevels > 3) {   // F2.14d: this chunk's exact 1 m level(s) into the region level(s)
                    const glm::ivec3 cutAt(rc.areaMarginVoxels / 8), at = (rc.chunk - lo) * kChunkM;
                    lodPaste(regionMain[size_t(v)], lodCut(mainLv[2], cutAt, glm::ivec3(kChunkM)), at);
                    if (!leafLv.empty()) lodPaste(regionLeaf[size_t(v)], lodCut(leafLv[2], cutAt, glm::ivec3(kChunkM)), at);
                }
            }
        }
        if (closedCasters && leafCards && rc.area) {   // F2.15b: closed casters -- leaf voxels as air (opt-in)
            VoxelGrid aSolid, aLeaf;
            lodSplitCards(*rc.area, sp.cards, aSolid, aLeaf);
            const glm::ivec3 off(rc.areaMarginVoxels), inner(kChunkM * kVoxelsPerM);
            const VoxelGrid cg = subGrid(aSolid, off, inner);
            const Neighbours cb = neighboursOf(aSolid, off, inner, Outside::Air);
            std::vector<PackedFace> cf;
            if (!voxFoundBuilder->build(cg, Edges(cb), cf, &err, nullptr)) return;
            gpuMs += voxFoundBuilder->lastGpuMs();
            const uint32_t base = uint32_t(casterFaces.size());
            const std::vector<FaceCluster> cells = clusterFaces(cf, clusterBlocks, dirOctant);
            for (const FaceCluster& c : splitByDirection(cf, cells)) {
                PreviewCluster pcl;
                const glm::vec3 b = glm::vec3((rc.chunk - lo) * kChunkM);
                pcl.lo = b + glm::vec3(c.lo) / float(kVoxelsPerM);
                pcl.hi = b + glm::vec3(c.hi) / float(kVoxelsPerM);
                pcl.firstFace = base + c.first;
                pcl.count = c.count;
                pcl.chunk = uint32_t(chunks.size());
                pcl.dir = c.dir;
                casterClusters.push_back(pcl);
            }
            casterFaces.insert(casterFaces.end(), cf.begin(), cf.end());
        }
        for (int pass = 0; pass < 2 && leafCards && rc.area; ++pass) {   // F2.15d: the entry seam, then the exit faces
            const bool exitPass = pass == 1;
            if ((exitPass ? exitReach : seamDepth) == 0 || (!exitPass && seamDepth < 0)) continue;
            uint8_t hide = 0;
            for (int m = 1; m < 256 && !hide; ++m)
                if (sp.cards.has(uint8_t(m))) hide = uint8_t(m);
            if (hide) {
                const VoxelGrid sg = exitPass ? casterExitGrid(*rc.area, sp.cards, exitReach, hide)
                                              : casterSeamGrid(*rc.area, sp.cards, seamDepth, hide);
                const glm::ivec3 off(rc.areaMarginVoxels), inner(kChunkM * kVoxelsPerM);
                SprigParams noSprigs = sp;
                noSprigs.threshold24 = 0;
                std::vector<PackedFace> sf;
                if (!voxFoundBuilder->build(subGrid(sg, off, inner), Edges(neighboursOf(sg, off, inner, Outside::Skip)),
                                            sf, &err, &noSprigs))
                    return;
                gpuMs += voxFoundBuilder->lastGpuMs();
                const uint32_t base = uint32_t(seamFaces.size());
                const std::vector<FaceCluster> cells = clusterFaces(sf, clusterBlocks, dirOctant);
                for (const FaceCluster& c : splitByDirection(sf, cells)) {
                    PreviewCluster pcl;
                    const glm::vec3 b = glm::vec3((rc.chunk - lo) * kChunkM);
                    pcl.lo = b + glm::vec3(c.lo) / float(kVoxelsPerM);
                    pcl.hi = b + glm::vec3(c.hi) / float(kVoxelsPerM);
                    pcl.firstFace = base + c.first;
                    pcl.count = c.count;
                    pcl.chunk = uint32_t(chunks.size());
                    pcl.dir = exitPass ? c.dir + kAwayGroup : c.dir;
                    seamClusters.push_back(pcl);
                }
                seamFaces.insert(seamFaces.end(), sf.begin(), sf.end());
            }
        }
        pc.firstFace = levels[0].firstFace.back();
        pc.faceCount = levels[0].faceCount.back();
        pc.sprigCount = uint32_t(sprigRecs.size()) - pc.firstSprig;
        pc.chunk = rc.chunk;
        pc.connect = blockFaceConnectivity(rc.grid, seeThrough);
        chunks.push_back(pc);   // kept even with nothing to draw: a solid chunk still blocks sight (F2.10)
        palette = pal;
        tGen = std::chrono::steady_clock::now();
        buildMs += std::chrono::duration<double, std::milli>(tGen - t1).count();
    };
    // F3.4: {"source":"world_db"} -- the region's chunks are the open world.db's SAVED chunks (PXB2) through the
    // converter (VoxFound::convertPxb2) instead of the generator; flora, recipe and margins do not apply.
    const bool fromWorldDb = cmd.params.value("source", std::string("generator")) == "world_db";
    // F7.27: {"source":"today"} -- each region chunk is TODAY's resident chunk encoded (ChunkBlobCodec) and converted
    // (VoxFound::convertPxb2): the region is today's world as it stands, hand-built content included
    const bool fromToday = cmd.params.value("source", std::string("generator")) == "today";
    StoredRegionStats stored;
    RegionBuild rb;
    if (fromToday) {
        if (!chunkManager) {
            response = {{"error", "source today: no chunk manager"}};
            return;
        }
        std::string convErr;
        rb = forEachStoredChunk([this](glm::ivec3 c, std::vector<uint8_t>& out) {
                                    const Chunk* ch = chunkManager->getChunkAtCoord(c);
                                    if (!ch) return false;
                                    out = Phyxel::ChunkBlobCodec::encode(*ch);
                                    return !out.empty();
                                },
                                lo, hi, onChunk, &stored, &convErr);
        if (!convErr.empty() && err.empty()) err = convErr;
    } else if (fromWorldDb) {
        auto* ws = chunkManager ? chunkManager->m_streamingManager.getWorldStorage() : nullptr;
        if (!ws || !ws->getDb()) {
            response = {{"error", "source world_db: no world storage (world.db) open"}};
            return;
        }
        std::string convErr;
        rb = forEachStoredChunk([ws](glm::ivec3 c, std::vector<uint8_t>& out) { return ws->readChunkBlob(c, out); },
                                lo, hi, onChunk, &stored, &convErr);
        if (!convErr.empty() && err.empty()) err = convErr;
    } else {
        // F7.9a: {"gen_threads":n} chunk areas on n worker threads (default 4; 1 = the old serial path);
        // {"gen_legacy":true} the per-voxel material lookup (the reference) -- both A/B knobs
        setTerrainLegacy(cmd.params.value("gen_legacy", false));
        rb = forEachRegionChunk(gen, lo, hi, flora ? &floraDir : nullptr, onChunk, bounded ? &window : nullptr,
                                std::clamp(cmd.params.value("gen_threads", 4), 1, 32));   // 4: measured fastest (F7.9a)
        setTerrainLegacy(false);
    }
    if (!err.empty()) {
        response = {{"error", err}};
        return;
    }
    // F2.14d: levels 4..lodLevels from the region pyramid, each chunk's cut meshed with its region neighbours
    double regionLodMs = 0.0;
    nlohmann::json topView = nlohmann::json::object();   // F2.14d look gate: level k vs the region's 1 m level
    if (lodLevels > 3 && regionMain[0].grid.size()) {
        const auto t0 = std::chrono::steady_clock::now();
        SprigParams sp;   // the region palette's card set (the per-chunk one grew with the palette)
        for (size_t m = 1; m < rb.palette.size(); ++m) {
            const auto* md = Core::MaterialRegistry::instance().getMaterial(rb.palette[m]);
            if (md && md->billboarded) sp.cards.set(uint8_t(m));
        }
        sp.threshold24 = 0;   // leaves stay cards (no faces); their sprigs come from level 0
        const LodGrid regionL3 = regionMain[0];
        for (int k = 4; k <= lodLevels && err.empty(); ++k) {
            for (int v = 0; v < nVariants; ++v) {
                regionMain[size_t(v)] = lodDown(regionMain[size_t(v)]);
                if (regionLeaf[size_t(v)].grid.size()) regionLeaf[size_t(v)] = lodDown(regionLeaf[size_t(v)]);
            }
            const LodTopStats ts = lodTopStats(regionL3, regionMain[0], sp.cards);
            nlohmann::json sh = nlohmann::json::object();
            for (size_t m = 1; m < rb.palette.size() && m < ts.shareRef.size(); ++m)
                if (ts.shareRef[m] >= 0.05 || ts.share[m] >= 0.05)
                    sh[rb.palette[m]] = {std::round(ts.shareRef[m] * 1000.0) / 1000.0,
                                         std::round(ts.share[m] * 1000.0) / 1000.0};
            nlohmann::json hid = nlohmann::json::object();
            for (size_t m = 1; m < rb.palette.size() && m < ts.shareHidden.size(); ++m)
                if (ts.shareHidden[m] >= 0.01) hid[rb.palette[m]] = std::round(ts.shareHidden[m] * 1000.0) / 1000.0;
            topView[std::to_string(k)] = {{"mean_err_cells", ts.meanErrCells}, {"max_err_cells", ts.maxErrCells},
                                          {"share_l3_vs_lk", sh}, {"hidden_top", ts.hiddenTop},
                                          {"hidden_top_materials", hid}};
            const int n = (kChunkM * kVoxelsPerM) >> k;   // cells per chunk side at level k
            for (int v = 0; v < nVariants && err.empty(); ++v)
                for (size_t i = 0; i < chunks.size() && err.empty(); ++i)
                    if (!meshCoarse(v, k, regionMain[size_t(v)].grid,
                                    regionLeaf[size_t(v)].grid.size() ? &regionLeaf[size_t(v)] : nullptr,
                                    (chunks[i].chunk - lo) * n, glm::ivec3(n), sp, uint32_t(i), chunks[i].chunk))
                        break;
        }
        regionLodMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!err.empty()) {
            response = {{"error", err}};
            return;
        }
    }
    palette = rb.palette;
    voxFoundPreview.faceTex.assign(kFaceTexWords, 0u);
    std::vector<uint16_t> leafTex(palette.size(), 0);
    for (size_t m = 1; m < palette.size() && m < 256; ++m)
        for (int f = 0; f < 6; ++f) {
            const uint16_t t = Core::MaterialRegistry::instance().getTextureIndex(palette[m], f);
            voxFoundPreview.faceTex[m * 6 + f] = t;
            if (f == 0) leafTex[m] = t;
        }
    voxFoundPalette = palette;   // F2.16: edits look materials up here (and may append)
    voxFoundLeafTex = leafTex;
    voxFoundPreview.sprigs.resize(sprigRecs.size());
    for (size_t i = 0; i < sprigRecs.size(); ++i) {   // as the single-area preview: leaf texture face 0, full sky
        const uint32_t m = (sprigRecs[i].b >> 16) & 0xFFu;
        voxFoundPreview.sprigs[i] = {(sprigRecs[i].a & 0xFFFFFFu) | (15u << 24), m < leafTex.size() ? leafTex[m] : 0u};
    }
    voxFoundPreview.chunks = std::move(chunks);
    voxFoundPreview.casterFaces = std::move(casterFaces);         // F2.15b (selectLevel / selectLod append them)
    voxFoundPreview.casterClustersDir = std::move(casterClusters);
    voxFoundPreview.seamFaces = std::move(seamFaces);                 // F2.15d (appended with the casters)
    voxFoundPreview.seamClustersDir = std::move(seamClusters);
    voxFoundPreview.seamDepth = seamDepth;
    voxFoundPreview.exitReach = exitReach;
    voxFoundPreview.levels = std::move(sets[0]);
    voxFoundPreview.levelsAlt.clear();   // F2.14g A/B
    if (nVariants > 1) voxFoundPreview.levelsAlt = std::move(sets[1]);
    voxFoundPreview.lodSplit = variantSplit(0);   // (F2.14h: lod_alt_on swaps whatever the alt variant is)
    voxFoundPreview.lodUseAlt = false;
    voxFoundPreview.lodSprigLevel = sprigLevel;   // F2.14c
    // F2.14: with coarser levels built, all of them go on the device (level 0 first) for selection by size on screen
    if (voxFoundPreview.levels.size() < 2 || !voxFoundPreview.selectLod()) voxFoundPreview.selectLevel(0);
    voxFoundPreview.anchorMetres = glm::dvec3(lo * kChunkM);
    voxFoundPreview.clusterBlocks = clusterBlocks;
    voxFoundPreview.shown = true;
    voxFoundPreview.originMetres = glm::dvec3(lo * kChunkM);
    ++voxFoundPreview.revision;
    renderCoordinator->setVoxFoundPreview(&voxFoundPreview);
    nlohmann::json lodFaces = nlohmann::json::array(), lodVolume = nlohmann::json::array(),
                   lodLeafFaces = nlohmann::json::array(), lodFacesAlt = nlohmann::json::array(),
                   lodLeafFacesAlt = nlohmann::json::array();
    for (size_t k = 1; k < voxFoundPreview.levelsAlt.size(); ++k) {
        lodFacesAlt.push_back(voxFoundPreview.levelsAlt[k].faces.size());
        uint64_t leaf = 0;
        for (uint32_t n : voxFoundPreview.levelsAlt[k].leafCount) leaf += n;
        lodLeafFacesAlt.push_back(leaf);
    }
    for (size_t k = 0; k < voxFoundPreview.levels.size(); ++k) {
        lodFaces.push_back(voxFoundPreview.levels[k].faces.size());
        uint64_t leaf = 0;   // F2.14c
        for (uint32_t n : voxFoundPreview.levels[k].leafCount) leaf += n;
        lodLeafFaces.push_back(leaf);
        lodVolume.push_back(levelSolid[0] ? double(levelSolid[k]) * double(uint64_t(1) << (3 * k)) / double(levelSolid[0])
                                          : 0.0);
    }
    // F3.3: {"load_saved":true} -- the saved chunks of this region win over the generator
    nlohmann::json loadSaved = nullptr;
    if (editable && cmd.params.value("load_saved", false)) loadSaved = loadVoxFoundSaved();
    response = {{"provenance", "engine WorldGenerator (" + typeName + " seed " + std::to_string(seed) +
                                   ") at 1/8 m, generated chunk by chunk by VoxFound::forEachRegionChunk" +
                                   (flora ? " + engine flora plan stamped from " + floraDir : "") +
                                   "; faces by FaceBuilderGpu with each chunk's real neighbours"},
                {"chunks_from", {lo.x, lo.y, lo.z}}, {"chunks_to", {hi.x, hi.y, hi.z}},
                {"chunks", rb.chunks},
                {"clusters", voxFoundPreview.clusters.size()}, {"cluster_blocks", clusterBlocks},
                {"lod_levels", lodLevels}, {"lod_faces", lodFaces}, {"lod_solid_volume_vs_l0", lodVolume},
                {"lod_sprig_level", sprigLevel}, {"lod_leaf_mass_faces", lodLeafFaces},
                {"chunks_with_geometry", std::count_if(voxFoundPreview.chunks.begin(), voxFoundPreview.chunks.end(),
                                                       [](const PreviewChunk& c) { return c.faceCount || c.sprigCount; })},
                {"chunks_fully_blocking", std::count_if(voxFoundPreview.chunks.begin(), voxFoundPreview.chunks.end(),
                                                        [](const PreviewChunk& c) {
                                                            return c.connect == FaceConnect{0, 0, 0, 0, 0, 0};
                                                        })},
                {"solid_voxels", solid},
                {"faces", voxFoundPreview.faces.size()}, {"sprigs", voxFoundPreview.sprigs.size()},
                {"face_bytes", voxFoundPreview.faces.size() * sizeof(PackedFace)},
                {"caster_faces", voxFoundPreview.casterFaces.size()},
                {"seam_faces", voxFoundPreview.seamFaces.size()}, {"seam_depth", seamDepth},
                {"exit_reach", exitReach}, {"exit_chunk_exact", exitReach >= 0 && exitReach <= 7},
                {"sprig_bytes", voxFoundPreview.sprigs.size() * sizeof(PreviewSprig)},
                {"face_tex_table_bytes", kFaceTexWords * sizeof(uint32_t)},
                {"gpu_face_build_ms_sum", gpuMs}, {"generate_ms", genMs}, {"build_wall_ms", buildMs},
                {"region_lod_ms", regionLodMs}, {"lod_top_view", topView},
                {"lod_gpu", lodGpu}, {"lod_level_ms", lodLevelMs}, {"lod_gpu_device_ms_sum", lodGpuDeviceMs},
                {"lod_split_cards", voxFoundPreview.lodSplit}, {"lod_faces_alt", lodFacesAlt},
                {"lod_leaf_fill", vFill[0]}, {"lod_alt_leaf_fill", nVariants > 1 ? vFill[1] : 0.0},
                {"lod_leaf_mass_faces_alt", lodLeafFacesAlt},
                {"palette", palette}, {"leaves", leafCards ? "cards" : "voxels"},
                {"flora_stamped_per_chunk_sum", rb.flora.stamped}, {"flora_missing", rb.flora.missingNames},
                {"recipe", recipeUsed}, {"flora_window", bounded ? "bounded" : "open"},
                {"flora_planned_today_rect", flora && bounded
                     ? gen.planFlora(lo.x * kChunkM, lo.z * kChunkM, (hi.x + 1) * kChunkM - 1, (hi.z + 1) * kChunkM - 1).size()
                     : size_t(0)},
                {"editable", editable}, {"edit_store_bytes", voxFoundEdit.bytes()},   // F2.16
                {"load_saved", loadSaved},   // F3.3
                {"source", fromToday ? "today (resident chunks through VoxFound::convertPxb2)"
                                     : fromWorldDb ? "world_db (PXB2 through VoxFound::convertPxb2)" : "generator"},
                {"converted", (fromWorldDb || fromToday) ? nlohmann::json{{"chunks_stored", stored.chunksStored},
                                                           {"chunks_missing", stored.chunksMissing},
                                                           {"cubes", stored.sum.cubes}, {"subcubes", stored.sum.subcubes},
                                                           {"microcubes", stored.sum.microcubes},
                                                           {"cells_resampled", stored.sum.cellsResampled},
                                                           {"materials_rescued", stored.sum.rescued},
                                                           {"tinted_dropped", stored.sum.tinted},
                                                           {"state_dropped", stored.sum.withState},
                                                           {"water_spans_dropped", stored.sum.waterSpans},
                                                           {"convert_ms", stored.convertMs},
                                                           {"volume_in_m3", stored.volumeIn},
                                                           {"volume_out_m3", stored.volumeOut}}
                                          : nlohmann::json(nullptr)},   // F3.4
                {"hide_world", renderCoordinator->hideWorld()}};
}

// ============================================================================
// Water-sim (CPU WaterManager) debug commands Ã¢â‚¬â€ first domain migrated off the processAPICommands
// if-chain onto the CommandRegistry. Handlers run on the game-loop thread; the dispatcher owns
// onComplete. Registered once during init.
// MatSim lab routes (docs/MaterialSimPlan.md Ã‚Â§6a): POST /api/matsim/<action> -> "matsim_<action>".
// The lab is created at init; a handler that runs before it exists, or throws on a mistyped field,
// docs/VoxelFoundationPlan.md step 7 D3-core: the per-frame work, moved from Application::update so every host (the
// editor, GameShell) runs the same boot / stream / LOD / grass / bodies sequence.
void VoxFoundWorld::tick(float dt) {
    stepVoxFoundBodies(dt);   // F5.3 bodies lab (its own run switch; inert without a rig)
    if (!voxFoundLodDirty.empty()) voxFoundLodRefresh(1);   // F7.6d: one dirty chunk's coarse levels per frame
    voxFoundStreamTick();                                   // F7.9b: the switched world follows the camera
    if (voxFoundBootPending && voxFoundBootConfig.contains("voxfound") &&
        (voxFoundBootConfig["voxfound"].value("source", std::string()) == "today" ||
         voxFoundBootConfig["voxfound"].value("source", std::string()) == "world_db") && chunkManager) {
        // F7.27: today's world must have LANDED before it is converted -- no deferred DB loads, no generation pending,
        // and the resident set unchanged since the last frame (VillageChat booted on 27 of its chunks while loading)
        static size_t s_lastResident = 0;
        const size_t resident = chunkManager->chunkMap.size();
        if (chunkManager->hasDeferredDbLoads() || chunkManager->streamingManagerRO().pendingGenerationCount() > 0 ||
            resident != s_lastResident)
            voxFoundBootFrames = 0;
        s_lastResident = resident;
    }
    voxFoundGrassRefreshTick();   // F7.30
    if (voxFoundBootPending && ++voxFoundBootFrames > 30) {  // F7.16: the boot mode, once the first frames ran
        voxFoundBootPending = false;
        try {   // F7.27: a boot that fails is REPORTED, never fatal (TavernTest: a null world field killed the app)
            voxFoundBoot();
        } catch (const std::exception& e) {
            voxFoundBootReport = {{"booted", false}, {"build_error", std::string("boot threw: ") + e.what()}};
            LOG_ERROR("VoxFound", std::string("boot mode failed: ") + e.what());
        }
    }
}

// D3-core: read game.json's boot mode and set today's-world switch -- BEFORE the runtime initializes (the DB
// stream-in boot runs inside EngineRuntime::initialize). Returns true when the project boots switched.
bool VoxFoundWorld::preInit(const std::string& gameJsonPath) {
    voxFoundReadBootConfig(gameJsonPath);
    const bool off = (voxFoundBootPending || voxFoundMultiScene) && !voxFoundTodayKeep;
    ChunkStreamingManager::setTodayWorldDisabled(off);
    return off;
}

// docs/VoxelFoundationPlan.md step 7 D3-core part 3: the special editors' stage -- an editable region over chunk
// (0,0,0) from the Flat generator (top at 16 m, like the old Stone floor at y 15), no flora, switched on. Today's
// chunks are the caller's to clear.
nlohmann::json VoxFoundWorld::buildEditorStage() {
    Core::APICommand c;
    nlohmann::json r = nlohmann::json::object(), on = nlohmann::json::object();
    c.params = {{"type", "Flat"}, {"seed", 1}, {"flora", false}, {"editable", true}, {"hide_world", false},
                {"region", {{"from", {0, 0, 0}}, {"to", {0, 0, 0}}}}};
    handleVoxFoundPreview(c, r);
    if (r.contains("error")) return {{"success", false}, {"build", r}};
    c.params = {{"world_switch", true}, {"load_saved", false}};
    handleVoxFoundPreview(c, on);
    // the old floor's top was exactly 16 m (a Stone cube at y 15): clear the generator's surface layer above it (the
    // Flat surface is half a metre of Grass over 16 m) so templates stand on 16 m and the save box holds only edits
    std::vector<std::pair<glm::ivec3, uint8_t>> carve;
    constexpr int kVpm = Core::VoxFound::kVoxelsPerM, kTop = 16 * kVpm;
    for (int x = 0; x < 32 * kVpm; ++x)
        for (int z = 0; z < 32 * kVpm; ++z)
            for (int y = kTop; y < kTop + kVpm; ++y)
                if (voxFoundEdit.voxelAt({x, y, z})) carve.push_back({{x, y, z}, 0});
    const nlohmann::json cr = carve.empty() ? nlohmann::json{{"voxels_changed", 0}} : voxFoundPaintCells("editor_stage", carve);
    voxFoundHistory.clear();   // the stage is not an undoable edit
    return {{"success", !on.contains("error")}, {"build_ms", r.value("build_ms", 0.0)}, {"switch", on},
            {"carved", cr.value("voxels_changed", 0)}};
}

nlohmann::json VoxFoundWorld::saveGrid8Template(const std::string& outPath, glm::ivec3 lo, glm::ivec3 hi,
                                                const std::string& name) {
    struct Run { glm::ivec3 p; int len; uint8_t m; };
    std::vector<Run> runs;
    glm::ivec3 mn(std::numeric_limits<int>::max());
    std::map<std::string, size_t> perMat;
    size_t cells = 0;
    for (int x = lo.x; x < hi.x; ++x)
        for (int y = lo.y; y < hi.y; ++y)
            for (int z = lo.z; z < hi.z; ++z) {
                const uint8_t m = voxFoundEdit.voxelAt({x, y, z});
                if (!m) continue;
                ++cells;
                mn = glm::min(mn, glm::ivec3(x, y, z));
                const std::string mat = m < voxFoundPalette.size() ? voxFoundPalette[m] : "Default";
                ++perMat[mat];
                if (!runs.empty() && runs.back().m == m && runs.back().p.x == x && runs.back().p.y == y &&
                    runs.back().p.z + runs.back().len == z)
                    ++runs.back().len;
                else
                    runs.push_back({{x, y, z}, 1, m});
            }
    if (!cells) return {{"success", false}, {"error", "nothing to save in the box"}};
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path(), ec);
    std::ofstream f(outPath, std::ios::binary);
    if (!f) return {{"success", false}, {"error", "cannot write " + outPath}};
    f << "# grid: 8\n# name: " << name << "\n# saved by the asset editor (docs/VoxelFoundationPlan.md D3-core)\n";
    for (const Run& r : runs) {
        const glm::ivec3 p = r.p - mn;
        const std::string mat = r.m < voxFoundPalette.size() ? voxFoundPalette[r.m] : "Default";
        if (r.len == 1) f << "V " << p.x << ' ' << p.y << ' ' << p.z << ' ' << mat << '\n';
        else f << "B " << p.x << ' ' << p.y << ' ' << p.z << " 1 1 " << r.len << ' ' << mat << '\n';
    }
    return {{"success", true}, {"path", outPath}, {"cells", cells}, {"boxes", runs.size()}, {"per_material", perMat}};
}

// D3-scenes: the single-scene default rule (F7.27 / D1b), as a function of one world object (null = DB-only)
nlohmann::json VoxFoundWorld::defaultBootConfig(const nlohmann::json& worldOrNull) const {
    nlohmann::json w = worldOrNull.is_object() ? worldOrNull : nlohmann::json::object();
    for (auto it = w.begin(); it != w.end();) it = it->is_null() ? w.erase(it) : std::next(it);   // null fields: absent
    if (w.contains("type") && w["type"].is_string()) {   // D1b: "flat" -> "Flat" (the generator's names)
        std::string t = w["type"].get<std::string>();
        for (size_t k = 0; k < t.size(); ++k) t[k] = char(k == 0 ? std::toupper(uint8_t(t[k])) : std::tolower(uint8_t(t[k])));
        w["type"] = t;
    }
    if (w.value("streaming", false) || w.contains("type"))   // D1b: typed = generated around the camera
        w["voxfound"] = {{"enabled", true}, {"size", {4, 2, 4}}, {"y_from", 0}, {"load_saved", true},
                         {"stream", true}, {"default", true}, {"overlay_world_db", true}};
    else
        w["voxfound"] = {{"enabled", true}, {"source", "world_db"}, {"load_saved", true}, {"stream", false},
                         {"default", true}};   // D1: the DB's stored chunks -- no Chunk object read
    return w;
}

void VoxFoundWorld::sceneSave() {
    if (!voxFoundWorldReads || voxFoundEdit.empty()) return;
    nlohmann::json saved = nlohmann::json::object(), off = nlohmann::json::object();
    handleVoxFoundSave(saved);
    Core::APICommand c;
    c.params = {{"world_switch", false}};
    handleVoxFoundPreview(c, off);
    voxFoundStreamOn = false;
    voxFoundStreamCache.clear();   // the leaving scene's chunks
    voxFoundStructures.clear();    // its registry (saved with it; the next scene loads its own)
    voxFoundOverlayDb = false;
    voxFoundHistory.clear();
    voxFoundSceneLog.push_back({{"save", saved}, {"off", off}});
}

void VoxFoundWorld::sceneLoad(const nlohmann::json& world) {
    voxFoundBootConfig = defaultBootConfig(world);
    voxFoundBootPending = false;
    try {
        voxFoundBoot();
    } catch (const std::exception& e) {
        voxFoundBootReport = {{"booted", false}, {"build_error", std::string("scene boot threw: ") + e.what()}};
    }
    voxFoundSceneLog.push_back({{"load", voxFoundBootReport.value("booted", false)},
                                {"source", voxFoundBootReport.value("source", std::string())},
                                {"error", voxFoundBootReport.value("build_error", std::string())}});
    LOG_INFO("VoxFound", "scene region: " + voxFoundBootReport.dump());
}

}  // namespace Phyxel
