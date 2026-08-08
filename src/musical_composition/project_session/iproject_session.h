#pragma once
#include <string>
#include <memory>
#include "common/system_primitives.h"
#include "musical_composition/project_session/missing_plugin_report.h"
#include "musical_composition/interfaces/iarrangement_manager.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/arranger/iarranger_track.h"
#include "musical_composition/chord_track/ichord_track.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "musical_composition/clipboard/iclipboard.h"
#include "musical_composition/comping/icomping_engine.h"
#include "musical_composition/command_history/icommand_history.h"

namespace Layer2 { class IMutationBridge; class IStringRegistry; }
namespace Layer3 { class IDSPKernel; class IPluginManager; }
namespace DSP { class LatencyFactory; }

namespace composition {

struct ProjectMetadata {
    std::string projectName;
    std::string author;
    uint32_t sampleRate = 0;
    float initialTempoBPM = 120.0f;
    uint8_t timeSignatureNumerator = 4;
    uint8_t timeSignatureDenominator = 4;
    uint32_t targetBitDepth = 24;          // e.g. 16, 24, 32
    double sessionDurationSeconds = 0.0;    // e.g. 300.0 (0.0 for unlimited)
};

struct MixStatistics {
    bool isAnalyzed = false;
    float integratedLoudnessLUFS = 0.0f;
    float truePeakDBTP = 0.0f;
    bool clippingDetected = false;
    float samplePeakDBFS = -120.0f;
    float midRmsDbfs = -120.0f;
    float sideRmsDbfs = -120.0f;
    float msRatioDb = 0.0f;
    float stereoWidthPct = 0.0f;
    float monoFoldLossDb = 0.0f;
    float stereoCorrelation = 1.0f;
};

class IMarkerManager;
class IKeySignatureMap;
class IRegionMetadataManager;

class IProjectSession {
public:
    virtual ~IProjectSession() = default;

    static std::unique_ptr<IProjectSession> create(
        std::unique_ptr<class ITrackPipelineBuilder> builder,
        Layer3::IDSPKernel* kernel,
        Layer2::IMutationBridge* mutationBridge,
        Layer3::IPluginManager* pluginManager,
        NodeID masterChannelStripNode,
        NodeID masterPluginSlotNode,
        NodeID masterLatencyNode,
        DSP::LatencyFactory* latencyFactory
    );

    virtual ITrackManager* getTrackManager() = 0;
    virtual IArrangementManager* getArrangementManager() = 0;
    virtual ICommandHistory* getCommandHistory() = 0;
    virtual IArrangerTrack* getArrangerTrack() = 0;
    virtual IChordTrack* getChordTrack() = 0;
    virtual IAudioRegionSourceManager* getRegionSourceManager() = 0;
    virtual IClipboard* getClipboard() = 0;
    virtual ICompingEngine* getCompingEngine() = 0;
    virtual IMarkerManager* getMarkerManager() = 0;
    virtual IKeySignatureMap* getKeySignatureMap() = 0;
    virtual IRegionMetadataManager* getRegionMetadataManager() = 0;

    virtual const ProjectMetadata& getMetadata() const = 0;
    virtual void setMetadata(const ProjectMetadata& meta) = 0;

    virtual MixStatistics getMixStatistics() const = 0;
    virtual void setMixStatistics(const MixStatistics& stats) = 0;

    virtual bool saveToFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr) = 0;
    virtual bool loadFromFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr, std::vector<MissingPluginReport>* outMissingPlugins = nullptr) = 0;
    virtual bool saveToJsonFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr) = 0;
    virtual bool loadFromJsonFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr, std::vector<MissingPluginReport>* outMissingPlugins = nullptr) = 0;
};

} // namespace composition
