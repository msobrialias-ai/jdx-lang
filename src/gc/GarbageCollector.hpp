#pragma once

#include <memory>
#include <mutex>
#include <vector>

namespace jdx::gc {

class GarbageCollector final {
public:
    static GarbageCollector& instance();

    template <typename T>
    void track(const std::shared_ptr<T>& object) {
        if (!object) {
            return;
        }
        std::scoped_lock lock(mutex_);
        tracked_.emplace_back(object);
    }

    void collect() noexcept;

    [[nodiscard]] std::size_t trackedCount() const noexcept;

private:
    GarbageCollector() = default;

    mutable std::mutex mutex_;
    std::vector<std::weak_ptr<void>> tracked_;
};

} // namespace jdx::gc
