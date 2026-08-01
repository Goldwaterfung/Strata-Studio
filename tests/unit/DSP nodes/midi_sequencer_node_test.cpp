#include <catch2/catch_test_macros.hpp>
#include "DSP nodes/sequencer/midi_sequencer_node.h"
#include <vector>
#include <cstring>

namespace {

class MockMidiClipDataProvider : public IMidiClipDataProvider {
public:
    std::vector<MIDINote> notes;
    std::vector<MIDICCPoint> ccPoints;
    std::vector<MIDIPitchPoint> pitchPoints;

    uint32_t getNotesInClip(ClipID, MIDINote* outNotes, uint32_t maxNotes) const override {
        uint32_t count = 0;
        for (const auto& note : notes) {
            if (count < maxNotes) {
                outNotes[count++] = note;
            }
        }
        return count;
    }

    uint32_t getCCPointsInClip(ClipID, MIDICCPoint* outPoints, uint32_t maxPoints) const override {
        uint32_t count = 0;
        for (const auto& pt : ccPoints) {
            if (count < maxPoints) {
                outPoints[count++] = pt;
            }
        }
        return count;
    }

    uint32_t getPitchPointsInClip(ClipID, MIDIPitchPoint* outPoints, uint32_t maxPoints) const override {
        uint32_t count = 0;
        for (const auto& pt : pitchPoints) {
            if (count < maxPoints) {
                outPoints[count++] = pt;
            }
        }
        return count;
    }
};

} // namespace

TEST_CASE("MidiSequencerNode: Render Notes, CC, PitchBend, and Passthrough", "[DSP][MidiSequencer]") {
    DSP::MidiSequencerFactory factory;
    auto node = factory.createNode();
    REQUIRE(node.isValid());

    auto* state = factory.getRegistry().get(node);
    REQUIRE(state != nullptr);

    TrackID trackId{1, 10};
    state->trackId = trackId;

    MockMidiClipDataProvider mockProvider;
    // Add Note: offset 10 samples from region position, duration 100 samples
    MIDINote note1{};
    note1.noteId = 101;
    note1.pitch = 60;
    note1.velocity = 100;
    note1.channel = 1;
    note1.offsetSample = 10;
    note1.durationSample = 100;
    mockProvider.notes.push_back(note1);

    // Add CC point: offset 15 samples
    MIDICCPoint cc1{};
    cc1.samplePosition = 15;
    cc1.channel = 1;
    cc1.controllerNumber = 7; // Volume
    cc1.value = 120;
    mockProvider.ccPoints.push_back(cc1);

    // Add PitchBend: offset 20 samples
    MIDIPitchPoint pb1{};
    pb1.samplePosition = 20;
    pb1.channel = 1;
    pb1.value = 8192; // Center
    mockProvider.pitchPoints.push_back(pb1);

    // Setup input events (live/passthrough)
    EventData inputEvents[2];
    inputEvents[0].eventType = EventType::MIDI_NOTE_ON;
    inputEvents[0].sampleOffset = 2;
    inputEvents[0].payload.midiNote.pitch = 72;
    inputEvents[0].payload.midiNote.velocity = 90;
    inputEvents[0].payload.midiNote.channel = 2;

    inputEvents[1].eventType = EventType::MIDI_NOTE_OFF;
    inputEvents[1].sampleOffset = 30;
    inputEvents[1].payload.midiNote.pitch = 72;
    inputEvents[1].payload.midiNote.velocity = 0;
    inputEvents[1].payload.midiNote.channel = 2;

    EventData outEvents[512];
    uint32_t outEventCount = 0;

    TimelineSnapshot snapshot{};
    snapshot.regionCount = 1;
    snapshot.regions[0].trackId = trackId;
    snapshot.regions[0].type = RegionType::MIDI;
    snapshot.regions[0].sourceId = 5; // maps to ClipID
    snapshot.regions[0].positionSample = 100; // starts at timeline sample 100
    snapshot.regions[0].sourceStartSample = 0;
    snapshot.regions[0].durationProjectFrames = 1000;
    snapshot.regions[0].gain = 1.0f;
    snapshot.regions[0].isMuted = false;

    ProcessContext context{};
    context.timelineSnapshot = &snapshot;
    context.midiClipDataProvider = &mockProvider;
    context.transportState = TransportState::PLAYING;
    context.transport.positionSample = 95; // process block starts at timeline pos 95
    context.loopEnabled = false;

    DSP::processMidiSequencer(
        node,
        nullptr,
        nullptr,
        0,
        30, // block size = 30 samples (95 to 124)
        inputEvents,
        2,
        outEvents,
        &outEventCount,
        &context,
        nullptr,
        nullptr
    );

    // Timeline positions processed: 95 to 124.
    // - Passthrough events (at offset 2, 30) should be copied.
    // - Region position = 100.
    // - NoteOn: absStart = 100 + 10 = 110. In block? Yes, offset in block: 110 - 95 = 15.
    // - CCPoint: absPos = 100 + 15 = 115. In block? Yes, offset in block: 115 - 95 = 20.
    // - PitchBend: absPos = 100 + 20 = 120. In block? Yes, offset in block: 120 - 95 = 25.
    // - NoteOff: absEnd = 110 + 100 = 210. In block? No.

    // Let's verify events. Out event count should be 5: 2 passthrough + 1 NoteOn + 1 CC + 1 PitchBend.
    REQUIRE(outEventCount == 5);

    // They should be sorted by sampleOffset:
    // Index 0: Passthrough NoteOn (offset 2)
    // Index 1: Sequencer NoteOn (offset 15)
    // Index 2: Sequencer CC (offset 20)
    // Index 3: Sequencer PitchBend (offset 25)
    // Index 4: Passthrough NoteOff (offset 30)
    CHECK(outEvents[0].eventType == EventType::MIDI_NOTE_ON);
    CHECK(outEvents[0].sampleOffset == 2);
    CHECK(outEvents[0].payload.midiNote.pitch == 72);

    CHECK(outEvents[1].eventType == EventType::MIDI_NOTE_ON);
    CHECK(outEvents[1].sampleOffset == 15);
    CHECK(outEvents[1].payload.midiNote.pitch == 60);

    CHECK(outEvents[2].eventType == EventType::MIDI_CC);
    CHECK(outEvents[2].sampleOffset == 20);
    CHECK(outEvents[2].payload.midiCC.controllerNumber == 7);
    CHECK(outEvents[2].payload.midiCC.value == 120);

    CHECK(outEvents[3].eventType == EventType::MIDI_PITCH);
    CHECK(outEvents[3].sampleOffset == 25);
    CHECK(outEvents[3].payload.midiPitch.value == 8192);

    CHECK(outEvents[4].eventType == EventType::MIDI_NOTE_OFF);
    CHECK(outEvents[4].sampleOffset == 30);
    CHECK(outEvents[4].payload.midiNote.pitch == 72);

    factory.destroyNode(node);
}

