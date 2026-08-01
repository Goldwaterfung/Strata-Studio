// src/Middle Bridge/session_manager.h
#pragma once

#include "project/isession_manager.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include <mutex>
#include <vector>

#include "common/system_primitives.h"

namespace Layer2 { class IStringRegistry; class IMutationBridge; }
namespace MediaManagement { class IMediaIntakePipeline; class ICodecFactory; }

namespace bridge {

class SessionManager : public ISessionManager, 
                       public Layer3::IAudioEngine::IMIDIPlayheadRenderer,
                       public IMidiClipDataProvider {
public:
    explicit SessionManager(MediaManagement::IMediaIntakePipeline* intakePipeline = nullptr)
        : intakePipeline_(intakePipeline) {}
    ~SessionManager() override = default;

    // --- IMidiClipDataProvider Interface ---
    uint32_t getNotesInClip(ClipID clipId, MIDINote* outNotes, uint32_t maxNotes) const override;
    uint32_t getCCPointsInClip(ClipID clipId, MIDICCPoint* outPoints, uint32_t maxPoints) const override;
    uint32_t getPitchPointsInClip(ClipID clipId, MIDIPitchPoint* outPoints, uint32_t maxPoints) const override;

    // --- IMIDIPlayheadRenderer Interface ---
    void renderMIDIPlayback(
        uint64_t startSample,
        uint32_t numSamples,
        bool loopEnabled,
        uint64_t loopStart,
        uint64_t loopEnd,
        Layer2::IEventQueue* eventQueue,
        bool isPlaying
    ) override;

    // Prevent copying
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // --- ISessionManager Interface ---
    composition::IProjectSession* getActiveSession() const override;
    void setActiveSession(std::unique_ptr<composition::IProjectSession> newSession) override;
    void closeActiveSession() override;

    void registerChangeListener(ISessionChangeListener* listener) override;
    void unregisterChangeListener(ISessionChangeListener* listener) override;
    void triggerSessionRefresh() override;
    void onTempoMapChanged(Layer2::ITempoService* tempoService) override;
    Layer2::IStringRegistry* getStringRegistry() const override { return stringRegistry_; }


    void setIntakePipeline(MediaManagement::IMediaIntakePipeline* pipeline) {
        intakePipeline_ = pipeline;
    }

    void setAudioEngine(Layer3::IAudioEngine* engine) {
        audioEngine_ = engine;
    }

    void setStringRegistry(Layer2::IStringRegistry* registry) {
        stringRegistry_ = registry;
    }

    void setMutationBridge(Layer2::IMutationBridge* bridge) {
        mutationBridge_ = bridge;
    }

private:
    std::unique_ptr<composition::IProjectSession> activeSession_ = nullptr;
    std::atomic<composition::IProjectSession*> activeSessionRT_{nullptr};
    std::vector<ISessionChangeListener*> listeners_;
    mutable std::recursive_mutex mutex_;
    MediaManagement::IMediaIntakePipeline* intakePipeline_ = nullptr;
    Layer3::IAudioEngine* audioEngine_ = nullptr;
    Layer2::IStringRegistry* stringRegistry_ = nullptr;
    Layer2::IMutationBridge* mutationBridge_ = nullptr;
};

} // namespace bridge
