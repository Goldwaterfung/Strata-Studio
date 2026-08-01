// filesystem_defaults.h
// Layer 1: Hardware/OS Abstraction - Filesystem Defaults and Constants

#pragma once

#include <cstdint>

namespace Layer1 {
namespace FilesystemDefaults {

// Worker configuration
static constexpr size_t DEFAULT_WORKER_THREADS = 2;

// Buffer configuration
static constexpr size_t STANDARD_BUFFER_SIZE = 65536; // 64KB

// File system constants
#if defined(_WIN32)
static constexpr uint32_t DEFAULT_FILE_PERMISSIONS = 0; // Not used directly in CreateFileA
#else
static constexpr uint32_t DEFAULT_FILE_PERMISSIONS = 0644;
#endif

// Error Codes
static constexpr int ERROR_CODE_GENERIC = -1;

// Error Messages
static constexpr const char* MSG_READ_FAILED = "Read failed or file not found";
static constexpr const char* MSG_WRITE_FAILED = "Write failed";
static constexpr const char* MSG_STUB_NOT_SUPPORTED = "File system stub: operations not supported";

// WAV Codec Constants
static constexpr const char* WAV_CHUNK_ID_RIFF = "RIFF";
static constexpr const char* WAV_FORMAT_WAVE = "WAVE";
static constexpr const char* WAV_SUBCHUNK1_ID_FMT = "fmt ";
static constexpr const char* WAV_SUBCHUNK2_ID_DATA = "data";
static constexpr size_t WAV_TAG_SIZE = 4;

// FLAC Codec Constants
static constexpr const char* FLAC_ID = "fLaC";
static constexpr size_t FLAC_ID_SIZE = 4;

// MP3 Codec Constants
static constexpr size_t MP3_MIN_SIZE = 3;
static constexpr uint8_t MP3_ID3_BYTE0 = 'I';
static constexpr uint8_t MP3_ID3_BYTE1 = 'D';
static constexpr uint8_t MP3_ID3_BYTE2 = '3';
static constexpr uint8_t MP3_SYNC_BYTE0 = 0xFF;
static constexpr uint8_t MP3_SYNC_BYTE1_MASK = 0xE0;
static constexpr uint8_t MP3_SYNC_BYTE1_MATCH = 0xE0;

} // namespace FilesystemDefaults
} // namespace Layer1
