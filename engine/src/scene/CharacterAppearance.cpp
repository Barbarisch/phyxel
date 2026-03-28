#include "scene/CharacterAppearance.h"
#include "graphics/Animation.h"
#include <algorithm>
#include <functional>
#include <unordered_map>

namespace Phyxel {
namespace Scene {

// ============================================================================
// Morphology Detection
// ============================================================================

MorphologyType detectMorphology(const Skeleton& skeleton) {
    std::vector<std::string> names;
    names.reserve(skeleton.bones.size());
    for (const auto& bone : skeleton.bones) {
        names.push_back(bone.name);
    }
    return detectMorphology(names);
}

MorphologyType detectMorphology(const std::vector<std::string>& boneNames) {
    // Check for distinctive bone name patterns
    bool hasPelvis = false, hasChest = false, hasTail = false;
    bool hasPaw = false, hasFang = false, hasWing = false;
    bool hasLeg1Coxa = false, hasAbdomen = false, hasThorax = false;
    bool hasNeck1 = false, hasMixamoHips = false;

    for (const auto& name : boneNames) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Humanoid (Mixamo) markers
        if (lower == "hips" || lower == "mixamorighips") hasMixamoHips = true;

        // Quadruped markers
        if (lower == "pelvis") hasPelvis = true;
        if (lower == "chest") hasChest = true;
        if (lower.find("tail") != std::string::npos) hasTail = true;
        if (lower.find("paw") != std::string::npos) hasPaw = true;

        // Arachnid markers
        if (lower.find("leg1_coxa") != std::string::npos) hasLeg1Coxa = true;
        if (lower == "abdomen") hasAbdomen = true;
        if (lower == "thorax") hasThorax = true;
        if (lower.find("fang") != std::string::npos) hasFang = true;

        // Dragon markers
        if (lower.find("wing") != std::string::npos) hasWing = true;
        if (lower == "neck_1") hasNeck1 = true;
    }

    // Arachnid: has leg1_coxa pattern + thorax/abdomen
    if (hasLeg1Coxa && (hasThorax || hasAbdomen)) {
        return MorphologyType::Arachnid;
    }

    // Dragon: has wings + tail + neck segments
    if (hasWing && hasTail && hasNeck1) {
        return MorphologyType::Dragon;
    }

    // Quadruped: has pelvis + paw/tail + chest (but not wings)
    if (hasPelvis && (hasPaw || hasTail) && hasChest && !hasWing) {
        return MorphologyType::Quadruped;
    }

    // Humanoid: Mixamo-style rig, or default fallback
    if (hasMixamoHips) {
        return MorphologyType::Humanoid;
    }

