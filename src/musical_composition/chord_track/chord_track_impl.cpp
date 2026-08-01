#include "chord_track_impl.h"
#include "chord_commands.h"
#include "musical_composition/command_history/icommand_history.h"
#include <algorithm>
#include <cstring>

namespace composition {

ChordTrackImpl::ChordTrackImpl(ICommandHistory* history) : history_(history) {}

ChordID ChordTrackImpl::addChord(const ChordEvent& chord) {
    ChordID newId = generateNextId();
    ChordEvent c = chord;
    c.id = newId;

    auto it = std::lower_bound(chords_.begin(), chords_.end(), c,
        [](const ChordEvent& a, const ChordEvent& b) { return a.positionSample < b.positionSample; });
    
    // If exact position exists, update. Otherwise insert.
    if (it != chords_.end() && it->positionSample == c.positionSample) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::CHORD_TRACK;
            delta.operationType = ChordOps::UPDATE_CHORD;
            delta.targetId = handleToUint64(it->id);
            delta.oldStateSize = sizeof(ChordEvent);
            std::memcpy(delta.oldState, &(*it), sizeof(ChordEvent));
            delta.newStateSize = sizeof(ChordEvent);
            std::memcpy(delta.newState, &c, sizeof(ChordEvent));
            history_->pushDelta(delta);
        }
        *it = c;
    } else {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::CHORD_TRACK;
            delta.operationType = ChordOps::ADD_CHORD;
            delta.targetId = handleToUint64(newId);
            delta.newStateSize = sizeof(ChordEvent);
            std::memcpy(delta.newState, &c, sizeof(ChordEvent));
            history_->pushDelta(delta);
        }
        chords_.insert(it, c);
    }
    return newId;
}

void ChordTrackImpl::removeChord(ChordID id) {
    auto it = std::find_if(chords_.begin(), chords_.end(),
        [id](const ChordEvent& c) { return c.id == id; });
    
    if (it != chords_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::CHORD_TRACK;
            delta.operationType = ChordOps::REMOVE_CHORD;
            delta.targetId = handleToUint64(id);
            delta.oldStateSize = sizeof(ChordEvent);
            std::memcpy(delta.oldState, &(*it), sizeof(ChordEvent));
            history_->pushDelta(delta);
        }
        chords_.erase(it);
    }
}

void ChordTrackImpl::updateChord(ChordID id, const ChordEvent& newChord) {
    auto it = std::find_if(chords_.begin(), chords_.end(),
        [id](const ChordEvent& c) { return c.id == id; });
    
    if (it != chords_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::CHORD_TRACK;
            delta.operationType = ChordOps::UPDATE_CHORD;
            delta.targetId = handleToUint64(id);
            delta.oldStateSize = sizeof(ChordEvent);
            std::memcpy(delta.oldState, &(*it), sizeof(ChordEvent));
            delta.newStateSize = sizeof(ChordEvent);
            std::memcpy(delta.newState, &newChord, sizeof(ChordEvent));
            history_->pushDelta(delta);
        }
        *it = newChord;
        it->id = id;
    }
}

bool ChordTrackImpl::getActiveChordAt(uint64_t samplePosition, ChordEvent& outChord) const {
    if (chords_.empty()) return false;
    
    // Find first chord starting AFTER samplePosition
    auto it = std::upper_bound(chords_.begin(), chords_.end(), samplePosition,
        [](uint64_t pos, const ChordEvent& c) { return pos < c.positionSample; });
    
    // The active chord is the one before that
    if (it == chords_.begin()) return false;
    
    outChord = *std::prev(it);
    return true;
}

ChordID ChordTrackImpl::generateNextId() {
    return { ++nextIdCounter_, 1 };
}

void ChordTrackImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    ChordEvent c;
    std::memcpy(&c, isUndo ? delta.oldState : delta.newState, sizeof(ChordEvent));
    
    switch (delta.operationType) {
        case ChordOps::ADD_CHORD:
            if (isUndo) removeChord(c.id);
            else addChord(c);
            break;
        case ChordOps::REMOVE_CHORD:
            if (isUndo) addChord(c);
            else removeChord(c.id);
            break;
        case ChordOps::UPDATE_CHORD:
            updateChord(c.id, c);
            break;
    }
}

} // namespace composition
