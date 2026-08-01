#pragma once

#include "Media management/registry/media_primitives.h"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace Layer2 { 
    class IStringRegistry; 
    class ITelemetryBridge;
}

namespace MediaManagement { class IPreviewPipelineBuilder; }

namespace MediaManagement {

/**
 * @brief Resolution of sampler loop types.
 */
enum class SamplerLoopType : uint8_t {
    ONE_SHOT = 0,
    LOOP = 1,
    FORWARD_LOOP = 2,
    ALTERNATING_LOOP = 3
};

/**
 * @brief Detailed entry in the sample library (TRUE POD).
 * 
 * Represents a managed asset in the library database with rich metadata.
 */
struct LibraryEntry {
    MediaID mediaId;
    uint32_t pathId;         ///< String Registry handle
    uint32_t nameId;         ///< String Registry handle
    uint32_t tags[16];       ///< String Registry IDs for tags
    uint32_t numTags;
    uint32_t playCount;
    uint64_t lastPlayedTime;
    float rating;
    uint32_t colorARGB;

    // Technical Metadata (added for search support)
    float bpm;
    uint32_t sampleRate;
    uint16_t numChannels;
    uint16_t bitDepth;

    struct {
        uint32_t rootKey;
        float detuneCents;
        SamplerLoopType type;
        uint32_t loopStartSample;
        uint32_t loopEndSample;
    } samplerInfo;
};

static_assert(std::is_trivially_copyable<LibraryEntry>::value, "LibraryEntry must be trivially copyable");

/**
 * @brief Configuration for previewing a sample.
 */
struct PreviewConfig {
    float volumeDecibels;
    bool loopPreview;
    uint32_t fadeInSamples;
    uint32_t fadeOutSamples;
    uint64_t startPosition;
    uint64_t length;
    float startProgress = 0.0f;

    static constexpr PreviewConfig defaults() {
        return { 0.0f, false, 480, 480, 0, 0, 0.0f }; // 10ms fades at 48kHz
    }
};

/**
 * @brief Search criteria for the sample library.
 */
struct SampleQuery {
    uint32_t namePatternId; ///< String handle for name pattern (0 for none)
    float minBpm;
    float maxBpm;
    uint32_t minSampleRate;
    uint16_t numChannels;
    uint16_t bitDepth;
    bool onlyAnalyzed;
};

/**
 * @brief Interface for the Sample Library Browser service.
 * 
 * Provides a managed database of media assets with integrated preview playback.
 */
class ISampleLibraryBrowser {
public:
    virtual ~ISampleLibraryBrowser() = default;

    //=== Entry Management (CRUD) ===//

    virtual bool addEntry(const LibraryEntry& entry) = 0;
    virtual bool removeEntry(MediaID mediaId) = 0;
    virtual bool updateEntry(const LibraryEntry& entry) = 0;
    virtual bool getEntry(MediaID mediaId, LibraryEntry& outEntry) const = 0;
    virtual uint32_t getEntryCount() const = 0;

    //=== Search & Filtering ===//

    /**
     * @brief Search for assets matching the query.
     * 
     * @param query Criteria to filter by.
     * @param outEntries Buffer to fill with results.
     * @param maxEntries Capacity of the output buffer.
     * @return Actual number of entries written to outEntries.
     */
    virtual uint32_t search(const SampleQuery& query, LibraryEntry* outEntries, uint32_t maxEntries) const = 0;

    virtual uint32_t searchByName(uint32_t queryId, LibraryEntry* outEntries, uint32_t maxEntries) const = 0;
    virtual uint32_t filterByTag(uint32_t tagId, LibraryEntry* outEntries, uint32_t maxEntries) const = 0;
    virtual uint32_t filterByRating(float minRating, float maxRating, LibraryEntry* outEntries, uint32_t maxEntries) const = 0;

    //=== Tag Management ===//

    virtual bool addTag(MediaID id, uint32_t tagId) = 0;
    virtual uint32_t getTags(MediaID id, uint32_t* outTagIds, uint32_t maxTags) const = 0;

    //=== Preview Playback ===//

    virtual bool startPreview(MediaID mediaId, const PreviewConfig& config) = 0;
    virtual void stopPreview() = 0;
    virtual bool isPreviewing() const = 0;
    virtual float getPreviewPosition() const = 0; // 0.0 to 1.0

    //=== Bulk Operations ===//

    /**
     * @brief Index an entire directory (recursive).
     */
    virtual void indexDirectoryAsync(uint32_t pathId) = 0;

    /**
     * @brief Service tick called from Main Thread.
     */
    virtual void update() = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<ISampleLibraryBrowser> create(const std::string& dbPath,
                                                         class IMediaRegistry* registry,
                                                         Layer2::IStringRegistry* strings,
                                                         IPreviewPipelineBuilder* previewBuilder,
                                                         class ICodecFactory* codecFactory,
                                                         Layer2::ITelemetryBridge* telemetry = nullptr);
};

} // namespace MediaManagement
