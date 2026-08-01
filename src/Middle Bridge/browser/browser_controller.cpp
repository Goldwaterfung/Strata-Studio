// src/Middle Bridge/browser_controller.cpp
#include "browser/browser_controller.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "Media management/browser/iproject_browser.h"
#include "Media management/library/isample_library_browser.h"
#include "Media management/registry/imedia_registry.h"
#include <algorithm>
#include <set>
#include <cstring>
#include <iostream>

namespace bridge {

using MediaManagement::LibraryEntry;
using MediaManagement::SampleQuery;
using MediaManagement::AssetInfo;

BrowserController::BrowserController(
    ISessionManager* sessionManager,
    Layer2::IStringRegistry* stringRegistry,
    MediaManagement::IProjectBrowser* projectBrowser,
    MediaManagement::ISampleLibraryBrowser* sampleLibraryBrowser,
    MediaManagement::IMediaRegistry* mediaRegistry,
    Layer3::IPluginManager* pluginManager
)
    : sessionManager_(sessionManager)
    , stringRegistry_(stringRegistry)
    , projectBrowser_(projectBrowser)
    , sampleLibraryBrowser_(sampleLibraryBrowser)
    , mediaRegistry_(mediaRegistry)
    , pluginManager_(pluginManager)
{
    if (!stringRegistry_) {
        fallbackStringRegistry_ = Layer2::IStringRegistry::create();
        stringRegistry_ = fallbackStringRegistry_.get();
    }
    registerVirtualStrings();
}

BrowserController::~BrowserController() {
    stopAudioPreview();
}

void BrowserController::setActiveTab(BrowserTab tab) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    activeTab_ = tab;
}

BrowserTab BrowserController::getActiveTab() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return activeTab_;
}

void BrowserController::collapseAll() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    collapsed_ = true;
}

void BrowserController::refreshScanner() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sampleLibraryBrowser_ && stringRegistry_) {
        // Triggers async refresh of standard system library directory
        uint32_t standardPacksPathId = stringRegistry_->registerString("./Packs");
        sampleLibraryBrowser_->indexDirectoryAsync(standardPacksPathId);
    }
    if (projectBrowser_) {
        projectBrowser_->pruneJobs();
    }
}

bool BrowserController::getItemName(uint32_t stringId, std::string& outName) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!stringRegistry_) return false;
    return stringRegistry_->getString(stringId, outName);
}

