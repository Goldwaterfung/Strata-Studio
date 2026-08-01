#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Media management/browser/ipreview_pipeline_builder.h"
#include "Media management/codecs/icodec_factory.h"
#include "Media management/registry/imedia_registry.h"
#include "isample_library_browser.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <queue>
#include <sqlite3.h>
#include <sstream>
#include <thread>

namespace MediaManagement {

class SampleLibraryBrowserImpl : public ISampleLibraryBrowser {
public:
  SampleLibraryBrowserImpl(const std::string &dbPath, IMediaRegistry *registry,
                           Layer2::IStringRegistry *strings,
                           IPreviewPipelineBuilder *previewBuilder,
                           ICodecFactory *codecFactory,
                           Layer2::ITelemetryBridge *telemetry)
      : registry_(registry), strings_(strings), previewBuilder_(previewBuilder),
        codecFactory_(codecFactory), telemetry_(telemetry) {
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
      std::cerr << "Failed to open Sample Library database: "
                << sqlite3_errmsg(db_) << std::endl;
    } else {
      createTables();
    }

    workerActive_ = true;
    workerThread_ = std::thread(&SampleLibraryBrowserImpl::workerLoop, this);
  }

  ~SampleLibraryBrowserImpl() override {
    workerActive_ = false;
    cv_.notify_all();
    if (workerThread_.joinable()) {
      workerThread_.join();
    }

    if (db_) {
      sqlite3_close(db_);
    }
  }

  //=== Entry Management (CRUD) ===//

  bool addEntry(const LibraryEntry &entry) override {
    std::unique_lock lock(mutex_);
    const char *sql =
        "INSERT OR REPLACE INTO assets (id, generation, path_id, name_id, "
        "name, rating, play_count, "
        "last_played, color, root_key, detune, loop_type, loop_start, "
        "loop_end, "
        "bpm, sample_rate, channels, bit_depth) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return false;

    std::string name;
    strings_->getString(entry.nameId, name);

    sqlite3_bind_int(stmt, 1,
                     static_cast<int>(entry.mediaId.toRaw() & 0xFFFFFFFF));
    sqlite3_bind_int(stmt, 2, static_cast<int>(entry.mediaId.toRaw() >> 32));
    sqlite3_bind_int(stmt, 3, static_cast<int>(entry.pathId));
    sqlite3_bind_int(stmt, 4, static_cast<int>(entry.nameId));
    sqlite3_bind_text(stmt, 5, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, static_cast<double>(entry.rating));
    sqlite3_bind_int(stmt, 7, static_cast<int>(entry.playCount));
    sqlite3_bind_int64(stmt, 8,
                       static_cast<sqlite3_int64>(entry.lastPlayedTime));
    sqlite3_bind_int(stmt, 9, static_cast<int>(entry.colorARGB));
    sqlite3_bind_int(stmt, 10, static_cast<int>(entry.samplerInfo.rootKey));
    sqlite3_bind_double(stmt, 11,
                        static_cast<double>(entry.samplerInfo.detuneCents));
    sqlite3_bind_int(stmt, 12, static_cast<int>(entry.samplerInfo.type));
    sqlite3_bind_int(stmt, 13,
                     static_cast<int>(entry.samplerInfo.loopStartSample));
    sqlite3_bind_int(stmt, 14,
                     static_cast<int>(entry.samplerInfo.loopEndSample));
    sqlite3_bind_double(stmt, 15, static_cast<double>(entry.bpm));
    sqlite3_bind_int(stmt, 16, static_cast<int>(entry.sampleRate));
    sqlite3_bind_int(stmt, 17, static_cast<int>(entry.numChannels));
    sqlite3_bind_int(stmt, 18, static_cast<int>(entry.bitDepth));

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (success) {
      updateTags(entry.mediaId, entry.tags, entry.numTags);
    }

    return success;
  }

