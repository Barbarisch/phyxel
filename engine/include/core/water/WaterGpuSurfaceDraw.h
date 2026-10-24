#pragma once

#include "core/water/WaterLook.h"
#include <vulkan/vulkan.h>
#include <cstdint>

namespace Phyxel::Core::Water {

/// WaterCore 29 S1 (docs/WaterCore.md 29): one volume's surface as the renderer draws it straight from the device - the dense
/// field, the corners / meta / extras list wc_surface_mesh.comp made in the water's own call, and the extras' indirect draw args.
/// The buffers are the volume's own (WaterCoreGpu::SurfaceView); valid until the volume is destroyed (destroyVolume waits idle).
struct GpuSurfaceDraw {
    VkBuffer surf = VK_NULL_HANDLE, corners = VK_NULL_HANDLE, meta = VK_NULL_HANDLE, list = VK_NULL_HANDLE, args = VK_NULL_HANDLE;
    VkDeviceSize surfBytes = 0, cornersBytes = 0, metaBytes = 0, listBytes = 0;
    int originX = 0, originZ = 0, nx = 0, nz = 0;   ///< the field's lattice (cells)
    float h = 0.0f;
    uint64_t serial = 0;                           ///< the volume's (the renderer keys its descriptor sets by it)
    int rippleLayer = -1;                          ///< 22: the ripple layer drawn on it (-1 = none)
    WaterLookPacked look;                          ///< G3: the body's look
};

}  // namespace Phyxel::Core::Water