std::vector<BrowserItem> BrowserController::getChildren(const BrowserItem& parent) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<BrowserItem> items;

    if (parent.type != BrowserItemType::Folder) {
        return items;
    }

    uint32_t stringId = parent.stringId;

    // A. All Folders Sub-hierarchy
    if (stringId == idPacks_) {
        if (sampleLibraryBrowser_) {
            LibraryEntry entries[50];
            SampleQuery q{};
            uint32_t filled = sampleLibraryBrowser_->search(q, entries, 50);
            for (uint32_t i = 0; i < filled; ++i) {
                BrowserItem item{};
                item.stringId = entries[i].nameId;
                item.mediaId = entries[i].mediaId;
                item.type = BrowserItemType::AudioFile;
                item.colorARGB = entries[i].colorARGB;
                item.isFavorite = (entries[i].rating >= 4.0f);
                items.push_back(item);
            }
        }
    }
    else if (stringId == idPluginDatabase_) {
        BrowserItem it1{idInstruments_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00FFCC, false};
        BrowserItem it2{idAudioEffects_, MediaID::invalid(), BrowserItemType::Folder, 0xFF5D3FD3, false};
        BrowserItem it3{idAU_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00AAFF, false};
        items.push_back(it1);
        items.push_back(it2);
        items.push_back(it3);
    }
    else if (stringId == idInstruments_) {
        if (pluginManager_) {
            auto plugins = pluginManager_->getAvailablePlugins();
            for (const auto& plug : plugins) {
                if (plug.category == PluginCategory::INSTRUMENT || (plug.capabilities & 0x01)) {
                    BrowserItem item{};
                    item.stringId = stringRegistry_->registerString(plug.name);
                    item.mediaId = MediaID{plug.pluginId, 1};
                    item.type = BrowserItemType::PluginGenerator;
                    item.colorARGB = 0xFF00FFCC;
                    item.isFavorite = false;
                    items.push_back(item);
                }
            }
        }
    }
    else if (stringId == idAudioEffects_) {
        items.push_back(BrowserItem{idDelayReverb_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00AAFF, false});
        items.push_back(BrowserItem{idDistortion_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFF0055, false});
        items.push_back(BrowserItem{idDynamics_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFFD700, false});
        items.push_back(BrowserItem{idEQFilter_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00FFCC, false});
        items.push_back(BrowserItem{idModulation_, MediaID::invalid(), BrowserItemType::Folder, 0xFF5D3FD3, false});
        items.push_back(BrowserItem{idOthers_, MediaID::invalid(), BrowserItemType::Folder, 0xFF8A94A6, false});
    }
    else if (stringId == idDelayReverb_ || stringId == idDistortion_ || stringId == idDynamics_ || stringId == idEQFilter_ || stringId == idModulation_ || stringId == idOthers_) {
        if (pluginManager_) {
            auto plugins = pluginManager_->getAvailablePlugins();
            for (const auto& plug : plugins) {
                // Must be an effect, not an instrument
                if (plug.category == PluginCategory::INSTRUMENT || (plug.capabilities & 0x01)) {
                    continue;
                }
                
                bool belongs = false;
                if (stringId == idDelayReverb_ && plug.category == PluginCategory::EFFECT_DELAY_REVERB) belongs = true;
                else if (stringId == idDistortion_ && plug.category == PluginCategory::EFFECT_DISTORTION) belongs = true;
                else if (stringId == idDynamics_ && plug.category == PluginCategory::EFFECT_DYNAMICS) belongs = true;
                else if (stringId == idEQFilter_ && plug.category == PluginCategory::EFFECT_EQ_FILTER) belongs = true;
                else if (stringId == idModulation_ && plug.category == PluginCategory::EFFECT_MODULATION) belongs = true;
                else if (stringId == idOthers_ && plug.category == PluginCategory::EFFECT_OTHER) belongs = true;
                
                if (belongs) {
                    BrowserItem item{};
                    item.stringId = stringRegistry_->registerString(plug.name);
                    item.mediaId = MediaID{plug.pluginId, 1};
                    item.type = BrowserItemType::PluginEffect;
                    item.colorARGB = 0xFF5D3FD3;
                    item.isFavorite = false;
                    items.push_back(item);
                }
            }
        }
    }
    else if (stringId == idInstalled_) {
        if (pluginManager_) {
            auto plugins = pluginManager_->getAvailablePlugins();
            for (const auto& plug : plugins) {
                BrowserItem item{};
                item.stringId = stringRegistry_->registerString(plug.name);
                item.mediaId = MediaID{plug.pluginId, 1};
                item.type = (plug.category == PluginCategory::INSTRUMENT || (plug.capabilities & 0x01)) ? BrowserItemType::PluginGenerator : BrowserItemType::PluginEffect;
                item.colorARGB = (plug.category == PluginCategory::INSTRUMENT || (plug.capabilities & 0x01)) ? 0xFF00FFCC : 0xFF5D3FD3;
                item.isFavorite = false;
                items.push_back(item);
            }
        }
    }
    else if (stringId == idEffects_) {
        // Effects categories
        items.push_back(BrowserItem{idController_, MediaID::invalid(), BrowserItemType::Folder, 0xFF8A94A6, false});
        items.push_back(BrowserItem{idDelayReverb_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00AAFF, false});
        items.push_back(BrowserItem{idDistortion_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFF0055, false});
        items.push_back(BrowserItem{idDynamics_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFFD700, false});
        items.push_back(BrowserItem{idFilter_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00FFCC, false});
    }
    else if (stringId == idGenerators_) {
        if (pluginManager_) {
            auto plugins = pluginManager_->getAvailablePlugins();
            for (const auto& plug : plugins) {
                if (plug.capabilities & 0x01) { // Generator capability
                    BrowserItem item{};
                    item.stringId = stringRegistry_->registerString(plug.name);
                    item.mediaId = MediaID{plug.pluginId, 1};
                    item.type = BrowserItemType::PluginGenerator;
                    item.colorARGB = 0xFF00FFCC;
                    item.isFavorite = false;
                    items.push_back(item);
                }
            }
        }
    }

    // B. Current Project Sub-hierarchy
    else if (stringId == idSamples_) {
        if (mediaRegistry_) {
            auto ids = mediaRegistry_->getAllMediaIDs();
            for (auto id : ids) {
                AssetInfo info{};
                if (mediaRegistry_->getAssetInfo(id, info)) {
                    BrowserItem item{};
                    item.stringId = info.nameId;
                    item.mediaId = id;
                    item.type = BrowserItemType::AudioFile;
                    item.colorARGB = info.colorARGB;
                    item.isFavorite = false;
                    items.push_back(item);
                }
            }
        }
    }
    else if (stringId == idAU_) {
        if (pluginManager_) {
            auto plugins = pluginManager_->getAvailablePlugins();
            std::set<std::string> manufacturers;
            for (const auto& plug : plugins) {
                if (plug.formatFlags & 0x0002) { // AU
                    std::string manuf = plug.manufacturer;
                    if (manuf.empty()) manuf = "Unknown";
                    manufacturers.insert(manuf);
                }
            }
            for (const auto& manuf : manufacturers) {
                BrowserItem item{};
                item.stringId = stringRegistry_->registerString(manuf.c_str());
                item.mediaId = MediaID::invalid();
                item.type = BrowserItemType::Folder;
                item.colorARGB = 0xFF00AAFF;
                item.isFavorite = false;
                items.push_back(item);
            }
        }
    }
    else if (stringId == idCurrentProject_) {
        items.push_back(BrowserItem{idPatterns_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFF0055, false});
        items.push_back(BrowserItem{idRemoteControl_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00AAFF, false});
        items.push_back(BrowserItem{idSamples_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFFD700, false});
    }
    else {
        // Dynamic manufacturer folder expansion
        std::string folderName;
        if (getItemName(stringId, folderName) && !folderName.empty()) {
            if (pluginManager_) {
                auto plugins = pluginManager_->getAvailablePlugins();
                for (const auto& plug : plugins) {
                    if (plug.formatFlags & 0x0002) { // AU
                        std::string manuf = plug.manufacturer;
                        if (manuf.empty()) manuf = "Unknown";
                        if (manuf == folderName) {
                            BrowserItem item{};
                            item.stringId = stringRegistry_->registerString(plug.name);
                            item.mediaId = MediaID{plug.pluginId, 1};
                            item.type = plug.isInstrument() ? BrowserItemType::PluginGenerator : BrowserItemType::PluginEffect;
                            item.colorARGB = plug.isInstrument() ? 0xFF00FFCC : 0xFF5D3FD3;
                            item.isFavorite = false;
                            items.push_back(item);
                        }
                    }
                }
            }
        }
    }

    return items;
}

