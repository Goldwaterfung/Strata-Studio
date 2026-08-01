// plugin_validator.h
// Layer 2: Core Infrastructure Services - Plugin Validator
//
// Validates plugin files without loading them into the process.
// Checks file structure, required entry points, and known malicious signatures.
//
// Thread-safety: All methods are thread-safe (no shared state)

#pragma once

#include "iplugin_scanner.h"
#include "system_primitives.h"
#include "Hardware/OS abstraction/common/platform_detection.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Layer2 {

// =============================================================================
// PLUGIN VALIDATOR - Format-Specific Validation
// =============================================================================

class PluginValidator : public IPluginValidator {
public:
    using ValidationResult = IPluginValidator::ValidationResult;

    // === Common validation checks === //
    static bool checkFileExists(const char* pluginPath);
    static bool checkFileReadable(const char* pluginPath);
    static bool checkFileSignature(const char* pluginPath, const uint8_t* signature, size_t size);

public:
    // Extract basic metadata without loading
    bool extractBasicMetadata(const char* pluginPath,
                             IPluginScanner::PluginFormat format,
                             PluginDescriptor& outDesc);

    // Classify a plugin based on VST3 categories and instrument capability
    static uint8_t classifyPluginByVST3Category(const std::string& subCategories, const std::string& category, bool isInstrument);



private:
    // VST3 specific validation
    bool validateVST3(const char* pluginPath, ValidationResult& outResult);

    // AU specific validation
    bool validateAU(const char* pluginPath, ValidationResult& outResult);

    // CLAP specific validation
    bool validateCLAP(const char* pluginPath, ValidationResult& outResult);

    // Check for known malicious signatures
    bool checkMaliciousSignatures(const char* pluginPath, ValidationResult& outResult);

    // Platform-specific binary validation
#ifdef LAYER1_PLATFORM_WINDOWS
    bool validateWindowsBinary(const char* pluginPath, ValidationResult& outResult);
#elif defined(LAYER1_PLATFORM_MACOS)
    bool validateMacOSBundle(const char* pluginPath, ValidationResult& outResult);
#else
    bool validateLinuxBinary(const char* pluginPath, ValidationResult& outResult);
#endif

    // Compute checksum for cache validation
    uint32_t computeFileChecksum(const char* pluginPath);

public:
    PluginValidator() = default;
    ~PluginValidator() override = default;

    // === IPluginValidator Implementation === //

    ValidationResult validatePlugin(const char* pluginPath,
                                    IPluginScanner::PluginFormat format) override;

    uint32_t computeChecksum(const char* pluginPath) override;

    // === Format Detection === //

    // Detect plugin format from file extension/structure
    static IPluginScanner::PluginFormat detectFormat(const char* pluginPath);

    // Get expected file extension for format
    static const char* getFormatExtension(IPluginScanner::PluginFormat format);

    // Check if path matches expected format structure
    static bool matchesFormatStructure(const char* pluginPath, IPluginScanner::PluginFormat format);
};

// =============================================================================
// VST3 SPECIFIC VALIDATION
// =============================================================================

class VST3Validator {
public:
    // Validate VST3 bundle structure
    static bool validateBundle(const char* bundlePath,
                              IPluginValidator::ValidationResult& outResult);

    // Validate VST3 binary
    static bool validateBinary(const char* binaryPath,
                              IPluginValidator::ValidationResult& outResult);

    // Check for required entry points
    static bool checkEntryPoints(const char* binaryPath);

    // Extract plugin info from plist
    static bool extractInfoFromPlist(const char* plistPath, PluginDescriptor& outDesc);

    // VST3 on Windows uses different structure
    static bool validateWindowsModule(const char* modulePath,
                                     PluginValidator::ValidationResult& outResult);
};

// =============================================================================
// AU SPECIFIC VALIDATION
// =============================================================================

class AUValidator {
public:
    // Validate AU bundle structure
    static bool validateBundle(const char* bundlePath,
                              IPluginValidator::ValidationResult& outResult);

    // Validate AU component
    static bool validateComponent(const char* componentPath,
                                 IPluginValidator::ValidationResult& outResult);

    // Check for required entry points
    static bool checkEntryPoints(const char* binaryPath);

    // Extract plugin info from Info.plist
    static bool extractInfoFromPlist(const char* plistPath, PluginDescriptor& outDesc);

    // Verify component registration
    static bool verifyComponentRegistration(const char* componentPath);
};

// =============================================================================
// CLAP SPECIFIC VALIDATION
// =============================================================================

class CLAPValidator {
public:
    // Validate CLAP binary
    static bool validateBinary(const char* binaryPath,
                              IPluginValidator::ValidationResult& outResult);

    // Check for required clap_entry symbol
    static bool checkEntryPoint(const char* binaryPath);

    // Validate CLAP plugin entry
    static bool validateClapEntry(const char* binaryPath,
                                 IPluginValidator::ValidationResult& outResult);

    // Extract plugin info from clap_entry
    static bool extractPluginInfo(const char* binaryPath, PluginDescriptor& outDesc);
};

// =============================================================================
// INLINE IMPLEMENTATIONS
// =============================================================================

inline const char* PluginValidator::getFormatExtension(IPluginScanner::PluginFormat format) {
    switch (format) {
        case IPluginScanner::PluginFormat::VST3:
#ifdef LAYER1_PLATFORM_WINDOWS
            return ".vst3";
#else
            return ".vst3";  // Actually a bundle on macOS
#endif
        case IPluginScanner::PluginFormat::AU:
            return ".component";
        case IPluginScanner::PluginFormat::CLAP:
            return ".clap";
        default:
            return "";
    }
}

inline bool PluginValidator::matchesFormatStructure(const char* pluginPath,
                                                   IPluginScanner::PluginFormat format) {
    std::string path(pluginPath);
    std::string ext = getFormatExtension(format);

    if (ext.empty()) {
        return false;
    }

    // Check if path ends with expected extension
    if (path.length() >= ext.length() &&
        path.substr(path.length() - ext.length()) == ext) {
        return true;
    }

    return false;
}

inline IPluginScanner::PluginFormat PluginValidator::detectFormat(const char* pluginPath) {
    std::string path(pluginPath);

    // Check for VST3
    if (path.find(".vst3") != std::string::npos) {
        return IPluginScanner::PluginFormat::VST3;
    }

    // Check for AU
    if (path.find(".component") != std::string::npos) {
        return IPluginScanner::PluginFormat::AU;
    }

    // Check for CLAP
    if (path.find(".clap") != std::string::npos) {
        return IPluginScanner::PluginFormat::CLAP;
    }

    return IPluginScanner::PluginFormat::NONE;
}

} // namespace Layer2
