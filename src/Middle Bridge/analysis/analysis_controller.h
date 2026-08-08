#pragma once
#include "ianalysis_controller.h"

namespace MediaManagement {
class IAudioAnalysisEngine;
class IMediaRegistry;
}

namespace Layer2 {
class IStringRegistry;
}

namespace bridge {

class IHardwareSettingsFacade;
class ISessionManager;
class IMeteringProvider;
class IRenderController;
class IProjectLifecycleController;
class ITimelineController;
class IArrangementController;

class AnalysisController : public IAnalysisController {
public:
    explicit AnalysisController(MediaManagement::IAudioAnalysisEngine* analysisEngine = nullptr,
                                IHardwareSettingsFacade* hardwareSettings = nullptr);

    void setAnalysisEngine(MediaManagement::IAudioAnalysisEngine* engine);
    void setHardwareSettings(IHardwareSettingsFacade* hardwareSettings);
    void setMediaRegistry(MediaManagement::IMediaRegistry* mediaRegistry);
    void setSessionManager(ISessionManager* sessionManager);
    void setMeteringProvider(IMeteringProvider* meteringProvider);
    void setStringRegistry(Layer2::IStringRegistry* stringRegistry);
    void setRenderController(IRenderController* renderController);
    void setLifecycleController(IProjectLifecycleController* lifecycleController);
    void setTimelineController(ITimelineController* timelineController);
    void setArrangementController(IArrangementController* arrangementController);

    MaskingAnalysisResult computeMasking(uint32_t primaryTrackId, uint32_t vsTrackId) override;
    ResonanceAnalysisResult computeResonances(uint32_t trackId) override;
    PhaseMatrixAnalysisResult computePhaseMatrix(const std::vector<uint32_t>& trackIds) override;
    WindowTelemetryAnalysisResult getWindowTelemetry(uint32_t trackId, const std::string& startPos, const std::string& durPos) override;
    PhaseAlignAnalysisResult computePhaseAlign(uint32_t trackA, uint32_t trackB) override;
    SpectrumAnalysisResult computeSpectrum(uint32_t trackId) override;
    LoudnessAnalysisResult computeLoudness(uint32_t trackId) override;
    TruePeakAnalysisResult computeTruePeak(uint32_t trackId) override;
    StereoWidthAnalysisResult computeStereoWidth(uint32_t trackId) override;

private:
    MediaManagement::IAudioAnalysisEngine* m_analysisEngine{nullptr};
    IHardwareSettingsFacade* m_hardwareSettings{nullptr};
    MediaManagement::IMediaRegistry* m_mediaRegistry{nullptr};
    ISessionManager* m_sessionManager{nullptr};
    IMeteringProvider* m_meteringProvider{nullptr};
    Layer2::IStringRegistry* m_stringRegistry{nullptr};
    IRenderController* m_renderController{nullptr};
    IProjectLifecycleController* m_lifecycleController{nullptr};
    ITimelineController* m_timelineController{nullptr};
    IArrangementController* m_arrangementController{nullptr};
};

} // namespace bridge