std::vector<BrowserItem> BrowserController::getRootItems(BrowserTab tab) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<BrowserItem> items;

    switch (tab) {
        case BrowserTab::AllFolders:
            items.push_back(BrowserItem{idCurrentProject_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFFD700, false});
            items.push_back(BrowserItem{idRecentFiles_, MediaID::invalid(), BrowserItemType::Folder, 0xFF8A94A6, false});
            items.push_back(BrowserItem{idPluginDatabase_, MediaID::invalid(), BrowserItemType::Folder, 0xFF5D3FD3, false});
            items.push_back(BrowserItem{idPacks_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00FFCC, false});
            items.push_back(BrowserItem{idBackup_, MediaID::invalid(), BrowserItemType::Folder, 0xFF8A94A6, false});
            break;

        case BrowserTab::CurrentProject:
            items.push_back(BrowserItem{idPatterns_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFF0055, false});
            items.push_back(BrowserItem{idRemoteControl_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00AAFF, false});
            items.push_back(BrowserItem{idSamples_, MediaID::invalid(), BrowserItemType::Folder, 0xFFFFD700, false});
            break;

        case BrowserTab::PluginDatabase:
            items.push_back(BrowserItem{idInstruments_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00FFCC, false});
            items.push_back(BrowserItem{idAudioEffects_, MediaID::invalid(), BrowserItemType::Folder, 0xFF5D3FD3, false});
            items.push_back(BrowserItem{idAU_, MediaID::invalid(), BrowserItemType::Folder, 0xFF00AAFF, false});
            break;


        case BrowserTab::Favorites:
            if (sampleLibraryBrowser_) {
                LibraryEntry entries[16];
                uint32_t filled = sampleLibraryBrowser_->filterByRating(4.0f, 5.0f, entries, 16);
                for (uint32_t i = 0; i < filled; ++i) {
                    BrowserItem item{};
                    item.stringId = entries[i].nameId;
                    item.mediaId = entries[i].mediaId;
                    item.type = BrowserItemType::AudioFile;
                    item.colorARGB = entries[i].colorARGB;
                    item.isFavorite = true;
                    items.push_back(item);
                }
            }
            // Fallback premium favorites
            if (items.empty()) {
                items.push_back(BrowserItem{stringRegistry_->registerString("Dreamy Synth Pad.wav"), MediaID::invalid(), BrowserItemType::AudioFile, 0xFF00FFCC, true});
                items.push_back(BrowserItem{stringRegistry_->registerString("Cinematic Reverb Snare.wav"), MediaID::invalid(), BrowserItemType::AudioFile, 0xFFFF0055, true});
            }
            break;

        default:
            break;
    }

    return items;
}

