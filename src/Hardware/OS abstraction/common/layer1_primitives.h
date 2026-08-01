// layer1_primitives.h
// Layer 1: Hardware/OS Abstraction - POD Primitives
// All primitives must be Plain Old Data (POD) for cross-boundary safety

#pragma once

#include <cstdint>
#include <type_traits>

namespace Layer1 {

// =============================================================================
// CONSTANTS
// =============================================================================

constexpr uint32_t MAX_NAME_LENGTH = 256;
constexpr uint32_t MAX_SUPPORTED_SAMPLE_RATES = 16;
constexpr uint32_t UNUSED_DEVICE_INDEX = 0xFFFFFFFF;
constexpr uint32_t UNUSED_CORE = 0xFFFFFFFF;
constexpr uint64_t INVALID_FILE_HANDLE_VALUE = UINT64_MAX;
constexpr uint64_t INVALID_OPERATION_HANDLE_VALUE = UINT64_MAX;

// =============================================================================
// AUDIO PRIMITIVES
// =============================================================================

enum class AudioAPI : uint8_t {
    NONE,           // No audio API selected
    ASIO,           // Windows low-latency (Steinberg)
    WASAPI,         // Windows standard (Microsoft)
    CORE_AUDIO,     // macOS (Apple)
    DIRECT_SOUND,   // Windows legacy (DirectSound)
    WINDOWS_UWP,    // Windows Universal Platform
    SHARED,         // Cross-platform shared mode (auto-detect)
    COUNT           // Sentinel value for iteration
};

enum class StreamError : uint8_t {
    NONE,                   // No error
    DEVICE_NOT_FOUND,       // Invalid device index
    DEVICE_IN_USE,          // Device already opened by another process
    UNSUPPORTED_FORMAT,     // Sample rate or buffer size not supported
    INVALID_CONFIGURATION,  // Conflicting or impossible configuration
    SYSTEM_ERROR,           // OS-level error (check errorMessage)
    DEVICE_DISCONNECTED     // Device was unplugged (hot-unplug)
};

enum class StreamState : uint8_t {
    IDLE,           // Stream not opened
    OPEN,           // Stream opened but not started
    RUNNING,        // Stream actively processing audio
    ERROR,          // Stream in error state (recoverable)
    DISCONNECTED    // Device disconnected (requires re-open)
};

struct DeviceInfo {
    char name[MAX_NAME_LENGTH];              // Human-readable device name
    char manufacturer[MAX_NAME_LENGTH];      // Manufacturer identifier
    uint32_t maxInputChannels;               // Maximum simultaneous input channels
    uint32_t maxOutputChannels;              // Maximum simultaneous output channels
    uint32_t defaultSampleRate;              // Native sample rate (Hz)
    uint32_t supportedSampleRates[MAX_SUPPORTED_SAMPLE_RATES];  // Fixed-size array
    uint32_t numSampleRates;                 // Count of supported rates (0-16)
    uint32_t preferredBufferSize;            // Optimal buffer size (frames)
    bool isDefaultInput;                     // True if system default input
    bool isDefaultOutput;                    // True if system default output
};

// Ensure DeviceInfo is POD
static_assert(std::is_pod_v<DeviceInfo>, "DeviceInfo must be Plain Old Data");

struct OpenResult {
    bool success;                      // True if stream opened successfully
    StreamError error;                 // Error code if !success
    char errorMessage[256];            // Fixed-size error message (NO dynamic allocation)
};

// Ensure OpenResult is POD
static_assert(std::is_pod_v<OpenResult>, "OpenResult must be Plain Old Data");


// =============================================================================
// MIDI PRIMITIVES
// =============================================================================

struct MIDIMessage {
    using Timestamp = uint64_t;  // Nanoseconds since epoch (high-resolution)

    Timestamp timestamp;         // When message was received (monotonic clock)
    uint32_t deviceIndex;        // Which physical device sent this message
    uint16_t size;               // Number of valid bytes in data[]
    uint8_t data[512];           // MIDI message data (max sysex size)

