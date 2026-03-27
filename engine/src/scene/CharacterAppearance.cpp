#include "scene/CharacterAppearance.h"
#include <algorithm>
#include <functional>

namespace Phyxel {
namespace Scene {

// ============================================================================
// Bone → Color mapping
// ============================================================================

glm::vec4 CharacterAppearance::getColorForBone(const std::string& boneName) const {
    std::string lower = boneName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("head") != std::string::npos ||
        lower.find("hand") != std::string::npos)
        return skinColor;

    if (lower.find("arm") != std::string::npos ||
        lower.find("shoulder") != std::string::npos)
        return armColor;

    if (lower.find("leg") != std::string::npos ||
        lower.find("foot") != std::string::npos ||
        lower.find("thigh") != std::string::npos ||
        lower.find("shin") != std::string::npos)
        return legColor;

    if (lower.find("spine") != std::string::npos ||
        lower.find("torso") != std::string::npos ||
        lower.find("hip") != std::string::npos ||
        lower.find("chest") != std::string::npos ||
        lower.find("neck") != std::string::npos)
        return torsoColor;

    return defaultColor;
}

// ============================================================================
// Factory methods
// ============================================================================

CharacterAppearance CharacterAppearance::defaultAppearance() {
    return CharacterAppearance{}; // Default member values match original hardcoded colors
}

CharacterAppearance CharacterAppearance::generateFromSeed(const std::string& name, const std::string& role) {
    // Deterministic hash from name
    std::hash<std::string> hasher;
    size_t h = hasher(name);

    // Helper: extract a float [0,1) from a hash by shifting bits
    auto hashFloat = [](size_t hash, int shift) -> float {
        return static_cast<float>((hash >> shift) & 0xFF) / 255.0f;
    };

    CharacterAppearance app;

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

CharacterAppearance CharacterAppearance::fromJson(const nlohmann::json& j) {
    CharacterAppearance app;
    app.skinColor    = readColor(j, "skinColor",    app.skinColor);
    app.torsoColor   = readColor(j, "torsoColor",   app.torsoColor);
    app.armColor     = readColor(j, "armColor",     app.armColor);
    app.legColor     = readColor(j, "legColor",     app.legColor);
    app.defaultColor = readColor(j, "defaultColor", app.defaultColor);
    app.heightScale  = j.value("heightScale", app.heightScale);
    app.bulkScale    = j.value("bulkScale",   app.bulkScale);
    app.headScale    = j.value("headScale",   app.headScale);
    app.armLengthScale     = j.value("armLengthScale",     app.armLengthScale);
    app.legLengthScale     = j.value("legLengthScale",     app.legLengthScale);
    app.torsoLengthScale   = j.value("torsoLengthScale",   app.torsoLengthScale);
    app.shoulderWidthScale = j.value("shoulderWidthScale", app.shoulderWidthScale);
    return app;
}

nlohmann::json CharacterAppearance::toJson() const {
    return {
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
        {"shoulderWidthScale", shoulderWidthScale}
    };
}

} // namespace Scene
} // namespace Phyxel