void BrowserController::executeSearch(const char* queryText, uint32_t tagFilterMask) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    currentQuery_ = queryText ? queryText : "";
    tagFilterMask_ = tagFilterMask;
    searchResults_.clear();

    if (currentQuery_.empty() && tagFilterMask == 0) {
        return;
    }

    // 1. Tokenize query
    std::string qLower = currentQuery_;
    std::transform(qLower.begin(), qLower.end(), qLower.begin(), ::tolower);
    std::vector<std::string> tokens;
    size_t start = 0, end;
    while ((end = qLower.find(' ', start)) != std::string::npos) {
        if (end != start) {
            tokens.push_back(qLower.substr(start, end - start));
        }
        start = end + 1;
    }
    if (start < qLower.length()) {
        tokens.push_back(qLower.substr(start));
    }

    // Parse specific bits: Bit 0 (LOOPS), Bit 1 (SHOTS), Bit 2 (SYNTH), Bit 3 (FX)
    bool requiresSynth = (tagFilterMask & (1 << 2)) != 0;
    bool requiresFX    = (tagFilterMask & (1 << 3)) != 0;
    bool requiresLoops = (tagFilterMask & (1 << 0)) != 0;
    bool requiresShots = (tagFilterMask & (1 << 1)) != 0;
    
    bool filtersActive = (tagFilterMask != 0);

    // 2. Search Audio Samples
    // Skip audio samples if SYNTH or FX are exclusively required without LOOPS/SHOTS
    bool skipSamples = filtersActive && !requiresLoops && !requiresShots && (requiresSynth || requiresFX);
    
    if (!skipSamples && sampleLibraryBrowser_ && stringRegistry_) {
        SampleQuery q{};
        if (!currentQuery_.empty()) {
            q.namePatternId = stringRegistry_->registerString(currentQuery_);
        }
        
        LibraryEntry entries[50];
        uint32_t filled = sampleLibraryBrowser_->search(q, entries, 50);
        for (uint32_t i = 0; i < filled; ++i) {
            BrowserItem item{};
            item.stringId = entries[i].nameId;
            item.mediaId = entries[i].mediaId;
            item.type = BrowserItemType::AudioFile;
            item.colorARGB = entries[i].colorARGB;
            item.isFavorite = false; // Favorites eradicated
            searchResults_.push_back(item);
        }
    }

    // 3. Search Plugins
    // Skip plugins if LOOPS or SHOTS are exclusively required
    bool skipPlugins = filtersActive && !requiresSynth && !requiresFX && (requiresLoops || requiresShots);

    if (!skipPlugins && pluginManager_ && stringRegistry_) {
        auto plugins = pluginManager_->getAvailablePlugins();
        for (const auto& plug : plugins) {
            bool isInstrument = plug.category == PluginCategory::INSTRUMENT || (plug.capabilities & 0x01);
            
            if (filtersActive) {
                if (isInstrument && !requiresSynth) continue;
                if (!isInstrument && !requiresFX) continue;
            }

            std::string plugNameLower = plug.name;
            std::transform(plugNameLower.begin(), plugNameLower.end(), plugNameLower.begin(), ::tolower);
            
            bool match = true;
            for (const auto& token : tokens) {
                if (plugNameLower.find(token) == std::string::npos) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                BrowserItem item{};
                item.stringId = stringRegistry_->registerString(plug.name);
                item.mediaId = MediaID{plug.pluginId, 1}; // Gen 1 denotes plugin
                item.type = isInstrument ? BrowserItemType::PluginGenerator : BrowserItemType::PluginEffect;
                item.colorARGB = isInstrument ? 0xFF00FFCC : 0xFF5D3FD3;
                item.isFavorite = false;
                searchResults_.push_back(item);
            }
        }
    }
}

