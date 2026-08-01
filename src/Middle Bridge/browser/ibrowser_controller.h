// src/Middle Bridge/ibrowser_controller.h
#pragma once

#include "browser/browser_primitives.h"
#include <vector>

namespace bridge {

class IBrowserController {
public:
    virtual ~IBrowserController() = default;

    //=== Navigation & Tab State ===//
    virtual void setActiveTab(BrowserTab tab) = 0;
    virtual BrowserTab getActiveTab() const = 0;
    virtual void collapseAll() = 0;
    virtual void refreshScanner() = 0;
    virtual bool getItemName(uint32_t stringId, std::string& outName) const = 0;

    //=== Item Fetching & Trees ===//
    virtual std::vector<BrowserItem> getChildren(const BrowserItem& parent) const = 0;
    virtual std::vector<BrowserItem> getRootItems(BrowserTab tab) const = 0;

    //=== Search & Tag Querying ===//
    virtual void executeSearch(const char* queryText, uint32_t tagFilterMask) = 0;
    virtual std::vector<BrowserItem> getSearchResults() const = 0;
    virtual void clearSearch() = 0;

    //=== Contextual Actions ===//
    virtual void setFavorite(MediaID mediaId, bool favorite) = 0;
    virtual void triggerImport(MediaID mediaId) = 0;
    virtual bool getMediaPath(MediaID mediaId, std::string& outPath) const = 0;
    
    /// Returns the pluginId (from PluginDescriptor) associated with a MediaID.
    /// Returns UINT32_MAX if the mediaId does not refer to a known plugin.
    virtual uint32_t getPluginIdForMedia(MediaID mediaId) const = 0;

    //=== Preview Playback Controls ===//
    virtual void startAudioPreview(MediaID mediaId, bool loop = false, float startProgress = 0.0f) = 0;
    virtual void stopAudioPreview() = 0;
    virtual bool isPreviewing() const = 0;
    virtual float getPreviewPlayhead() const = 0; // 0.0f to 1.0f
};

} // namespace bridge
