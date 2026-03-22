#pragma once

#include "story/StoryTypes.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace Phyxel {
namespace Story {

// ============================================================================
// EventBus — broadcasts WorldEvents to registered listeners
//
// Listeners are callbacks keyed by a subscription ID. They can optionally
// filter by event type. The bus is thread-safe for emit but listeners
// are invoked synchronously on the caller's thread.
// ============================================================================

using EventListener = std::function<void(const WorldEvent&)>;

class EventBus {
public:
    /// Subscribe to all events. Returns a subscription ID for unsubscribing.
    int subscribe(EventListener listener);

    /// Subscribe to events of a specific type only.
    int subscribeToType(const std::string& eventType, EventListener listener);

    /// Remove a subscription by ID.
    void unsubscribe(int subscriptionId);

    /// Broadcast an event to all matching listeners.
    void emit(const WorldEvent& event);

    /// Number of active subscriptions.
    size_t subscriberCount() const;

    /// Remove all subscriptions.
    void clear();

private:
    struct Subscription {
        int id;
        std::string filterType; // Empty = all events
        EventListener listener;
    };

    mutable std::mutex m_mutex;
    std::vector<Subscription> m_subscriptions;
    int m_nextId = 1;
};

} // namespace Story
} // namespace Phyxel
