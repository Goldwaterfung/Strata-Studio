#pragma once

#include "export_primitives.h"
#include <memory>
#include <cstdint>

namespace Layer2 { class IStringRegistry; }
namespace Layer3 { class IDSPKernel; }

namespace MediaManagement {
class IMediaRegistry;

/**
 * @brief Interface for the Export Service.
 * 
 * Orchestrates offline rendering of the project.
 */
class IExportService {
public:
    virtual ~IExportService() = default;

    /**
     * @brief Signature for export completion callback.
     * Guaranteed to be allocation-free.
     */
    using CompletionCallback = void (*)(uint64_t jobId, bool success, const char* error, void* context);

    /**
     * @brief Start an asynchronous export.
     * 
     * @param config Export parameters.
     * @param callback Function to call when finished.
     * @param context User data passed to the callback.
     * @return jobId for tracking.
     */
    virtual uint64_t exportRangeAsync(const ExportConfig& config, 
                                     CompletionCallback callback,
                                     void* context) = 0;

    /**
     * @brief Results from the silent loudness and peak analysis.
     */
    struct AnalysisResult {
        float integratedLoudnessLUFS = 0.0f;
        float truePeakDBTP = 0.0f;
        bool clippingDetected = false;
    };

    /**
     * @brief Signature for loudness analysis completion callback.
     */
    using AnalysisCallback = void (*)(uint64_t jobId, bool success, const AnalysisResult& result, const char* error, void* context);

    /**
     * @brief Start an asynchronous silent mix analysis.
     */
    virtual uint64_t analyzeSessionLoudnessAsync(uint64_t startSample,
                                                 uint64_t endSample,
                                                 uint32_t sampleRate,
                                                 uint16_t numChannels,
                                                 AnalysisCallback callback,
                                                 void* context) = 0;

    /**
     * @brief Poll for progress of an ongoing job.
     */
    virtual bool getProgress(uint64_t jobId, ExportProgress& outProgress) const = 0;

    /**
     * @brief Cancel an ongoing export.
     */
    virtual bool cancelExport(uint64_t jobId) = 0;

    /**
     * @brief Service tick called from Main Thread.
     */
    virtual void update() = 0;

    //=== Capability Queries ===//

    /**
     * @brief Check if a file format is supported by the current system codecs.
     */
    virtual bool isFormatSupported(ExportFormat format) const = 0;

    /**
     * @brief Get list of supported sample rates.
     * @param rates Pointer to array to fill.
     * @param count In: Capacity of rates array, Out: Number of rates written.
     */
    virtual void getSupportedSampleRates(uint32_t* rates, uint32_t* count) const = 0;

    /**
     * @brief Get list of supported bit depths for a specific format.
     */
    virtual void getSupportedBitDepths(ExportFormat format, ExportBitDepth* depths, uint32_t* count) const = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IExportService> create(IMediaRegistry* registry, 
                                                Layer2::IStringRegistry* strings,
                                                Layer3::IDSPKernel* kernel,
                                                const IMidiClipDataProvider* midiProvider = nullptr);
};

} // namespace MediaManagement
