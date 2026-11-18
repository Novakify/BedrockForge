#pragma once
// Bedrock Forge SDK - EventAPI (v0)
// Thin RAII wrapper over LeviLamina's EventBus so mods never leak listeners
// when they are disabled.

#include <functional>
#include <utility>
#include <vector>

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"

namespace bf::events {

/// Owns a set of listeners and removes them all on clear() / destruction.
class Subscriptions {
public:
    Subscriptions() = default;
    Subscriptions(Subscriptions const&)            = delete;
    Subscriptions& operator=(Subscriptions const&) = delete;
    ~Subscriptions() { clear(); }

    /// Subscribe to a LeviLamina event type, e.g. on<ll::event::ClientJoinLevelEvent>(...)
    template <class Event, class Fn>
    void on(Fn&& fn) {
        auto listener = ll::event::EventBus::getInstance().emplaceListener<Event>(std::forward<Fn>(fn));
        if (listener) mListeners.emplace_back(std::move(listener));
    }

    void clear() {
        auto& bus = ll::event::EventBus::getInstance();
        for (auto& l : mListeners) bus.removeListener(l);
        mListeners.clear();
    }

    [[nodiscard]] size_t size() const { return mListeners.size(); }

private:
    std::vector<ll::event::ListenerPtr> mListeners;
};

} // namespace bf::events
