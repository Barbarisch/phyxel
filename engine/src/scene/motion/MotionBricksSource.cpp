#include "scene/motion/MotionBricksSource.h"
#include "scene/motion/HumanoidRetargeter.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Phyxel::Scene::Motion {
namespace {

using Status = std::uint32_t;
constexpr Status kOk = 0;
constexpr std::uint32_t kExpectedAbi = 1;
constexpr double kFramesPerSecond = 30.0;
constexpr double kLowWaterSeconds = 0.25;

struct RuntimeOptions;
struct Model;
struct Style;
struct Command;
struct Agent;
struct Motion;

struct Api {
    std::uint32_t (*abiVersion)() = nullptr;
    const char* (*statusString)(Status) = nullptr;
    Status (*optionsCreate)(RuntimeOptions**, char*, std::uint64_t) = nullptr;
    void (*optionsFree)(RuntimeOptions*) = nullptr;
    Status (*optionsSetDevice)(RuntimeOptions*, std::uint32_t, char*, std::uint64_t) = nullptr;
    Status (*optionsSetThreads)(RuntimeOptions*, std::uint32_t, char*, std::uint64_t) = nullptr;
    Status (*modelLoad)(const char*, const RuntimeOptions*, Model**, char*, std::uint64_t) = nullptr;
    void (*modelFree)(Model*) = nullptr;
    Status (*modelJointCount)(const Model*, std::uint32_t*, char*, std::uint64_t) = nullptr;
    Status (*modelJointName)(const Model*, std::uint32_t, const char**, char*, std::uint64_t) = nullptr;
    Status (*styleLoad)(const Model*, const char*, Style**, char*, std::uint64_t) = nullptr;
    void (*styleFree)(Style*) = nullptr;
    Status (*commandCreate)(Command**, char*, std::uint64_t) = nullptr;
    void (*commandFree)(Command*) = nullptr;
    Status (*commandSetStyle)(Command*, const Style*, char*, std::uint64_t) = nullptr;
    Status (*commandSetMovement)(Command*, float, float, float, char*, std::uint64_t) = nullptr;
    Status (*commandSetFacing)(Command*, float, float, float, char*, std::uint64_t) = nullptr;
    Status (*commandSetSpeed)(Command*, float, char*, std::uint64_t) = nullptr;
    Status (*commandSetTarget)(Command*, float, float, float, float, std::uint32_t,
                               char*, std::uint64_t) = nullptr;
    Status (*commandSetSeed)(Command*, std::uint64_t, char*, std::uint64_t) = nullptr;
    Status (*agentCreate)(const Model*, Agent**, char*, std::uint64_t) = nullptr;
    void (*agentFree)(Agent*) = nullptr;
    Status (*agentReset)(Agent*, const Style*, char*, std::uint64_t) = nullptr;
    Status (*agentPlan)(Agent*, const Command*, Motion**, char*, std::uint64_t) = nullptr;
    Status (*agentAdvance)(Agent*, std::uint32_t, char*, std::uint64_t) = nullptr;
    void (*motionFree)(Motion*) = nullptr;
    Status (*motionFrameCount)(const Motion*, std::uint64_t*, char*, std::uint64_t) = nullptr;
    Status (*motionJointCount)(const Motion*, std::uint64_t*, char*, std::uint64_t) = nullptr;
    Status (*motionRoots)(const Motion*, const float**, std::uint64_t*, char*, std::uint64_t) = nullptr;
    Status (*motionRotations)(const Motion*, const float**, std::uint64_t*, char*, std::uint64_t) = nullptr;
};

template <typename T>
bool loadSymbol(void* library, const char* name, T& output) {
#ifdef _WIN32
    output = reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    output = reinterpret_cast<T>(dlsym(library, name));
#endif
    return output != nullptr;
}

void* loadLibraryOnce(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) return nullptr;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
                                          nullptr, 0);
    if (count <= 0) return nullptr;
    std::wstring widePath(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
                            widePath.data(), count) <= 0) return nullptr;
    return static_cast<void*>(LoadLibraryW(widePath.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

// GGML owns process-global/TLS state and the pinned Windows build aborts when
// the module is repeatedly unloaded and loaded. Keep one module handle per path
// for the process lifetime; all model/style/agent allocations are still freed.
// This is bounded (normally one entry) and also prevents LoadLibrary ref leaks.
void* openLibrary(const std::string& path) {
    static std::mutex registryMutex;
    static std::unordered_map<std::string, void*> registry;
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto existing = registry.find(path);
    if (existing != registry.end()) return existing->second;
    void* library = loadLibraryOnce(path);
    if (library) registry.emplace(path, library);
    return library;
}

void closeLibrary(void*) {}

std::string nativeLoadError(const std::string& path) {
#ifdef _WIN32
    return "failed to load MotionBricks library '" + path + "' (Windows error " +
           std::to_string(GetLastError()) + ")";
#else
    const char* message = dlerror();
    return "failed to load MotionBricks library '" + path + "': " +
           (message ? message : "unknown error");
#endif
}

bool loadApi(void* library, Api& api, std::string& error) {
#define MB_LOAD(field, symbol) \
    if (!loadSymbol(library, symbol, api.field)) { error = "missing MotionBricks ABI symbol: " symbol; return false; }
    MB_LOAD(abiVersion, "mb_abi_version")
    MB_LOAD(statusString, "mb_status_string")
    MB_LOAD(optionsCreate, "mb_runtime_options_create")
    MB_LOAD(optionsFree, "mb_runtime_options_free")
    MB_LOAD(optionsSetDevice, "mb_runtime_options_set_device")
    MB_LOAD(optionsSetThreads, "mb_runtime_options_set_threads")
    MB_LOAD(modelLoad, "mb_model_load")
    MB_LOAD(modelFree, "mb_model_free")
    MB_LOAD(modelJointCount, "mb_model_get_joint_count")
    MB_LOAD(modelJointName, "mb_model_get_joint_name")
    MB_LOAD(styleLoad, "mb_style_load")
    MB_LOAD(styleFree, "mb_style_free")
    MB_LOAD(commandCreate, "mb_command_create")
    MB_LOAD(commandFree, "mb_command_free")
    MB_LOAD(commandSetStyle, "mb_command_set_style")
    MB_LOAD(commandSetMovement, "mb_command_set_movement_direction")
    MB_LOAD(commandSetFacing, "mb_command_set_facing_direction")
    MB_LOAD(commandSetSpeed, "mb_command_set_target_speed")
    MB_LOAD(commandSetTarget, "mb_command_set_world_target")
    MB_LOAD(commandSetSeed, "mb_command_set_seed")
    MB_LOAD(agentCreate, "mb_agent_create")
    MB_LOAD(agentFree, "mb_agent_free")
    MB_LOAD(agentReset, "mb_agent_reset")
    MB_LOAD(agentPlan, "mb_agent_plan")
    MB_LOAD(agentAdvance, "mb_agent_advance")
    MB_LOAD(motionFree, "mb_motion_free")
    MB_LOAD(motionFrameCount, "mb_motion_get_frame_count")
    MB_LOAD(motionJointCount, "mb_motion_get_joint_count")
    MB_LOAD(motionRoots, "mb_motion_get_root_translations")
    MB_LOAD(motionRotations, "mb_motion_get_local_rotations_xyzw")
#undef MB_LOAD
    return true;
}

std::string callError(const Api& api, Status status, const char* detail) {
    std::string result = api.statusString ? api.statusString(status) : "MotionBricks error";
    if (detail && detail[0]) result += ": " + std::string(detail);
    return result;
}

} // namespace

struct MotionBricksRuntime::Impl {
    MotionBricksConfig config;
    void* library = nullptr;
    Api api;
    RuntimeOptions* options = nullptr;
    Model* model = nullptr;
    Style* style = nullptr;
    std::vector<std::string> jointNames;
    // GGML planning saturates the CPU by itself. Concurrent plans on one model
    // caused severe oversubscription (>3 minutes for 10 agents), so callers
    // queue here while game threads continue sampling/falling back.
    std::mutex inferenceMutex;

    ~Impl() {
        if (style && api.styleFree) api.styleFree(style);
        if (model && api.modelFree) api.modelFree(model);
        if (options && api.optionsFree) api.optionsFree(options);
        closeLibrary(library);
    }
};

MotionBricksRuntime::MotionBricksRuntime(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl)) {}

