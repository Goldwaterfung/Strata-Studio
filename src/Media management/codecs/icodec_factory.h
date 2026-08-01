#pragma once

#include "icodec_reader.h"
#include "icodec_writer.h"
#include <memory>
#include <string>

namespace MediaManagement {

/**
 * @brief Factory for creating codec readers and writers.
 * 
 * This abstracts the concrete codec implementations (like libsndfile) from the rest of the system.
 */
class ICodecFactory {
public:
    virtual ~ICodecFactory() = default;

    /**
     * @brief Create a reader for a specific file.
     * @param filePath Path to the audio file.
     * @return Unique pointer to an ICodecReader, or nullptr if no suitable codec is found.
     */
    virtual std::unique_ptr<ICodecReader> createReader(const std::string& filePath) = 0;

    /**
     * @brief Create a writer for a specific file and format.
     * @param filePath Path to the output file.
     * @param sampleRate Sample rate of the output.
     * @param numChannels Number of channels.
     * @param bitDepth Bit depth (16, 24, 32).
     * @return Unique pointer to an ICodecWriter, or nullptr if failed.
     */
    virtual std::unique_ptr<ICodecWriter> createWriter(const std::string& filePath, 
                                                       uint32_t sampleRate, 
                                                       uint16_t numChannels, 
                                                       uint16_t bitDepth) = 0;

    /**
     * @brief Factory method for the factory itself.
     */
    static std::unique_ptr<ICodecFactory> create();
};

} // namespace MediaManagement
