#pragma once
// docs/VoxelFoundationPlan.md F7.3b (section 4.4; prediction docs/evidence/voxfound_f7_prediction.txt O1-O5) -- the
// light occupancy (LightingPipeline.md D / F, R8: the micro-resolution pool the probe field and the light visibility walk
// read) built from the new store.
//
// One engine chunk's BlockStore (256^3 voxels of 1/8 m) -> ChunkLightOccupancy (32^3 cubes, 9^3 micro cells each):
// a cube (one 8^3 block) with 512 solid voxels is SOLID, 0 is empty, otherwise MIXED with its 729 micro bits = the 1/8 m
// voxel under each 1/9 m micro cell's centre -- the same sampling as WorldReads::occupiedMicro (F7.1), so what blocks
// light is what the NPCs read. A mixed cube whose 729 samples are all air carries nothing (as buildLightOccupancy).

#include "core/voxfound/StorageLab.h"
#include "graphics/VoxelLightOccupancy.h"

namespace Phyxel::Core::VoxFound {

Graphics::ChunkLightOccupancy lightOccupancyOf(const BlockStore& store);

}  // namespace Phyxel::Core::VoxFound