bool MotionBricksRuntime::probeLibrary(const std::string& libraryPath, std::string& error) {
    error.clear();
    void* library = openLibrary(libraryPath);
    if (!library) { error = nativeLoadError(libraryPath); return false; }
    Api api;
    const bool complete = loadApi(library, api, error);
    if (complete && api.abiVersion() != kExpectedAbi) {
        error = "MotionBricks ABI mismatch: expected 1, got " +
                std::to_string(api.abiVersion());
        closeLibrary(library);
        return false;
    }
    closeLibrary(library);
    return complete;
}

MotionBricksRuntime::~MotionBricksRuntime() = default;

std::shared_ptr<MotionBricksRuntime> MotionBricksRuntime::create(
    const MotionBricksConfig& config, std::string& error) {
    error.clear();
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->library = openLibrary(config.libraryPath);
    if (!impl->library) { error = nativeLoadError(config.libraryPath); return {}; }
    if (!loadApi(impl->library, impl->api, error)) {
        return {};
    }
    if (impl->api.abiVersion() != kExpectedAbi) {
        error = "MotionBricks ABI mismatch: expected 1, got " +
                std::to_string(impl->api.abiVersion());
        return {};
    }

    char detail[512]{};
    Status status = impl->api.optionsCreate(&impl->options, detail, sizeof(detail));
    if (status != kOk) { error = callError(impl->api, status, detail); return {}; }
    status = impl->api.optionsSetDevice(impl->options,
        static_cast<std::uint32_t>(config.device), detail, sizeof(detail));
    if (status != kOk) { error = callError(impl->api, status, detail); return {}; }
    if (config.threads > 0) {
        status = impl->api.optionsSetThreads(impl->options, config.threads, detail, sizeof(detail));
        if (status != kOk) { error = callError(impl->api, status, detail); return {}; }
    }
    status = impl->api.modelLoad(config.modelDirectory.c_str(), impl->options,
                                 &impl->model, detail, sizeof(detail));
    if (status != kOk) { error = callError(impl->api, status, detail); return {}; }

    std::uint32_t jointCount = 0;
    status = impl->api.modelJointCount(impl->model, &jointCount, detail, sizeof(detail));
    if (status != kOk || jointCount != 34) {
        error = status == kOk ? "MotionBricks model must contain 34 joints"
                              : callError(impl->api, status, detail);
        return {};
    }
    for (std::uint32_t i = 0; i < jointCount; ++i) {
        const char* name = nullptr;
        status = impl->api.modelJointName(impl->model, i, &name, detail, sizeof(detail));
        if (status != kOk || !name || !name[0]) {
            error = status == kOk ? "MotionBricks model contains an unnamed joint"
                                  : callError(impl->api, status, detail);
            return {};
        }
        impl->jointNames.emplace_back(name);
    }
    status = impl->api.styleLoad(impl->model, config.stylePath.c_str(),
                                 &impl->style, detail, sizeof(detail));
    if (status != kOk) { error = callError(impl->api, status, detail); return {}; }
    return std::shared_ptr<MotionBricksRuntime>(new MotionBricksRuntime(std::move(impl)));
}

