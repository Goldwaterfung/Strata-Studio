// src/Middle Bridge/iproject_lifecycle_controller.h
#pragma once

#include "common/system_primitives.h"
#include "musical_composition/project_session/missing_plugin_report.h"
#include <string>
#include <vector>

namespace bridge {

/**
 * @brief Representation of the metadata of a project session, exported as standard POD for Layer 7 UI.
 */
struct ProjectMetadataState {
    char projectName[MAX_NAME_LENGTH];
    char author[MAX_NAME_LENGTH];
    uint32_t sampleRate;
    float initialTempoBPM;
    uint8_t timeSignatureNumerator;
    uint8_t timeSignatureDenominator;
    uint32_t targetBitDepth;          // NEW: e.g. 16, 24, 32
    double sessionDurationSeconds;    // NEW: e.g. 300.0 (0.0 for unlimited)
};

struct MixStatisticsState {
    bool isAnalyzed;
    float integratedLoudnessLUFS;
    float truePeakDBTP;
    bool clippingDetected;
    float samplePeakDBFS;
    float midRmsDbfs;
    float sideRmsDbfs;
    float msRatioDb;
    float stereoWidthPct;
    float monoFoldLossDb;
    float stereoCorrelation;
};

/**
 * @brief Single point of entry for managing project lifetimes, dirty tracking, and progress.
 */
class IProjectLifecycleController {
public:
    virtual ~IProjectLifecycleController() = default;

    /**
     * @brief Resets current session and initializes a new blank project.
     * @param metadata Configuration for the new project.
     * @return true if successful
     */
    virtual bool createNewProject(const ProjectMetadataState& metadata) = 0;
    
    /**
     * @brief Asynchronously loads a project file from disk.
     * Automatically coordinates track pipeline tearing-down and reconstruction.
     * @param absoluteFilePath Path to load from.
     * @return true if successful
     */
    virtual bool loadProject(const char* absoluteFilePath) = 0;
    
    /**
     * @brief Asynchronously saves the current project state.
     * @param absoluteFilePath Optional new path (Save As). If empty, saves to existing path.
     * @return true if successful
     */
    virtual bool saveProject(const char* absoluteFilePath = "") = 0;

    /**
     * @brief Exports the project to a JSON file.
     */
    virtual bool exportProjectToJson(const char* absoluteFilePath) = 0;
    
    /**
     * @brief Imports a project from a JSON file.
     */
    virtual bool importProjectFromJson(const char* absoluteFilePath) = 0;

    /**
     * @brief Closes the active project session.
     */
    virtual void closeProject() = 0;

    // --- State Queries ---
    
    /**
     * @brief Returns true if there is an active loaded project.
     */
    virtual bool hasActiveProject() const = 0;

    /**
     * @brief Returns true if there are unsaved changes.
     */
    virtual bool isProjectDirty() const = 0;
    
    /**
     * @brief Returns the absolute system path of the currently open project.
     */
    virtual std::string getCurrentProjectPath() const = 0;
    
    /**
     * @brief Get the metadata of the currently open project.
     */
    virtual ProjectMetadataState getCurrentProjectMetadata() const = 0;

    /**
     * @brief Get the mix statistics of the currently open project.
     */
    virtual MixStatisticsState getMixStatisticsState() const = 0;

    /**
     * @brief Returns a list of plugins that were missing during the last project load.
     */
    virtual std::vector<composition::MissingPluginReport> getMissingPluginsFromLastLoad() const = 0;

    // --- Async Operation Progress (For UI Loading Spinners) ---
    
    virtual bool isOperationPending() const = 0;
    virtual float getOperationProgress() const = 0; // 0.0f (started) to 1.0f (done)
};

} // namespace bridge
