#pragma once
// docs/VoxelFoundationPlan.md section 7 step 6, F7.6b -- undo / redo on the switched world.
//
// One entry per edit: the edit's box [lo, hi] (world 1/8 m voxels) and its materials before and after (dense bytes,
// x-major as forEach visits them). undo() / redo() return only the voxels whose current material differs from the
// side being restored -- written back by RegionEdit::applyCells, the exact incremental path. Depth-capped like the
// engine's SnapshotManager stack (20); a new edit clears redo. Edits over maxVoxels are not recorded (the caller
// reports it). Header-only, no engine dependencies (VoxFoundEditHistory tests).

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace Phyxel::Core::VoxFound {

class EditHistory {
public:
    using Read = std::function<uint8_t(glm::ivec3)>;
    using Cells = std::vector<std::pair<glm::ivec3, uint8_t>>;
    struct Entry {
        std::string label;
        glm::ivec3 lo{0}, hi{-1};
        std::vector<uint8_t> before, after;
    };
    static constexpr size_t kMaxDepth = 20;
    static constexpr int64_t kMaxVoxels = int64_t(16) << 20;

    static int64_t volume(glm::ivec3 lo, glm::ivec3 hi) {
        const glm::ivec3 s = hi - lo + 1;
        return (s.x <= 0 || s.y <= 0 || s.z <= 0) ? 0 : int64_t(s.x) * s.y * s.z;
    }
    static void forEach(glm::ivec3 lo, glm::ivec3 hi, const std::function<void(glm::ivec3)>& f) {
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) f({x, y, z});
    }
    static std::vector<uint8_t> capture(glm::ivec3 lo, glm::ivec3 hi, const Read& read) {
        std::vector<uint8_t> out;
        out.reserve(size_t(volume(lo, hi)));
        forEach(lo, hi, [&](glm::ivec3 p) { out.push_back(read(p)); });
        return out;
    }

    /// Before an edit: false (nothing pending) when the box is empty or over the limit.
    bool begin(const std::string& label, glm::ivec3 lo, glm::ivec3 hi, const Read& read) {
        m_pending = {};
        m_hasPending = false;
        const int64_t v = volume(lo, hi);
        if (v <= 0 || v > kMaxVoxels) return false;
        m_pending.label = label;
        m_pending.lo = lo;
        m_pending.hi = hi;
        m_pending.before = capture(lo, hi, read);
        m_hasPending = true;
        return true;
    }
    /// After the edit: records it when any voxel of the box changed (true); clears redo.
    bool commit(const Read& read) {
        if (!m_hasPending) return false;
        m_hasPending = false;
        m_pending.after = capture(m_pending.lo, m_pending.hi, read);
        if (m_pending.after == m_pending.before) return false;
        m_undo.push_back(std::move(m_pending));
        if (m_undo.size() > kMaxDepth) m_undo.pop_front();
        m_redo.clear();
        return true;
    }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    size_t undoDepth() const { return m_undo.size(); }
    size_t redoDepth() const { return m_redo.size(); }
    const std::deque<Entry>& undoStack() const { return m_undo; }
    const std::deque<Entry>& redoStack() const { return m_redo; }
    void clear() { m_undo.clear(); m_redo.clear(); m_hasPending = false; }

    /// The cells that bring the box back to its `before` side; the entry moves to redo. label = the edit's.
    Cells undo(const Read& read, std::string* label = nullptr) {
        if (m_undo.empty()) return {};
        Entry e = std::move(m_undo.back());
        m_undo.pop_back();
        Cells c = diff(e, e.before, read);
        if (label) *label = e.label;
        m_redo.push_back(std::move(e));
        return c;
    }
    /// The cells that bring the box to its `after` side again; the entry moves back to undo.
    Cells redo(const Read& read, std::string* label = nullptr) {
        if (m_redo.empty()) return {};
        Entry e = std::move(m_redo.back());
        m_redo.pop_back();
        Cells c = diff(e, e.after, read);
        if (label) *label = e.label;
        m_undo.push_back(std::move(e));
        return c;
    }

private:
    static Cells diff(const Entry& e, const std::vector<uint8_t>& side, const Read& read) {
        Cells c;
        size_t k = 0;
        forEach(e.lo, e.hi, [&](glm::ivec3 p) {
            const uint8_t want = side[k++];
            if (read(p) != want) c.emplace_back(p, want);
        });
        return c;
    }
    std::deque<Entry> m_undo, m_redo;
    Entry m_pending;
    bool m_hasPending = false;
};

}  // namespace Phyxel::Core::VoxFound
