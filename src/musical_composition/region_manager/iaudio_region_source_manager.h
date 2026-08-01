#pragma once
#include "musical_composition/musical_primitives.h"
#include <cstdint>

namespace composition {

struct AudioSourceDescriptor {
    SourceID sourceId;          
    uint32_t nameId;            
    uint64_t totalLengthSamples;
    uint32_t channelCount;
    uint32_t sampleRate;
    uint64_t mediaId;           // Handle for Layer 6
};

class IAudioRegionSourceManager {
public:
    virtual ~IAudioRegionSourceManager() = default;

    virtual SourceID registerSource(const AudioSourceDescriptor& descriptor, const std::string& filePath = "") = 0;
    virtual bool getSource(SourceID id, AudioSourceDescriptor& outDescriptor) const = 0;

    virtual void incrementReference(SourceID id) = 0;
    virtual void decrementReference(SourceID id) = 0;
};

} // namespace composition
