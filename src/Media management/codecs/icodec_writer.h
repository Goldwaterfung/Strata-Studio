#pragma once

#include <cstdint>

namespace MediaManagement {

/**
 * @brief Interface for encoding audio data to a file.
 */
class ICodecWriter {
public:
    virtual ~ICodecWriter() = default;
    
    /**
     * @brief Write audio frames to the destination.
     * @param buffer Interleaved buffer to write.
     * @param numFrames Number of frames to write.
     * @return Actual number of frames written.
     */
    virtual uint32_t writeFrames(const float* buffer, uint32_t numFrames) = 0;
    
    /**
     * @brief Finalize and close the file.
     */
    virtual void close() = 0;
    
    virtual bool isValid() const = 0;

    /**
     * @brief Set string metadata (Title, Artist, etc.)
     */
    virtual void setStringMetadata(int key, const char* value) = 0;
};

} // namespace MediaManagement
