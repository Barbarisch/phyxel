#include "core/APICommandQueue.h"

namespace Phyxel {
namespace Core {

void APICommandQueue::push(APICommand cmd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(std::move(cmd));
}

size_t APICommandQueue::drainCommands(std::vector<APICommand>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = m_queue.size();
    while (!m_queue.empty()) {
        out.push_back(std::move(m_queue.front()));
        m_queue.pop();
    }
    return count;
}

bool APICommandQueue::hasPending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_queue.empty();
}

size_t APICommandQueue::approximateSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

} // namespace Core
} // namespace Phyxel
