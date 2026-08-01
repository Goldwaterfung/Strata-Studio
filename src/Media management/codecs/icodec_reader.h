#pragma once

#include <cstdint>

namespace MediaManagement {

/**
 * @brief Interface for streaming audio data from a file or memory.
 * 
 * Provides basic metadata and a method to read interleaved floating-point samples.
 */
class ICodecReader {
public:
    virtual ~ICodecReader() = default;
    
    /**
     * @brief Read audio frames from the source.
     * @param buffer Interleaved buffer to fill (float array of size maxFrames * numChannels).
     * @param maxFrames Maximum number of frames to read.
     * @return Actual number of frames read.
     */
    virtual uint32_t readFrames(float* buffer, uint32_t maxFrames) = 0;
    
    /**
     * @brief Seek to a specific frame in the audio stream.
     * @param frame Frame index to seek to.
     * @return true if seek succeeded, false otherwise.
     */
    virtual bool seek(uint64_t frame) = 0;
    
    virtual uint32_t getSampleRate() const = 0;
    virtual uint16_t getNumChannels() const = 0;
    virtual uint64_t getTotalFrames() const = 0;
    virtual uint16_t getBitDepth() const = 0;
    
    /**
     * @brief Check if the reader is in a valid state (e.g., file opened successfully).
     */
    virtual bool isValid() const = 0;
};

} // namespace MediaManagement
