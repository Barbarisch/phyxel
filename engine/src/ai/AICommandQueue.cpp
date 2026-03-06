#include "ai/AICommandQueue.h"

namespace Phyxel {
namespace AI {

void AICommandQueue::push(AICommand cmd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(std::move(cmd));
}

void AICommandQueue::pushBatch(std::vector<AICommand> cmds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& cmd : cmds) {
        m_queue.push(std::move(cmd));
    }
}

size_t AICommandQueue::drainCommands(std::vector<AICommand>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = m_queue.size();
    out.reserve(out.size() + count);
    while (!m_queue.empty()) {
        out.push_back(std::move(m_queue.front()));
        m_queue.pop();
    }
    return count;
}

bool AICommandQueue::hasPending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue.empty();
}

size_t AICommandQueue::approximateSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

} // namespace AI
} // namespace Phyxel
