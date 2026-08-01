// src/Middle Bridge/session_manager.cpp
#include "project/session_manager.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/playlist/iplaylist.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "Media management/intake/imedia_intake_pipeline.h"
#include "Media management/recording/idisk_writer_service.h"
#include "Media management/codecs/icodec_factory.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
namespace bridge {

composition::IProjectSession* SessionManager::getActiveSession() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return activeSession_.get();
}

void SessionManager::setActiveSession(std::unique_ptr<composition::IProjectSession> newSession) {
    // 1. Notify observers that session is changing (teardown phase)
    std::vector<ISessionChangeListener*> listenersCopy;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        listenersCopy = listeners_;
    }

    for (auto* listener : listenersCopy) {
        if (listener) {
            listener->onSessionChanging();
        }
    }

    // 2. Perform the swap under lock
    std::unique_ptr<composition::IProjectSession> oldSession;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        oldSession = std::move(activeSession_);
        activeSession_ = std::move(newSession);
        activeSessionRT_.store(activeSession_.get(), std::memory_order_release);
    }

    // 3. Notify observers of the new active session (setup phase)
    composition::IProjectSession* sessionPtr = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        sessionPtr = activeSession_.get();
        listenersCopy = listeners_;
    }

    for (auto* listener : listenersCopy) {
        if (listener) {
            listener->onSessionChanged(sessionPtr);
        }
    }
    
    // oldSession goes out of scope and is destroyed here, outside the mutex lock
}

void SessionManager::closeActiveSession() {
    setActiveSession(nullptr);
}

void SessionManager::registerChangeListener(ISessionChangeListener* listener) {
    if (!listener) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void SessionManager::unregisterChangeListener(ISessionChangeListener* listener) {
    if (!listener) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::remove(listeners_.begin(), listeners_.end(), listener);
    if (it != listeners_.end()) {
        listeners_.erase(it, listeners_.end());
    }
}

void SessionManager::renderMIDIPlayback(
    uint64_t startSample,
    uint32_t numSamples,
    bool loopEnabled,
    uint64_t loopStart,
    uint64_t loopEnd,
    Layer2::IEventQueue* eventQueue,
    bool isPlaying
) {
    auto* session = activeSessionRT_.load(std::memory_order_acquire);
    if (!session) return;
    
    auto* trackManager = session->getTrackManager();
    if (!trackManager) return;
    
    trackManager->renderMIDIPlayback(startSample, numSamples, loopEnabled, loopStart, loopEnd, eventQueue, isPlaying);
}

uint32_t SessionManager::getNotesInClip(ClipID clipId, MIDINote* outNotes, uint32_t maxNotes) const {
    auto* session = activeSessionRT_.load(std::memory_order_acquire);
    if (!session) return 0;
    
    auto* trackManager = session->getTrackManager();
    if (!trackManager) return 0;
    
    if (auto* provider = dynamic_cast<const IMidiClipDataProvider*>(trackManager)) {
        return provider->getNotesInClip(clipId, outNotes, maxNotes);
    }
    return 0;
}

uint32_t SessionManager::getCCPointsInClip(ClipID clipId, MIDICCPoint* outPoints, uint32_t maxPoints) const {
    auto* session = activeSessionRT_.load(std::memory_order_acquire);
    if (!session) return 0;
    
    auto* trackManager = session->getTrackManager();
    if (!trackManager) return 0;
    
    if (auto* provider = dynamic_cast<const IMidiClipDataProvider*>(trackManager)) {
        return provider->getCCPointsInClip(clipId, outPoints, maxPoints);
    }
    return 0;
}

uint32_t SessionManager::getPitchPointsInClip(ClipID clipId, MIDIPitchPoint* outPoints, uint32_t maxPoints) const {
    auto* session = activeSessionRT_.load(std::memory_order_acquire);
    if (!session) return 0;
    
    auto* trackManager = session->getTrackManager();
    if (!trackManager) return 0;
    
    if (auto* provider = dynamic_cast<const IMidiClipDataProvider*>(trackManager)) {
        return provider->getPitchPointsInClip(clipId, outPoints, maxPoints);
    }
    return 0;
}

void SessionManager::triggerSessionRefresh() {
    std::vector<ISessionChangeListener*> listenersCopy;
    composition::IProjectSession* sessionPtr = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        listenersCopy = listeners_;
        sessionPtr = activeSession_.get();
    }

    for (auto* listener : listenersCopy) {
        if (listener) {
            listener->onSessionChanging();
        }
    }

    for (auto* listener : listenersCopy) {
        if (listener) {
            listener->onSessionChanged(sessionPtr);
        }
    }
}

void SessionManager::onTempoMapChanged(Layer2::ITempoService* tempoService) {
    // Use the RT-atomic pointer for safe cross-thread access
    auto* session = activeSessionRT_.load(std::memory_order_acquire);
    if (!session) return;

    auto* trackManager = session->getTrackManager();
    if (!trackManager) return;

    // Delegate the full cache-invalidation walk to the TrackManager
    trackManager->recalculateTimeCaches(tempoService);
}



} // namespace bridge
