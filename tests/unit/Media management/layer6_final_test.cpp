#include <catch2/catch_test_macros.hpp>
#include "Media management/export/iexport_service.h"
#include "Media management/presets/ipreset_manager.h"
#include "Media management/library/isample_library_browser.h"
#include "Media management/registry/imedia_registry.h"
#include <thread>
#include <chrono>
#include "Core infrastructure/memory/istring_registry.h"
#include "Core audio engine/scheduler/idsp_kernel.h"

using namespace MediaManagement;

TEST_CASE("Layer 6 Final Modules", "[MediaManagement][Final]") {
    auto registry = IMediaRegistry::create();
    auto strings = Layer2::IStringRegistry::create();
    
    SECTION("Export Service plumbing") {
        auto kernel = Layer3::IDSPKernel::create();
        auto exportService = IExportService::create(registry.get(), strings.get(), kernel.get());
        
        ExportConfig config{};
        config.outputPathId = strings->registerString("test_export.wav");
        config.sampleRate = 44100;
        config.numChannels = 2;
        config.startSample = 0;
        config.endSample = 44100;
        config.format = ExportFormat::WAV;
        config.bitDepth = ExportBitDepth::BIT_16;
        
        bool completed = false;
        auto callback = [](uint64_t jobId, bool success, const char* error, void* context) {
            (void)jobId;
            (void)error;
            (void)success;
            bool* c = static_cast<bool*>(context);
            *c = true;
        };

        exportService->exportRangeAsync(config, callback, &completed);
        
        for (int i = 0; i < 100; ++i) {
            exportService->update();
            if (completed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        REQUIRE(completed);
    }
    
    SECTION("Preset Manager basic ops") {
        auto presetManager = IPresetManager::create(strings.get());
        
        float dummyData = 0.5f;
        uint32_t nameId = strings->registerString("test_preset");
        
        Preset meta = {};
        meta.nameId = nameId;
        meta.pluginId = 123;
        meta.stateDataSize = sizeof(float);
        
        REQUIRE(presetManager->savePreset(meta, reinterpret_cast<const uint8_t*>(&dummyData), sizeof(float)));
        
        Preset loadedMeta = {};
        float loadedData = 0.0f;
        bool success = presetManager->loadPreset(nameId, loadedMeta, reinterpret_cast<uint8_t*>(&loadedData), sizeof(float));
        
        REQUIRE(success);
        REQUIRE(loadedMeta.pluginId == 123);
        REQUIRE(loadedData == 0.5f);
        
        REQUIRE(presetManager->getPresetCount() == 1);
        REQUIRE(presetManager->deletePreset(nameId));
        REQUIRE(presetManager->getPresetCount() == 0);
    }
    
    SECTION("Sample Library SQLite plumbing") {
        auto library = ISampleLibraryBrowser::create("test_library.db", registry.get(), strings.get(), nullptr, nullptr, nullptr);
        SampleQuery query{};
        LibraryEntry dummy;
        auto results = library->search(query, &dummy, 1);
        // Initially empty but shouldn't crash
        REQUIRE(results == 0);
    }
}
