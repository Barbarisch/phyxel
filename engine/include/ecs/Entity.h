#pragma once

#include <cstdint>
#include <functional>

namespace Phyxel {
namespace Ecs {

/// A generational entity handle. `index` is a slot into the registry's arrays;
/// `generation` is bumped each time a slot is reused, so a stale handle to a
/// destroyed entity compares unequal to the live one occupying the same slot.
/// generation == 0 is reserved for the null handle.
struct Entity {
    uint32_t index      = 0;
    uint32_t generation = 0;  ///< 0 == null/invalid

    constexpr bool valid() const { return generation != 0; }
    constexpr bool operator==(const Entity& o) const {
        return index == o.index && generation == o.generation;
    }
    constexpr bool operator!=(const Entity& o) const { return !(*this == o); }
};

inline constexpr Entity NullEntity{0, 0};

} // namespace Ecs
} // namespace Phyxel

namespace std {
template <>
struct hash<Phyxel::Ecs::Entity> {
    size_t operator()(const Phyxel::Ecs::Entity& e) const noexcept {
        return (static_cast<size_t>(e.generation) << 32) ^ e.index;
    }
};
} // namespace std
