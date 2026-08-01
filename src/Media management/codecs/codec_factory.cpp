#include "icodec_factory.h"
#include "sndfile_reader.h"
#include "sndfile_writer.h"

namespace MediaManagement {

class CodecFactoryImpl : public ICodecFactory {
public:
    std::unique_ptr<ICodecReader> createReader(const std::string& filePath) override {
        auto reader = std::make_unique<SndFileReader>(filePath);
        if (reader && reader->isValid()) {
            return reader;
        }
        return nullptr;
    }

    std::unique_ptr<ICodecWriter> createWriter(const std::string& filePath, 
                                               uint32_t sampleRate, 
                                               uint16_t numChannels, 
                                               uint16_t bitDepth) override {
        int format = SF_FORMAT_WAV;
        if (bitDepth == 32) {
            format |= SF_FORMAT_FLOAT;
        } else if (bitDepth == 24) {
            format |= SF_FORMAT_PCM_24;
        } else {
            format |= SF_FORMAT_PCM_16;
        }
        auto writer = std::make_unique<SndFileWriter>(filePath, sampleRate, numChannels, format);
        if (writer && writer->isValid()) {
            return writer;
        }
        return nullptr;
    }
};

std::unique_ptr<ICodecFactory> ICodecFactory::create() {
    return std::make_unique<CodecFactoryImpl>();
}

} // namespace MediaManagement
