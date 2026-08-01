#pragma once
#include "common/system_primitives.h"

namespace bridge {

/**
 * @brief Controller interface managing the offline rendering and export pipeline.
 * Coordinates with the background bounce engine and tracks rendering progress/errors.
 */
class IRenderController {
public:
    virtual ~IRenderController() = default;

    virtual void startOfflineRender(const RenderConfiguration& config) = 0;
    virtual bool isRenderingActive() const = 0;
    virtual float getRenderProgress() const = 0;           // Range [0.0f, 1.0f]
    virtual const char* getRenderStatusMessage() const = 0; // Current step detail
    virtual void cancelOfflineRender() = 0;
    virtual bool hasFailed(char* outError, uint32_t maxLen) const = 0;
    virtual void startSilentMixAnalysis(uint64_t startFrame, uint64_t endFrame, uint32_t sampleRate) = 0;
};

} // namespace bridge
