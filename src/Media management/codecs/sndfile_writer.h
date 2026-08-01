#pragma once

#include "icodec_writer.h"
#include <sndfile.hh>
#include <string>

namespace MediaManagement {

/**
 * @brief Concrete implementation of ICodecWriter using libsndfile.
 */
class SndFileWriter : public ICodecWriter {
public:
    /**
     * @brief Constructor for SndFileWriter.
     * @param filePath Output path.
     * @param sampleRate Sampling rate.
     * @param numChannels Number of channels.
     * @param format libsndfile format flags (e.g. SF_FORMAT_WAV | SF_FORMAT_PCM_24).
     */
    SndFileWriter(const std::string& filePath, int sampleRate, int numChannels, int format)
        : file_(filePath.c_str(), SFM_WRITE, format, numChannels, sampleRate)
    {
    }

    ~SndFileWriter() override = default;

    uint32_t writeFrames(const float* buffer, uint32_t numFrames) override {
        if (!isValid()) return 0;
        return static_cast<uint32_t>(file_.writef(buffer, numFrames));
    }

    void close() override {
        // SndfileHandle handles closing in its destructor, but we can trigger it here
        // by replacing with an empty handle if needed, or just let it scope out.
    }

    bool isValid() const override {
        return file_.error() == 0;
    }

    void setStringMetadata(int key, const char* value) override {
        if (isValid() && value) {
            file_.setString(key, value);
        }
    }

private:
    SndfileHandle file_;
};

} // namespace MediaManagement