std::vector<BrowserItem> BrowserController::getSearchResults() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return searchResults_;
}

void BrowserController::clearSearch() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    currentQuery_.clear();
    tagFilterMask_ = 0;
    searchResults_.clear();
}

void BrowserController::setFavorite(MediaID mediaId, bool favorite) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!sampleLibraryBrowser_) return;

    LibraryEntry entry{};
    if (sampleLibraryBrowser_->getEntry(mediaId, entry)) {
        entry.rating = favorite ? 5.0f : 0.0f;
        sampleLibraryBrowser_->updateEntry(entry);
    }
}

void BrowserController::triggerImport(MediaID mediaId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!projectBrowser_ || !sampleLibraryBrowser_ || !stringRegistry_) return;

    LibraryEntry entry{};
    if (sampleLibraryBrowser_->getEntry(mediaId, entry)) {
        std::string filePath;
        if (stringRegistry_->getString(entry.pathId, filePath)) {
            MediaManagement::ImportJob job{};
            job.filePathId = entry.pathId;
            job.options = MediaManagement::ImportOptions::defaults();
            job.jobId = 0;
            
            projectBrowser_->importAssetAsync(job, nullptr, nullptr);
        }
    }
}

bool BrowserController::getMediaPath(MediaID mediaId, std::string& outPath) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sampleLibraryBrowser_ && stringRegistry_) {
        LibraryEntry entry{};
        if (sampleLibraryBrowser_->getEntry(mediaId, entry)) {
            if (stringRegistry_->getString(entry.pathId, outPath)) {
                return true;
            }
        }
    }

    // Mock fallback for premium demo items
    uint64_t raw = mediaId.toRaw();
    if (raw >= 1 && raw <= 5) {
        const char* mockNames[] = {
            "Cyberpunk Kick.wav",
            "Dreamy Synth Pad.wav",
            "Cinematic Reverb Snare.wav",
            "Industrial Tom Loop.wav",
            "Sub Bass Drop.wav"
        };
        outPath = mockNames[raw - 1];
        return true;
    }

    return false;
}

