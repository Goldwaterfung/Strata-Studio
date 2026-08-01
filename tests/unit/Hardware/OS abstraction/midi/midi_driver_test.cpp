#include <catch2/catch_test_macros.hpp>
#include "Hardware/OS abstraction/midi/imidi_driver.h"
#include <iostream>
#include <vector>
#include <cstring>

using namespace Layer1;

class MockMIDICallback : public IMIDIDriver::IMIDICallback {
public:
    uint32_t receivedCount = 0;
    void onMIDIReceived(uint32_t deviceIndex,
                        const MIDIMessage& message,
                        ILockFreeQueue<MIDIMessage>* outQueue) override {
        (void)deviceIndex;
        receivedCount++;
        if (outQueue) outQueue->push(message);
    }
};

TEST_CASE("MIDI Port Enumeration", "[Layer1][MIDI]") {
    auto driver = IMIDIDriver::create(AudioAPI::CORE_AUDIO);
    REQUIRE(driver != nullptr);

    SECTION("Hardware Device Enumeration") {
        uint32_t count = driver->getDeviceCount();
        std::cout << "  Found " << count << " MIDI devices." << std::endl;

        for (uint32_t i = 0; i < count; ++i) {
            char name[MAX_NAME_LENGTH];
            uint32_t len = driver->getDeviceName(i, name, MAX_NAME_LENGTH);
            
            REQUIRE(len > 0);
            REQUIRE(std::strlen(name) == len);
            std::cout << "  [" << i << "] \"" << name << "\"  SUCCESS" << std::endl;
        }
    }

    SECTION("Virtual Port Creation") {
        VirtualPortHandle handle = driver->createVirtualInputPort("Agent MIDI Test");
        
        // On some platforms or CI environments, virtual port creation might fail 
        // if permissions are lacking, but on macOS/Linux it should generally work.
        if (handle.isValid()) {
            std::cout << "  Virtual Port \"Agent MIDI Test\" created  PASS" << std::endl;
            bool closed = driver->closeVirtualPort(handle);
            CHECK(closed == true);
        } else {
            WARN("Virtual port creation not supported or failed in this environment.");
        }
    }
}

TEST_CASE("MIDI Port Lifecycle", "[Layer1][MIDI]") {
    auto driver = IMIDIDriver::create(AudioAPI::CORE_AUDIO);
    REQUIRE(driver != nullptr);

    uint32_t count = driver->getDeviceCount();
    if (count == 0) return;

    SECTION("Open and Close Input Port") {
        MockMIDICallback callback;
        bool opened = driver->openInputPort(0, &callback, 1024);
        
        // This might fail if the device is busy, but for a unit test we hope it works.
        if (opened) {
            bool closed = driver->closeInputPort(0);
            CHECK(closed == true);
        }
    }
}
