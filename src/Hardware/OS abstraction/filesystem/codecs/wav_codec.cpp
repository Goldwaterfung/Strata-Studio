// wav_codec.cpp
// Layer 1: Hardware/OS Abstraction - WAV Codec Implementation

#include "wav_codec.h"
#include <cstring>
#include "../filesystem_defaults.h"

namespace Layer1 {

bool WAVCodec::readHeader(const uint8_t* data, Header& header) {
    // Basic RIFF/WAVE validation (first 12 bytes)
    if (std::strncmp(reinterpret_cast<const char*>(data), "RIFF", 4) != 0) return false;
    if (std::strncmp(reinterpret_cast<const char*>(data + 8), "WAVE", 4) != 0) return false;

    std::memcpy(header.chunkID, data, 4);
    std::memcpy(&header.chunkSize, data + 4, 4);
    std::memcpy(header.format, data + 8, 4);

    bool foundFmt = false;
    bool foundData = false;
    uint32_t offset = 12;

    // We search up to 64KB for headers
    while (offset < 65528) {
        const char* chunkID = reinterpret_cast<const char*>(data + offset);
        uint32_t chunkSize;
        std::memcpy(&chunkSize, data + offset + 4, 4);

        if (std::strncmp(chunkID, "fmt ", 4) == 0) {
            std::memcpy(header.subchunk1ID, "fmt ", 4);
            header.subchunk1Size = chunkSize;
            std::memcpy(&header.audioFormat, data + offset + 8, 2);
            std::memcpy(&header.numChannels, data + offset + 10, 2);
            std::memcpy(&header.sampleRate, data + offset + 12, 4);
            std::memcpy(&header.byteRate, data + offset + 16, 4);
            std::memcpy(&header.blockAlign, data + offset + 20, 2);
            std::memcpy(&header.bitsPerSample, data + offset + 22, 2);
            foundFmt = true;
        } else if (std::strncmp(chunkID, "data", 4) == 0) {
            std::memcpy(header.subchunk2ID, "data", 4);
            header.subchunk2Size = chunkSize;
            header.dataOffset = offset + 8;
            foundData = true;
            break; 
        }

        if (chunkSize > 1000000) break; // Safety break for garbage

        offset += 8 + chunkSize;
        // Align to 2 bytes if necessary (standard WAV)
        if (offset % 2 != 0) offset++;
    }

    return foundFmt && foundData;
}

bool WAVCodec::writeHeader(uint8_t* data, const Header& header) {
    std::memcpy(data, &header, sizeof(Header));
    return true;
}

} // namespace Layer1
