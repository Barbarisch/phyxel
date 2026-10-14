#include <gtest/gtest.h>
#include "core/ItemEffectSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <map>

using namespace Phyxel::Core;

namespace {

// A torch-like item: one always-on light at a template-local tip anchor.
ItemDefinition makeTorch() {
    ItemDefinition d;
    d.id = "test_torch";
    ItemEffectDef e;
    e.id = "flame";
    e.anchor = {0.0f, 0.4f, 0.0f};
    e.hasLight = true;
    d.effects.push_back(e);
    return d;
}

struct FakeLights {
    std::map<int, glm::vec3> lights;
    int next = 1;
    void attach(ItemEffectSystem& fx) {
        fx.setLightCallbacks(
            [this](const glm::vec3& p, const glm::vec3&, float, float) { lights[next] = p; return next++; },
            [this](int id, const glm::vec3& p) { lights[id] = p; },
            [this](int id) { lights.erase(id); });
    }
};

} // namespace

// A thrown torch must be lit the first frame it exists. The old code only created
// lights at the throttled condition check, staggered by registration count, so a
// newly registered torch stayed dark for up to ~0.2 s (measured live: 0.23 s).
TEST(ItemEffectSystemTest, NewInstanceLightsOnFirstUpdate) {
    const ItemDefinition torch = makeTorch();
    for (int preexisting = 0; preexisting < 4; ++preexisting) {  // every stagger phase
        ItemEffectSystem fx;
        FakeLights fl;
        fl.attach(fx);
        for (int i = 0; i < preexisting; ++i)
            fx.registerInstance("other" + std::to_string(i), &torch, false,
                                [] { return glm::mat4(1.0f); });
        const glm::mat4 at = glm::translate(glm::mat4(1.0f), glm::vec3(5, 17, 3));
        fx.registerInstance("thrown", &torch, false, [at] { return at; });
        fx.update(1.0f / 60.0f);
        EXPECT_EQ(fl.lights.size(), size_t(preexisting + 1)) << "stagger phase " << preexisting;
    }
}

// The light sits on the transformed anchor (the flame tip) and follows the item.
TEST(ItemEffectSystemTest, LightFollowsItemTransform) {
    const ItemDefinition torch = makeTorch();
    ItemEffectSystem fx;
    FakeLights fl;
    fl.attach(fx);
    glm::vec3 pos(1, 2, 3);
    fx.registerInstance("t", &torch, false, [&pos] { return glm::translate(glm::mat4(1.0f), pos); });
    fx.update(1.0f / 60.0f);
    ASSERT_EQ(fl.lights.size(), 1u);
    EXPECT_NEAR(fl.lights.begin()->second.y, 2.4f, 1e-5f);
    pos = {10, 20, 30};
    fx.update(1.0f / 60.0f);  // not a condition-check frame: the light must still move
    EXPECT_NEAR(fl.lights.begin()->second.x, 10.0f, 1e-5f);
    EXPECT_NEAR(fl.lights.begin()->second.y, 20.4f, 1e-5f);
}