    uint8_t getStatus() const {
        return (size > 0) ? data[0] : 0;
    }
};

// Ensure MIDIMessage is POD
static_assert(std::is_pod_v<MIDIMessage>, "MIDIMessage must be Plain Old Data");

struct VirtualPortHandle {
    uint32_t id;             // Port identifier
    uint32_t generation;     // Generation counter (detects stale handles)

    bool isValid() const {
        return generation != 0;
    }

    static constexpr VirtualPortHandle invalid() {
        return {UINT32_MAX, 0};
    }

    bool operator==(const VirtualPortHandle& other) const {
        return id == other.id && generation == other.generation;
    }
};

// Ensure VirtualPortHandle is POD
static_assert(std::is_pod_v<VirtualPortHandle>, "VirtualPortHandle must be Plain Old Data");

// =============================================================================
// FILE SYSTEM PRIMITIVES
// =============================================================================

using FileHandle = uint64_t;
constexpr FileHandle INVALID_FILE_HANDLE = INVALID_FILE_HANDLE_VALUE;

using OperationHandle = uint64_t;
constexpr OperationHandle INVALID_OPERATION_HANDLE = INVALID_OPERATION_HANDLE_VALUE;

enum class IOPriority : uint8_t {
    REALTIME,       // Media streaming during playback (highest)
    HIGH,           // Recording operations
    NORMAL,         // User-initiated file operations
    LOW             // Background scanning/caching (lowest)
};

struct FileInfo {
    char name[MAX_NAME_LENGTH];
    char extension[16];
    uint64_t size;
    uint64_t lastModified; // Unix timestamp
    bool isDirectory;
    bool isReadOnly;
    bool isHidden;
    uint8_t reserved[5]; // Padding to maintain alignment
};

static_assert(std::is_pod_v<FileInfo>, "FileInfo must be Plain Old Data");

// =============================================================================
// THREAD MANAGEMENT PRIMITIVES
// =============================================================================

struct ThreadHandle {
    uint32_t id;             // Thread identifier
    uint32_t generation;     // Generation counter for ABA prevention

    bool isValid() const {
        return generation != 0;
    }

    static constexpr ThreadHandle invalid() {
        return {UINT32_MAX, 0};
    }

    bool operator==(const ThreadHandle& other) const {
        return id == other.id && generation == other.generation;
    }
};

// Ensure ThreadHandle is POD
static_assert(std::is_pod_v<ThreadHandle>, "ThreadHandle must be Plain Old Data");

enum class ThreadPriority : uint8_t {
    IDLE,           // Below normal (background tasks)
    LOW,            // Slightly below normal
    NORMAL,         // Standard application priority
    HIGH,           // Above normal (GUI thread)
    REALTIME,       // Audio processing (time-critical)
    TIME_CRITICAL   // Highest possible (watchdog, safety)
};

struct RealTimeConstraints {
    uint64_t computationNs;    // Maximum CPU time per period
    uint64_t periodNs;         // Period between invocations
    uint64_t deadlineNs;       // Absolute deadline from period start
};

// Ensure RealTimeConstraints is POD
static_assert(std::is_pod_v<RealTimeConstraints>, "RealTimeConstraints must be Plain Old Data");

// Opaque handle for Real-Time Workgroups / MMCSS Groups
struct WorkgroupHandle {
    uintptr_t handle;
    uint32_t type; // 0 = None, 1 = AudioWorkgroup (macOS), 2 = MMCSS (Windows)

    static constexpr WorkgroupHandle invalid() {
        return {0, 0};
    }

    bool isValid() const {
        return handle != 0;
    }
};

// Ensure WorkgroupHandle is POD
static_assert(std::is_pod_v<WorkgroupHandle>, "WorkgroupHandle must be Plain Old Data");

} // namespace Layer1
