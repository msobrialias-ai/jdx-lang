#pragma once
#include <memory>
#include <vector>
#include <algorithm>

namespace jdx::gc {

class GarbageCollector {
public:
    template <typename T>
    std::shared_ptr<T> track(std::shared_ptr<T> ptr) {
        tracked_.push_back(ptr);
        return ptr;
    }

    void sweep() {
        tracked_.erase(std::remove_if(tracked_.begin(), tracked_.end(), [](const std::weak_ptr<void>& w) { return w.expired(); }), tracked_.end());
    }

    std::size_t trackedCount() const { return tracked_.size(); }

private:
    std::vector<std::weak_ptr<void>> tracked_;
};

} // namespace jdx::gc