const std::vector<std::string>& MotionBricksRuntime::jointNames() const { return m_impl->jointNames; }
std::uint32_t MotionBricksRuntime::abiVersion() const { return m_impl->api.abiVersion(); }
const MotionBricksConfig& MotionBricksRuntime::config() const { return m_impl->config; }

struct MotionBricksSource::Impl {
    struct MotionWindow {
        std::vector<glm::quat> localRotations;
        std::vector<glm::vec3> rootTranslations;
        std::size_t frameCount = 0;
    };

    Agent* agent = nullptr;
    Command* command = nullptr;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopping = false;
    bool planRequested = true;
    bool resetRequested = false;
    bool haveIntent = false;
    MotionIntent intent;
    MotionWindow window;
    double windowStartSeconds = 0.0;
    double lastSampleSeconds = 0.0;
    std::uint32_t consumedFrames = 0;
    MotionSourceStatus status;
    std::deque<double> planLatencies;
    std::chrono::steady_clock::time_point requestedAt{};
    bool underrunning = false;
};

MotionBricksSource::MotionBricksSource(std::shared_ptr<MotionBricksRuntime> runtime,
                                       std::unique_ptr<Impl> impl)
    : m_runtime(std::move(runtime)), m_impl(std::move(impl)) {
    m_impl->worker = std::thread([this] {
        Api& api = m_runtime->m_impl->api;
        for (;;) {
            MotionIntent intent;
            std::uint32_t advanceFrames = 0;
            bool haveIntent = false;
            bool resetRequested = false;
            {
                std::unique_lock<std::mutex> lock(m_impl->mutex);
                m_impl->wake.wait(lock, [this] {
                    return m_impl->stopping || m_impl->planRequested || m_impl->resetRequested;
                });
                if (m_impl->stopping) return;
                resetRequested = m_impl->resetRequested;
                m_impl->resetRequested = false;
                haveIntent = m_impl->haveIntent;
                if (haveIntent) intent = m_impl->intent;
                advanceFrames = m_impl->consumedFrames;
                m_impl->consumedFrames = 0;
                m_impl->planRequested = false;
                m_impl->status.state = MotionSourceState::Degraded;
                if (!haveIntent && !resetRequested) continue;
            }

            char detail[512]{};
            auto failed = [&](Status value) {
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                m_impl->status.state = MotionSourceState::Error;
                m_impl->status.effectiveProvider = "clips";
                m_impl->status.error = callError(api, value, detail);
            };
            std::unique_lock<std::mutex> inferenceLock(m_runtime->m_impl->inferenceMutex);
            {
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                if (m_impl->requestedAt.time_since_epoch().count() != 0) {
                    m_impl->status.queueAgeSeconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - m_impl->requestedAt).count();
                }
            }
            if (resetRequested) {
                const Status s = api.agentReset(m_impl->agent, m_runtime->m_impl->style,
                                                detail, sizeof(detail));
                if (s != kOk) { failed(s); continue; }
                advanceFrames = 0;
            }
            if (!haveIntent) continue;
            if (advanceFrames > 0) {
                const Status s = api.agentAdvance(m_impl->agent, advanceFrames, detail, sizeof(detail));
                if (s != kOk) { failed(s); continue; }
            }
            bool commandOk = true;
#define MB_SET(call) do { if (commandOk) { const Status commandStatus = (call); \
    if (commandStatus != kOk) { failed(commandStatus); commandOk = false; } } } while(false)
            MB_SET(api.commandSetMovement(m_impl->command, intent.movementDirection.x,
                intent.movementDirection.y, intent.movementDirection.z, detail, sizeof(detail)));
            MB_SET(api.commandSetFacing(m_impl->command, intent.facingDirection.x,
                intent.facingDirection.y, intent.facingDirection.z, detail, sizeof(detail)));
            MB_SET(api.commandSetSpeed(m_impl->command, intent.targetSpeed, detail, sizeof(detail)));
            MB_SET(api.commandSetTarget(m_impl->command, intent.worldTarget.x, intent.worldTarget.y,
                intent.worldTarget.z, intent.targetHeadingRadians, intent.hasWorldTarget ? 1u : 0u,
                detail, sizeof(detail)));
            MB_SET(api.commandSetSeed(m_impl->command, intent.seed, detail, sizeof(detail)));
#undef MB_SET
            if (!commandOk) continue;

            const auto start = std::chrono::steady_clock::now();
            Motion* motion = nullptr;
            const Status planStatus = api.agentPlan(m_impl->agent, m_impl->command,
                                                     &motion, detail, sizeof(detail));
            const double latency = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            if (planStatus != kOk || !motion) { failed(planStatus); continue; }

            std::uint64_t frames = 0, joints = 0, rootValues = 0, rotationValues = 0;
            const float* roots = nullptr;
            const float* rotations = nullptr;
            Status s = api.motionFrameCount(motion, &frames, detail, sizeof(detail));
            if (s == kOk) s = api.motionJointCount(motion, &joints, detail, sizeof(detail));
            if (s == kOk) s = api.motionRoots(motion, &roots, &rootValues, detail, sizeof(detail));
            if (s == kOk) s = api.motionRotations(motion, &rotations, &rotationValues,
                                                  detail, sizeof(detail));
            if (s != kOk || frames == 0 || joints != 34 ||
                rootValues != frames * 3 || rotationValues != frames * joints * 4 ||
                !roots || !rotations) {
                api.motionFree(motion);
                if (s == kOk) std::strncpy(detail, "invalid motion buffer dimensions", sizeof(detail)-1);
                failed(s == kOk ? 4u : s);
                continue;
            }

            Impl::MotionWindow copied;
            copied.frameCount = static_cast<std::size_t>(frames);
            copied.localRotations.reserve(static_cast<std::size_t>(frames * joints));
            copied.rootTranslations.reserve(static_cast<std::size_t>(frames));
            for (std::uint64_t f = 0; f < frames; ++f) {
                copied.rootTranslations.emplace_back(roots[f * 3], roots[f * 3 + 1],
                                                     roots[f * 3 + 2]);
                for (std::uint64_t j = 0; j < joints; ++j) {
                    const std::size_t q = static_cast<std::size_t>((f * joints + j) * 4);
                    glm::quat rotation(rotations[q + 3], rotations[q], rotations[q + 1], rotations[q + 2]);
                    const glm::quat previous = (f == 0)
                        ? rotation : copied.localRotations[copied.localRotations.size() - joints];
                    if (!normalizeAndMatchHemisphere(rotation, previous)) rotation = glm::quat(1,0,0,0);
                    copied.localRotations.push_back(rotation);
                }
            }
            api.motionFree(motion);

            {
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                m_impl->window = std::move(copied);
                m_impl->windowStartSeconds = m_impl->lastSampleSeconds;
                m_impl->status.state = MotionSourceState::Ready;
                m_impl->status.effectiveProvider = "motionbricks";
                m_impl->status.lastPlanLatencySeconds = latency;
                m_impl->planLatencies.push_back(latency);
                if (m_impl->planLatencies.size() > 128) m_impl->planLatencies.pop_front();
                std::vector<double> ordered(m_impl->planLatencies.begin(), m_impl->planLatencies.end());
                std::sort(ordered.begin(), ordered.end());
                auto percentile = [&](double p) {
                    return ordered[std::min(ordered.size() - 1,
                        static_cast<std::size_t>(p * static_cast<double>(ordered.size() - 1)))];
                };
                m_impl->status.planLatencyP50Seconds = percentile(0.50);
                m_impl->status.planLatencyP95Seconds = percentile(0.95);
                m_impl->status.planLatencyP99Seconds = percentile(0.99);
                ++m_impl->status.completedPlans;
                m_impl->status.bufferedSeconds = static_cast<double>(frames) / kFramesPerSecond;
                m_impl->status.error.clear();
                m_impl->underrunning = false;
            }
        }
    });
}

