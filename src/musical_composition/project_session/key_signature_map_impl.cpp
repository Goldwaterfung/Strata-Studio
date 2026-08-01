#include "key_signature_map_impl.h"
#include <algorithm>
#include <cstring>

namespace composition {

namespace KeySignatureMapOps {
    constexpr uint32_t ADD_KEY_SIGNATURE = 0;
    constexpr uint32_t REMOVE_KEY_SIGNATURE = 1;
}

KeySignatureMapImpl::KeySignatureMapImpl(ICommandHistory* history)
    : history_(history) {
}

void KeySignatureMapImpl::addKeySignature(uint64_t positionSample, PitchClass root, KeyType type, bool pushDelta) {
    KeySignaturePoint oldPt{};
    bool hasOld = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::lower_bound(events_.begin(), events_.end(), positionSample,
            [](const KeySignaturePoint& pt, uint64_t pos) {
                return pt.positionSample < pos;
            });
        if (it != events_.end() && it->positionSample == positionSample) {
            oldPt = *it;
            hasOld = true;
        }
    }

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::KEY_SIGNATURE_MAP;
        delta.operationType = KeySignatureMapOps::ADD_KEY_SIGNATURE;
        delta.targetId = positionSample;

        KeySignaturePoint newPt{};
        newPt.positionSample = positionSample;
        newPt.rootNote = root;
        newPt.type = type;

        delta.newStateSize = sizeof(KeySignaturePoint);
        std::memcpy(delta.newState, &newPt, sizeof(KeySignaturePoint));

        if (hasOld) {
            delta.oldStateSize = sizeof(KeySignaturePoint);
            std::memcpy(delta.oldState, &oldPt, sizeof(KeySignaturePoint));
        } else {
            delta.oldStateSize = 0;
        }
        history_->pushDelta(delta);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        addKeySignatureInternal(positionSample, root, type);
    }
}

void KeySignatureMapImpl::removeKeySignature(uint64_t positionSample, bool pushDelta) {
    KeySignaturePoint oldPt{};
    bool hasOld = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::lower_bound(events_.begin(), events_.end(), positionSample,
            [](const KeySignaturePoint& pt, uint64_t pos) {
                return pt.positionSample < pos;
            });
        if (it != events_.end() && it->positionSample == positionSample) {
            oldPt = *it;
            hasOld = true;
        }
    }

    if (!hasOld) return;

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::KEY_SIGNATURE_MAP;
        delta.operationType = KeySignatureMapOps::REMOVE_KEY_SIGNATURE;
        delta.targetId = positionSample;

        delta.newStateSize = 0;
        delta.oldStateSize = sizeof(KeySignaturePoint);
        std::memcpy(delta.oldState, &oldPt, sizeof(KeySignaturePoint));

        history_->pushDelta(delta);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        removeKeySignatureInternal(positionSample);
    }
}

bool KeySignatureMapImpl::getKeySignatureAt(uint64_t positionSample, PitchClass& outRoot, KeyType& outType) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (events_.empty()) {
        return false;
    }
    
    auto it = std::upper_bound(events_.begin(), events_.end(), positionSample,
        [](uint64_t pos, const KeySignaturePoint& pt) {
            return pos < pt.positionSample;
        });
        
    if (it == events_.begin()) {
        return false;
    }
    
    const auto& pt = *(it - 1);
    outRoot = pt.rootNote;
    outType = pt.type;
    return true;
}

void KeySignatureMapImpl::setEvents(const std::vector<KeySignaturePoint>& events) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_ = events;
    std::sort(events_.begin(), events_.end(), [](const KeySignaturePoint& a, const KeySignaturePoint& b) {
        return a.positionSample < b.positionSample;
    });
}

void KeySignatureMapImpl::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
}

void KeySignatureMapImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (delta.operationType == KeySignatureMapOps::ADD_KEY_SIGNATURE) {
        if (isUndo) {
            if (delta.oldStateSize == sizeof(KeySignaturePoint)) {
                KeySignaturePoint oldPt{};
                std::memcpy(&oldPt, delta.oldState, sizeof(KeySignaturePoint));
                addKeySignatureInternal(oldPt.positionSample, oldPt.rootNote, oldPt.type);
            } else {
                KeySignaturePoint newPt{};
                std::memcpy(&newPt, delta.newState, sizeof(KeySignaturePoint));
                removeKeySignatureInternal(newPt.positionSample);
            }
        } else {
            KeySignaturePoint newPt{};
            std::memcpy(&newPt, delta.newState, sizeof(KeySignaturePoint));
            addKeySignatureInternal(newPt.positionSample, newPt.rootNote, newPt.type);
        }
    } else if (delta.operationType == KeySignatureMapOps::REMOVE_KEY_SIGNATURE) {
        if (isUndo) {
            KeySignaturePoint oldPt{};
            std::memcpy(&oldPt, delta.oldState, sizeof(KeySignaturePoint));
            addKeySignatureInternal(oldPt.positionSample, oldPt.rootNote, oldPt.type);
        } else {
            KeySignaturePoint oldPt{};
            std::memcpy(&oldPt, delta.oldState, sizeof(KeySignaturePoint));
            removeKeySignatureInternal(oldPt.positionSample);
        }
    }
}

void KeySignatureMapImpl::addKeySignatureInternal(uint64_t positionSample, PitchClass root, KeyType type) {
    auto it = std::lower_bound(events_.begin(), events_.end(), positionSample,
        [](const KeySignaturePoint& pt, uint64_t pos) {
            return pt.positionSample < pos;
        });
    if (it != events_.end() && it->positionSample == positionSample) {
        it->rootNote = root;
        it->type = type;
    } else {
        KeySignaturePoint pt{};
        pt.positionSample = positionSample;
        pt.rootNote = root;
        pt.type = type;
        events_.insert(it, pt);
    }
}

void KeySignatureMapImpl::removeKeySignatureInternal(uint64_t positionSample) {
    auto it = std::lower_bound(events_.begin(), events_.end(), positionSample,
        [](const KeySignaturePoint& pt, uint64_t pos) {
            return pt.positionSample < pos;
        });
    if (it != events_.end() && it->positionSample == positionSample) {
        events_.erase(it);
    }
}

} // namespace composition
