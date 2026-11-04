#pragma once
// docs/MaterialSimPlan.md §6a/§6c — MatSim P1b: the lab service behind POST /api/matsim/<action>.
//
// Owns one lab box: the CPU core (the state's owner and every edit) and, when a Vulkan device is
// given, the GPU stepper. Steps run on the GPU unless the box was created with backend "cpu".
// The two copies are kept honest by two flags: an edit or a read on the host first downloads a
// GPU-stepped state; a step on the GPU first uploads a host-edited one.
//
// Inert until `create` (MatSimDefaults.InertUntilCreated): update() and the view do nothing.
// No existing engine default changes. Solids come from the micro occupancy pool (Unknown =
// solid and counted; create refuses an unknown box).

#include "core/matsim/MatSimCore.h"
#include "core/Types.h"
#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>

namespace Phyxel::Core::MatSim {

class MatSimGpu;

class MatSimLab {
public:
    using Json = nlohmann::json;

    explicit MatSimLab(MicroSolidFn occupancy);
    ~MatSimLab();

    /// Optional: without it every box steps on the CPU (reported as backend "cpu").
    bool initGpu(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
                 const std::string& shaderDir, std::string* err);

    /// One API action (create, fill_box, step, realtime, set_cell_size, resolidify, ledger, stats,
    /// view, clear, parity). Fills `out`; refusals carry "error" and change nothing.
    void handle(const std::string& action, const Json& params, Json& out);
    static const std::vector<std::string>& actions();

    /// Realtime stepping at the fixed 60 Hz tick (at most 4 ticks per frame; the rest is dropped
    /// and counted). Rebuilds the raw-cell view when it is on and anything moved.
    void update(double dtSeconds);

    bool viewOn() const { return view_ && core_.created(); }
    /// The raw-cell view: exposed faces of every non-empty cell, each cell drawn at its fill height.
    const std::vector<DynamicSubcubeInstanceData>& faces() const { return faces_; }
    static constexpr size_t kMaxFaces = 393216;   ///< 25 MB of faces; above it the view is cut and the cut is reported

private:
    bool ensureHost();   ///< the host core holds the latest state (false: a download failed, the host is stale)
    bool ensureGpu();    ///< the device holds the latest state (false: CPU stepping)
    StepStats doStep(int n);
    void rebuildFaces();
    Json ledgerJson() const;
    Json boxJson() const;
    void echo(Json& out) const;   ///< ledger, cells_per_m, box
    bool metresToCells(const Json& v, IVec3& out, std::string& why, const char* what) const;

    MicroSolidFn occ_;
    MatSimCore core_;
    std::unique_ptr<MatSimGpu> gpu_;
    bool useGpu_ = false;
    bool hostCurrent_ = true;
    bool gpuCurrent_ = false;
    bool realtime_ = false;
    bool view_ = false;
    bool facesDirty_ = true;
    double acc_ = 0.0;
    uint64_t droppedTicks_ = 0;
    uint64_t lastEligible_ = 0;
    double lastGpuMsPerTick_ = -1.0;
    double lastWallMsPerTick_ = -1.0;
    std::vector<DynamicSubcubeInstanceData> faces_;
    uint64_t facesCut_ = 0;
    std::string gpuError_;
};

} // namespace Phyxel::Core::MatSim
