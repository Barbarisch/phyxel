#include "ai/TTSService.h"
#include "core/AudioSystem.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

namespace Phyxel {
namespace AI {

namespace {

constexpr const char* kTag = "TTS";

// 64-bit FNV-1a — deterministic across platforms/runs so the cache key matches
// the offline pre-generation tool (tools/pregenerate_tts.py).
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string toHex(uint64_t v) {
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = digits[v & 0xF]; v >>= 4; }
    return out;
}

// Strip the engine's LLM action tags ([EMOTE:x], [GIVE_ITEM:y], ...) and
// collapse whitespace so Piper only speaks the spoken words on one line.
std::string sanitize(const std::string& text) {
    std::string s;
    s.reserve(text.size());
    int depth = 0;
    for (char c : text) {
        if (c == '[') { ++depth; continue; }
        if (c == ']') { if (depth > 0) --depth; continue; }
        if (depth > 0) continue;
        s += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    // collapse runs of spaces + trim
    std::string out;
    out.reserve(s.size());
    bool prevSpace = true;
    for (char c : s) {
        bool sp = (c == ' ');
        if (sp && prevSpace) continue;
        out += c;
        prevSpace = sp;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool fileExistsNonEmpty(const std::string& p) {
    std::error_code ec;
    return fs::exists(p, ec) && fs::file_size(p, ec) > 44; // > WAV header
}

} // namespace

TTSService::TTSService() = default;

TTSService::~TTSService() { shutdown(); }

bool TTSService::initialize(Core::AudioSystem* audio, const TTSConfig& config,
                            const std::string& resourceRoot) {
    m_audio  = audio;
    m_config = config;

    if (!config.enabled) {
        LOG_INFO(kTag, "TTS disabled by config");
        return false;
    }

    // Candidate roots to probe for the bundled Piper assets.
    std::vector<fs::path> roots;
    if (!resourceRoot.empty()) roots.emplace_back(resourceRoot);
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    roots.push_back(cwd);
    roots.push_back(cwd / "..");
    roots.push_back(cwd / ".." / "..");

    // Resolve the piper executable.
    std::string piper = config.piperExe;
    if (piper.empty()) {
#ifdef _WIN32
        const char* names[] = {"external/piper/piper/piper.exe", "external/piper/piper.exe"};
#else
        const char* names[] = {"external/piper/piper/piper", "external/piper/piper"};
#endif
        for (const auto& root : roots) {
            for (const char* n : names) {
                fs::path c = root / n;
                if (fs::exists(c, ec)) { piper = c.lexically_normal().string(); break; }
            }
            if (!piper.empty()) break;
        }
    }

    // Resolve the voice model.
    std::string model = config.modelPath;
    if (model.empty()) {
        const char* rel = "external/piper/models/en_US-libritts_r-medium.onnx";
        for (const auto& root : roots) {
            fs::path c = root / rel;
            if (fs::exists(c, ec)) { model = c.lexically_normal().string(); break; }
        }
    }

    if (piper.empty() || model.empty() || !fs::exists(piper, ec) || !fs::exists(model, ec)) {
        LOG_WARN(kTag, "Piper assets not found (run tools/setup_piper.py) — NPC voices disabled");
        m_available = false;
        return false;
    }
    m_config.piperExe = piper;
    m_config.modelPath = model;
    m_modelName = fs::path(model).filename().string();

    // Read speaker count + default cache dir.
    if (m_config.numSpeakers <= 0) {
        m_config.numSpeakers = 1;
        try {
            std::ifstream jf(model + ".json");
            if (jf) {
                nlohmann::json j; jf >> j;
                if (j.contains("num_speakers")) m_config.numSpeakers = std::max(1, j["num_speakers"].get<int>());
            }
        } catch (const std::exception& e) {
            LOG_WARN(kTag, "Could not read model speaker count: {}", e.what());
        }
    }

    if (m_config.cacheDir.empty()) m_config.cacheDir = (cwd / "cache" / "tts").string();
    fs::create_directories(m_config.cacheDir, ec);

    m_available = true;
    m_running = true;
    m_worker = std::thread(&TTSService::workerLoop, this);

    LOG_INFO(kTag, "Piper TTS ready: model='{}' speakers={} cache='{}'",
             m_modelName, m_config.numSpeakers, m_config.cacheDir);
    return true;
}

void TTSService::shutdown() {
    if (m_running.exchange(false)) {
        m_cv.notify_all();
        if (m_worker.joinable()) m_worker.join();
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.clear();
}

int TTSService::speakerIdFor(const std::string& voiceKey) const {
    if (m_config.numSpeakers <= 1) return 0;
    return static_cast<int>(fnv1a(voiceKey) % static_cast<uint64_t>(m_config.numSpeakers));
}

float TTSService::lengthScaleFor(const std::string& voiceKey) const {
    uint64_t h = fnv1a(voiceKey + "#rate");
    float t = static_cast<float>(h % 1000) / 999.0f;          // 0..1, stable per voice
    return m_config.lengthScaleMin + t * (m_config.lengthScaleMax - m_config.lengthScaleMin);
}

std::string TTSService::cachePathFor(const std::string& text, int speakerId, float lengthScale) const {
    std::ostringstream key;
    key << text << '|' << speakerId << '|'
        << std::fixed << static_cast<int>(lengthScale * 1000) << '|' << m_modelName;
    return (fs::path(m_config.cacheDir) / (toHex(fnv1a(key.str())) + ".wav")).string();
}

void TTSService::enqueue(const std::string& voiceKey, const std::string& text,
                         const glm::vec3& position, bool spatial) {
    if (!m_available || !m_running) return;
    std::string clean = sanitize(text);
    if (clean.empty()) return;

    Job job;
    job.text        = std::move(clean);
    job.speakerId   = speakerIdFor(voiceKey);
    job.lengthScale = lengthScaleFor(voiceKey);
    job.position    = position;
    job.spatial     = spatial;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_queue.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void TTSService::speak(const std::string& voiceKey, const std::string& text, const glm::vec3& position) {
    enqueue(voiceKey, text, position, true);
}

void TTSService::speak2D(const std::string& voiceKey, const std::string& text) {
    enqueue(voiceKey, text, glm::vec3(0.0f), false);
}

bool TTSService::synthesize(const Job& job, const std::string& outWav) {
    // Feed text via a temp file so LLM-generated content never touches the
    // command line (no shell injection). The worker is single-threaded, so a
    // per-output temp name is collision-free.
    std::string inTxt = outWav + ".in.txt";
    {
        std::ofstream f(inTxt, std::ios::binary);
        if (!f) { LOG_ERROR(kTag, "Cannot write TTS temp input '{}'", inTxt); return false; }
        f << job.text << "\n";
    }

    std::ostringstream cmd;
    cmd << '"' << m_config.piperExe << '"'
        << " --model \"" << m_config.modelPath << '"'
        << " --speaker " << job.speakerId
        << " --length_scale " << job.lengthScale
        << " --output_file \"" << outWav << '"'
        << " < \"" << inTxt << '"';
#ifdef _WIN32
    cmd << " 2>nul";
    // cmd.exe strips the outermost quotes of the whole command line, so wrap it.
    std::string full = "\"" + cmd.str() + "\"";
#else
    cmd << " 2>/dev/null";
    std::string full = cmd.str();
#endif

    int rc = std::system(full.c_str());
    std::error_code ec;
    fs::remove(inTxt, ec);

    if (rc != 0 || !fileExistsNonEmpty(outWav)) {
        LOG_ERROR(kTag, "Piper synthesis failed (rc={}) for speaker {}", rc, job.speakerId);
        fs::remove(outWav, ec);
        return false;
    }
    return true;
}

void TTSService::workerLoop() {
    while (m_running) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this] { return !m_running || !m_queue.empty(); });
            if (!m_running) break;
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }

        std::string wav = cachePathFor(job.text, job.speakerId, job.lengthScale);
        bool ready = fileExistsNonEmpty(wav) || synthesize(job, wav);
        if (!ready || !m_audio) continue;

        if (job.spatial)
            m_audio->playSound3D(wav, job.position, Core::AudioChannel::Voice, m_config.volume);
        else
            m_audio->playSound(wav, Core::AudioChannel::Voice, m_config.volume);
    }
}

} // namespace AI
} // namespace Phyxel