    return MorphologyType::Unknown;
}

// ============================================================================
// Bone → Color mapping (morphology-aware)
// ============================================================================

static glm::vec4 getColorHumanoid(const std::string& lower, const CharacterAppearance& a) {
    if (lower.find("head") != std::string::npos ||
        lower.find("hand") != std::string::npos)
        return a.skinColor;

    if (lower.find("arm") != std::string::npos ||
        lower.find("shoulder") != std::string::npos)
        return a.armColor;

    if (lower.find("leg") != std::string::npos ||
        lower.find("foot") != std::string::npos ||
        lower.find("thigh") != std::string::npos ||
        lower.find("shin") != std::string::npos)
        return a.legColor;

    if (lower.find("spine") != std::string::npos ||
        lower.find("torso") != std::string::npos ||
        lower.find("hip") != std::string::npos ||
        lower.find("chest") != std::string::npos ||
        lower.find("neck") != std::string::npos)
        return a.torsoColor;

    return a.defaultColor;
}

// Quadruped: skinColor=face, torsoColor=fur_primary, armColor=fur_secondary, legColor=paw
static glm::vec4 getColorQuadruped(const std::string& lower, const CharacterAppearance& a) {
    // Face/head region → skinColor (face)
    if (lower.find("head") != std::string::npos ||
        lower.find("jaw") != std::string::npos ||
        lower.find("mouth") != std::string::npos ||
        lower.find("eye") != std::string::npos ||
        lower.find("ear") != std::string::npos ||
        lower.find("eyebrow") != std::string::npos ||
        lower.find("eyelid") != std::string::npos ||
        lower.find("nose") != std::string::npos)
        return a.skinColor;

    // Paw/foot region → legColor (paw)
    if (lower.find("paw") != std::string::npos)
        return a.legColor;

    // Tail, belly, underbody → armColor (fur_secondary)
    if (lower.find("tail") != std::string::npos ||
        lower.find("belly") != std::string::npos)
        return a.armColor;

    // Main body (chest, neck, back, shoulder, legs) → torsoColor (fur_primary)
    if (lower.find("chest") != std::string::npos ||
        lower.find("neck") != std::string::npos ||
        lower.find("pelvis") != std::string::npos ||
        lower.find("shoulder") != std::string::npos ||
        lower.find("leg") != std::string::npos ||
        lower.find("ankle") != std::string::npos)
        return a.torsoColor;

    return a.defaultColor;
}

// Arachnid: skinColor=fang, torsoColor=carapace, armColor=abdomen, legColor=leg
static glm::vec4 getColorArachnid(const std::string& lower, const CharacterAppearance& a) {
    // Fang/mouth → skinColor (fang)
    if (lower.find("fang") != std::string::npos ||
        lower.find("pedipalp") != std::string::npos)
        return a.skinColor;

    // Abdomen → armColor (abdomen)
    if (lower.find("abdomen") != std::string::npos)
        return a.armColor;

    // Legs → legColor
    if (lower.find("leg") != std::string::npos ||
        lower.find("coxa") != std::string::npos ||
        lower.find("femur") != std::string::npos ||
        lower.find("tibia") != std::string::npos)
        return a.legColor;

    // Cephalothorax/thorax/root → torsoColor (carapace)
    if (lower.find("cephalothorax") != std::string::npos ||
        lower.find("thorax") != std::string::npos ||
        lower.find("root") != std::string::npos)
        return a.torsoColor;

    return a.defaultColor;
}

// Dragon: skinColor=belly, torsoColor=scales_primary, armColor=scales_secondary, legColor=wing
static glm::vec4 getColorDragon(const std::string& lower, const CharacterAppearance& a) {
    // Belly/underbody → skinColor (belly)
    if (lower.find("belly") != std::string::npos ||
        lower.find("abdomen") != std::string::npos ||
        lower.find("breast") != std::string::npos ||
        lower.find("lower_lip") != std::string::npos ||
        lower.find("tongue") != std::string::npos)
        return a.skinColor;

    // Wing membrane → legColor (wing)
    if (lower.find("wing") != std::string::npos)
        return a.legColor;

    // Head, jaw, eyes → armColor (scales_secondary)
    if (lower.find("head") != std::string::npos ||
        lower.find("jaw") != std::string::npos ||
        lower.find("snout") != std::string::npos ||
        lower.find("eye") != std::string::npos ||
        lower.find("nostril") != std::string::npos ||
        lower.find("cheek") != std::string::npos ||
        lower.find("horn") != std::string::npos ||
        lower.find("lip") != std::string::npos)
        return a.armColor;

    // Main body (neck, legs, tail, pelvis) → torsoColor (scales_primary)
    if (lower.find("neck") != std::string::npos ||
        lower.find("pelvis") != std::string::npos ||
        lower.find("leg") != std::string::npos ||
        lower.find("arm") != std::string::npos ||
        lower.find("tail") != std::string::npos ||
        lower.find("forearm") != std::string::npos)
        return a.torsoColor;

    return a.defaultColor;
}

glm::vec4 CharacterAppearance::getColorForBone(const std::string& boneName) const {
    std::string lower = boneName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    switch (morphology) {
        case MorphologyType::Quadruped: return getColorQuadruped(lower, *this);
        case MorphologyType::Arachnid:  return getColorArachnid(lower, *this);
        case MorphologyType::Dragon:    return getColorDragon(lower, *this);
        case MorphologyType::Humanoid:
        case MorphologyType::Unknown:
        default:                        return getColorHumanoid(lower, *this);
    }
}

// ============================================================================
// Factory methods
// ============================================================================

CharacterAppearance CharacterAppearance::defaultAppearance() {
    return CharacterAppearance{}; // Default member values match original hardcoded colors
}

// Helper type for hashFloat lambda
using HashFloatFn = std::function<float(size_t, int)>;

// ============================================================================
// Creature-specific seed generation
// ============================================================================

static CharacterAppearance generateQuadrupedFromSeed(CharacterAppearance app,
    const std::string& name, const std::string& role,
    size_t h, HashFloatFn hashFloat, std::hash<std::string> hasher) {

    // Fur color palettes: primary, secondary, face, paw
    struct FurPalette { glm::vec3 primary; glm::vec3 secondary; glm::vec3 face; glm::vec3 paw; };
    static const FurPalette palettes[] = {
        {{0.45f, 0.35f, 0.25f}, {0.55f, 0.45f, 0.30f}, {0.40f, 0.30f, 0.20f}, {0.30f, 0.25f, 0.20f}}, // Brown
        {{0.55f, 0.50f, 0.45f}, {0.65f, 0.60f, 0.55f}, {0.50f, 0.45f, 0.40f}, {0.40f, 0.35f, 0.30f}}, // Gray
        {{0.15f, 0.12f, 0.10f}, {0.20f, 0.18f, 0.15f}, {0.25f, 0.20f, 0.18f}, {0.10f, 0.08f, 0.06f}}, // Dark/black
        {{0.85f, 0.80f, 0.70f}, {0.90f, 0.85f, 0.75f}, {0.80f, 0.70f, 0.60f}, {0.75f, 0.70f, 0.65f}}, // Cream
        {{0.60f, 0.30f, 0.15f}, {0.70f, 0.40f, 0.20f}, {0.55f, 0.25f, 0.10f}, {0.35f, 0.20f, 0.10f}}, // Red/fox
        {{0.70f, 0.65f, 0.55f}, {0.80f, 0.75f, 0.65f}, {0.65f, 0.55f, 0.45f}, {0.50f, 0.45f, 0.40f}}, // Sandy
    };
    static constexpr int kNumPalettes = sizeof(palettes) / sizeof(palettes[0]);
    int idx = static_cast<int>(h % kNumPalettes);

    std::string roleLower = role;
    std::transform(roleLower.begin(), roleLower.end(), roleLower.begin(), ::tolower);

    if (roleLower.find("alpha") != std::string::npos) {
        idx = 2; // Dark
        app.heightScale = 1.2f; app.bulkScale = 1.15f;
    } else if (roleLower.find("juvenile") != std::string::npos || roleLower.find("pup") != std::string::npos) {
        app.heightScale = 0.65f; app.bulkScale = 0.7f; app.headScale = 1.3f;
    } else if (roleLower.find("elder") != std::string::npos) {
        idx = 1; // Gray
        app.heightScale = 0.95f; app.neckLengthScale = 0.9f;
    } else if (roleLower.find("albino") != std::string::npos) {
        idx = 3; // Cream (closest to white)
        app.skinColor = glm::vec4(0.95f, 0.90f, 0.88f, 1.0f);
    } else if (roleLower.find("runt") != std::string::npos) {
        app.heightScale = 0.75f; app.bulkScale = 0.8f;
    }

    const auto& pal = palettes[idx];
    float v = hashFloat(h, 16) * 0.08f - 0.04f;
    app.torsoColor = glm::vec4(glm::clamp(pal.primary + v, 0.0f, 1.0f), 1.0f);
    app.armColor   = glm::vec4(glm::clamp(pal.secondary + v, 0.0f, 1.0f), 1.0f);
    if (roleLower.find("albino") == std::string::npos)
        app.skinColor  = glm::vec4(glm::clamp(pal.face + v, 0.0f, 1.0f), 1.0f);
    app.legColor   = glm::vec4(glm::clamp(pal.paw + v, 0.0f, 1.0f), 1.0f);

    // Per-animal variation
    app.heightScale *= 0.90f + hashFloat(h, 20) * 0.20f;
    app.bulkScale   *= 0.90f + hashFloat(h, 24) * 0.20f;
    size_t h2 = hasher(name + "_body");
    app.tailLengthScale *= 0.85f + hashFloat(h2, 0) * 0.30f;
    app.neckLengthScale *= 0.90f + hashFloat(h2, 4) * 0.20f;
    app.legLengthScale  *= 0.90f + hashFloat(h2, 8) * 0.20f;

    return app;
}

static CharacterAppearance generateArachnidFromSeed(CharacterAppearance app,
    const std::string& name, const std::string& role,
    size_t h, HashFloatFn hashFloat, std::hash<std::string> hasher) {

    // Chitin color palettes: carapace, abdomen, leg, fang
    struct ChitinPalette { glm::vec3 carapace; glm::vec3 abdomen; glm::vec3 leg; glm::vec3 fang; };
    static const ChitinPalette palettes[] = {
        {{0.15f, 0.12f, 0.10f}, {0.20f, 0.15f, 0.12f}, {0.18f, 0.14f, 0.11f}, {0.40f, 0.15f, 0.10f}}, // Dark brown
        {{0.10f, 0.10f, 0.10f}, {0.12f, 0.12f, 0.12f}, {0.08f, 0.08f, 0.08f}, {0.30f, 0.10f, 0.08f}}, // Black
        {{0.50f, 0.20f, 0.10f}, {0.55f, 0.25f, 0.12f}, {0.45f, 0.18f, 0.08f}, {0.60f, 0.25f, 0.10f}}, // Red/brown
        {{0.30f, 0.35f, 0.20f}, {0.35f, 0.40f, 0.25f}, {0.25f, 0.30f, 0.15f}, {0.40f, 0.20f, 0.10f}}, // Green
        {{0.60f, 0.55f, 0.30f}, {0.65f, 0.60f, 0.35f}, {0.55f, 0.50f, 0.25f}, {0.50f, 0.20f, 0.10f}}, // Golden
    };
    static constexpr int kNumPalettes = sizeof(palettes) / sizeof(palettes[0]);
    int idx = static_cast<int>(h % kNumPalettes);

    std::string roleLower = role;
    std::transform(roleLower.begin(), roleLower.end(), roleLower.begin(), ::tolower);

    if (roleLower.find("alpha") != std::string::npos || roleLower.find("queen") != std::string::npos) {
        idx = 2; // Red
        app.heightScale = 1.3f; app.bulkScale = 1.2f;
    } else if (roleLower.find("juvenile") != std::string::npos || roleLower.find("spiderling") != std::string::npos) {
        app.heightScale = 0.5f; app.bulkScale = 0.5f;
    } else if (roleLower.find("elder") != std::string::npos) {
        idx = 1; // Black
        app.heightScale = 1.15f;
    } else if (roleLower.find("albino") != std::string::npos) {
        idx = 4; // Golden (closest to pale)
    } else if (roleLower.find("runt") != std::string::npos) {
        app.heightScale = 0.7f; app.bulkScale = 0.75f;
    }

    const auto& pal = palettes[idx];
    float v = hashFloat(h, 16) * 0.06f - 0.03f;
    app.torsoColor = glm::vec4(glm::clamp(pal.carapace + v, 0.0f, 1.0f), 1.0f);
    app.armColor   = glm::vec4(glm::clamp(pal.abdomen + v, 0.0f, 1.0f), 1.0f);
    app.legColor   = glm::vec4(glm::clamp(pal.leg + v, 0.0f, 1.0f), 1.0f);
    app.skinColor  = glm::vec4(glm::clamp(pal.fang + v, 0.0f, 1.0f), 1.0f);

    // Per-spider variation
    app.heightScale *= 0.85f + hashFloat(h, 20) * 0.30f;
    app.bulkScale   *= 0.85f + hashFloat(h, 24) * 0.30f;
    size_t h2 = hasher(name + "_body");
    app.legLengthScale *= 0.85f + hashFloat(h2, 0) * 0.30f;

    return app;
}

static CharacterAppearance generateDragonFromSeed(CharacterAppearance app,
    const std::string& name, const std::string& role,
    size_t h, HashFloatFn hashFloat, std::hash<std::string> hasher) {

    // Scale color palettes: primary, secondary, belly, wing
    struct ScalePalette { glm::vec3 primary; glm::vec3 secondary; glm::vec3 belly; glm::vec3 wing; };
    static const ScalePalette palettes[] = {
        {{0.20f, 0.55f, 0.15f}, {0.25f, 0.65f, 0.20f}, {0.70f, 0.65f, 0.40f}, {0.30f, 0.60f, 0.25f}}, // Green
        {{0.60f, 0.15f, 0.10f}, {0.70f, 0.20f, 0.12f}, {0.80f, 0.60f, 0.30f}, {0.65f, 0.20f, 0.15f}}, // Red
        {{0.10f, 0.10f, 0.12f}, {0.15f, 0.15f, 0.18f}, {0.30f, 0.25f, 0.20f}, {0.12f, 0.12f, 0.15f}}, // Black
        {{0.15f, 0.25f, 0.55f}, {0.20f, 0.30f, 0.65f}, {0.60f, 0.55f, 0.70f}, {0.25f, 0.35f, 0.60f}}, // Blue
        {{0.80f, 0.75f, 0.65f}, {0.85f, 0.80f, 0.70f}, {0.90f, 0.88f, 0.80f}, {0.82f, 0.78f, 0.68f}}, // White/ice
        {{0.65f, 0.55f, 0.20f}, {0.75f, 0.65f, 0.25f}, {0.85f, 0.80f, 0.50f}, {0.70f, 0.60f, 0.22f}}, // Gold
    };
    static constexpr int kNumPalettes = sizeof(palettes) / sizeof(palettes[0]);
    int idx = static_cast<int>(h % kNumPalettes);

    std::string roleLower = role;
    std::transform(roleLower.begin(), roleLower.end(), roleLower.begin(), ::tolower);

    if (roleLower.find("alpha") != std::string::npos || roleLower.find("ancient") != std::string::npos) {
        idx = 2; // Black
        app.heightScale = 1.4f; app.bulkScale = 1.3f; app.wingSpanScale = 1.3f;
    } else if (roleLower.find("juvenile") != std::string::npos || roleLower.find("hatchling") != std::string::npos) {
        app.heightScale = 0.5f; app.bulkScale = 0.5f; app.wingSpanScale = 0.7f; app.headScale = 1.3f;
    } else if (roleLower.find("elder") != std::string::npos) {
        idx = 5; // Gold
        app.heightScale = 1.25f; app.wingSpanScale = 1.2f; app.neckLengthScale = 1.15f;
    } else if (roleLower.find("albino") != std::string::npos) {
        idx = 4; // White/ice
    } else if (roleLower.find("runt") != std::string::npos) {
        app.heightScale = 0.7f; app.bulkScale = 0.75f; app.wingSpanScale = 0.8f;
    }

    const auto& pal = palettes[idx];
    float v = hashFloat(h, 16) * 0.06f - 0.03f;
    app.torsoColor = glm::vec4(glm::clamp(pal.primary + v, 0.0f, 1.0f), 1.0f);
    app.armColor   = glm::vec4(glm::clamp(pal.secondary + v, 0.0f, 1.0f), 1.0f);
    app.skinColor  = glm::vec4(glm::clamp(pal.belly + v, 0.0f, 1.0f), 1.0f);
    app.legColor   = glm::vec4(glm::clamp(pal.wing + v, 0.0f, 1.0f), 1.0f);

    // Per-dragon variation
    app.heightScale *= 0.85f + hashFloat(h, 20) * 0.30f;
    app.bulkScale   *= 0.85f + hashFloat(h, 24) * 0.30f;
    size_t h2 = hasher(name + "_body");
    app.wingSpanScale    *= 0.85f + hashFloat(h2, 0) * 0.30f;
    app.neckLengthScale  *= 0.85f + hashFloat(h2, 4) * 0.30f;
    app.tailLengthScale  *= 0.85f + hashFloat(h2, 8) * 0.30f;

    return app;
}

// ============================================================================
// Primary seed generation (dispatches to creature helpers)
// ============================================================================

CharacterAppearance CharacterAppearance::generateFromSeed(const std::string& name, const std::string& role, MorphologyType morphology) {
    // Deterministic hash from name
    std::hash<std::string> hasher;
    size_t h = hasher(name);

    // Helper: extract a float [0,1) from a hash by shifting bits
    auto hashFloat = [](size_t hash, int shift) -> float {
        return static_cast<float>((hash >> shift) & 0xFF) / 255.0f;
    };

    CharacterAppearance app;
    app.morphology = morphology;

    // Dispatch by morphology type
    if (morphology == MorphologyType::Quadruped) {
        return generateQuadrupedFromSeed(app, name, role, h, hashFloat, hasher);
    } else if (morphology == MorphologyType::Arachnid) {
        return generateArachnidFromSeed(app, name, role, h, hashFloat, hasher);
    } else if (morphology == MorphologyType::Dragon) {
        return generateDragonFromSeed(app, name, role, h, hashFloat, hasher);
    }

    // ---- Humanoid / Unknown ----

    // Skin tone: warm hue variation (0.7-1.0 R, 0.5-0.85 G, 0.3-0.7 B)
    app.skinColor = glm::vec4(
        0.7f + hashFloat(h, 0)  * 0.3f,
        0.5f + hashFloat(h, 4)  * 0.35f,
        0.3f + hashFloat(h, 8)  * 0.4f,
        1.0f
    );

    // Curated clothing palettes — pick from a set to avoid ugly random colors
    struct Palette { glm::vec3 torso; glm::vec3 arm; glm::vec3 leg; };
    static const Palette palettes[] = {
        {{0.8f, 0.2f, 0.2f}, {0.7f, 0.15f, 0.15f}, {0.2f, 0.2f, 0.6f}},  // Red top, dark pants
        {{0.2f, 0.5f, 0.8f}, {0.15f, 0.4f, 0.7f},  {0.3f, 0.3f, 0.3f}},  // Blue top, gray pants
        {{0.15f, 0.6f, 0.3f}, {0.1f, 0.5f, 0.25f}, {0.4f, 0.25f, 0.1f}},  // Green top, brown pants
        {{0.6f, 0.4f, 0.2f}, {0.5f, 0.35f, 0.15f}, {0.25f, 0.25f, 0.5f}}, // Brown top, blue pants
        {{0.7f, 0.7f, 0.7f}, {0.6f, 0.6f, 0.6f},  {0.2f, 0.2f, 0.2f}},  // Gray top, dark pants
        {{0.5f, 0.1f, 0.5f}, {0.4f, 0.08f, 0.4f}, {0.3f, 0.3f, 0.35f}},  // Purple top, slate pants
        {{0.9f, 0.7f, 0.2f}, {0.8f, 0.6f, 0.15f}, {0.15f, 0.15f, 0.4f}}, // Gold top, navy pants
        {{0.1f, 0.1f, 0.1f}, {0.15f, 0.15f, 0.15f}, {0.1f, 0.1f, 0.1f}}, // All black
    };
    static constexpr int kNumPalettes = sizeof(palettes) / sizeof(palettes[0]);

    int paletteIdx = static_cast<int>(h % kNumPalettes);

    // Role-based overrides: pick palette ranges that make sense
    std::string roleLower = role;
    std::transform(roleLower.begin(), roleLower.end(), roleLower.begin(), ::tolower);

    if (roleLower.find("blacksmith") != std::string::npos ||
        roleLower.find("smith") != std::string::npos) {
        paletteIdx = 7; // Dark/sooty
        app.bulkScale = 1.2f;
        app.shoulderWidthScale = 1.15f;
        app.armLengthScale = 1.05f;
    } else if (roleLower.find("guard") != std::string::npos ||
               roleLower.find("soldier") != std::string::npos ||
               roleLower.find("warrior") != std::string::npos) {
        paletteIdx = static_cast<int>((h >> 12) % 2) == 0 ? 0 : 4; // Red or gray
        app.bulkScale = 1.15f;
        app.heightScale = 1.05f;
        app.shoulderWidthScale = 1.1f;
        app.legLengthScale = 1.05f;
    } else if (roleLower.find("herbalist") != std::string::npos ||
               roleLower.find("healer") != std::string::npos ||
               roleLower.find("druid") != std::string::npos) {
        paletteIdx = 2; // Green
        app.heightScale = 0.9f;
        app.bulkScale = 0.9f;
        app.armLengthScale = 1.05f;
    } else if (roleLower.find("merchant") != std::string::npos ||
               roleLower.find("trader") != std::string::npos) {
        paletteIdx = 6; // Gold
        app.bulkScale = 1.1f;
        app.torsoLengthScale = 1.05f;
    } else if (roleLower.find("wizard") != std::string::npos ||
               roleLower.find("mage") != std::string::npos) {
        paletteIdx = 5; // Purple
        app.heightScale = 1.1f;
        app.bulkScale = 0.85f;
        app.armLengthScale = 1.1f;
        app.legLengthScale = 1.05f;
    } else if (roleLower.find("farmer") != std::string::npos ||
               roleLower.find("peasant") != std::string::npos) {
        paletteIdx = 3; // Brown
        app.heightScale = 0.95f;
        app.bulkScale = 1.05f;
        app.shoulderWidthScale = 1.05f;
    } else if (roleLower.find("noble") != std::string::npos ||
               roleLower.find("royal") != std::string::npos) {
        paletteIdx = 6; // Gold
        app.heightScale = 1.08f;
        app.bulkScale = 0.92f;
        app.headScale = 1.05f;
    } else if (roleLower.find("thief") != std::string::npos ||
               roleLower.find("rogue") != std::string::npos ||
               roleLower.find("assassin") != std::string::npos) {
        paletteIdx = 7; // Black
        app.heightScale = 0.95f;
        app.bulkScale = 0.85f;
        app.armLengthScale = 1.1f;
        app.legLengthScale = 1.08f;
    } else if (roleLower.find("child") != std::string::npos ||
               roleLower.find("kid") != std::string::npos) {
        paletteIdx = static_cast<int>((h >> 12) % kNumPalettes);
        app.heightScale = 0.7f;
        app.bulkScale = 0.75f;
        app.headScale = 1.25f;
        app.legLengthScale = 0.85f;
        app.armLengthScale = 0.9f;
    } else if (roleLower.find("dwarf") != std::string::npos) {
        paletteIdx = 3; // Brown/earthy
        app.heightScale = 0.6f;
        app.bulkScale = 1.4f;
        app.headScale = 1.15f;
        app.shoulderWidthScale = 1.35f;
        app.armLengthScale = 0.85f;
        app.legLengthScale = 0.7f;
        app.torsoLengthScale = 1.1f;
    } else if (roleLower.find("elder") != std::string::npos ||
               roleLower.find("old") != std::string::npos) {
        paletteIdx = 4; // Gray
        app.heightScale = 0.92f;
        app.bulkScale = 0.95f;
        app.torsoLengthScale = 0.95f;
    }

    const auto& pal = palettes[paletteIdx];

    // Add slight per-NPC variation to the palette
    float variation = hashFloat(h, 16) * 0.1f - 0.05f; // -0.05 to +0.05
    app.torsoColor = glm::vec4(glm::clamp(pal.torso + variation, 0.0f, 1.0f), 1.0f);
    app.armColor   = glm::vec4(glm::clamp(pal.arm + variation, 0.0f, 1.0f), 1.0f);
    app.legColor   = glm::vec4(glm::clamp(pal.leg + variation, 0.0f, 1.0f), 1.0f);

    // Per-NPC body variation (±15% height, ±12% bulk, ±8% limbs)
    app.heightScale *= 0.85f + hashFloat(h, 20) * 0.30f;
    app.bulkScale   *= 0.88f + hashFloat(h, 24) * 0.24f;
    app.headScale   *= 0.92f + hashFloat(h, 28) * 0.16f;

    // Limb proportion variation (±10%)
    size_t h2 = hasher(name + "_limbs");
    app.armLengthScale     *= 0.90f + hashFloat(h2, 0) * 0.20f;
    app.legLengthScale     *= 0.90f + hashFloat(h2, 4) * 0.20f;
    app.torsoLengthScale   *= 0.92f + hashFloat(h2, 8) * 0.16f;
    app.shoulderWidthScale *= 0.90f + hashFloat(h2, 12) * 0.20f;

    return app;
}

// ============================================================================
// JSON serialization
// ============================================================================

static glm::vec4 readColor(const nlohmann::json& j, const std::string& key, const glm::vec4& def) {
    if (!j.contains(key)) return def;
    const auto& c = j[key];
    return glm::vec4(
        c.value("r", def.r),
        c.value("g", def.g),
        c.value("b", def.b),
        c.value("a", def.a)
    );
}

static nlohmann::json writeColor(const glm::vec4& c) {
    return {{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}};
}

static const std::unordered_map<std::string, MorphologyType> sMorphNames = {
    {"humanoid", MorphologyType::Humanoid},
    {"quadruped", MorphologyType::Quadruped},
    {"arachnid", MorphologyType::Arachnid},
    {"dragon", MorphologyType::Dragon},
    {"unknown", MorphologyType::Unknown}
};

static const char* morphologyToString(MorphologyType m) {
    switch (m) {
        case MorphologyType::Humanoid:  return "humanoid";
        case MorphologyType::Quadruped: return "quadruped";
        case MorphologyType::Arachnid:  return "arachnid";
        case MorphologyType::Dragon:    return "dragon";
        default:                        return "unknown";
    }
}

CharacterAppearance CharacterAppearance::fromJson(const nlohmann::json& j) {
    CharacterAppearance app;

    // Morphology
    if (j.contains("morphology")) {
        std::string mstr = j["morphology"].get<std::string>();
        std::transform(mstr.begin(), mstr.end(), mstr.begin(), ::tolower);
        auto it = sMorphNames.find(mstr);
        if (it != sMorphNames.end()) app.morphology = it->second;
    }

    // Colors
    app.skinColor    = readColor(j, "skinColor",    app.skinColor);
    app.torsoColor   = readColor(j, "torsoColor",   app.torsoColor);
    app.armColor     = readColor(j, "armColor",     app.armColor);
    app.legColor     = readColor(j, "legColor",     app.legColor);
    app.defaultColor = readColor(j, "defaultColor", app.defaultColor);

    // Proportion scales
    app.heightScale  = j.value("heightScale", app.heightScale);
    app.bulkScale    = j.value("bulkScale",   app.bulkScale);
    app.headScale    = j.value("headScale",   app.headScale);
    app.armLengthScale     = j.value("armLengthScale",     app.armLengthScale);
    app.legLengthScale     = j.value("legLengthScale",     app.legLengthScale);
    app.torsoLengthScale   = j.value("torsoLengthScale",   app.torsoLengthScale);
    app.shoulderWidthScale = j.value("shoulderWidthScale", app.shoulderWidthScale);

    // Creature-specific scales
    app.tailLengthScale = j.value("tailLengthScale", app.tailLengthScale);
    app.wingSpanScale   = j.value("wingSpanScale",   app.wingSpanScale);
    app.neckLengthScale = j.value("neckLengthScale", app.neckLengthScale);

    return app;
}

nlohmann::json CharacterAppearance::toJson() const {
    return {
        {"morphology",   morphologyToString(morphology)},
        {"skinColor",    writeColor(skinColor)},
        {"torsoColor",   writeColor(torsoColor)},
        {"armColor",     writeColor(armColor)},
        {"legColor",     writeColor(legColor)},
        {"defaultColor", writeColor(defaultColor)},
        {"heightScale",  heightScale},
        {"bulkScale",    bulkScale},
        {"headScale",    headScale},
        {"armLengthScale",     armLengthScale},
        {"legLengthScale",     legLengthScale},
        {"torsoLengthScale",   torsoLengthScale},
        {"shoulderWidthScale", shoulderWidthScale},
        {"tailLengthScale",  tailLengthScale},
        {"wingSpanScale",    wingSpanScale},
        {"neckLengthScale",  neckLengthScale}
    };
}

} // namespace Scene
} // namespace Phyxel
