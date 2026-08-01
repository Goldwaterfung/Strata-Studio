// src/Middle Bridge/browser_controller.h
#pragma once

#include "browser/ibrowser_controller.h"
#include "project/isession_manager.h"
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace Layer2 { class IStringRegistry; }
namespace Layer3 { class IPluginManager; }
namespace MediaManagement {
    class IProjectBrowser;
    class ISampleLibraryBrowser;
    class IMediaRegistry;
}

namespace bridge {

class BrowserController : public IBrowserController, public ISessionChangeListener {
public:
    BrowserController(
        ISessionManager* sessionManager,
        Layer2::IStringRegistry* stringRegistry,
        MediaManagement::IProjectBrowser* projectBrowser,
        MediaManagement::ISampleLibraryBrowser* sampleLibraryBrowser,
        MediaManagement::IMediaRegistry* mediaRegistry,
        Layer3::IPluginManager* pluginManager
    );
    ~BrowserController() override;

    //=== IBrowserController Interfaces ===//
    void setActiveTab(BrowserTab tab) override;
    BrowserTab getActiveTab() const override;
    void collapseAll() override;
    void refreshScanner() override;
    bool getItemName(uint32_t stringId, std::string& outName) const override;

    std::vector<BrowserItem> getChildren(const BrowserItem& parent) const override;
    std::vector<BrowserItem> getRootItems(BrowserTab tab) const override;

    void executeSearch(const char* queryText, uint32_t tagFilterMask) override;
    std::vector<BrowserItem> getSearchResults() const override;
    void clearSearch() override;

    void setFavorite(MediaID mediaId, bool favorite) override;
    void triggerImport(MediaID mediaId) override;
    bool getMediaPath(MediaID mediaId, std::string& outPath) const override;
    uint32_t getPluginIdForMedia(MediaID mediaId) const override;

    void startAudioPreview(MediaID mediaId, bool loop = false, float startProgress = 0.0f) override;
    void stopAudioPreview() override;
    bool isPreviewing() const override;
    float getPreviewPlayhead() const override;

    //=== ISessionChangeListener Interfaces ===//
    void onSessionChanging() override;
    void onSessionChanged(composition::IProjectSession* newSession) override;

private:
    void registerVirtualStrings();

    ISessionManager*                        sessionManager_;
    Layer2::IStringRegistry*                stringRegistry_;
    std::unique_ptr<Layer2::IStringRegistry> fallbackStringRegistry_;
    MediaManagement::IProjectBrowser*       projectBrowser_;
    MediaManagement::ISampleLibraryBrowser* sampleLibraryBrowser_;
    MediaManagement::IMediaRegistry*        mediaRegistry_;
    Layer3::IPluginManager*                 pluginManager_;

    BrowserTab                              activeTab_{BrowserTab::AllFolders};
    bool                                    collapsed_{false};
    
    // Search states
    std::string                             currentQuery_;
    uint32_t                                tagFilterMask_{0};
    mutable std::vector<BrowserItem>        searchResults_;

    // Registry IDs for Virtual standard directories/subfolders
    // Root standard names
    uint32_t idCurrentProject_{0};
    uint32_t idRecentFiles_{0};
    uint32_t idPluginDatabase_{0};
    uint32_t idBackup_{0};
    uint32_t idClipboard_{0};
    uint32_t idEnvelopes_{0};
    uint32_t idPacks_{0};
    uint32_t idProjectBones_{0};
    uint32_t idMixerPresets_{0};
    uint32_t idChannelPresets_{0};
    uint32_t idPluginPresets_{0};
    uint32_t idScores_{0};
    uint32_t idDemoProjects_{0};

    // Subfolder category names
    uint32_t idHistory_{0};
    uint32_t idPatterns_{0};
    uint32_t idEffects_{0};
    uint32_t idGenerators_{0};
    uint32_t idRemoteControl_{0};
    uint32_t idInitializedControls_{0};
    uint32_t idSamples_{0};
    uint32_t idInstalled_{0};

    // Category Subfolders
    uint32_t idController_{0};
    uint32_t idDelayReverb_{0};
    uint32_t idDistortion_{0};
    uint32_t idDynamics_{0};
    uint32_t idFilter_{0};
    uint32_t idFlanger_{0};
    uint32_t idGain_{0};
    uint32_t idInstruments_{0};
    uint32_t idAudioEffects_{0};
    uint32_t idEQFilter_{0};
    uint32_t idModulation_{0};
    uint32_t idOthers_{0};
    uint32_t idAU_{0};


    mutable std::recursive_mutex            mutex_;
};

} // namespace bridge