  bool removeEntry(MediaID mediaId) override {
    std::unique_lock lock(mutex_);
    const char *sql = "DELETE FROM assets WHERE id = ? AND generation = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(mediaId.toRaw() & 0xFFFFFFFF));
    sqlite3_bind_int(stmt, 2, static_cast<int>(mediaId.toRaw() >> 32));

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (success) {
      const char *tagSql =
          "DELETE FROM tags WHERE asset_id = ? AND asset_generation = ?;";
      sqlite3_prepare_v2(db_, tagSql, -1, &stmt, nullptr);
      sqlite3_bind_int(stmt, 1, static_cast<int>(mediaId.toRaw() & 0xFFFFFFFF));
      sqlite3_bind_int(stmt, 2, static_cast<int>(mediaId.toRaw() >> 32));
      sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }

    return success;
  }

  bool updateEntry(const LibraryEntry &entry) override {
    return addEntry(entry); // INSERT OR REPLACE handles update
  }

  bool getEntry(MediaID mediaId, LibraryEntry &outEntry) const override {
    std::unique_lock lock(mutex_);
    const char *sql = "SELECT * FROM assets WHERE id = ? AND generation = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(mediaId.toRaw() & 0xFFFFFFFF));
    sqlite3_bind_int(stmt, 2, static_cast<int>(mediaId.toRaw() >> 32));

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      found = true;
      outEntry.mediaId = mediaId;
      outEntry.pathId = static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
      outEntry.nameId = static_cast<uint32_t>(sqlite3_column_int(stmt, 3));
      outEntry.rating = static_cast<float>(sqlite3_column_double(stmt, 5));
      outEntry.playCount = static_cast<uint32_t>(sqlite3_column_int(stmt, 6));
      outEntry.lastPlayedTime =
          static_cast<uint64_t>(sqlite3_column_int64(stmt, 7));
      outEntry.colorARGB = static_cast<uint32_t>(sqlite3_column_int(stmt, 8));
      outEntry.samplerInfo.rootKey =
          static_cast<uint32_t>(sqlite3_column_int(stmt, 9));
      outEntry.samplerInfo.detuneCents =
          static_cast<float>(sqlite3_column_double(stmt, 10));
      outEntry.samplerInfo.type =
          static_cast<SamplerLoopType>(sqlite3_column_int(stmt, 11));
      outEntry.samplerInfo.loopStartSample =
          static_cast<uint32_t>(sqlite3_column_int(stmt, 12));
      outEntry.samplerInfo.loopEndSample =
          static_cast<uint32_t>(sqlite3_column_int(stmt, 13));
      outEntry.bpm = static_cast<float>(sqlite3_column_double(stmt, 14));
      outEntry.sampleRate = static_cast<uint32_t>(sqlite3_column_int(stmt, 15));
      outEntry.numChannels =
          static_cast<uint16_t>(sqlite3_column_int(stmt, 16));
      outEntry.bitDepth = static_cast<uint16_t>(sqlite3_column_int(stmt, 17));
    }
    sqlite3_finalize(stmt);

    if (found) {
      outEntry.numTags = getTags(mediaId, outEntry.tags, 16);
    }

