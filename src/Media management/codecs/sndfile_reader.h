#pragma once

#include "icodec_reader.h"
#include <sndfile.hh>
#include <string>

namespace MediaManagement {

/**
 * @brief Concrete implementation of ICodecReader using libsndfile.
 */
class SndFileReader : public ICodecReader {
public:
    explicit SndFileReader(const std::string& filePath) 
        : file_(filePath.c_str())
    {
    }

    ~SndFileReader() override = default;

    bool isValid() const override { return file_.error() == 0; }

    uint32_t readFrames(float* buffer, uint32_t maxFrames) override {
        if (!isValid()) return 0;
        return static_cast<uint32_t>(file_.readf(buffer, maxFrames));
    }

    bool seek(uint64_t frame) override {
        if (!isValid()) return false;
        return file_.seek(static_cast<sf_count_t>(frame), SEEK_SET) != -1;
    }

    uint32_t getSampleRate() const override {
        return static_cast<uint32_t>(file_.samplerate());
    }

    uint16_t getNumChannels() const override {
        return static_cast<uint16_t>(file_.channels());
    }

    uint64_t getTotalFrames() const override {
        return static_cast<uint64_t>(file_.frames());
    }
    
    uint16_t getBitDepth() const override {
        int format = file_.format() & SF_FORMAT_SUBMASK;
        switch (format) {
            case SF_FORMAT_PCM_S8: return 8;
            case SF_FORMAT_PCM_16: return 16;
            case SF_FORMAT_PCM_24: return 24;
            case SF_FORMAT_PCM_32: return 32;
            case SF_FORMAT_FLOAT:  return 32;
            case SF_FORMAT_DOUBLE: return 64;
            default: return 16;
        }
    }

private:
    SndfileHandle file_;
};

} // namespace MediaManagement
