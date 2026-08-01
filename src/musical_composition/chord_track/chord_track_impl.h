#pragma once
#include "ichord_track.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <vector>

namespace composition {

class ICommandHistory;

class ChordTrackImpl : public IChordTrack {
public:
    ChordTrackImpl(ICommandHistory* history);

    ChordID addChord(const ChordEvent& chord) override;
    void removeChord(ChordID id) override;
    void updateChord(ChordID id, const ChordEvent& newChord) override;

    bool getActiveChordAt(uint64_t samplePosition, ChordEvent& outChord) const override;

    void applyDelta(const ProjectDelta& delta, bool isUndo);

private:
    ICommandHistory* history_;
    std::vector<ChordEvent> chords_;
    uint32_t nextIdCounter_ = 0;

    ChordID generateNextId();
};

} // namespace composition