    return found;
  }

  uint32_t getEntryCount() const override {
    std::unique_lock lock(mutex_);
    const char *sql = "SELECT COUNT(*) FROM assets;";
    sqlite3_stmt *stmt;
    uint32_t count = 0;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
      }
      sqlite3_finalize(stmt);
    }
    return count;
  }

  //=== Search & Filtering ===//

  uint32_t search(const SampleQuery &query, LibraryEntry *outEntries,
                  uint32_t maxEntries) const override {
    std::unique_lock lock(mutex_);
    if (!outEntries || maxEntries == 0)
      return 0;

    std::stringstream ss;
    ss << "SELECT id, generation FROM assets WHERE 1=1";

    if (query.minBpm > 0)
      ss << " AND bpm >= " << query.minBpm;
    if (query.maxBpm > 0)
      ss << " AND bpm <= " << query.maxBpm;
    if (query.minSampleRate > 0)
      ss << " AND sample_rate >= " << query.minSampleRate;
    if (query.numChannels > 0)
      ss << " AND channels = " << query.numChannels;
    if (query.bitDepth > 0)
      ss << " AND bit_depth = " << query.bitDepth;

    if (query.namePatternId != 0) {
      std::string pattern;
      if (strings_->getString(query.namePatternId, pattern)) {
        // Sanitize pattern for LIKE
        std::string sanitized = pattern;
        std::replace(sanitized.begin(), sanitized.end(), '\'', ' ');
        ss << " AND name LIKE '%" << sanitized << "%'";
      }
    }

    if (query.onlyAnalyzed) {
      // This would require a join with MediaRegistry if not stored here
      // Assuming for now that only analyzed assets are indexed or we store the
      // flag
    }

    std::string sql = ss.str();
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
      return 0;

    uint32_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < maxEntries) {
      uint32_t id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
      uint32_t gen = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
      MediaID mediaId =
          MediaID::fromRaw((static_cast<uint64_t>(gen) << 32) | id);

      // Note: getEntry re-locks, so we need to be careful.
      // In a real impl we'd have a non-locking internal version.
      lock.unlock();
      getEntry(mediaId, outEntries[count]);
      lock.lock();
      count++;
    }
    sqlite3_finalize(stmt);

    return count;
  }

  uint32_t searchByName(uint32_t queryId, LibraryEntry *outEntries,
                        uint32_t maxEntries) const override {
    SampleQuery q{};
    q.namePatternId = queryId;
    return search(q, outEntries, maxEntries);
  }

  uint32_t filterByTag(uint32_t tagId, LibraryEntry *outEntries,
                       uint32_t maxEntries) const override {
    std::unique_lock lock(mutex_);
    if (!outEntries || maxEntries == 0)
      return 0;

    const char *sql =
        "SELECT asset_id, asset_generation FROM tags WHERE tag_id = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return 0;

    sqlite3_bind_int(stmt, 1, static_cast<int>(tagId));

    uint32_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < maxEntries) {
      uint32_t id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
      uint32_t gen = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
      MediaID mediaId =
          MediaID::fromRaw((static_cast<uint64_t>(gen) << 32) | id);

      getEntry(mediaId, outEntries[count]);
      count++;
    }
    sqlite3_finalize(stmt);

    return count;
  }

  uint32_t filterByRating(float minRating, float maxRating,
                          LibraryEntry *outEntries,
                          uint32_t maxEntries) const override {
    std::unique_lock lock(mutex_);
    if (!outEntries || maxEntries == 0)
      return 0;

    const char *sql =
        "SELECT id, generation FROM assets WHERE rating >= ? AND rating <= ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return 0;

    sqlite3_bind_double(stmt, 1, static_cast<double>(minRating));
    sqlite3_bind_double(stmt, 2, static_cast<double>(maxRating));

    uint32_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < maxEntries) {
      uint32_t id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
      uint32_t gen = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
      MediaID mediaId =
          MediaID::fromRaw((static_cast<uint64_t>(gen) << 32) | id);

      getEntry(mediaId, outEntries[count]);
      count++;
    }
    sqlite3_finalize(stmt);

    return count;
  }

  //=== Tag Management ===//

  bool addTag(MediaID id, uint32_t tagId) override {
    std::unique_lock lock(mutex_);
    const char *sql = "INSERT OR IGNORE INTO tags (asset_id, asset_generation, "
                      "tag_id) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(id.toRaw() & 0xFFFFFFFF));
    sqlite3_bind_int(stmt, 2, static_cast<int>(id.toRaw() >> 32));
    sqlite3_bind_int(stmt, 3, static_cast<int>(tagId));

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
  }

  uint32_t getTags(MediaID id, uint32_t *outTagIds,
                   uint32_t maxTags) const override {
    std::unique_lock lock(mutex_);
    if (!outTagIds || maxTags == 0)
      return 0;

    const char *sql =
        "SELECT tag_id FROM tags WHERE asset_id = ? AND asset_generation = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
      return 0;

    sqlite3_bind_int(stmt, 1, static_cast<int>(id.toRaw() & 0xFFFFFFFF));
    sqlite3_bind_int(stmt, 2, static_cast<int>(id.toRaw() >> 32));

    uint32_t count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < maxTags) {
      outTagIds[count++] = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);

    return count;
  }

  //=== Preview Playback ===//

  bool startPreview(MediaID mediaId, const PreviewConfig &config) override {
    currentPreview_ = mediaId;
    isPreviewing_ = true;
    lastPreviewPosition_ = config.startProgress;

    DSPPreviewConfig dspConfig{};
    dspConfig.mediaId = mediaId;
    dspConfig.startPosition = config.startProgress;
    dspConfig.duration = static_cast<float>(config.length);
    dspConfig.loop = config.loopPreview;

    return previewBuilder_->buildPreviewPipeline(dspConfig);
  }

  void stopPreview() override {
    if (isPreviewing_) {
      previewBuilder_->destroyPreviewPipeline(currentPreview_);
      isPreviewing_ = false;
    }
  }

  bool isPreviewing() const override { return isPreviewing_; }

  void update() override {
    if (!isPreviewing_ || !telemetry_)
      return;

    Layer2::ITelemetryBridge::BridgeTelemetryFrame frames[16];
    uint32_t count = telemetry_->pollTelemetry(frames, 16);
    for (uint32_t i = 0; i < count; ++i) {
      if (frames[i].type == TelemetryFrame::PLAYHEAD_POSITION) {
        // Playhead is packed as uint64_t in payload[0] and payload[1]
        uint64_t samples = (static_cast<uint64_t>(frames[i].payload[1]) << 32) |
                           frames[i].payload[0];
        lastPreviewPosition_ = static_cast<float>(samples);
      }
    }
  }

  float getPreviewPosition() const override { return lastPreviewPosition_; }

  //=== Bulk Operations ===//

  void indexDirectoryAsync(uint32_t pathId) override {
    std::string path;
    if (strings_->getString(pathId, path)) {
      std::unique_lock lock(jobMutex_);
      indexQueue_.push(path);
      cv_.notify_one();
    }
  }