std::shared_ptr<MotionBricksSource> MotionBricksRuntime::createSource(std::string& error) {
    error.clear();
    auto impl = std::make_unique<MotionBricksSource::Impl>();
    char detail[512]{};
    Status s = m_impl->api.agentCreate(m_impl->model, &impl->agent, detail, sizeof(detail));
    if (s == kOk) s = m_impl->api.agentReset(impl->agent, m_impl->style, detail, sizeof(detail));
    if (s == kOk) s = m_impl->api.commandCreate(&impl->command, detail, sizeof(detail));
    if (s == kOk) s = m_impl->api.commandSetStyle(impl->command, m_impl->style,
                                                  detail, sizeof(detail));
    if (s != kOk) {
        if (impl->command) m_impl->api.commandFree(impl->command);
        if (impl->agent) m_impl->api.agentFree(impl->agent);
        error = callError(m_impl->api, s, detail);
        return {};
    }
    impl->status.requestedProvider = "motionbricks";
    impl->status.effectiveProvider = "clips";
    impl->status.state = MotionSourceState::Degraded;
    impl->status.backend = m_impl->config.device == MotionBricksDevice::Vulkan ? "vulkan" : "cpu";
    return std::shared_ptr<MotionBricksSource>(
        new MotionBricksSource(shared_from_this(), std::move(impl)));
}