uint32_t BrowserController::getPluginIdForMedia(MediaID mediaId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!pluginManager_) return UINT32_MAX;

    // Check if the mediaId is a packed pluginId (generation 1, and matches an available plugin)
    if (mediaId.generation == 1) {
        auto plugins = pluginManager_->getAvailablePlugins();
        for (const auto& p : plugins) {
            if (p.pluginId == mediaId.id) {
                return p.pluginId;
            }
        }
    }

    // Fallback to resolving via filePath (for backwards compatibility/assets)
    if (!mediaRegistry_ || !stringRegistry_) return UINT32_MAX;
    std::string filePath;
    if (sampleLibraryBrowser_) {
        LibraryEntry entry{};
        if (sampleLibraryBrowser_->getEntry(mediaId, entry)) {
            stringRegistry_->getString(entry.pathId, filePath);
        }
    }

    if (filePath.empty()) {
        return UINT32_MAX;
    }

    auto plugins = pluginManager_->getAvailablePlugins();
    for (const auto& p : plugins) {
        if (filePath == p.filePath) return p.pluginId;
    }
    return UINT32_MAX;
}

void BrowserController::startAudioPreview(MediaID mediaId, bool loop, float startProgress) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!sampleLibraryBrowser_) return;

    MediaManagement::PreviewConfig config = MediaManagement::PreviewConfig::defaults();
    config.loopPreview = loop;
    config.startProgress = startProgress;
    sampleLibraryBrowser_->startPreview(mediaId, config);
}

void BrowserController::stopAudioPreview() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (sampleLibraryBrowser_) {
        sampleLibraryBrowser_->stopPreview();
    }
}

bool BrowserController::isPreviewing() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sampleLibraryBrowser_ ? sampleLibraryBrowser_->isPreviewing() : false;
}

float BrowserController::getPreviewPlayhead() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return sampleLibraryBrowser_ ? sampleLibraryBrowser_->getPreviewPosition() : 0.0f;
}

void BrowserController::onSessionChanging() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stopAudioPreview();
    clearSearch();
}

void BrowserController::onSessionChanged(composition::IProjectSession* newSession) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    (void)newSession;
    (void)sessionManager_;
}

void BrowserController::registerVirtualStrings() {
    idCurrentProject_ = stringRegistry_->registerString("Current project");
    idRecentFiles_ = stringRegistry_->registerString("Recent files");
    idPluginDatabase_ = stringRegistry_->registerString("Plugin database");
    idBackup_ = stringRegistry_->registerString("Backup");
    idClipboard_ = stringRegistry_->registerString("Clipboard files");
    idEnvelopes_ = stringRegistry_->registerString("Envelopes");
    idPacks_ = stringRegistry_->registerString("Packs");
    idProjectBones_ = stringRegistry_->registerString("Project bones");
    idMixerPresets_ = stringRegistry_->registerString("Mixer presets");
    idChannelPresets_ = stringRegistry_->registerString("Channel presets");
    idPluginPresets_ = stringRegistry_->registerString("Plugin presets");
    idScores_ = stringRegistry_->registerString("Scores");
    idDemoProjects_ = stringRegistry_->registerString("Demo projects");

    idHistory_ = stringRegistry_->registerString("History");
    idPatterns_ = stringRegistry_->registerString("Patterns");
    idEffects_ = stringRegistry_->registerString("Effects");
    idGenerators_ = stringRegistry_->registerString("Generators");
    idRemoteControl_ = stringRegistry_->registerString("Remote control");
    idInitializedControls_ = stringRegistry_->registerString("Initialized controls");
    idSamples_ = stringRegistry_->registerString("Samples");
    idInstalled_ = stringRegistry_->registerString("Installed");

    idController_ = stringRegistry_->registerString("Controller");
    idDelayReverb_ = stringRegistry_->registerString("Delay / Reverb");
    idDistortion_ = stringRegistry_->registerString("Distortion");
    idDynamics_ = stringRegistry_->registerString("Dynamics");
    idFilter_ = stringRegistry_->registerString("Filter");
    idFlanger_ = stringRegistry_->registerString("Flanger");
    idGain_ = stringRegistry_->registerString("Gain");
    idInstruments_ = stringRegistry_->registerString("Instruments");
    idAudioEffects_ = stringRegistry_->registerString("Audio Effects");
    idEQFilter_ = stringRegistry_->registerString("EQ / Filter");
    idModulation_ = stringRegistry_->registerString("Modulation");
    idOthers_ = stringRegistry_->registerString("others");
    idAU_ = stringRegistry_->registerString("AU");
}

} // namespace bridge