private:
  void createTables() {
    const char *sql = "CREATE TABLE IF NOT EXISTS assets ("
                      "  id INTEGER,"
                      "  generation INTEGER,"
                      "  path_id INTEGER,"
                      "  name_id INTEGER,"
                      "  rating REAL,"
                      "  play_count INTEGER,"
                      "  last_played INTEGER,"
                      "  color INTEGER,"
                      "  root_key INTEGER,"
                      "  detune REAL,"
                      "  loop_type INTEGER,"
                      "  loop_start INTEGER,"
                      "  loop_end INTEGER,"
                      "  bpm REAL,"
                      "  sample_rate INTEGER,"
                      "  channels INTEGER,"
                      "  bit_depth INTEGER,"
                      "  PRIMARY KEY(id, generation)"
                      ");"
                      "CREATE TABLE IF NOT EXISTS tags ("
                      "  asset_id INTEGER,"
                      "  asset_generation INTEGER,"
                      "  tag_id INTEGER,"
                      "  FOREIGN KEY(asset_id, asset_generation) REFERENCES "
                      "assets(id, generation),"
                      "  UNIQUE(asset_id, asset_generation, tag_id)"
                      ");"
                      "CREATE INDEX IF NOT EXISTS idx_tags_asset ON "
                      "tags(asset_id, asset_generation);"
                      "CREATE INDEX IF NOT EXISTS idx_tags_id ON tags(tag_id);";

    char *errMsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
      std::cerr << "Failed to create tables: " << errMsg << std::endl;
      sqlite3_free(errMsg);
    }
  }

  void updateTags(MediaID id, const uint32_t *tags, uint32_t numTags) {
    // Clear old tags
    const char *delSql =
        "DELETE FROM tags WHERE asset_id = ? AND asset_generation = ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db_, delSql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, static_cast<int>(id.toRaw() & 0xFFFFFFFF));
    sqlite3_bind_int(stmt, 2, static_cast<int>(id.toRaw() >> 32));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Add new tags
    for (uint32_t i = 0; i < numTags; ++i) {
      addTag(id, tags[i]);
    }
  }

  void workerLoop() {
    while (workerActive_) {
      std::string path;
      {
        std::unique_lock lock(jobMutex_);
        cv_.wait(lock,
                 [this] { return !workerActive_ || !indexQueue_.empty(); });
        if (!workerActive_)
          break;
        path = indexQueue_.front();
        indexQueue_.pop();
      }

      try {
        if (!std::filesystem::exists(path) ||
            !std::filesystem::is_directory(path))
          continue;

        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(path)) {
          if (!workerActive_)
            break;
          if (!entry.is_regular_file())
            continue;

          std::string filePath = entry.path().string();
          auto extension = entry.path().extension().string();
          std::transform(extension.begin(), extension.end(), extension.begin(),
                         ::tolower);

          if (extension == ".wav" || extension == ".aif" ||
              extension == ".flac") {
            indexFile(filePath);
          }
        }
      } catch (...) {
        // Log error
      }
    }
  }

  void indexFile(const std::string &filePath) {
    auto reader = codecFactory_->createReader(filePath);
    if (!reader || !reader->isValid())
      return;

    AssetInfo info{};
    info.pathId = strings_->registerString(filePath);
    info.nameId = strings_->registerString(
        std::filesystem::path(filePath).filename().string());
    info.sampleRate = reader->getSampleRate();
    info.numChannels = static_cast<uint16_t>(reader->getNumChannels());
    info.durationSamples = reader->getTotalFrames();
    info.bitDepth = reader->getBitDepth();

    MediaID mediaId = registry_->registerAsset(info);

    LibraryEntry libEntry{};
    libEntry.mediaId = mediaId;
    libEntry.pathId = info.pathId;
    libEntry.nameId = info.nameId;
    libEntry.sampleRate = info.sampleRate;
    libEntry.numChannels = info.numChannels;
    libEntry.bitDepth = info.bitDepth;
    libEntry.samplerInfo.type = SamplerLoopType::ONE_SHOT;

    addEntry(libEntry);
  }

  IMediaRegistry *registry_;
  Layer2::IStringRegistry *strings_;
  IPreviewPipelineBuilder *previewBuilder_;
  ICodecFactory *codecFactory_;
  Layer2::ITelemetryBridge *telemetry_;

  sqlite3 *db_ = nullptr;
  mutable std::mutex mutex_;

  // Background worker
  std::thread workerThread_;
  std::atomic<bool> workerActive_{false};
  std::queue<std::string> indexQueue_;
  std::mutex jobMutex_;
  std::condition_variable cv_;

  bool isPreviewing_ = false;
  MediaID currentPreview_ = MediaID::invalid();
  float lastPreviewPosition_ = 0.0f;
};

std::unique_ptr<ISampleLibraryBrowser> ISampleLibraryBrowser::create(
    const std::string &dbPath, class IMediaRegistry *registry,
    Layer2::IStringRegistry *strings, IPreviewPipelineBuilder *previewBuilder,
    class ICodecFactory *codecFactory, Layer2::ITelemetryBridge *telemetry) {
  return std::make_unique<SampleLibraryBrowserImpl>(
      dbPath, registry, strings, previewBuilder, codecFactory, telemetry);
}

} // namespace MediaManagement
