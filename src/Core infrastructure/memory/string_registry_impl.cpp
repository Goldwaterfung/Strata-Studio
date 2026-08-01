#include "istring_registry.h"
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <memory>

namespace Layer2 {

class StringRegistryImpl : public IStringRegistry {
public:
    StringRegistryImpl() : nextId_(1) {}

    uint32_t registerString(const std::string& str) override {
        {
            std::shared_lock readLock(mutex_);
            auto it = stringToId_.find(str);
            if (it != stringToId_.end()) {
                return it->second;
            }
        }

        std::unique_lock writeLock(mutex_);
        // Double check after acquiring write lock
        auto it = stringToId_.find(str);
        if (it != stringToId_.end()) {
            return it->second;
        }

        uint32_t id = nextId_++;
        stringToId_[str] = id;
        idToString_[id] = str;
        return id;
    }

    bool getString(uint32_t id, std::string& outStr) const override {
        std::shared_lock readLock(mutex_);
        auto it = idToString_.find(id);
        if (it != idToString_.end()) {
            outStr = it->second;
            return true;
        }
        return false;
    }

    void unregisterString(uint32_t id) override {
        std::unique_lock writeLock(mutex_);
        auto it = idToString_.find(id);
        if (it != idToString_.end()) {
            stringToId_.erase(it->second);
            idToString_.erase(it);
        }
    }

private:
    mutable std::shared_mutex mutex_;
    std::atomic<uint32_t> nextId_;
    std::unordered_map<std::string, uint32_t> stringToId_;
    std::unordered_map<uint32_t, std::string> idToString_;
};

std::unique_ptr<IStringRegistry> IStringRegistry::create() {
    return std::make_unique<StringRegistryImpl>();
}

} // namespace Layer2
