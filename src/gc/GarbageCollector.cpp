#include "gc/GarbageCollector.hpp"

#include <algorithm>

namespace jdx::gc {

GarbageCollector& GarbageCollector::instance() {
    static GarbageCollector gc;
    return gc;
}

void GarbageCollector::collect() noexcept {
    std::scoped_lock lock(mutex_);
    auto it = std::remove_if(tracked_.begin(), tracked_.end(),
                             [](const std::weak_ptr<void>& ref) { return ref.expired(); });
    tracked_.erase(it, tracked_.end());
}

std::size_t GarbageCollector::trackedCount() const noexcept {
    std::scoped_lock lock(mutex_);
    return tracked_.size();
}

} // namespace jdx::gc
