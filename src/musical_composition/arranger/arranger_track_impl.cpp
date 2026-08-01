#include "arranger_track_impl.h"
#include "arranger_commands.h"
#include "musical_composition/command_history/icommand_history.h"
#include <algorithm>
#include <cstring>

namespace composition {

ArrangerTrackImpl::ArrangerTrackImpl(ICommandHistory* history) : history_(history) {}

void ArrangerTrackImpl::addSection(const Section& section) {
    auto it = std::lower_bound(sections_.begin(), sections_.end(), section,
        [](const Section& a, const Section& b) { return a.startSample < b.startSample; });
    
    // Simple overlap check: if it matches exactly, update. Otherwise insert.
    if (it != sections_.end() && it->startSample == section.startSample) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::ARRANGER;
            delta.operationType = ArrangerOps::UPDATE_SECTION;
            delta.oldStateSize = sizeof(Section);
            std::memcpy(delta.oldState, &(*it), sizeof(Section));
            delta.newStateSize = sizeof(Section);
            std::memcpy(delta.newState, &section, sizeof(Section));
            history_->pushDelta(delta);
        }
        *it = section;
    } else {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::ARRANGER;
            delta.operationType = ArrangerOps::ADD_SECTION;
            delta.newStateSize = sizeof(Section);
            std::memcpy(delta.newState, &section, sizeof(Section));
            history_->pushDelta(delta);
        }
        sections_.insert(it, section);
    }
}

void ArrangerTrackImpl::removeSection(uint64_t startSample) {
    auto it = std::find_if(sections_.begin(), sections_.end(),
        [startSample](const Section& s) { return s.startSample == startSample; });
    
    if (it != sections_.end()) {
        if (history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::ARRANGER;
            delta.operationType = ArrangerOps::REMOVE_SECTION;
            delta.oldStateSize = sizeof(Section);
            std::memcpy(delta.oldState, &(*it), sizeof(Section));
            history_->pushDelta(delta);
        }
        sections_.erase(it);
    }
}

bool ArrangerTrackImpl::getSectionAt(uint64_t samplePosition, Section& outSection) const {
    for (const auto& s : sections_) {
        if (samplePosition >= s.startSample && samplePosition < (s.startSample + s.lengthSamples)) {
            outSection = s;
            return true;
        }
    }
    return false;
}

uint32_t ArrangerTrackImpl::getAllSections(Section* outSections, uint32_t maxSections) const {
    uint32_t count = 0;
    for (const auto& s : sections_) {
        if (count < maxSections) {
            outSections[count] = s;
            count++;
        } else {
            break;
        }
    }
    return count;
}

void ArrangerTrackImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    Section s;
    std::memcpy(&s, isUndo ? delta.oldState : delta.newState, sizeof(Section));
    
    switch (delta.operationType) {
        case ArrangerOps::ADD_SECTION:
            if (isUndo) removeSection(s.startSample);
            else addSection(s);
            break;
        case ArrangerOps::REMOVE_SECTION:
            if (isUndo) addSection(s);
            else removeSection(s.startSample);
            break;
        case ArrangerOps::UPDATE_SECTION:
            addSection(s); // addSection handles update if start matches
            break;
    }
}

} // namespace composition
