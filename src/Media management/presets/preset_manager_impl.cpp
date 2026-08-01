#include "ipreset_manager.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <vector>

namespace MediaManagement {

namespace fs = std::filesystem;

class PresetManagerImpl : public IPresetManager {
public:
    explicit PresetManagerImpl(Layer2::IStringRegistry* registry, IPluginStateBridge* bridge) 
        : m_registry(registry), m_bridge(bridge) {
        if (!fs::exists(m_basePath)) {
            try {
                fs::create_directories(m_basePath);
            } catch (const fs::filesystem_error&) { }
        }
        rebuildCache();
    }

    bool savePreset(const Preset& preset, const uint8_t* stateData, uint32_t stateDataSize) override {
        if (preset.nameId == 0) return false;
        
        std::string name;
        if (!m_registry->getString(preset.nameId, name)) return false;

        std::replace(name.begin(), name.end(), '/', '_');
        std::replace(name.begin(), name.end(), '\\', '_');

        fs::path filePath = m_basePath / (name + ".bin");
        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        Preset metadata = preset;
        metadata.stateDataSize = stateDataSize;
        
        if (metadata.modifiedTime == 0) {
            metadata.modifiedTime = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        }

        file.write(reinterpret_cast<const char*>(&metadata), sizeof(Preset));
        if (file.fail()) return false;
        
        if (stateData && stateDataSize > 0) {
            file.write(reinterpret_cast<const char*>(stateData), stateDataSize);
            if (file.fail()) return false;
        }

        // Update cache
        updateCache(metadata);
        return true;
    }

    bool loadPreset(uint32_t nameId, Preset& outPreset, uint8_t* outStateData, uint32_t maxStateSize) const override {
        std::string name;
        if (!m_registry->getString(nameId, name)) return false;

        fs::path filePath = m_basePath / (name + ".bin");
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return false;

        file.read(reinterpret_cast<char*>(&outPreset), sizeof(Preset));
        if (file.gcount() != sizeof(Preset)) return false;

        if (outStateData && outPreset.stateDataSize > 0) {
            uint32_t toRead = std::min(outPreset.stateDataSize, maxStateSize);
            file.read(reinterpret_cast<char*>(outStateData), toRead);
            if (file.gcount() != static_cast<std::streamsize>(toRead)) return false;
        }

        return true;
    }

    bool deletePreset(uint32_t nameId) override {
        std::string name;
        if (!m_registry->getString(nameId, name)) return false;

        fs::path filePath = m_basePath / (name + ".bin");
        if (fs::exists(filePath)) {
            if (fs::remove(filePath)) {
                removeFromCache(nameId);
                return true;
            }
        }
        return false;
    }

    bool renamePreset(uint32_t oldNameId, uint32_t newNameId) override {
        std::string oldName, newName;
        if (!m_registry->getString(oldNameId, oldName)) return false;
        if (!m_registry->getString(newNameId, newName)) return false;

        fs::path oldPath = m_basePath / (oldName + ".bin");
        fs::path newPath = m_basePath / (newName + ".bin");

        if (!fs::exists(oldPath)) return false;
        
        try {
            fs::rename(oldPath, newPath);
            
            std::fstream file(newPath, std::ios::binary | std::ios::in | std::ios::out);
            if (file.is_open()) {
                Preset metadata;
                file.read(reinterpret_cast<char*>(&metadata), sizeof(Preset));
                metadata.nameId = newNameId;
                metadata.modifiedTime = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
                file.seekp(0);
                file.write(reinterpret_cast<const char*>(&metadata), sizeof(Preset));
                
                removeFromCache(oldNameId);
                updateCache(metadata);
                return true;
            }
            return false;
        } catch (...) {
            return false;
        }
    }

    uint32_t getPresetCount() const override {
        return static_cast<uint32_t>(m_cache.size());
    }

    uint32_t getPresetsByCategory(uint32_t categoryId, Preset* outPresets, uint32_t maxPresets) const override {
        uint32_t count = 0;
        for (const auto& p : m_cache) {
            if (count >= maxPresets) break;
            if (p.categoryId == categoryId) {
                outPresets[count++] = p;
            }
        }
        return count;
    }

