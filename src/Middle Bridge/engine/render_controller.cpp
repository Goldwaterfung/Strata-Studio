#include "Middle Bridge/engine/render_controller.h"
#include "Media management/export/iexport_service.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "project/isession_manager.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include <cstring>
#include <algorithm>

namespace bridge {

RenderController::RenderController(MediaManagement::IExportService* exportService, Layer2::IStringRegistry* stringRegistry, ISessionManager* sessionManager)
    : m_exportService(exportService)
    , m_stringRegistry(stringRegistry)
    , m_sessionManager(sessionManager)
{
}

RenderController::~RenderController() = default;

void RenderController::startOfflineRender(const RenderConfiguration& config)
{
    if (!m_exportService || !m_stringRegistry) return;

    // Convert RenderConfiguration to MediaManagement::ExportConfig
    MediaManagement::ExportConfig exportConfig{};
    
    // Register output path string
    exportConfig.outputPathId = m_stringRegistry->registerString(config.outputFilePath);
    
    exportConfig.startSample = config.startFrame;
    exportConfig.endSample = config.endFrame;
    exportConfig.sampleRate = config.sampleRate;
    exportConfig.numChannels = 2; // Default to stereo

    // Format
    switch (config.format) {
        case RenderFormat::WAV:
            exportConfig.format = MediaManagement::ExportFormat::WAV;
            break;
        case RenderFormat::FLAC:
            exportConfig.format = MediaManagement::ExportFormat::FLAC;
            break;
        case RenderFormat::MP3:
            exportConfig.format = MediaManagement::ExportFormat::MP3;
            break;
    }

    // Bit depth
    switch (config.bitDepth) {
        case 16:
            exportConfig.bitDepth = MediaManagement::ExportBitDepth::BIT_16;
            break;
        case 24:
            exportConfig.bitDepth = MediaManagement::ExportBitDepth::BIT_24;
            break;
        case 32:
            exportConfig.bitDepth = MediaManagement::ExportBitDepth::BIT_32_FLOAT;
            break;
        default:
            exportConfig.bitDepth = MediaManagement::ExportBitDepth::BIT_24;
            break;
    }

    exportConfig.normalize = false;
    exportConfig.normalizationdB = 0.0f;
    exportConfig.dither = config.enableDither ? MediaManagement::DitherType::TPDF : MediaManagement::DitherType::NONE;
    exportConfig.stemExport = false;
    exportConfig.numStemNodes = 0;

    exportConfig.titleId = 0;
    exportConfig.artistId = 0;
    exportConfig.albumId = 0;
    exportConfig.genreId = 0;
    exportConfig.commentId = 0;

    // Reset state
    m_hasFailed = false;
    m_lastError[0] = '\0';
    m_progress = 0.0f;
    m_statusMsg = "Preparing...";

    // Start async export
    m_activeJobId = m_exportService->exportRangeAsync(exportConfig, &RenderController::onExportCompleted, this);
    m_hasActiveJob = true;
}

bool RenderController::isRenderingActive() const
{
    if (m_exportService) {
        m_exportService->update();
    }
    return m_hasActiveJob;
}

float RenderController::getRenderProgress() const
{
    if (m_exportService) {
        m_exportService->update();
    }

    if (!m_hasActiveJob) {
        return m_hasFailed ? 0.0f : 1.0f;
    }

    MediaManagement::ExportProgress progressStruct{};
    if (m_exportService && m_exportService->getProgress(m_activeJobId, progressStruct)) {
        m_progress = progressStruct.progress;
    }
    return m_progress;
}

const char* RenderController::getRenderStatusMessage() const
{
    if (m_exportService) {
        m_exportService->update();
    }

    if (!m_hasActiveJob) {
        if (m_hasFailed) {
            return "Render failed";
        }
        return "Idle";
    }

    MediaManagement::ExportProgress progressStruct{};
    if (m_exportService && m_exportService->getProgress(m_activeJobId, progressStruct)) {
        switch (progressStruct.status) {
            case MediaManagement::ExportStatus::PENDING:
                m_statusMsg = "Pending...";
                break;
            case MediaManagement::ExportStatus::PREPARING:
                m_statusMsg = "Preparing render...";
                break;
            case MediaManagement::ExportStatus::PROCESSING:
                m_statusMsg = "Rendering audio blocks synchronously...";
                break;
            case MediaManagement::ExportStatus::FINALIZING:
                m_statusMsg = "Writing output file...";
                break;
            case MediaManagement::ExportStatus::COMPLETED:
                m_statusMsg = "Render complete";
                break;
            case MediaManagement::ExportStatus::FAILED:
                m_statusMsg = "Render failed";
                break;
            case MediaManagement::ExportStatus::CANCELLED:
                m_statusMsg = "Render cancelled";
                break;
        }
    }
    return m_statusMsg.c_str();
}

void RenderController::cancelOfflineRender()
{
    if (m_exportService && m_hasActiveJob) {
        m_exportService->cancelExport(m_activeJobId);
        m_hasActiveJob = false;
    }
    if (m_audioEngine) {
        m_audioEngine->setOfflineExportActive(false);
    }
}

bool RenderController::hasFailed(char* outError, uint32_t maxLen) const
{
    if (m_exportService) {
        m_exportService->update();
    }

    if (m_hasFailed) {
        std::strncpy(outError, m_lastError, maxLen - 1);
        outError[maxLen - 1] = '\0';
        return true;
    }
    return false;
}

void RenderController::onExportCompleted(uint64_t jobId, bool success, const char* error, void* context)
{
    auto* self = static_cast<RenderController*>(context);
    if (self && self->m_activeJobId == jobId) {
        self->m_hasActiveJob = false;
        if (self->m_audioEngine) {
            self->m_audioEngine->setOfflineExportActive(false);
        }
        if (!success) {
            self->m_hasFailed = true;
            if (error) {
                std::strncpy(self->m_lastError, error, sizeof(self->m_lastError) - 1);
                self->m_lastError[sizeof(self->m_lastError) - 1] = '\0';
            } else {
                std::strcpy(self->m_lastError, "Export failed");
            }
        }
    }
}

void RenderController::startSilentMixAnalysis(uint64_t startFrame, uint64_t endFrame, uint32_t sampleRate, uint32_t isolateTrackId) {
    if (!m_exportService) return;

    m_hasFailed = false;
    m_lastError[0] = '\0';
    m_progress = 0.0f;
    m_statusMsg = "Preparing analysis...";

    if (m_audioEngine) {
        m_audioEngine->setOfflineExportActive(true);
    }

    m_activeJobId = m_exportService->analyzeSessionLoudnessAsync(
        startFrame,
        endFrame,
        sampleRate,
        2, // Stereo
        isolateTrackId,
        &RenderController::onAnalysisCompleted,
        this
    );
    m_hasActiveJob = true;
}

void RenderController::onAnalysisCompleted(
    uint64_t jobId,
    bool success,
    const MediaManagement::IExportService::AnalysisResult& result,
    const char* error,
    void* context
) {
    auto* self = static_cast<RenderController*>(context);
    if (self && self->m_activeJobId == jobId) {
        self->m_hasActiveJob = false;
        if (self->m_audioEngine) {
            self->m_audioEngine->setOfflineExportActive(false);
        }
        if (success) {
            if (self->m_sessionManager) {
                if (auto* session = self->m_sessionManager->getActiveSession()) {
                    composition::MixStatistics stats{};
                    stats.isAnalyzed = true;
                    stats.integratedLoudnessLUFS = result.integratedLoudnessLUFS;
                    stats.truePeakDBTP = result.truePeakDBTP;
                    stats.clippingDetected = result.clippingDetected;
                    stats.samplePeakDBFS = result.samplePeakDBFS;
                    stats.midRmsDbfs = result.midRmsDbfs;
                    stats.sideRmsDbfs = result.sideRmsDbfs;
                    stats.msRatioDb = result.msRatioDb;
                    stats.stereoWidthPct = result.stereoWidthPct;
                    stats.monoFoldLossDb = result.monoFoldLossDb;
                    stats.stereoCorrelation = result.stereoCorrelation;
                    session->setMixStatistics(stats);
                }
            }
        } else {
            self->m_hasFailed = true;
            if (error) {
                std::strncpy(self->m_lastError, error, sizeof(self->m_lastError) - 1);
                self->m_lastError[sizeof(self->m_lastError) - 1] = '\0';
            } else {
                std::strcpy(self->m_lastError, "Analysis failed");
            }
        }
    }
}

bool RenderController::renderTrackToBufferSync(uint32_t trackId, uint64_t startFrame, uint64_t endFrame, uint32_t sampleRate, std::vector<float>& outBuffer) {
    if (!m_exportService) return false;
    return m_exportService->renderTrackToBufferSync(trackId, startFrame, endFrame, sampleRate, outBuffer);
}

void RenderController::setAudioEngine(Layer3::IAudioEngine* engine) {
    m_audioEngine = engine;
}

} // namespace bridge
