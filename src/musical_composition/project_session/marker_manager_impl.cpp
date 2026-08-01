#include "marker_manager_impl.h"
#include "musical_composition/command_history/timeline_commands.h"
#include <algorithm>
#include <cstring>
#include <random>

namespace composition {

MarkerManagerImpl::MarkerManagerImpl(ICommandHistory* history)
    : history_(history)
{
}

MarkerUUID MarkerManagerImpl::generateUUID() const
{
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    
    MarkerUUID uuid{};
    uint64_t* ptr = reinterpret_cast<uint64_t*>(uuid.bytes);
    ptr[0] = gen();
    ptr[1] = gen();
    
    // Set UUID v4 bits: version 4, variant 1 (RFC 4122)
    uuid.bytes[6] = (uuid.bytes[6] & 0x0F) | 0x40; // version 4
    uuid.bytes[8] = (uuid.bytes[8] & 0x3F) | 0x80; // variant 1 (8, 9, A, B)
    
    return uuid;
}

void MarkerManagerImpl::sortAndReindexMarkers()
{
    std::sort(markers_.begin(), markers_.end(), [](const MarkerInfo& a, const MarkerInfo& b) {
        return a.framePosition < b.framePosition;
    });
    for (uint32_t i = 0; i < markers_.size(); ++i) {
        markers_[i].markerNumber = i + 1;
    }
}

MarkerUUID MarkerManagerImpl::addMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB, bool pushDelta)
{
    std::lock_guard<std::mutex> lock(mutex_);

    MarkerUUID finalUuid = uuid;
    if (finalUuid.isZero()) {
        finalUuid = generateUUID();
    }

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MARKER_TRACK;
        delta.operationType = MarkerOps::ADD_MARKER;
        delta.targetId = 0; // Global timeline target

        MarkerPayload newPayload{};
        newPayload.uuid = finalUuid;
        newPayload.framePosition = framePosition;
        std::strncpy(newPayload.label, label ? label : "", MAX_NAME_LENGTH - 1);
        newPayload.label[MAX_NAME_LENGTH - 1] = '\0';
        newPayload.colorARGB = colorARGB;

        delta.newStateSize = sizeof(MarkerPayload);
        std::memcpy(delta.newState, &newPayload, sizeof(MarkerPayload));

        // Check for existing marker to populate oldState
        bool found = false;
        for (const auto& marker : markers_) {
            if (marker.uuid == finalUuid) {
                MarkerPayload oldPayload{};
                oldPayload.uuid = marker.uuid;
                oldPayload.framePosition = marker.framePosition;
                std::strncpy(oldPayload.label, marker.label, MAX_NAME_LENGTH - 1);
                oldPayload.label[MAX_NAME_LENGTH - 1] = '\0';
                oldPayload.colorARGB = marker.colorARGB;

                delta.oldStateSize = sizeof(MarkerPayload);
                std::memcpy(delta.oldState, &oldPayload, sizeof(MarkerPayload));
                found = true;
                break;
            }
        }

        if (!found) {
            delta.oldStateSize = 0;
        }

        history_->pushDelta(delta);
    }

    addMarkerInternal(finalUuid, framePosition, label, colorARGB);
    return finalUuid;
}

void MarkerManagerImpl::removeMarker(const MarkerUUID& uuid, bool pushDelta)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (pushDelta && history_) {
        bool found = false;
        MarkerPayload oldPayload{};

        for (const auto& marker : markers_) {
            if (marker.uuid == uuid) {
                oldPayload.uuid = marker.uuid;
                oldPayload.framePosition = marker.framePosition;
                std::strncpy(oldPayload.label, marker.label, MAX_NAME_LENGTH - 1);
                oldPayload.label[MAX_NAME_LENGTH - 1] = '\0';
                oldPayload.colorARGB = marker.colorARGB;
                found = true;
                break;
            }
        }

        if (found) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::MARKER_TRACK;
            delta.operationType = MarkerOps::REMOVE_MARKER;
            delta.targetId = 0;

            delta.oldStateSize = sizeof(MarkerPayload);
            std::memcpy(delta.oldState, &oldPayload, sizeof(MarkerPayload));
            delta.newStateSize = 0;

            history_->pushDelta(delta);
        }
    }

    removeMarkerInternal(uuid);
}

void MarkerManagerImpl::updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB, bool pushDelta)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (pushDelta && history_) {
        bool found = false;
        MarkerPayload oldPayload{};
        for (const auto& marker : markers_) {
            if (marker.uuid == uuid) {
                oldPayload.uuid = marker.uuid;
                oldPayload.framePosition = marker.framePosition;
                std::strncpy(oldPayload.label, marker.label, MAX_NAME_LENGTH - 1);
                oldPayload.label[MAX_NAME_LENGTH - 1] = '\0';
                oldPayload.colorARGB = marker.colorARGB;
                found = true;
                break;
            }
        }

        if (found) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::MARKER_TRACK;
            delta.operationType = MarkerOps::UPDATE_MARKER;
            delta.targetId = 0;

            delta.oldStateSize = sizeof(MarkerPayload);
            std::memcpy(delta.oldState, &oldPayload, sizeof(MarkerPayload));

            MarkerPayload newPayload{};
            newPayload.uuid = uuid;
            newPayload.framePosition = framePosition;
            std::strncpy(newPayload.label, label ? label : "", MAX_NAME_LENGTH - 1);
            newPayload.label[MAX_NAME_LENGTH - 1] = '\0';
            newPayload.colorARGB = colorARGB;

            delta.newStateSize = sizeof(MarkerPayload);
            std::memcpy(delta.newState, &newPayload, sizeof(MarkerPayload));

            history_->pushDelta(delta);
        }
    }

    updateMarkerInternal(uuid, framePosition, label, colorARGB);
}

