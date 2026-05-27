#pragma once

#include "ecs/Entity.h"

#include <cstdint>
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <cassert>
#include <utility>

// ============================================================================
// Phyxel::Ecs — minimal sparse-set entity-component registry.
//
// Single source of truth for an entity's data: a component lives in exactly one
// pool, keyed by the entity's slot index. Derived subsystems (render, physics,
// persistence) read components via views and reconcile their own state — they
// do not own a second copy.
//
// Design notes:
//   * Sparse-set storage: O(1) add/get/has/remove, contiguous component arrays
//     for cache-friendly iteration. Right fit for the modest entity counts of
//     the world-object/furniture domain (the pilot).
//   * Generational handles: a destroyed entity's slot may be reused; stale
//     handles are detected via the generation counter (see Entity.h). This also
//     removes the raw-pointer/dangling-registration class of bugs.
//   * NOT thread-safe. Drive it from the main thread (MCP handlers already
//     marshal there via queueAndWait).
// ============================================================================

namespace Phyxel {
namespace Ecs {

class IPool {
public:
    virtual ~IPool() = default;
    virtual void removeIfPresent(uint32_t entityIndex) = 0;
    virtual bool contains(uint32_t entityIndex) const = 0;
};

/// Sparse set of component C, indexed by entity slot index.
template <class C>
class Pool : public IPool {
public:
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

    C& add(uint32_t e, C value) {
        if (e >= m_sparse.size()) m_sparse.resize(e + 1, kInvalid);
        if (m_sparse[e] != kInvalid) {           // overwrite existing
            m_dense[m_sparse[e]] = std::move(value);
            return m_dense[m_sparse[e]];
        }
        m_sparse[e] = static_cast<uint32_t>(m_dense.size());
        m_denseEntities.push_back(e);
        m_dense.push_back(std::move(value));
        return m_dense.back();
    }

    bool contains(uint32_t e) const override {
        return e < m_sparse.size() && m_sparse[e] != kInvalid;
    }

    C* get(uint32_t e) {
        return contains(e) ? &m_dense[m_sparse[e]] : nullptr;
    }

    void removeIfPresent(uint32_t e) override {
        if (!contains(e)) return;
        uint32_t denseIdx = m_sparse[e];
        uint32_t lastIdx  = static_cast<uint32_t>(m_dense.size() - 1);
        m_dense[denseIdx]         = std::move(m_dense[lastIdx]);
        m_denseEntities[denseIdx] = m_denseEntities[lastIdx];
        m_sparse[m_denseEntities[denseIdx]] = denseIdx;   // patch moved entity
        m_dense.pop_back();
        m_denseEntities.pop_back();
        m_sparse[e] = kInvalid;
    }

    std::vector<C>&        components() { return m_dense; }
    std::vector<uint32_t>& entities()   { return m_denseEntities; }
    size_t size() const { return m_dense.size(); }

private:
    std::vector<uint32_t> m_sparse;        ///< entity index -> dense index
    std::vector<uint32_t> m_denseEntities; ///< dense index  -> entity index
    std::vector<C>        m_dense;         ///< dense index  -> component
};

class Registry {
public:
    Entity create() {
        uint32_t idx;
        if (!m_free.empty()) {
            idx = m_free.back();
            m_free.pop_back();
        } else {
            idx = static_cast<uint32_t>(m_generations.size());
            m_generations.push_back(1);  // generation 0 reserved for null
            m_alive.push_back(0);
        }
        m_alive[idx] = 1;
        return Entity{idx, m_generations[idx]};
    }

    bool valid(Entity e) const {
        return e.generation != 0
            && e.index < m_generations.size()
            && m_generations[e.index] == e.generation
            && m_alive[e.index];
    }

    void destroy(Entity e) {
        if (!valid(e)) return;
        for (auto& [type, pool] : m_pools)
            pool->removeIfPresent(e.index);
        m_alive[e.index] = 0;
        if (++m_generations[e.index] == 0) m_generations[e.index] = 1;  // skip null gen
        m_free.push_back(e.index);
    }

    template <class C> C& add(Entity e, C value) {
        assert(valid(e));
        return pool<C>().add(e.index, std::move(value));
    }
    template <class C> C* get(Entity e) {
        if (!valid(e)) return nullptr;
        auto* p = poolPtr<C>();
        return p ? p->get(e.index) : nullptr;
    }
    template <class C> bool has(Entity e) {
        if (!valid(e)) return false;
        auto* p = poolPtr<C>();
        return p && p->contains(e.index);
    }
    template <class C> void remove(Entity e) {
        if (!valid(e)) return;
        if (auto* p = poolPtr<C>()) p->removeIfPresent(e.index);
    }

    /// Iterate every live entity that has all of C, Rest...; calls
    /// fn(Entity, C&, Rest&...). Driven off the (smaller) primary pool C.
    /// Do not create/destroy entities or add/remove the iterated component
    /// types from within fn.
    template <class C, class... Rest, class Fn>
    void view(Fn&& fn) {
        auto* base = poolPtr<C>();
        if (!base) return;
        auto& ents = base->entities();
        for (size_t i = 0; i < ents.size(); ++i) {
            uint32_t ei = ents[i];
            if (!allHave<Rest...>(ei)) continue;
            Entity e{ei, m_generations[ei]};
            fn(e, *base->get(ei), *poolPtr<Rest>()->get(ei)...);
        }
    }

    size_t aliveCount() const {
        size_t n = 0;
        for (uint8_t a : m_alive) if (a) ++n;
        return n;
    }

private:
    template <class C> Pool<C>& pool() {
        std::type_index ti(typeid(C));
        auto it = m_pools.find(ti);
        if (it == m_pools.end())
            it = m_pools.emplace(ti, std::make_unique<Pool<C>>()).first;
        return *static_cast<Pool<C>*>(it->second.get());
    }
    template <class C> Pool<C>* poolPtr() {
        auto it = m_pools.find(std::type_index(typeid(C)));
        return it == m_pools.end() ? nullptr : static_cast<Pool<C>*>(it->second.get());
    }
    template <class... Cs> bool allHave(uint32_t ei) {
        bool ok = true;
        ((ok = ok && poolPtr<Cs>() && poolPtr<Cs>()->contains(ei)), ...);
        return ok;
    }

    std::vector<uint32_t> m_generations;  ///< per slot; 0 never used as live gen
    std::vector<uint8_t>  m_alive;        ///< per slot liveness
    std::vector<uint32_t> m_free;         ///< reusable slot indices
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;
};

} // namespace Ecs
} // namespace Phyxel
