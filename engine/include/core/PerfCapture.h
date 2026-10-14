#pragma once

// PerfCapture -- the per-frame half of the city benchmark tooling (docs/PerfProgram2026-09.md section
// 16: I11 frame pacing, I12 camera-path stream_follow, I14 streaming focus, I15 route recorder). Owned
// by EngineRuntime so the editor and a standalone game share ONE implementation; each host calls
// onFrameEnd() once per frame, right after PerformanceProfiler::endFrame():
//   * the editor from its own loop (Application::run),
//   * a standalone game from EngineRuntime::endFrame().
// Measurement only: nothing here changes what is rendered or simulated, except that a camera path
// started with stream_follow moves the streaming focus (I14) -- which is its purpose.

#include <cstdint>

#include "utils/CpuTimingRecorder.h"
#include "utils/RouteRecorder.h"

namespace Phyxel {

class PerformanceProfiler;
class ChunkManager;
namespace Graphics {
class Camera;
class CameraManager;
class RenderCoordinator;
}  // namespace Graphics

namespace Core {

class PerfCapture {
public:
    PerfCapture() : framePacing_(240) {}

    // Wires the frame-pacing recorder into the profiler (every ScopedTimer scope is recorded too).
    void attachProfiler(PerformanceProfiler* profiler);
    // The host's render coordinator: the source of the GPU serial each frame records and of the
    // resolved GPU scopes the route recorder attaches later. Hooked on the first onFrameEnd().
    void setRenderCoordinator(Graphics::RenderCoordinator* rc) { rc_ = rc; gpuHooked_ = false; }

    // Once per frame, after PerformanceProfiler::endFrame().
    void onFrameEnd(ChunkManager* chunks, Graphics::CameraManager* cameras, const Graphics::Camera* camera);

    CpuTimingRecorder& framePacing() { return framePacing_; }
    const CpuTimingRecorder& framePacing() const { return framePacing_; }
    RouteRecorder& recorder() { return recorder_; }
    const RouteRecorder& recorder() const { return recorder_; }

    // I12 stream_follow: while the camera path plays, the streaming focus (holder "camera_path") tracks
    // the camera, moving at most StreamingFocus::kMaxStepPerFrame per frame. Released when the path
    // finishes or stops. Set by the camera-path API.
    void setStreamFollow(bool on) { streamFollow_ = on; }
    bool streamFollow() const { return streamFollow_; }
    static constexpr const char* kFocusHolder = "camera_path";

private:
    CpuTimingRecorder framePacing_;
    RouteRecorder recorder_;
    PerformanceProfiler* profiler_ = nullptr;
    Graphics::RenderCoordinator* rc_ = nullptr;
    bool gpuHooked_ = false;
    bool streamFollow_ = false;
};

}  // namespace Core
}  // namespace Phyxel
