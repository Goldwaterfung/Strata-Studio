// wav_codec.h
// Layer 1: Hardware/OS Abstraction - WAV Codec Implementation

#pragma once

#include <cstdint>
#include <vector>
#include "../filesystem_defaults.h"

namespace Layer1 {

class WAVCodec {
public:
    struct Header {
        char chunkID[FilesystemDefaults::WAV_TAG_SIZE];        // "RIFF"
        uint32_t chunkSize;
        char format[FilesystemDefaults::WAV_TAG_SIZE];         // "WAVE"
        char subchunk1ID[FilesystemDefaults::WAV_TAG_SIZE];    // "fmt "
        uint32_t subchunk1Size;
        uint16_t audioFormat;
        uint16_t numChannels;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample;
        char subchunk2ID[FilesystemDefaults::WAV_TAG_SIZE];    // "data"
        uint32_t subchunk2Size;
        uint32_t dataOffset; // Offset to start of PCM data
    };

    static bool readHeader(const uint8_t* data, Header& header);
    static bool writeHeader(uint8_t* data, const Header& header);
};

} // namespace Layer1