MotionBricksSource::~MotionBricksSource() {
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->wake.notify_one();
    if (m_impl->worker.joinable()) m_impl->worker.join();
    Api& api = m_runtime->m_impl->api;
    if (m_impl->command) api.commandFree(m_impl->command);
    if (m_impl->agent) api.agentFree(m_impl->agent);
}

void MotionBricksSource::submitIntent(const MotionIntent& intent) {
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (!m_impl->haveIntent || !(m_impl->intent == intent)) {
            m_impl->intent = intent;
            m_impl->haveIntent = true;
            m_impl->planRequested = true;
            m_impl->requestedAt = std::chrono::steady_clock::now();
        }
    }
    m_impl->wake.notify_one();
}

bool MotionBricksSource::sample(double timeSeconds, LocalPoseFrame& output) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->lastSampleSeconds = timeSeconds;
    const std::size_t joints = m_runtime->jointNames().size();
    const std::size_t frames = m_impl->window.frameCount;
    if (frames == 0) {
        if (!m_impl->underrunning && m_impl->haveIntent) {
            ++m_impl->status.underruns;
            m_impl->underrunning = true;
        }
        return false;
    }
    double elapsed = std::max(0.0, timeSeconds - m_impl->windowStartSeconds);
    std::size_t frame = std::min(static_cast<std::size_t>(elapsed * kFramesPerSecond), frames - 1);
    output = {};
    output.timeSeconds = timeSeconds;
    output.jointNames = m_runtime->jointNames();
    output.localRotations.assign(
        m_impl->window.localRotations.begin() + frame * joints,
        m_impl->window.localRotations.begin() + (frame + 1) * joints);
    output.rootTranslation = m_impl->window.rootTranslations[frame];
    output.provenance = MotionProvenance::MotionBricks;
    m_impl->consumedFrames = std::max(m_impl->consumedFrames,
                                      static_cast<std::uint32_t>(frame));
    const double remaining = static_cast<double>(frames - frame - 1) / kFramesPerSecond;
    m_impl->status.bufferedSeconds = remaining;
    if (remaining < kLowWaterSeconds && !m_impl->planRequested) {
        m_impl->planRequested = true;
        m_impl->requestedAt = std::chrono::steady_clock::now();
        m_impl->wake.notify_one();
    }
    return output.structurallyValid();
}

void MotionBricksSource::reset() {
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->window = {};
        m_impl->consumedFrames = 0;
        m_impl->resetRequested = true;
        m_impl->planRequested = m_impl->haveIntent;
        m_impl->requestedAt = std::chrono::steady_clock::now();
        m_impl->status.state = MotionSourceState::Degraded;
        m_impl->status.effectiveProvider = "clips";
    }
    m_impl->wake.notify_one();
}

MotionSourceStatus MotionBricksSource::status() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->status;
}

} // namespace Phyxel::Scene::Motion
