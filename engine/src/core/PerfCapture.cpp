#include "core/PerfCapture.h"

#include <cmath>

#include "core/ChunkManager.h"
#include "core/StreamingFocus.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "graphics/RenderCoordinator.h"
#include "utils/GpuProfiler.h"
#include "utils/PerformanceProfiler.h"

namespace Phyxel {
namespace Core {

void PerfCapture::attachProfiler(PerformanceProfiler* profiler) {
    profiler_ = profiler;
    if (profiler_) profiler_->setFrameRecorder(&framePacing_);
}

void PerfCapture::onFrameEnd(ChunkManager* chunks, Graphics::CameraManager* cameras,
                             const Graphics::Camera* camera) {
    GpuProfiler* gpu = rc_ ? rc_->getGpuProfiler() : nullptr;
    if (gpu && !gpuHooked_) {
        gpu->setFrameResolvedCallback([this](uint64_t serial, const std::vector<GpuTimingSample>& samples) {
            recorder_.onGpuFrame(serial, samples);
        });
        gpuHooked_ = true;
    }

    // I12/I14: the streaming focus follows the playing camera path. The focus moves at most
    // StreamingFocus::kMaxStepPerFrame per frame (the same no-teleport bound WorldForge uses). When
    // the path is no longer playing, the hold is released (only ours: another holder is untouched).
    const bool pathPlaying = cameras && cameras->getPath().isPlaying();
    if (streamFollow_ && chunks) {
        if (pathPlaying && camera) {
            chunks->setStreamingFocusOverride(
                StreamingFocus::stepToward(chunks->streamingAnchor(), camera->getPosition()), kFocusHolder);
        } else {
            chunks->clearStreamingFocusOverride(kFocusHolder);
            streamFollow_ = false;
        }
    }

    // I15: one row per frame while recording. Reads only what is already computed this frame.
    if (recorder_.recording()) {
        RouteRecorder::FrameInput in;
        in.frame = profiler_ ? profiler_->frameRecorderSerial() : 0;
        in.gpuSerial = gpu ? gpu->getFrameSerial() : 0;
        const auto& phases = framePacing_.lastFrameSamples();
        in.phases = &phases;
        // The render path's CPU scopes (drawFrame > ...), recorded by the render coordinator's own
        // recorder this frame. Keys start "drawFrame", never "Frame", so they cannot collide.
        if (rc_) in.renderPhases = &rc_->getCpuTiming().lastFrameSamples();
        for (const auto& s : phases)
            if (s.key == "Frame Interval") in.frameMs = static_cast<float>(s.ms);
        if (camera) {
            in.cameraPos = camera->getPosition();
            in.cameraYaw = camera->getYaw();
            in.cameraPitch = camera->getPitch();
        }
        in.pathProgress = pathPlaying ? cameras->getPath().progress() : -1.0f;
        if (chunks) {
            in.residentChunks = static_cast<uint32_t>(chunks->chunks.size());
            in.pendingGeneration = static_cast<uint32_t>(chunks->streamingManagerRO().pendingGenerationCount());
            in.pendingRemesh = static_cast<uint32_t>(chunks->dirtyTracker().getDirtyCount());
            if (camera) {
                const glm::vec3 p = camera->getPosition();
                const glm::ivec3 cc(static_cast<int>(std::floor(p.x / 32.0f)), static_cast<int>(std::floor(p.y / 32.0f)),
                                    static_cast<int>(std::floor(p.z / 32.0f)));
                in.cameraChunkResident = chunks->getChunkAtCoord(cc) ? 1 : 0;
            }
        }
        recorder_.addFrame(in);
    }
}

}  // namespace Core
}  // namespace Phyxel
