// src/Middle Bridge/pattern_data_provider.h
#pragma once

#include "Middle Bridge/telemetry/ipattern_data_provider.h"
#include "Middle Bridge/project/isession_manager.h"

namespace bridge {

class PatternDataProvider : public IPatternDataProvider {
public:
    explicit PatternDataProvider(ISessionManager* sessionManager);
    ~PatternDataProvider() override = default;

    uint32_t getNoteEventsForRegion(
        RegionID regionId,
        VisualNoteEvent* outEvents,
        uint32_t maxCount
    ) const override;

private:
    ISessionManager* sessionManager_ = nullptr;
};

} // namespace bridge
