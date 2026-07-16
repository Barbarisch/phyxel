#include "core/MapCoarseSource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "utils/Logger.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Phyxel {

float MapCoarseData::heightAtPixelClamped(float wx, float wz) const {
    if (height.empty()) return seaLevelY;
    int px = static_cast<int>(std::lround(wx / blocksPerPixel));
    int pz = static_cast<int>(std::lround(wz / blocksPerPixel));
    px = std::clamp(px, 0, widthPx - 1);
    pz = std::clamp(pz, 0, heightPx - 1);
    return static_cast<float>(height[static_cast<size_t>(pz) * widthPx + px]);
}

float MapCoarseData::sampleHeightWorld(float wx, float wz) const {
    if (height.empty()) return seaLevelY;
    float px = wx / blocksPerPixel, pz = wz / blocksPerPixel;
    px = std::clamp(px, 0.0f, widthPx - 1.001f);
    pz = std::clamp(pz, 0.0f, heightPx - 1.001f);
    const int x0 = static_cast<int>(px), z0 = static_cast<int>(pz);
    const float fx = px - x0, fz = pz - z0;
    auto H = [&](int x, int z) {
        return static_cast<float>(height[static_cast<size_t>(z) * widthPx + x]);
    };
    const float a = H(x0, z0), b = H(x0 + 1, z0), c = H(x0, z0 + 1), d = H(x0 + 1, z0 + 1);
    return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz;
}

std::shared_ptr<MapCoarseData> MapCoarseData::load(const std::string& terrainDir, std::string& err) {
    if (!fs::is_directory(terrainDir)) {
        err = "terrain dir not found: " + terrainDir;
        return nullptr;
    }
    // Largest me_height_<N>.u16 (highest N == full res). Raw uint16 LE, square (N×N),
    // written by tools/middle_earth/import_terrain.py — the repo's stb_image is too old for
    // 16-bit PNG, so we read the raw sidecar directly.
    fs::path best;
    long bestN = -1;
    for (const auto& e : fs::directory_iterator(terrainDir)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("me_height_", 0) == 0 && e.path().extension() == ".u16") {
            try {
                long n = std::stol(name.substr(10, name.size() - 14));  // strip prefix + ".u16"
                if (n > bestN) { bestN = n; best = e.path(); }
            } catch (...) {}
        }
    }
    if (best.empty()) { err = "no me_height_*.u16 in " + terrainDir + " (run import_terrain.py)"; return nullptr; }
    const int dim = static_cast<int>(bestN);

    auto data = std::make_shared<MapCoarseData>();

    // Meta (worldSizeBlocks + seaLevelY). Optional — fall back to sane defaults if absent.
    const fs::path metaPath = fs::path(terrainDir) / "me_terrain_meta.json";
    float worldSize = 0.0f;
    if (fs::exists(metaPath)) {
        try {
            std::ifstream f(metaPath);
            json m; f >> m;
            worldSize = m.value("worldSizeBlocks", 0.0f);
            data->seaLevelY = m.value("seaLevelY", 16.0f);
        } catch (const std::exception& e2) {
            LOG_WARN("MapCoarseSource", std::string("meta parse failed, using defaults: ") + e2.what());
        }
    }

    // Raw uint16 LE height grid (value == world Y), dim×dim.
    const size_t count = static_cast<size_t>(dim) * dim;
    std::ifstream f(best, std::ios::binary);
    if (!f) { err = "cannot open " + best.string(); return nullptr; }
    data->height.resize(count);
    f.read(reinterpret_cast<char*>(data->height.data()),
           static_cast<std::streamsize>(count * sizeof(uint16_t)));
    if (static_cast<size_t>(f.gcount()) != count * sizeof(uint16_t)) {
        err = "short read on " + best.string() + " (expected " + std::to_string(dim) + "^2 u16)";
        return nullptr;
    }
    data->widthPx = dim; data->heightPx = dim;

    if (worldSize <= 0.0f) worldSize = static_cast<float>(dim) * 4.0f;  // assume 4 blocks/px
    data->worldSizeBlocks = worldSize;
    data->blocksPerPixel = worldSize / static_cast<float>(dim);

    uint16_t lo = 65535, hi = 0;
    for (uint16_t v : data->height) { lo = std::min(lo, v); hi = std::max(hi, v); }
    data->minY = lo; data->maxY = hi;

    LOG_INFO("MapCoarseSource", "Loaded heightmap " + best.filename().string() + " " +
             std::to_string(dim) + "x" + std::to_string(dim) + " (" +
             std::to_string(data->blocksPerPixel) + " blocks/px, world " +
             std::to_string(static_cast<int>(worldSize)) + ", Y " +
             std::to_string(static_cast<int>(data->minY)) + ".." +
             std::to_string(static_cast<int>(data->maxY)) + ")");
    return data;
}

CoarseWorldModel::SourceFunc makeMapCoarseSource(std::shared_ptr<const MapCoarseData> data) {
    return [data](float x, float z) {
        CoarseSample cs;
        cs.baseHeight = data->heightAtPixelClamped(x, z);
        const float span = std::max(1.0f, data->maxY - data->seaLevelY);
        float c = (cs.baseHeight - data->seaLevelY) / span;
        cs.continentalness = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
        return cs;
    };
}

} // namespace Phyxel
