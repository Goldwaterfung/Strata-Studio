#pragma once
#include "iaudio_region_source_manager.h"
#include <unordered_map>
#include <string>

namespace composition {

class ICommandHistory;
struct ProjectDelta;

class RegionSourceManagerImpl : public IAudioRegionSourceManager {
public:
    explicit RegionSourceManagerImpl(ICommandHistory* history = nullptr);

    SourceID registerSource(const AudioSourceDescriptor& descriptor, const std::string& filePath = "") override;
    bool getSource(SourceID id, AudioSourceDescriptor& outDescriptor) const override;

    void incrementReference(SourceID id) override;
    void decrementReference(SourceID id) override;

    void applyDelta(const ProjectDelta& delta, bool isUndo);

    struct SourceEntry {
        AudioSourceDescriptor descriptor;
        std::string filePath;
    };
    std::vector<SourceEntry> getAllSources() const;

private:
    struct SourceState {
        AudioSourceDescriptor descriptor;
        std::string filePath;
        uint32_t refCount = 0;
    };

    ICommandHistory* history_ = nullptr;
    bool isApplyingDelta_ = false;
    std::unordered_map<uint32_t, SourceState> sources_;
    uint32_t nextIdCounter_ = 0;

    SourceID generateNextId();
};

} // namespace composition
