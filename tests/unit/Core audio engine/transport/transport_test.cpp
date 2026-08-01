#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/transport/itransport.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include <memory>

using namespace Layer3;
using namespace Layer2;

TEST_CASE("Transport: Basic Controls", "[Layer3][Transport]") {
    auto transport = ITransport::create(48000);
    REQUIRE(transport != nullptr);

    SECTION("Initial State") {
        CHECK(transport->getState() == TransportState::STOPPED);
        CHECK(transport->getPosition() == 0);
    }

    SECTION("Play and Stop") {
        transport->play();
        CHECK(transport->getState() == TransportState::PLAYING);
        
        transport->stop();
        CHECK(transport->getState() == TransportState::STOPPED);
    }

    SECTION("Advance Position") {
        transport->play();
        transport->advancePosition(512);
        CHECK(transport->getPosition() == 512);
        
        transport->advancePosition(1024);
        CHECK(transport->getPosition() == 1536);
        
        transport->stop();
        // AdvancePosition should return false and not increment if stopped
        bool advanced = transport->advancePosition(512);
        CHECK(advanced == false);
        CHECK(transport->getPosition() == 1536); 
    }
}

TEST_CASE("Transport: Sample-Accurate Seeking", "[Layer3][Transport]") {
    auto transport = ITransport::create(48000);
    
    SECTION("IMMEDIATE Seek") {
        transport->play();
        transport->advancePosition(24000);
        REQUIRE(transport->getPosition() == 24000);
        
        transport->seek(12000, ITransport::SeekMode::IMMEDIATE);
        CHECK(transport->getPosition() == 12000);
    }

    SECTION("BUFFER_SYNC Seek") {
        transport->play();
        transport->advancePosition(12000);
        
        // Request seek to 36000, but deferred
        transport->seek(36000, ITransport::SeekMode::BUFFER_SYNC);
        CHECK(transport->getPosition() == 12000); // Should not have jumped yet
        
        // Next advance should trigger the jump
        transport->advancePosition(512);
        // Jumped to 36000, then added 512 = 36512
        CHECK(transport->getPosition() == 36512);
    }
}

TEST_CASE("Transport: Looping", "[Layer3][Transport]") {
    auto transport = ITransport::create(48000);
    transport->setLoopRange(1000, 5000);
    transport->setLoopEnabled(true);
    transport->play();
    
    SECTION("Loop Wrap") {
        transport->seek(4900, ITransport::SeekMode::IMMEDIATE);
        bool wrapped = transport->advancePosition(200); // Should wrap to 1000 + 100 = 1100
        
        CHECK(wrapped == true);
        CHECK(transport->getPosition() == 1100);
    }
    
    SECTION("No Wrap") {
        transport->seek(2000, ITransport::SeekMode::IMMEDIATE);
        bool wrapped = transport->advancePosition(500);
        
        CHECK(wrapped == false);
        CHECK(transport->getPosition() == 2500);
    }
}

TEST_CASE("Transport: Tempo Integration", "[Layer3][Transport]") {
    auto transport = ITransport::create(48000);
    auto tempoService = ITempoService::create();
    tempoService->setSampleRate(48000); // CRITICAL: Match transport sample rate
    
    transport->setTempoService(tempoService.get());
    
    SECTION("Detailed Position Sync") {
        // At default 120 BPM, 4/4
        // 120 BPM = 2 beats per second.
        // 1 second = 48000 samples = 2 beats.
        // 1 beat = 24000 samples.
        
        transport->seek(24000, ITransport::SeekMode::IMMEDIATE);
        transport->updateTempoCache(); // Force cache update (non-RT)
        
        auto pos = transport->getDetailedPosition();
        
        // 24000 samples = 1 beat elapsed = Bar 1, Beat 2, Tick 0
        // (Assuming 1-based bars and beats)
        CHECK(pos.bar == 1);
        CHECK(pos.beat == 2);
    }
}

TEST_CASE("Transport: Crossfade Seeking", "[Layer3][Transport]") {
    auto transport = ITransport::create(48000);
    transport->play();
    transport->advancePosition(10000);
    
    SECTION("FADE_CROSS Seek Initialization") {
        // Seek with crossfade
        transport->seek(20000, ITransport::SeekMode::FADE_CROSS);
        
        // Before advance, position should still be 10000 (wait for next cycle)
        CHECK(transport->getPosition() == 10000);
        
        // Advance should jump to 20000 + advanceAmount
        transport->advancePosition(100);
        CHECK(transport->getPosition() == 20100);
    }
}