TEST_CASE("MidiSequencerNode: Seek and Panic Triggering", "[DSP][MidiSequencer]") {
    DSP::MidiSequencerFactory factory;
    auto node = factory.createNode();
    REQUIRE(node.isValid());

    auto* state = factory.getRegistry().get(node);
    REQUIRE(state != nullptr);

    TrackID trackId{1, 10};
    state->trackId = trackId;
    state->hasLastExpected = true;
    state->lastExpectedSample = 200; // Previous cycle ended at 200

    MockMidiClipDataProvider mockProvider;
    // Add Note active at sample 200
    MIDINote note1{};
    note1.noteId = 101;
    note1.pitch = 64;
    note1.channel = 1;
    note1.offsetSample = 50;
    note1.durationSample = 100;
    mockProvider.notes.push_back(note1);

    TimelineSnapshot snapshot{};
    snapshot.regionCount = 1;
    snapshot.regions[0].trackId = trackId;
    snapshot.regions[0].type = RegionType::MIDI;
    snapshot.regions[0].sourceId = 5;
    snapshot.regions[0].positionSample = 100; // Note start = 150, Note end = 250
    snapshot.regions[0].durationProjectFrames = 1000;
    snapshot.regions[0].gain = 1.0f;
    snapshot.regions[0].isMuted = false;

    ProcessContext context{};
    context.timelineSnapshot = &snapshot;
    context.midiClipDataProvider = &mockProvider;
    context.transportState = TransportState::PLAYING;
    context.transport.positionSample = 500; // Seek to 500!
    context.loopEnabled = false;

    EventData outEvents[512];
    uint32_t outEventCount = 0;

    DSP::processMidiSequencer(
        node,
        nullptr,
        nullptr,
        0,
        30,
        nullptr,
        0,
        outEvents,
        &outEventCount,
        &context,
        nullptr,
        nullptr
    );

    // Seek occurred from 200 to 500.
    // Note was active at sample 199 (s->lastExpectedSample - 1).
    // It should emit:
    // - NoteOff for pitch 64 (offset 0)
    // - CC 123 (All Notes Off) for all 16 channels
    // - CC 120 (All Sound Off) for all 16 channels
    REQUIRE(outEventCount >= 33); // 1 NoteOff + 32 CC panics

    // First event should be NoteOff for 64
    CHECK(outEvents[0].eventType == EventType::MIDI_NOTE_OFF);
    CHECK(outEvents[0].payload.midiNote.pitch == 64);

    // CC 123 and 120 should follow
    int cc123Count = 0;
    int cc120Count = 0;
    for (uint32_t i = 1; i < outEventCount; ++i) {
        if (outEvents[i].eventType == EventType::MIDI_CC) {
            if (outEvents[i].payload.midiCC.controllerNumber == 123) {
                cc123Count++;
            } else if (outEvents[i].payload.midiCC.controllerNumber == 120) {
                cc120Count++;
            }
        }
    }
    CHECK(cc123Count == 16);
    CHECK(cc120Count == 16);

    factory.destroyNode(node);
}
