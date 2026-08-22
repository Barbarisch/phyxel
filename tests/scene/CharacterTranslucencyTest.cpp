// Per-character tint and opacity (RagdollCharacter::setRenderTint /
// setRenderAlpha) are what let one archetype rig cover a whole palette family
// and what makes incorporeal undead see-through. Both are consumed by the
// renderer at instance-blob build time, so the contract that matters here is:
//
//   * a change must invalidate the cached blob (partsVersion bump), or the
//     colour silently never updates
//   * isTranslucent() must classify exactly the characters that need the
//     blend-enabled pipeline, because that same flag decides whether the
//     character is kept OUT of the shadow batch list. It shipped once with
//     the shadow half missing, and a ghost threw the same solid shadow as a
//     living body (drawn_shadow counted all three of ghost, specter, orc).

#include <gtest/gtest.h>

#include "scene/RagdollCharacter.h"

using Phyxel::Scene::RagdollCharacter;

namespace {

/// RagdollCharacter is abstract-ish in practice (it owns render/update hooks);
/// the tint/alpha state under test lives entirely in the base, so a minimal
/// concrete subclass is enough.
class BareCharacter : public RagdollCharacter {
public:
    BareCharacter() : RagdollCharacter(nullptr, glm::vec3(0.0f)) {}
    void update(float) override {}   // the only pure virtual; unused here
};

TEST(CharacterTranslucency, DefaultsAreOpaqueAndUntinted) {
    BareCharacter ch;
    EXPECT_FLOAT_EQ(ch.getRenderAlpha(), 1.0f);
    EXPECT_EQ(ch.getRenderTint(), glm::vec3(1.0f));
    EXPECT_FALSE(ch.isTranslucent());
}

TEST(CharacterTranslucency, AlphaBelowOneMarksTranslucent) {
    BareCharacter ch;
    ch.setRenderAlpha(0.4f);
    EXPECT_TRUE(ch.isTranslucent())
        << "a ghost must reach the blend-enabled pipeline AND leave the shadow list";
    ch.setRenderAlpha(1.0f);
    EXPECT_FALSE(ch.isTranslucent());
}

TEST(CharacterTranslucency, TintChangeInvalidatesTheInstanceBlob) {
    BareCharacter ch;
    const uint32_t before = ch.partsVersion();
    ch.setRenderTint(glm::vec3(1.0f, 0.4f, 0.4f));
    EXPECT_GT(ch.partsVersion(), before)
        << "instance colours are baked into a cached blob; without a version "
           "bump the recolour would never reach the GPU";
    EXPECT_EQ(ch.getRenderTint(), glm::vec3(1.0f, 0.4f, 0.4f));
}

TEST(CharacterTranslucency, AlphaChangeInvalidatesTheInstanceBlob) {
    BareCharacter ch;
    const uint32_t before = ch.partsVersion();
    ch.setRenderAlpha(0.35f);
    EXPECT_GT(ch.partsVersion(), before);
}

TEST(CharacterTranslucency, SettingTheSameValueDoesNotChurnTheBlob) {
    BareCharacter ch;
    ch.setRenderTint(glm::vec3(0.5f));
    const uint32_t settled = ch.partsVersion();
    ch.setRenderTint(glm::vec3(0.5f));
    ch.setRenderAlpha(1.0f);          // already the default
    EXPECT_EQ(ch.partsVersion(), settled)
        << "spawn paths set these unconditionally; a no-op must not force a "
           "blob rebuild every frame";
}

}  // namespace