uint32_t MarkerManagerImpl::getMarkersInRange(uint64_t startFrame, uint64_t endFrame,
                                              MarkerInfo* outMarkers, uint32_t maxCount) const
{
    if (!outMarkers || maxCount == 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);

    // Binary search for first marker >= startFrame
    auto it = std::lower_bound(markers_.begin(), markers_.end(), startFrame,
        [](const MarkerInfo& marker, uint64_t val) {
            return marker.framePosition < val;
        });

    uint32_t count = 0;
    while (it != markers_.end() && it->framePosition <= endFrame && count < maxCount) {
        outMarkers[count++] = *it;
        ++it;
    }

    return count;
}

void MarkerManagerImpl::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    markers_.clear();
}

void MarkerManagerImpl::applyDelta(const ProjectDelta& delta, bool isUndo)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (delta.operationType == MarkerOps::ADD_MARKER) {
        if (isUndo) {
            if (delta.oldStateSize > 0) {
                MarkerPayload oldPayload{};
                std::memcpy(&oldPayload, delta.oldState, sizeof(MarkerPayload));
                addMarkerInternal(oldPayload.uuid, oldPayload.framePosition, oldPayload.label, oldPayload.colorARGB);
            } else {
                MarkerPayload newPayload{};
                std::memcpy(&newPayload, delta.newState, sizeof(MarkerPayload));
                removeMarkerInternal(newPayload.uuid);
            }
        } else {
            MarkerPayload newPayload{};
            std::memcpy(&newPayload, delta.newState, sizeof(MarkerPayload));
            addMarkerInternal(newPayload.uuid, newPayload.framePosition, newPayload.label, newPayload.colorARGB);
        }
    }
    else if (delta.operationType == MarkerOps::REMOVE_MARKER) {
        if (isUndo) {
            MarkerPayload oldPayload{};
            std::memcpy(&oldPayload, delta.oldState, sizeof(MarkerPayload));
            addMarkerInternal(oldPayload.uuid, oldPayload.framePosition, oldPayload.label, oldPayload.colorARGB);
        } else {
            MarkerPayload oldPayload{};
            std::memcpy(&oldPayload, delta.oldState, sizeof(MarkerPayload));
            removeMarkerInternal(oldPayload.uuid);
        }
    }
    else if (delta.operationType == MarkerOps::UPDATE_MARKER) {
        MarkerPayload payload{};
        if (isUndo) {
            std::memcpy(&payload, delta.oldState, sizeof(MarkerPayload));
        } else {
            std::memcpy(&payload, delta.newState, sizeof(MarkerPayload));
        }
        updateMarkerInternal(payload.uuid, payload.framePosition, payload.label, payload.colorARGB);
    }
}

void MarkerManagerImpl::setMarkersDirect(const std::vector<MarkerInfo>& markers)
{
    std::lock_guard<std::mutex> lock(mutex_);
    markers_ = markers;
    sortAndReindexMarkers();
}

void MarkerManagerImpl::addMarkerInternal(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB)
{
    // Replace duplicate UUID if exists
    for (auto& marker : markers_) {
        if (marker.uuid == uuid) {
            marker.framePosition = framePosition;
            std::strncpy(marker.label, label ? label : "", MAX_NAME_LENGTH - 1);
            marker.label[MAX_NAME_LENGTH - 1] = '\0';
            marker.colorARGB = colorARGB;
            sortAndReindexMarkers();
            return;
        }
    }

    // Insert new
    MarkerInfo info{};
    info.uuid = uuid;
    info.framePosition = framePosition;
    std::strncpy(info.label, label ? label : "", MAX_NAME_LENGTH - 1);
    info.label[MAX_NAME_LENGTH - 1] = '\0';
    info.colorARGB = colorARGB;

    markers_.push_back(info);
    sortAndReindexMarkers();
}

void MarkerManagerImpl::removeMarkerInternal(const MarkerUUID& uuid)
{
    markers_.erase(
        std::remove_if(markers_.begin(), markers_.end(),
            [&uuid](const MarkerInfo& m) {
                return m.uuid == uuid;
            }),
        markers_.end());
    sortAndReindexMarkers();
}

void MarkerManagerImpl::updateMarkerInternal(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB)
{
    for (auto& marker : markers_) {
        if (marker.uuid == uuid) {
            marker.framePosition = framePosition;
            std::strncpy(marker.label, label ? label : "", MAX_NAME_LENGTH - 1);
            marker.label[MAX_NAME_LENGTH - 1] = '\0';
            marker.colorARGB = colorARGB;
            break;
        }
    }
    sortAndReindexMarkers();
}

} // namespace composition
