#include "story/EventBus.h"
#include <algorithm>

namespace Phyxel {
namespace Story {

int EventBus::subscribe(EventListener listener) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextId++;
    m_subscriptions.push_back({id, "", std::move(listener)});
    return id;
}

int EventBus::subscribeToType(const std::string& eventType, EventListener listener) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextId++;
    m_subscriptions.push_back({id, eventType, std::move(listener)});
    return id;
}

void EventBus::unsubscribe(int subscriptionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscriptions.erase(
        std::remove_if(m_subscriptions.begin(), m_subscriptions.end(),
                        [subscriptionId](const Subscription& s) { return s.id == subscriptionId; }),
        m_subscriptions.end());
}

void EventBus::emit(const WorldEvent& event) {
    // Copy listeners under lock, invoke without lock to avoid deadlock
    std::vector<Subscription> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_subscriptions;
    }

    for (auto& sub : snapshot) {
        if (sub.filterType.empty() || sub.filterType == event.type) {
            sub.listener(event);
        }
    }
}

size_t EventBus::subscriberCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_subscriptions.size();
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscriptions.clear();
}

} // namespace Story
} // namespace Phyxel
