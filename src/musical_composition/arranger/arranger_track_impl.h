#pragma once
#include "iarranger_track.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <vector>

namespace composition {

class ICommandHistory;

class ArrangerTrackImpl : public IArrangerTrack {
public:
    ArrangerTrackImpl(ICommandHistory* history);

    void addSection(const Section& section) override;
    void removeSection(uint64_t startSample) override;
    
    bool getSectionAt(uint64_t samplePosition, Section& outSection) const override;
    uint32_t getAllSections(Section* outSections, uint32_t maxSections) const override;
    
    void applyDelta(const ProjectDelta& delta, bool isUndo);

private:
    ICommandHistory* history_;
    std::vector<Section> sections_;
};

} // namespace composition
