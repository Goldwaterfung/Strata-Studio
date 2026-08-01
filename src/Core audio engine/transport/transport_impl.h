// src/Core audio engine/transport/transport_impl.h
#pragma once

#include "itransport.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include <atomic>

namespace Layer3 {

//==============================================================================
// INTERNAL IMPLEMENTATION
//==============================================================================

// Tempo cache entry (double-buffered or single-buffered with seqlock)
struct TempoCacheEntry {
    uint64_t positionSample;
    double bpm;
    uint8_t numerator;
    uint8_t denominator;
    Layer2::BBTPosition bbt;
};

// Tempo cache entry using seqlock for lock-free reads
struct TempoCache {
    std::atomic<uint64_t> sequence;  // Seqlock counter
    TempoCacheEntry data;

    // Non-RT writer: Updates tempo data safely
    void write(const TempoCacheEntry& newData) {
        uint64_t seq = sequence.load(std::memory_order_relaxed);
        sequence.store(seq + 1, std::memory_order_release);  // Mark as writing

        // Write new data
        data = newData;

        std::atomic_thread_fence(std::memory_order_release);
        sequence.store(seq + 2, std::memory_order_release);  // Mark as readable
    }

    // RT reader: Reads tempo data safely (wait-free)
    bool read(TempoCacheEntry& outData) const {
        uint64_t seq1, seq2;

        do {
            seq1 = sequence.load(std::memory_order_acquire);
            if (seq1 & 1) return false;  // Writer is active

            std::atomic_thread_fence(std::memory_order_acquire);
            outData = data;
            std::atomic_thread_fence(std::memory_order_acquire);

            seq2 = sequence.load(std::memory_order_acquire);
        } while (seq1 != seq2);  // Retry if write occurred during read

        return true;
    }
};

// Transport implementation
class TransportImpl : public ITransport {
public:
    //==========================================================================
    // Construction/Destruction
    //==========================================================================

    explicit TransportImpl(uint32_t sampleRate);
    ~TransportImpl() override;

    //==========================================================================
    // ITransport Implementation
    //==========================================================================

    void play() override;
    void stop() override;
    bool record() override;
    bool isRecordArmed() const override;
    void setRecordArmed(bool armed) override;
    void setState(TransportState state) override;
    TransportState getState() const override;

    void seek(uint64_t position, SeekMode mode) override;
    uint64_t getPosition() const override;
    TransportPosition getDetailedPosition() const override;
    bool advancePosition(uint32_t numSamples) override;

    void setLoopRange(uint64_t start, uint64_t end) override;
    void setLoopEnabled(bool enabled) override;
    LoopState getLoopState() const override;

    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;

    void setTempoService(Layer2::ITempoService* tempoService) override;
    void setStateManager(Layer2::IStateManager* stateManager) override;
    void updateTempoCache() override;

    uint64_t createTransportSnapshot() const override;
    bool restoreTransportSnapshot(uint64_t snapshotId) override;

    double samplesToBeats(uint64_t samples) const override;
    uint64_t beatsToSamples(double beats) const override;

private:
    //==========================================================================
    // Internal State
    //==========================================================================

    // Transport state (atomic for RT-safe access)
    std::atomic<TransportState> state;
    std::atomic<uint64_t> positionSample;
    std::atomic<bool> recordArmed{false};

    // Loop state (atomic for RT-safe access)
    std::atomic<LoopState> loopState;

    // Metronome state (atomic for RT-safe access)
    std::atomic<bool> metronomeEnabled{false};

    // Pending seek state
    std::atomic<uint64_t> pendingSeekPosition;
    std::atomic<bool> hasPendingSeek;

    // Service pointers (not owned)
    Layer2::ITempoService* tempoService;
    Layer2::IStateManager* stateManager;

    // Tempo cache using seqlock (lock-free, wait-free reads)
    TempoCache tempoCache;

    //==========================================================================
    // Internal Methods
    //==========================================================================

    // Update tempo cache from ITempoService (non-RT)
    void refreshTempoCache();

    // Calculate BBT position from sample position
    Layer2::BBTPosition calculateBBT(uint64_t position) const;
};

} // namespace Layer3