    uint32_t getPresetsByTag(uint32_t tagId, Preset* outPresets, uint32_t maxPresets) const override {
        uint32_t count = 0;
        for (const auto& p : m_cache) {
            if (count >= maxPresets) break;
            for (uint32_t i = 0; i < p.numTags; ++i) {
                if (p.tags[i] == tagId) {
                    outPresets[count++] = p;
                    break;
                }
            }
        }
        return count;
    }

    uint32_t searchPresets(uint32_t queryId, Preset* outPresets, uint32_t maxPresets) const override {
        std::string query;
        if (!m_registry->getString(queryId, query)) return 0;
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);

        uint32_t count = 0;
        for (const auto& p : m_cache) {
            if (count >= maxPresets) break;
            
            bool match = false;
            std::string s;
            if (m_registry->getString(p.nameId, s)) {
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                if (s.find(query) != std::string::npos) match = true;
            }
            
            if (!match && m_registry->getString(p.categoryId, s)) {
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                if (s.find(query) != std::string::npos) match = true;
            }
            
            if (!match) {
                for (uint32_t i = 0; i < p.numTags; ++i) {
                    if (m_registry->getString(p.tags[i], s)) {
                        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                        if (s.find(query) != std::string::npos) { match = true; break; }
                    }
                }
            }
            
            if (match) {
                outPresets[count++] = p;
            }
        }
        return count;
    }

    bool quickSave(uint32_t pluginId, TrackID trackId, uint32_t nameId) override {
        if (!m_bridge) return false;
        uint64_t size = m_bridge->getPluginStateSize(pluginId, trackId);
        if (size == 0) return false;
        std::vector<uint8_t> buffer(size);
        if (!m_bridge->savePluginState(pluginId, trackId, buffer.data(), size)) return false;
        
        Preset meta = {};
        meta.nameId = nameId;
        meta.pluginId = pluginId;
        meta.trackId = trackId;
        meta.stateDataSize = static_cast<uint32_t>(size);
        meta.createdTime = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        meta.modifiedTime = meta.createdTime;
        
        return savePreset(meta, buffer.data(), meta.stateDataSize);
    }

    bool quickLoad(uint32_t pluginId, TrackID trackId, uint32_t nameId) override {
        if (!m_bridge) return false;
        Preset meta;
        if (!loadPreset(nameId, meta, nullptr, 0)) return false;
        if (meta.stateDataSize == 0) return false;
        std::vector<uint8_t> buffer(meta.stateDataSize);
        if (!loadPreset(nameId, meta, buffer.data(), static_cast<uint32_t>(buffer.size()))) return false;
        return m_bridge->loadPluginState(pluginId, trackId, buffer.data(), buffer.size());
    }

private:
    void rebuildCache() {
        m_cache.clear();
        if (!fs::exists(m_basePath)) return;
        for (const auto& entry : fs::directory_iterator(m_basePath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".bin") {
                std::ifstream file(entry.path(), std::ios::binary);
                if (file.is_open()) {
                    Preset p;
                    file.read(reinterpret_cast<char*>(&p), sizeof(Preset));
                    if (file.gcount() == sizeof(Preset)) {
                        m_cache.push_back(p);
                    }
                }
            }
        }
    }

    void updateCache(const Preset& p) {
        for (auto& cached : m_cache) {
            if (cached.nameId == p.nameId) {
                cached = p;
                return;
            }
        }
        m_cache.push_back(p);
    }

    void removeFromCache(uint32_t nameId) {
        m_cache.erase(std::remove_if(m_cache.begin(), m_cache.end(), 
            [nameId](const Preset& p) { return p.nameId == nameId; }), m_cache.end());
    }

    Layer2::IStringRegistry* m_registry;
    IPluginStateBridge* m_bridge;
    fs::path m_basePath = "presets";
    std::vector<Preset> m_cache;
};

std::unique_ptr<IPresetManager> IPresetManager::create(Layer2::IStringRegistry* registry, IPluginStateBridge* bridge) {
    return std::make_unique<PresetManagerImpl>(registry, bridge);
}

} // namespace MediaManagement
