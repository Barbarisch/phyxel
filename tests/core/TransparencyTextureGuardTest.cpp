#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "stb_image.h"

// R7 guard — docs/GlassTransparency.md §13.5, §13.14.
//
// WHY THIS FILE EXISTS. Commit 2ea8b8d9 ("high-def regen for 64px materials") regenerated 72
// textures and silently wrote 36 of them as RGB: all six Glass faces and all thirty leaf faces lost
// their alpha channel. No code changed, no test failed, and glass stayed opaque for three months
// until a bisect found it (§12.10). The leaves were repaired by hand later; glass never was.
//
// The defect class is "a texture whose alpha is LOAD-BEARING is stored without usable alpha".
// This test pins it at the data, where the regeneration happens, so the next regen fails here.
//
// WHICH MATERIALS. materials.json has no cutout flag — leaves are alpha 1.0 and look like any
// opaque material — so "textures that must carry alpha" cannot be derived from the data. The list
// below is therefore explicit (§13.14): every material whose texture alpha changes what is drawn.
// Adding a material that relies on texture alpha means adding it here.
//
// WHAT "USABLE" MEANS. An alpha channel is necessary but NOT sufficient: an RGBA file whose alpha is
// 255 everywhere is still the 2ea8b8d9 failure wearing a new hat. So each face must also carry
// COVERAGE — some fraction of texels meaningfully below opaque.

namespace Phyxel {
namespace Testing {

namespace {

// Materials whose texture alpha is load-bearing. Glass: coverage for the OIT blend. Leaves: cutout.
const std::vector<std::string> kAlphaBearingMaterials = {
    "Glass",
    "Leaf", "LeafBirch", "LeafSpruce", "LeafJungle", "LeafAutumn",
};

// A texel is "meaningfully below opaque" under this value (out of 255).
constexpr int kCoverageAlpha = 250;
// At least this fraction of a face's texels must be below kCoverageAlpha. Deliberately low: it is
// a guard against "no alpha at all", not an art-direction threshold. The stripped textures read
// 0% here; the repaired leaves read 40-65%.
constexpr double kMinCoverageFraction = 0.01;

std::filesystem::path findResources() {
    for (const char* p : {"resources", "../resources", "../../resources", "../../../resources"}) {
        if (std::filesystem::exists(std::filesystem::path(p) / "materials.json")) return p;
    }
    return "resources";
}

struct FaceCheck {
    std::string material;
    std::string file;
    bool loaded = false;
    int channels = 0;
    double coverage = 0.0;
};

FaceCheck checkFace(const std::filesystem::path& texDir, const std::string& material,
                    const std::string& file) {
    FaceCheck fc{material, file};
    const std::string path = (texDir / file).string();
    int w = 0, h = 0, n = 0;
    // Request 4 channels so the pixel read is uniform, but record the file's REAL channel count:
    // an RGB file loaded this way gets alpha 255 synthesised by stb and would otherwise look fine.
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return fc;
    fc.loaded = true;
    fc.channels = n;
    size_t below = 0;
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < count; ++i) {
        if (px[i * 4 + 3] < kCoverageAlpha) ++below;
    }
    fc.coverage = count ? static_cast<double>(below) / static_cast<double>(count) : 0.0;
    stbi_image_free(px);
    return fc;
}

std::vector<FaceCheck> checkAll() {
    const auto res = findResources();
    std::ifstream in(res / "materials.json");
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    const auto& mats = (doc.is_object() && doc.contains("materials")) ? doc["materials"] : doc;
    std::vector<FaceCheck> out;
    for (const auto& name : kAlphaBearingMaterials) {
        for (const auto& m : mats) {
            if (m.value("name", "") != name || !m.contains("textures")) continue;
            std::set<std::string> seen;   // several faces often share one file
            for (auto it = m["textures"].begin(); it != m["textures"].end(); ++it) {
                const std::string f = it.value().get<std::string>();
                if (seen.insert(f).second)
                    out.push_back(checkFace(res / "textures" / "source", name, f));
            }
        }
    }
    return out;
}

} // namespace

// Control: the list resolves to real files. Without this, a renamed material or a moved texture
// directory would make every assertion below vacuously pass on an empty list.
TEST(TransparencyTextureGuardTest, ControlEveryListedMaterialResolvesToTextures) {
    const auto faces = checkAll();
    std::set<std::string> materialsSeen;
    for (const auto& f : faces) {
        EXPECT_TRUE(f.loaded) << f.material << ": texture " << f.file << " could not be read";
        materialsSeen.insert(f.material);
    }
    for (const auto& name : kAlphaBearingMaterials)
        EXPECT_TRUE(materialsSeen.count(name))
            << name << " is on the alpha-bearing list but has no textures in materials.json";
}

TEST(TransparencyTextureGuardTest, AlphaBearingTexturesHaveAnAlphaChannel) {
    for (const auto& f : checkAll()) {
        if (!f.loaded) continue;   // reported by the control
        EXPECT_EQ(f.channels, 4)
            << f.material << ": " << f.file << " is stored with " << f.channels
            << " channels. Its alpha is load-bearing (docs/GlassTransparency.md §12.10) -- an RGB "
               "file is exactly what 2ea8b8d9 produced and what made glass opaque.";
    }
}

TEST(TransparencyTextureGuardTest, AlphaBearingTexturesCarryCoverage) {
    for (const auto& f : checkAll()) {
        if (!f.loaded) continue;
        EXPECT_GE(f.coverage, kMinCoverageFraction)
            << f.material << ": " << f.file << " has alpha, but only " << f.coverage * 100.0
            << "% of texels are below " << kCoverageAlpha << "/255. An alpha channel that is opaque "
               "everywhere changes nothing on screen and is the same failure as no alpha at all.";
    }
}

} // namespace Testing
} // namespace Phyxel
