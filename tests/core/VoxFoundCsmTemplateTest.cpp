// docs/VoxelFoundationPlan.md step 7 D3a -- today's C / S / M templates on the 1/8 m grid (loadCsmTemplate), through
// the SAME ninths -> eighths resampler as convertPxb2 (docs/evidence/voxfound_f7_prediction.txt D3a, T1 / T2).
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include "core/voxfound/StorageLab.h"

using namespace Phyxel::Core;

namespace {

std::string writeTemp(const std::string& name, const std::string& body) {
    const auto p = std::filesystem::temp_directory_path() / ("voxfound_csm_" + name + ".voxel");
    std::ofstream(p) << body;
    return p.string();
}

std::map<std::string, size_t> perMaterial(const VoxFound::GridTemplate& t) {
    std::map<std::string, size_t> n;
    for (const auto& b : t.boxes) n[b.material] += size_t(b.size.x) * size_t(b.size.y) * size_t(b.size.z);
    return n;
}

}  // namespace

// T1: cubes exact; one subcube = the resampler's count; two materials in one cube both kept; tints counted; fine refused
TEST(VoxFoundCsmTemplate, CubesExactSubcubesResampled) {
    {
        VoxFound::CsmTemplateStats st;
        const auto t = VoxFound::loadCsmTemplate(writeTemp("c", "# x\nC 0 0 0 Stone\nC 1 0 0 Wood tint=ff0000\n"), &st);
        ASSERT_TRUE(t.error.empty()) << t.error;
        EXPECT_EQ(t.cellsPerM, 8);
        EXPECT_EQ(t.bounds, glm::ivec3(16, 8, 8));
        const auto n = perMaterial(t);
        EXPECT_EQ(n.at("Stone"), 512u);
        EXPECT_EQ(n.at("Wood"), 512u);
        EXPECT_EQ(st.tintsDropped, 1u);
    }
    {   // the centre subcube: ninths 3..5 -> eighths 3..4 on each axis (voxel 3 = [27, 36)/72, 4 = [36, 45)/72)
        const auto t = VoxFound::loadCsmTemplate(writeTemp("s", "S 0 0 0 1 1 1 Stone\n"));
        ASSERT_TRUE(t.error.empty()) << t.error;
        EXPECT_EQ(perMaterial(t).at("Stone"), 8u);
        EXPECT_EQ(t.bounds, glm::ivec3(2, 2, 2));   // rebased
    }
    {   // two corner subcubes of different materials in one cube: both survive
        const auto t = VoxFound::loadCsmTemplate(writeTemp("two", "S 0 0 0 0 0 0 Stone\nS 0 0 0 2 2 2 Wood\n"));
        ASSERT_TRUE(t.error.empty()) << t.error;
        const auto n = perMaterial(t);
        EXPECT_GT(n.count("Stone") ? n.at("Stone") : 0u, 0u);
        EXPECT_GT(n.count("Wood") ? n.at("Wood") : 0u, 0u);
    }
    {   // a lone microcube: "no detail vanishes" rescues it into one voxel
        VoxFound::CsmTemplateStats st;
        const auto t = VoxFound::loadCsmTemplate(writeTemp("m", "M 0 0 0 1 1 1 1 1 1 Gold\n"), &st);
        ASSERT_TRUE(t.error.empty()) << t.error;
        EXPECT_EQ(perMaterial(t).at("Gold"), 1u);
        EXPECT_EQ(st.rescued, 1u);
    }
    {   // a cube and a grid at the same cell: the cube wins (as convertPxb2)
        const auto t = VoxFound::loadCsmTemplate(writeTemp("cw", "C 0 0 0 Stone\nS 0 0 0 0 0 0 Wood\n"));
        ASSERT_TRUE(t.error.empty()) << t.error;
        EXPECT_EQ(perMaterial(t).size(), 1u);
    }
    EXPECT_FALSE(VoxFound::loadCsmTemplate(writeTemp("fine", "# grid: 81\nV 0 0 0 Metal\n")).error.empty());
}

// T2: every legacy (non-grid) template in the library converts; per material with >= 1 m^3, voxels out / (volume * 512)
// within the PXB2 conversion's band (0.5 - 1.5)
TEST(VoxFoundCsmTemplate, WholeLegacyLibraryConverts) {
    namespace fs = std::filesystem;
    ASSERT_TRUE(fs::exists("resources/templates"));
    size_t files = 0, converted = 0, fine = 0;
    double worstLo = 1e9, worstHi = 0.0;
    std::string worstLoAt, worstHiAt;
    for (const auto& e : fs::recursive_directory_iterator("resources/templates")) {
        if (!e.is_regular_file() || e.path().extension() != ".voxel") continue;
        ++files;
        std::ifstream in(e.path());
        std::string head((std::istreambuf_iterator<char>(in)), {});
        if (head.find("# grid:") != std::string::npos) {
            ++fine;
            continue;
        }
        VoxFound::CsmTemplateStats st;
        const auto t = VoxFound::loadCsmTemplate(e.path().string(), &st);
        EXPECT_TRUE(t.error.empty()) << e.path().string() << ": " << t.error;
        if (!t.error.empty()) continue;
        ++converted;
        for (const auto& [m, vol] : st.volumeIn) {
            if (vol < 1.0) continue;
            const double r = double(st.voxelsOut[m]) / (vol * 512.0);
            EXPECT_GE(r, 0.5) << e.path().string() << " " << m;
            EXPECT_LE(r, 1.5) << e.path().string() << " " << m;
            if (r < worstLo) worstLo = r, worstLoAt = e.path().filename().string() + " " + m;
            if (r > worstHi) worstHi = r, worstHiAt = e.path().filename().string() + " " + m;
        }
    }
    std::printf("[csm] %zu templates: %zu converted, %zu fine-grid skipped; ratio band %.3f (%s) - %.3f (%s)\n", files,
                converted, fine, worstLo, worstLoAt.c_str(), worstHi, worstHiAt.c_str());
    EXPECT_EQ(converted + fine, files);
}
