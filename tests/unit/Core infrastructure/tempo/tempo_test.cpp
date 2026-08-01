#include <catch2/catch_test_macros.hpp>
#include "Core infrastructure/tempo/tempo_map.h"
#include "Core infrastructure/tempo/meter_map.h"
#include <vector>

using namespace Layer2;

TEST_CASE("TempoMap Accuracy", "[Layer2][Tempo]") {
    TempoMap tempoMap;
    
    SECTION("Default Tempo") {
        ITempoService::TempoPoint event;
        tempoMap.findTempoEvent(0, event);
        CHECK(event.bpm == 120.0);
        CHECK(event.positionSample == 0);
    }
    
    SECTION("Set and Get Tempo") {
        tempoMap.setTempoAtPosition(140.0, 48000); // 1 second at 48kHz
        
        ITempoService::TempoPoint event;
        tempoMap.findTempoEvent(0, event);
        CHECK(event.bpm == 120.0); // Before change
        
        tempoMap.findTempoEvent(48000, event);
        CHECK(event.bpm == 140.0); // At change
        
        tempoMap.findTempoEvent(100000, event);
        CHECK(event.bpm == 140.0); // After change
    }
    
    SECTION("Multiple Tempo Changes and Sorting") {
        tempoMap.setTempoAtPosition(100.0, 96000);
        tempoMap.setTempoAtPosition(140.0, 48000);
        
        ITempoService::TempoPoint event;
        tempoMap.findTempoEvent(50000, event);
        CHECK(event.bpm == 140.0);
        CHECK(event.positionSample == 48000);
        
        tempoMap.findTempoEvent(200000, event);
        CHECK(event.bpm == 100.0);
        CHECK(event.positionSample == 96000);
    }

    SECTION("Range Queries") {
        tempoMap.setTempoAtPosition(130.0, 48000);
        tempoMap.setTempoAtPosition(140.0, 96000);
        tempoMap.setTempoAtPosition(150.0, 144000);
        
        ITempoService::TempoPoint events[5];
        uint32_t count = tempoMap.getTempoRange(40000, 100000, events, 5);
        
        REQUIRE(count == 2);
        CHECK(events[0].bpm == 130.0);
        CHECK(events[1].bpm == 140.0);
    }
}

TEST_CASE("MeterMap Accuracy", "[Layer2][Tempo]") {
    MeterMap meterMap;
    
    SECTION("Default Meter") {
        uint8_t num, den;
        bool found = meterMap.getMeterAtPosition(0, num, den);
        CHECK(found == true);
        CHECK(num == 4);
        CHECK(den == 4);
    }
    
    SECTION("Set and Get Meter") {
        meterMap.setMeterAtPosition(3, 4, 48000);
        
        uint8_t num, den;
        meterMap.getMeterAtPosition(24000, num, den);
        CHECK(num == 4);
        CHECK(den == 4);
        
        meterMap.getMeterAtPosition(48000, num, den);
        CHECK(num == 3);
        CHECK(den == 4);
    }
}
