// src/Core audio engine/transport/transport_impl.cpp
#include "transport_impl.h"
#include "Core infrastructure/state/istate_manager.h"
#include <cstring>

namespace Layer3 {

using namespace Layer2;

//==============================================================================
// CONSTRUCTION/DESTRUCTION
//==============================================================================

TransportImpl::TransportImpl(uint32_t /*sampleRate*/)
    : state(TransportState::STOPPED)
    , positionSample(0)
    , tempoService(nullptr)
    , stateManager(nullptr)
{
    // Initialize loop state
    LoopState loop;
    loop.mode = LoopState::LoopMode::DISABLED;
    loop.startSample = 0;
    loop.endSample = UINT64_MAX;
    loop.crossfadeSamples = 0;
    loopState.store(loop, std::memory_order_release);

    pendingSeekPosition.store(0, std::memory_order_relaxed);
    hasPendingSeek.store(false, std::memory_order_relaxed);

    // Initialize tempo cache with seqlock
    TempoCacheEntry defaultTempo;
    defaultTempo.positionSample = 0;
    defaultTempo.bpm = 120.0;
    defaultTempo.numerator = 4;
    defaultTempo.denominator = 4;
    defaultTempo.bbt = BBTPosition(1, 1, 0);
    tempoCache.sequence.store(0, std::memory_order_release);
    tempoCache.data = defaultTempo;
}

TransportImpl::~TransportImpl() = default;

//==============================================================================
// TRANSPORT CONTROL
//==============================================================================

void TransportImpl::play() {
    if (recordArmed.load(std::memory_order_acquire)) {
        state.store(TransportState::RECORDING, std::memory_order_release);
    } else {
        state.store(TransportState::PLAYING, std::memory_order_release);
    }
}

void TransportImpl::stop() {
    state.store(TransportState::STOPPED, std::memory_order_release);
}

bool TransportImpl::record() {
    bool expectedArm = recordArmed.load(std::memory_order_acquire);
    bool newArm;
    do {
        newArm = !expectedArm;
    } while (!recordArmed.compare_exchange_weak(expectedArm, newArm, std::memory_order_release, std::memory_order_acquire));

    if (newArm) {
        TransportState expectedState = TransportState::PLAYING;
        state.compare_exchange_strong(expectedState, TransportState::RECORDING, std::memory_order_release, std::memory_order_relaxed);
    } else {
        TransportState expectedState = TransportState::RECORDING;
        state.compare_exchange_strong(expectedState, TransportState::PLAYING, std::memory_order_release, std::memory_order_relaxed);
    }
    
    return newArm;
}

bool TransportImpl::isRecordArmed() const {
    return recordArmed.load(std::memory_order_acquire);
}

void TransportImpl::setRecordArmed(bool armed) {
    bool expectedArm = recordArmed.load(std::memory_order_acquire);
    while (expectedArm != armed) {
        if (recordArmed.compare_exchange_weak(expectedArm, armed, std::memory_order_release, std::memory_order_acquire)) {
            if (armed) {
                TransportState expectedState = TransportState::PLAYING;
                state.compare_exchange_strong(expectedState, TransportState::RECORDING, std::memory_order_release, std::memory_order_relaxed);
            } else {
                TransportState expectedState = TransportState::RECORDING;
                state.compare_exchange_strong(expectedState, TransportState::PLAYING, std::memory_order_release, std::memory_order_relaxed);
            }
            break;
        }
    }
}

void TransportImpl::setState(TransportState newState) {
    state.store(newState, std::memory_order_release);
}

TransportState TransportImpl::getState() const {
    return state.load(std::memory_order_acquire);
}

//==============================================================================
// POSITION CONTROL
//==============================================================================

void TransportImpl::seek(uint64_t position, SeekMode mode) {
    if (mode == SeekMode::BUFFER_SYNC || mode == SeekMode::FADE_CROSS) {
        pendingSeekPosition.store(position, std::memory_order_release);
        hasPendingSeek.store(true, std::memory_order_release);
        return;
    }

    positionSample.store(position, std::memory_order_release);
    
    if (mode == SeekMode::FADE_CROSS) {
        // Signal the audio engine to perform a crossfade by setting crossfade samples
        // and jumping the position. The engine will detect the jump and use the crossfade.
        LoopState loop = loopState.load(std::memory_order_relaxed);
        loop.crossfadeSamples = 4410; // Default to 100ms at 44.1kHz (should be parameterizable)
        loopState.store(loop, std::memory_order_release);
    }
}

uint64_t TransportImpl::getPosition() const {
    return positionSample.load(std::memory_order_acquire);
}

TransportPosition TransportImpl::getDetailedPosition() const {
    TransportPosition pos = {}; // Zero-initialize all fields
    pos.positionSample = getPosition();

    // Read from seqlock-protected tempo cache (RT-safe, wait-free)
    TempoCacheEntry tempoData;
    if (tempoCache.read(tempoData)) {
        pos.bpm = tempoData.bpm;
        pos.numerator = tempoData.numerator;
        pos.denominator = tempoData.denominator;
        pos.bar = tempoData.bbt.bar;
        pos.beat = tempoData.bbt.beat;
        pos.tick = tempoData.bbt.tick;
        pos.ticksPerBeat = 960; // Standard DAW resolution
    } else {
        // Fallback defaults if write in progress (extremely rare)
        pos.bpm = 120.0;
        pos.numerator = 4;
        pos.denominator = 4;
        pos.bar = 1;
        pos.beat = 1;
        pos.tick = 0;
        pos.ticksPerBeat = 960; // Standard DAW resolution
    }

    return pos;
}

bool TransportImpl::advancePosition(uint32_t numSamples) {
    if (state.load(std::memory_order_acquire) == TransportState::STOPPED) {
        return false;
    }

    uint64_t currentPos = positionSample.load(std::memory_order_relaxed);

    // Apply pending seek if any
    if (hasPendingSeek.exchange(false, std::memory_order_acq_rel)) {
        currentPos = pendingSeekPosition.load(std::memory_order_acquire);
    }

    uint64_t nextPos = currentPos + numSamples;
    bool looped = false;

    LoopState loop = loopState.load(std::memory_order_relaxed);
    if (loop.isLooping() && nextPos >= loop.endSample) {
        // Calculate wrap-around
        uint64_t overflow = nextPos - loop.endSample;
        nextPos = loop.startSample + overflow;
        looped = true;
    }

    positionSample.store(nextPos, std::memory_order_relaxed);
    return looped;
}

//==============================================================================
// LOOP CONTROL
//==============================================================================

void TransportImpl::setLoopRange(uint64_t start, uint64_t end) {
    LoopState loop = loopState.load(std::memory_order_relaxed);
    loop.startSample = start;
    loop.endSample = end;
    loopState.store(loop, std::memory_order_release);
}

void TransportImpl::setLoopEnabled(bool enabled) {
    LoopState loop = loopState.load(std::memory_order_relaxed);
    loop.mode = enabled ? LoopState::LoopMode::ENABLED : LoopState::LoopMode::DISABLED;
    loopState.store(loop, std::memory_order_release);
}

LoopState TransportImpl::getLoopState() const {
    return loopState.load(std::memory_order_acquire);
}

void TransportImpl::setMetronomeEnabled(bool enabled) {
    metronomeEnabled.store(enabled, std::memory_order_release);
}

bool TransportImpl::isMetronomeEnabled() const {
    return metronomeEnabled.load(std::memory_order_acquire);
}

//==============================================================================
// TEMPO SERVICE INTEGRATION
//==============================================================================

void TransportImpl::setTempoService(ITempoService* service) {
    tempoService = service;
    refreshTempoCache();
}

void TransportImpl::setStateManager(Layer2::IStateManager* manager) {
    stateManager = manager;
}

void TransportImpl::updateTempoCache() {
    refreshTempoCache();
}

void TransportImpl::refreshTempoCache() {
    if (!tempoService) {
        return;
    }

    // Build new tempo entry
    TempoCacheEntry newTempo;
    uint64_t currentPos = getPosition();

    // Query tempo service
    newTempo.bpm = tempoService->getTempoAtPosition(currentPos);

    uint8_t num, den;
    if (tempoService->getMeterAtPosition(currentPos, num, den)) {
        newTempo.numerator = num;
        newTempo.denominator = den;
    } else {
        newTempo.numerator = 4;
        newTempo.denominator = 4;
    }

    newTempo.bbt = tempoService->samplesToBBT(currentPos);
    newTempo.positionSample = currentPos;

    // Write using seqlock (RT-safe for concurrent reads)
    tempoCache.write(newTempo);
}

//==============================================================================
// STATE SNAPSHOTS
//==============================================================================

uint64_t TransportImpl::createTransportSnapshot() const {
    if (!stateManager) {
        return UINT64_MAX;
    }

    // Capture current state in a POD struct
    struct TransportSnapshotData {
        uint64_t position;
        TransportState state;
        LoopState loop;
    };

    TransportSnapshotData data;
    data.position = getPosition();
    data.state = getState();
    data.loop = getLoopState();

    // Register this data with state manager
    uint32_t deltaId = stateManager->registerDeltaData(reinterpret_cast<const uint8_t*>(&data), sizeof(data));
    
    if (deltaId == Layer2::IStateManager::INVALID_DELTA_ID) {
        return UINT64_MAX;
    }

    // Create a snapshot in the state manager
    char desc[64];
    snprintf(desc, sizeof(desc), "Transport at %llu", data.position);
    return stateManager->createSnapshot(desc).id;
}

bool TransportImpl::restoreTransportSnapshot(uint64_t snapshotId) {
    if (!stateManager) {
        return false;
    }

    // Restore the snapshot via state manager
    StateSnapshotID id{snapshotId, 1, 0}; // Generation 1 as default for restoration
    if (!stateManager->restoreSnapshot(id, Layer2::ApplyContext::MAIN_THREAD)) {
        return false;
    }

    // Note: In a full implementation, the StateManager would notify listeners
    // or we would pull the data back here. For now, we'll assume the manager
    // handles the global state and we just need to trigger it.
    
    return true;
}

//==============================================================================
// TIME CONVERSION
//==============================================================================

double TransportImpl::samplesToBeats(uint64_t samples) const {
    if (!tempoService) {
        return 0.0;
    }
    return tempoService->samplesToBeats(samples);
}

uint64_t TransportImpl::beatsToSamples(double beats) const {
    if (!tempoService) {
        return 0;
    }
    return tempoService->beatsToSamples(beats);
}

//==============================================================================
// INTERNAL METHODS
//==============================================================================

BBTPosition TransportImpl::calculateBBT(uint64_t position) const {
    if (!tempoService) {
        return BBTPosition();
    }
    return tempoService->samplesToBBT(position);
}

//==============================================================================
// FACTORY
//==============================================================================

std::unique_ptr<ITransport> ITransport::create(uint32_t sampleRate) {
    return std::make_unique<TransportImpl>(sampleRate);
}

} // namespace Layer3
