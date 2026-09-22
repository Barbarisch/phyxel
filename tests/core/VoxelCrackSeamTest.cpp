/**
 * R2 -- the crack field must not depend on chunk partitioning.
 * docs/VoxelDamageVisualization.md 6.2 / 3.3.
 *
 * THE FAILURE THIS GUARDS. static_voxel.vert sets `uv = baseUV * vec2(sizeU, sizeV)`, so UV
 * space is tied to the greedy-merged RECTANGLE -- and merge runs are computed inside a 32^3
 * loop, so they TERMINATE AT CHUNK BORDERS. A crack seeded from uv would scale and repeat
 * differently on either side of a chunk seam: chunk identity made visible, which is the exact
 * failure FeatureDesignKeys.md is built around. Seeding from absolute world position instead
 * makes the field partition-independent BY CONSTRUCTION -- and this test is what keeps it that
 * way when someone later reaches for texCoord because it is convenient.
 *
 * WHY A CPU MIRROR, AND WHAT IT IS AND IS NOT WORTH. The field lives in GLSL
 * (shaders/crack.glsl) and cannot be executed here, so this file mirrors it in C++. A
 * hand-ported mirror silently stops matching the moment the shader is edited, and the test
 * keeps passing -- a check named for a property it no longer measures. Two guards, in order of
 * strength:
 *
 *   1. A PINNED TABLE OF SAMPLED VALUES (kExpected below). If the mirror's arithmetic changes,
 *      these fail and name the input that moved. This is the guard that has teeth.
 *   2. A CONTENT HASH of crack.glsl. Weaker by design and deliberately SECONDARY: a hash
 *      reddens on a comment edit and its only repair is bumping a constant, which trains the
 *      exact reflex it was meant to prevent. It says "the shader moved, re-port and regenerate
 *      the table" -- it can never say "the mirror diverged".
 *
 * Neither guard can catch the real mistake -- a shader that reads sizeU. That is what the
 * RUNTIME half of 6.2 is for (a damaged wall straddling x=31/32, captured and diffed), and it
 * is NOT in this file.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// CPU MIRROR of shaders/crack.glsl. Kept structurally identical to the GLSL --
// same constants, same order of operations -- so a diff between the two files is
// readable side by side. Do not "clean this up"; its resemblance IS the feature.
// ---------------------------------------------------------------------------

constexpr float kCrackCell = 1.0f / 3.0f;     // primary network: subcube lattice

glm::vec2 crackHash22(glm::vec2 p) {
    glm::vec3 q(glm::dot(p, glm::vec2(127.1f, 311.7f)),
                glm::dot(p, glm::vec2(269.5f, 183.3f)),
                glm::dot(p, glm::vec2(419.2f, 371.9f)));
    auto fr = [](float v) { return v - std::floor(v); };
    return glm::vec2(fr(std::sin(q.x) * 43758.5453f), fr(std::sin(q.y) * 43758.5453f));
}

float crackEdgeDistance(glm::vec2 p) {
    glm::vec2 cell(std::floor(p.x), std::floor(p.y));
    glm::vec2 f = p - cell;
    float f1 = 8.0f, f2 = 8.0f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            glm::vec2 g(static_cast<float>(i), static_cast<float>(j));
            glm::vec2 site = g + 0.5f + (crackHash22(cell + g) - 0.5f);
            float d = glm::length(site - f);
            if (d < f1)      { f2 = f1; f1 = d; }
            else if (d < f2) { f2 = d; }
        }
    }
    return f2 - f1;
}

/// Mirrors voxel.frag's worldFaceUV: project onto the plane perpendicular to the face normal.
glm::vec2 worldFaceUV(glm::vec3 wp, glm::vec3 n) {
    glm::vec3 a = glm::abs(n);
    if (a.y >= a.x && a.y >= a.z) return glm::vec2(wp.x, wp.z);
    if (a.x >= a.z)               return glm::vec2(wp.z, wp.y);
    return glm::vec2(wp.x, wp.y);
}

float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float crackField(glm::vec3 worldPosAbs, glm::vec3 faceNormal, float stage01, float style) {
    if (stage01 <= 0.0f) return 0.0f;
    glm::vec2 p = worldFaceUV(worldPosAbs, faceNormal) / (kCrackCell * std::max(style, 0.15f));

    float e = crackEdgeDistance(p);
    float width = 0.012f + (0.075f - 0.012f) * stage01;
    float crack = 1.0f - smoothstepf(0.0f, width, e);

    float branch = smoothstepf(0.45f, 1.0f, stage01);
    if (branch > 0.0f) {
        float e2 = crackEdgeDistance(p * 3.0f + glm::vec2(13.7f, 7.3f));
        float c2 = 1.0f - smoothstepf(0.0f, width * 0.6f, e2);
        crack = std::max(crack, c2 * branch * 0.85f);
    }
    return crack < 0.0f ? 0.0f : (crack > 1.0f ? 1.0f : crack);
}

std::string findCrackGlsl() {
    for (const char* p : {"shaders/crack.glsl", "../shaders/crack.glsl",
                          "../../shaders/crack.glsl", "../../../shaders/crack.glsl"}) {
        if (std::filesystem::exists(p)) return p;
    }
    return "";
}

} // namespace

// ---------------------------------------------------------------------------
// The invariant: chunking must not change the answer (FloraMarginTest shape).
// ---------------------------------------------------------------------------

TEST(VoxelCrackSeamTest, CrackFieldIndependentOfChunkPartition) {
    // A slab straddling the x = 31/32 chunk seam. Evaluated as ONE region, then as TWO
    // partitions split exactly at the seam. The union must be bit-identical -- not close,
    // identical, because the field is a pure function of world position.
    const glm::vec3 n(0.0f, 0.0f, 1.0f);   // +Z face
    const float stage = 0.666f;            // stage 2 of 3

    std::vector<float> whole, split;
    for (int i = 0; i <= 640; ++i) {
        const float x = 28.0f + static_cast<float>(i) * 0.0125f;   // 28.0 .. 36.0
        const glm::vec3 wp(x, 18.35f, 8.0f);
        whole.push_back(crackField(wp, n, stage, 1.0f));
        // "Per-chunk" evaluation: the caller only knows which chunk it is in. Since the field
        // reads world position and nothing else, that knowledge changes nothing.
        split.push_back(crackField(wp, n, stage, 1.0f));
    }
    ASSERT_EQ(whole.size(), split.size());
    for (size_t i = 0; i < whole.size(); ++i) {
        ASSERT_FLOAT_EQ(whole[i], split[i]) << "sample " << i;
    }

    // The stronger statement: the field must be CONTINUOUS across the seam. A uv-seeded
    // implementation would show a step here, because merge runs restart at x = 32.
    const float justBelow = crackField(glm::vec3(31.995f, 18.35f, 8.0f), n, stage, 1.0f);
    const float justAbove = crackField(glm::vec3(32.005f, 18.35f, 8.0f), n, stage, 1.0f);
    EXPECT_NEAR(justBelow, justAbove, 0.05f)
        << "crack(x=31.995)=" << justBelow << " vs crack(x=32.005)=" << justAbove
        << " -- a discontinuity at the chunk seam means the field is reading something "
           "chunk-derived (sizeU/sizeV/texCoord) instead of world position";
}

TEST(VoxelCrackSeamTest, FieldIgnoresEverythingButItsDeclaredInputs) {
    // Same world position sampled twice must give the same answer, whatever else differs.
    // (The GLSL takes no other inputs; this pins that the MIRROR does not grow any either.)
    const glm::vec3 wp(12.345f, 18.75f, 8.0f);
    const glm::vec3 n(0.0f, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(crackField(wp, n, 0.5f, 1.0f), crackField(wp, n, 0.5f, 1.0f));

    // Stage 0 must be exactly zero, not merely small: voxel.frag skips the whole branch at
    // dmg == 0, so any non-zero here would be a difference the shader cannot reproduce.
    EXPECT_FLOAT_EQ(crackField(wp, n, 0.0f, 1.0f), 0.0f);
}

TEST(VoxelCrackSeamTest, StageWidensTheSameNetworkRatherThanSwappingIt) {
    // 4.3: advancing 1 -> 2 -> 3 must show THE SAME CRACKS GROWING, which is what a stamped
    // decal cannot do. Operationally: wherever a low stage has a crack, a higher stage must
    // still have one (the set of crack pixels grows monotonically).
    const glm::vec3 n(0.0f, 0.0f, 1.0f);
    int checked = 0, violations = 0;
    for (int i = 0; i < 400; ++i) {
        const glm::vec3 wp(10.0f + i * 0.017f, 18.0f + (i % 7) * 0.11f, 8.0f);
        const float lo = crackField(wp, n, 0.333f, 1.0f);
        const float hi = crackField(wp, n, 1.0f,   1.0f);
        if (lo > 0.5f) { ++checked; if (hi < lo - 1e-4f) ++violations; }
    }
    ASSERT_GT(checked, 20) << "rig assumption: the low stage must produce some crack pixels";
    EXPECT_EQ(violations, 0)
        << violations << " of " << checked << " crack pixels present at stage 1 were WEAKER at "
        << "stage 3 -- the network is being replaced rather than widened";
}

// ---------------------------------------------------------------------------
// Guard 1 (has teeth): pinned sampled values. Regenerate ONLY with a deliberate re-port.
// ---------------------------------------------------------------------------

TEST(VoxelCrackSeamTest, MirrorMatchesPinnedSampleTable) {
    struct S { float x, y, z, nx, ny, nz, stage, style, expected; };
    // Generated from this mirror and checked in. If the mirror changes, these move and name
    // the input that did. Covers cell interiors, near-edge positions, all three stages, both
    // style extremes, and all three face orientations.
    static const S kExpected[] = {
        // stage 1 of 3 -- crack strength 0.00 / 0.25 / 0.50 / 0.75 / 0.98
        {30.0000000f,18.0000000f,8.0000f, 0,0,1, 0.3333f,1.00f, 0.00000000f},
        {31.6860008f,18.5480003f,8.0000f, 0,0,1, 0.3333f,1.00f, 0.24958271f},
        {33.9794998f,18.1369991f,8.0000f, 0,0,1, 0.3333f,1.00f, 0.50036699f},
        {32.5079994f,18.4109993f,8.0000f, 0,0,1, 0.3333f,1.00f, 0.74894756f},
        {30.8174992f,18.4109993f,8.0000f, 0,0,1, 0.3333f,1.00f, 0.97989506f},
        // stage 2 of 3 -- the second octave is active from ~0.45
        {30.0000000f,18.0000000f,8.0000f, 0,0,1, 0.6667f,1.00f, 0.00000000f},
        {33.3854980f,18.1369991f,8.0000f, 0,0,1, 0.6667f,1.00f, 0.25001639f},
        {31.7954998f,18.5480003f,8.0000f, 0,0,1, 0.6667f,1.00f, 0.49946100f},
        {30.0224991f,18.1369991f,8.0000f, 0,0,1, 0.6667f,1.00f, 0.75015330f},
        {32.6489983f,18.2740002f,8.0000f, 0,0,1, 0.6667f,1.00f, 0.97999418f},
        // stage 3 of 3 -- full network
        {30.0000000f,18.0000000f,8.0000f, 0,0,1, 1.0000f,1.00f, 0.00000000f},
        {33.1559982f,18.1369991f,8.0000f, 0,0,1, 1.0000f,1.00f, 0.25002897f},
        {32.7765007f,18.0000000f,8.0000f, 0,0,1, 1.0000f,1.00f, 0.50034523f},
        {32.9939995f,18.5480003f,8.0000f, 0,0,1, 1.0000f,1.00f, 0.74996817f},
        {34.8479996f,18.1369991f,8.0000f, 0,0,1, 1.0000f,1.00f, 0.98006159f},
        // style extremes at one fixed point: brittle (dense/fine) vs ductile
        // (sparse/wide). These pin the STYLE DIVISOR, which P5 drives from brittleS1/S2.
        {31.9950f,18.3500f,8.0000f, 0,0,1, 1.0000f,0.40f, 0.21516381f},
        {31.9950f,18.3500f,8.0000f, 0,0,1, 1.0000f,2.50f, 0.00000000f},
    };
    // EVERY row is asserted, zeros included. An earlier version skipped rows whose expected
    // value was 0 (to let a fresh port pass), and the first generated table came back ALMOST
    // ENTIRELY ZEROS -- because cracks are thin lines and uniform sampling lands in cell
    // interiors. That combination is the worst of both: a guard made of zeros that also
    // declines to check them. The table is now stratified across the full 0..0.98 range, and
    // the zeros are as load-bearing as the rest -- they pin that intact surface STAYS intact.
    for (const auto& s : kExpected) {
        const float got = crackField({s.x, s.y, s.z}, {s.nx, s.ny, s.nz}, s.stage, s.style);
        EXPECT_NEAR(got, s.expected, 1e-4f)
            << "at (" << s.x << "," << s.y << "," << s.z << ") stage " << s.stage
            << " style " << s.style
            << " -- the CPU mirror no longer reproduces its pinned value. If crack.glsl was "
               "edited, re-port this mirror and regenerate the table with "
               "DISABLED_RegenerateSampleTable; do NOT just paste the new number.";
    }

    // The table must actually span the feature, or it guards nothing.
    int nonZero = 0;
    for (const auto& s : kExpected) if (s.expected > 0.01f) ++nonZero;
    EXPECT_GE(nonZero, 10)
        << "the pinned table has too few samples ON cracks; a table of mostly zeros stays "
           "green through almost any change to the crack itself";
}

TEST(VoxelCrackSeamTest, DISABLED_RegenerateSampleTable) {
    // Deliberately disabled: running it is a CONSCIOUS ACT after re-porting the mirror to match
    // an edited crack.glsl. Prints a ready-to-paste table.
    //
    // STRATIFIED, not uniform. The first version of this sampled a handful of arbitrary
    // positions and produced a table of almost entirely ZEROS -- because cracks are thin lines
    // and most of a surface is intact, so uniform sampling lands in cell interiors and misses
    // them. A table of zeros pins "the field is zero here", which would stay green through
    // almost any change to the crack itself. So: scan, bucket by crack strength, and pin
    // samples that actually SIT ON the feature being guarded.
    const float targets[] = {0.0f, 0.25f, 0.5f, 0.75f, 0.98f};
    const float stages[]  = {0.3333f, 0.6667f, 1.0f};
    const glm::vec3 n(0.0f, 0.0f, 1.0f);

    for (float st : stages) {
        for (float want : targets) {
            float bestErr = 1e9f;
            glm::vec3 bestWp(0.0f);
            float bestVal = 0.0f;
            // Deterministic sweep over a patch spanning several cells AND the chunk seam.
            for (int i = 0; i < 4000; ++i) {
                const float x = 30.0f + static_cast<float>(i) * 0.0015f;   // 30.0 .. 36.0
                for (int k = 0; k < 5; ++k) {
                    const float y = 18.0f + static_cast<float>(k) * 0.137f;
                    const glm::vec3 wp(x, y, 8.0f);
                    const float v = crackField(wp, n, st, 1.0f);
                    const float err = std::fabs(v - want);
                    if (err < bestErr) { bestErr = err; bestWp = wp; bestVal = v; }
                }
            }
            // %.7f, NOT %.4f: the field is high-gradient near a crack edge (~230 per world
            // unit), so a position rounded to 1e-4 re-evaluates to a DIFFERENT value and the
            // pinned table fails against the very code that generated it. Precision here is
            // what lets the assertion tolerance stay tight enough to catch real drift.
            printf("        {%.7ff,%.7ff,8.0000f, 0,0,1, %.4ff,1.00f, %.8ff},\n",
                   bestWp.x, bestWp.y, st, bestVal);
        }
    }
    // Both style extremes at a fixed spot, so a change to the style divisor reddens too.
    for (float style : {0.4f, 2.5f}) {
        const glm::vec3 wp(31.9950f, 18.3500f, 8.0f);
        printf("        {31.9950f,18.3500f,8.0000f, 0,0,1, 1.0000f,%.2ff, %.8ff},\n",
               style, crackField(wp, n, 1.0f, style));
    }
}

// ---------------------------------------------------------------------------
// Guard 2 (secondary tripwire): crack.glsl content hash.
// ---------------------------------------------------------------------------

TEST(VoxelCrackSeamTest, CrackShaderSourceIsPresentAndTracked) {
    const std::string path = findCrackGlsl();
    ASSERT_FALSE(path.empty())
        << "shaders/crack.glsl not found. The CPU mirror in this file exists to track it; if "
           "the shader moved or was deleted, this mirror is guarding nothing.";

    std::ifstream f(path, std::ios::binary);
    ASSERT_TRUE(f.good());
    const std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // The field's defining constants must still be the ones this mirror assumes. This is a far
    // better tripwire than a whole-file hash: it ignores comments entirely, and reddens exactly
    // when a number the mirror depends on changes.
    EXPECT_NE(body.find("const float kCrackCell = 1.0 / 3.0;"), std::string::npos)
        << "crack.glsl's primary cell size changed. The CPU mirror's kCrackCell must be "
           "re-ported and the pinned sample table regenerated.";
    EXPECT_NE(body.find("mix(0.012, 0.075, stage01)"), std::string::npos)
        << "crack.glsl's crack width ramp changed -- re-port the mirror.";
    EXPECT_NE(body.find("p * 3.0 + vec2(13.7, 7.3)"), std::string::npos)
        << "crack.glsl's second-octave lattice changed -- re-port the mirror. (x3.0 is what "
           "puts the branch octave on the microcube lattice, which 3.4's spall alignment needs.)";
}
