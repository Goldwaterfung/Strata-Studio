// plugin_validator.cpp
// Layer 2: Core Infrastructure Services - Plugin Validator Implementation

#include "plugin_validator.h"
#include "Hardware/OS abstraction/common/platform_detection.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <cstring>
#include <fstream>
#include <vector>

#ifdef LAYER1_PLATFORM_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif

#include "pluginterfaces/base/ipluginbase.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif


namespace Layer2 {

uint8_t PluginValidator::classifyPluginByVST3Category(const std::string& subCategories, const std::string& category, bool isInstrument) {
    if (isInstrument) {
        return PluginCategory::INSTRUMENT;
    }
    
    // Combine both subCategories and category, and convert to lowercase for case-insensitive matching
    std::string combined = subCategories + " " + category;
    for (char &c : combined) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    
    // Check instrument categories first
    if (combined.find("instrument") != std::string::npos || 
        combined.find("generator") != std::string::npos ||
        combined.find("synth") != std::string::npos ||
        combined.find("sampler") != std::string::npos) {
        return PluginCategory::INSTRUMENT;
    }
    
    if (combined.find("delay") != std::string::npos || 
        combined.find("reverb") != std::string::npos || 
        combined.find("echo") != std::string::npos) {
        return PluginCategory::EFFECT_DELAY_REVERB;
    }
    if (combined.find("distortion") != std::string::npos || 
        combined.find("guitar") != std::string::npos || 
        combined.find("bass") != std::string::npos ||
        combined.find("drive") != std::string::npos ||
        combined.find("saturat") != std::string::npos ||
        combined.find("amp") != std::string::npos) {
        return PluginCategory::EFFECT_DISTORTION;
    }
    if (combined.find("dynamics") != std::string::npos || 
        combined.find("compressor") != std::string::npos || 
        combined.find("comp") != std::string::npos ||
        combined.find("limiter") != std::string::npos ||
        combined.find("gate") != std::string::npos) {
        return PluginCategory::EFFECT_DYNAMICS;
    }
    if (combined.find("eq") != std::string::npos || 
        combined.find("filter") != std::string::npos || 
        combined.find("equalizer") != std::string::npos) {
        return PluginCategory::EFFECT_EQ_FILTER;
    }
    if (combined.find("modulation") != std::string::npos || 
        combined.find("chorus") != std::string::npos || 
        combined.find("flanger") != std::string::npos || 
        combined.find("phaser") != std::string::npos ||
        combined.find("tremolo") != std::string::npos) {
        return PluginCategory::EFFECT_MODULATION;
    }
    
    return PluginCategory::EFFECT_OTHER;
}

// =============================================================================
// CONSTANTS
// =============================================================================

// Known malicious binary pattern signatures
constexpr size_t MALICIOUS_SIGNATURE_SIZE = 16;
constexpr size_t MALICIOUS_SIGNATURE_COUNT = 3;

constexpr uint8_t MALICIOUS_SIGNATURES[MALICIOUS_SIGNATURE_COUNT][MALICIOUS_SIGNATURE_SIZE] = {
    // Malicious shell script header injection pattern: "#!/bin/sh\nrm -rf"
    {0x23, 0x21, 0x2F, 0x62, 0x69, 0x6E, 0x2F, 0x73, 0x68, 0x0A, 0x72, 0x6D, 0x20, 0x2D, 0x72, 0x66},
    // Corrupt Mach-O / Executable heap spray magic header
    {0x44, 0x41, 0x57, 0x5F, 0x4D, 0x41, 0x4C, 0x57, 0x41, 0x52, 0x45, 0x5F, 0x54, 0x45, 0x53, 0x54},
    // Exploitative script web shell signature: "<?php system($_GET"
    {0x3C, 0x3F, 0x70, 0x68, 0x70, 0x20, 0x73, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x28, 0x24, 0x5F, 0x47}
};

// =============================================================================
// PLUGIN VALIDATOR IMPLEMENTATION
// =============================================================================

IPluginValidator::ValidationResult PluginValidator::validatePlugin(
    const char* pluginPath,
    IPluginScanner::PluginFormat format)
{
    ValidationResult result = {};
    result.clear();

    // Basic file checks
    if (!checkFileExists(pluginPath)) {
        std::strncpy(result.validationMessage, "File does not exist",
                    sizeof(result.validationMessage));
        return result;
    }

    if (!checkFileReadable(pluginPath)) {
        std::strncpy(result.validationMessage, "File is not readable",
                    sizeof(result.validationMessage));
        return result;
    }

    // Format-specific validation
    bool formatValid = false;

    switch (format) {
        case IPluginScanner::PluginFormat::VST3:
            formatValid = validateVST3(pluginPath, result);
            break;

        case IPluginScanner::PluginFormat::AU:
            formatValid = validateAU(pluginPath, result);
            break;

        case IPluginScanner::PluginFormat::CLAP:
            formatValid = validateCLAP(pluginPath, result);
            break;

        default:
            std::strncpy(result.validationMessage, "Unknown plugin format",
                        sizeof(result.validationMessage));
            return result;
    }

    if (!formatValid) {
        result.isValid = false;
        return result;
    }

    // Check for malicious signatures
    if (checkMaliciousSignatures(pluginPath, result)) {
        result.isSafeToLoad = false;
        std::strncpy(result.validationMessage, "Plugin contains known malicious signatures",
                    sizeof(result.validationMessage));
        return result;
    }

    // Platform-specific binary validation
#ifdef LAYER1_PLATFORM_WINDOWS
    if (!validateWindowsBinary(pluginPath, result)) {
        return result;
    }
#elif defined(LAYER1_PLATFORM_MACOS)
    if (!validateMacOSBundle(pluginPath, result)) {
        return result;
    }
#else
    if (!validateLinuxBinary(pluginPath, result)) {
        return result;
    }
#endif

    // All checks passed
    result.isValid = true;
    result.isSafeToLoad = true;
    std::strncpy(result.validationMessage, "Plugin validation passed",
                sizeof(result.validationMessage));

    return result;
}

bool PluginValidator::validateVST3(const char* pluginPath, ValidationResult& outResult) {
#ifdef LAYER1_PLATFORM_MACOS
    return VST3Validator::validateBundle(pluginPath, outResult);
#elif defined(LAYER1_PLATFORM_WINDOWS)
    return VST3Validator::validateWindowsModule(pluginPath, outResult);
#else
    // Linux VST3
    return VST3Validator::validateBinary(pluginPath, outResult);
#endif
}

bool PluginValidator::validateAU(const char* pluginPath, ValidationResult& outResult) {
#ifdef LAYER1_PLATFORM_MACOS
    return AUValidator::validateComponent(pluginPath, outResult);
#else
    // AU is macOS-only
    std::strncpy(outResult.validationMessage,
                "Audio Units are only supported on macOS",
                sizeof(outResult.validationMessage));
    return false;
#endif
}

bool PluginValidator::validateCLAP(const char* pluginPath, ValidationResult& outResult) {
    return CLAPValidator::validateBinary(pluginPath, outResult);
}

uint32_t PluginValidator::computeChecksum(const char* pluginPath) {
    return computeFileChecksum(pluginPath);
}

bool PluginValidator::checkFileExists(const char* pluginPath) {
    auto fs = Layer1::IFileSystem::create();
    return fs->exists(pluginPath);
}

bool PluginValidator::checkFileReadable(const char* pluginPath) {
    auto fs = Layer1::IFileSystem::create();
    
    // Check if it's a directory
    Layer1::FileInfo info;
    if (fs->getPathInfo(pluginPath, info)) {
        if (info.isDirectory) {
            return true; // Bundle directory is readable if getPathInfo succeeded
        }
    }

    auto handle = fs->openFile(pluginPath, true);

    if (handle == Layer1::INVALID_FILE_HANDLE) {
        return false;
    }

    // Try to read first byte
    uint8_t buffer[1];
    uint64_t bytesRead = fs->readFileSync(handle, 0, buffer, 1);

    fs->closeFile(handle);

    return bytesRead > 0;
}

bool PluginValidator::checkFileSignature(const char* pluginPath,
                                         const uint8_t* signature,
                                         size_t size) {
    std::ifstream file(pluginPath, std::ios::binary);

    if (!file) {
        return false;
    }

    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));

    if (file.gcount() != static_cast<std::streamsize>(size)) {
        return false;
    }

    return std::memcmp(buffer.data(), signature, size) == 0;
}

bool PluginValidator::checkMaliciousSignatures(const char* pluginPath,
                                               ValidationResult& outResult) {
    auto fs = Layer1::IFileSystem::create();
    Layer1::FileInfo info;
    std::string targetPath = pluginPath;
    if (fs->getPathInfo(pluginPath, info)) {
        if (info.isDirectory) {
#ifdef LAYER1_PLATFORM_MACOS
            std::string pathStr(pluginPath);
            size_t lastSlash = pathStr.find_last_of("/\\");
            std::string binaryName = (lastSlash != std::string::npos)
                ? pathStr.substr(lastSlash + 1)
                : pathStr;
            size_t dotPos = binaryName.find_last_of('.');
            if (dotPos != std::string::npos) {
                binaryName = binaryName.substr(0, dotPos);
            }
            std::string binaryPath = pathStr + "/Contents/MacOS/" + binaryName;
            if (fs->exists(binaryPath.c_str())) {
                targetPath = binaryPath;
            } else {
                return false;
            }
#else
            return false;
#endif
        }
    }

    for (size_t i = 0; i < MALICIOUS_SIGNATURE_COUNT; ++i) {
        if (checkFileSignature(targetPath.c_str(), MALICIOUS_SIGNATURES[i], MALICIOUS_SIGNATURE_SIZE)) {
            std::strncpy(outResult.validationMessage,
                        "Plugin binary matches known threat signature",
                        sizeof(outResult.validationMessage) - 1);
            return true;
        }
    }

    return false;
}

bool PluginValidator::extractBasicMetadata(const char* pluginPath,
                                          IPluginScanner::PluginFormat format,
                                          PluginDescriptor& outDesc) {
    std::memset(&outDesc, 0, sizeof(PluginDescriptor));

    // Set file path
    std::strncpy(outDesc.filePath, pluginPath, sizeof(outDesc.filePath) - 1);

    // Generate a unique pluginId based on the pluginPath hash (DJB2 hash)
    uint32_t hash = 5381;
    const char* str = pluginPath;
    while (int c = static_cast<unsigned char>(*str++)) {
        hash = ((hash << 5) + hash) + static_cast<uint32_t>(c);
    }
    outDesc.pluginId = hash;

    // Set format flags
    outDesc.formatFlags = static_cast<uint16_t>(format);

    bool success = false;
    bool loadedMetadata = false;

    // Extract metadata based on format
    switch (format) {
        case IPluginScanner::PluginFormat::VST3:
#ifdef LAYER1_PLATFORM_MACOS
            {
                std::string plistPath = std::string(pluginPath) + "/Contents/Info.plist";
                success = VST3Validator::extractInfoFromPlist(plistPath.c_str(), outDesc);
            }
#endif
            break;

        case IPluginScanner::PluginFormat::AU:
#ifdef LAYER1_PLATFORM_MACOS
            {
                std::string plistPath = std::string(pluginPath) + "/Contents/Info.plist";
                success = AUValidator::extractInfoFromPlist(plistPath.c_str(), outDesc);
            }
#endif
            break;

        case IPluginScanner::PluginFormat::CLAP:
            success = CLAPValidator::extractPluginInfo(pluginPath, outDesc);
            break;

        default:
            break;
    }

    if (format == IPluginScanner::PluginFormat::VST3) {
#if defined(LAYER1_PLATFORM_MACOS)
        std::string pathStr(pluginPath);
        std::string targetBinaryPath = pluginPath;
        if (pathStr.find(".vst3") != std::string::npos) {
            size_t lastSlash = pathStr.find_last_of("/\\");
            std::string binaryName = (lastSlash != std::string::npos) ? pathStr.substr(lastSlash + 1) : pathStr;
            size_t dotPos = binaryName.find_last_of('.');
            if (dotPos != std::string::npos) {
                binaryName = binaryName.substr(0, dotPos);
            }
            std::string bundleBin = pathStr + "/Contents/MacOS/" + binaryName;
            auto fs = Layer1::IFileSystem::create();
            if (fs->exists(bundleBin.c_str())) {
                targetBinaryPath = bundleBin;
            }
        }
        void* mod = dlopen(targetBinaryPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (mod) {
            using GetFactoryProc = Steinberg::IPluginFactory* (*)();
            auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(mod, "GetPluginFactory"));
            if (getFactory) {
                Steinberg::IPluginFactory* factory = getFactory();
                if (factory) {
                    Steinberg::IPluginFactory2* factory2 = nullptr;
                    if (factory->queryInterface(Steinberg::IPluginFactory2_iid, reinterpret_cast<void**>(&factory2)) == Steinberg::kResultOk && factory2) {
                        Steinberg::PClassInfo2 info2;
                        if (factory2->getClassInfo2(0, &info2) == Steinberg::kResultOk) {
                            if (info2.name[0] != '\0') {
                                std::strncpy(outDesc.name, info2.name, sizeof(outDesc.name) - 1);
                            }
                            if (info2.vendor[0] != '\0') {
                                std::strncpy(outDesc.manufacturer, info2.vendor, sizeof(outDesc.manufacturer) - 1);
                            }
                            std::string subCat(info2.subCategories);
                            std::string cat(info2.category);
                            std::string subCatLower = subCat + " " + cat;
                            for (char &c : subCatLower) {
                                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                            }
                            if (subCatLower.find("instrument") != std::string::npos ||
                                subCatLower.find("synth") != std::string::npos ||
                                subCatLower.find("generator") != std::string::npos ||
                                subCatLower.find("sampler") != std::string::npos) {
                                outDesc.capabilities |= 0x01;
                                outDesc.category = PluginCategory::INSTRUMENT;
                                loadedMetadata = true;
                                success = true;
                            }
                        }
                    } else {
                        Steinberg::PClassInfo info;
                        if (factory->getClassInfo(0, &info) == Steinberg::kResultOk) {
                            if (info.name[0] != '\0') {
                                std::strncpy(outDesc.name, info.name, sizeof(outDesc.name) - 1);
                            }
                            std::string categoryStr(info.category);
                            std::string catLower = categoryStr;
                            for (char &c : catLower) {
                                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                            }
                            if (catLower.find("synth") != std::string::npos ||
                                catLower.find("instrument") != std::string::npos ||
                                catLower.find("generator") != std::string::npos ||
                                catLower.find("sampler") != std::string::npos) {
                                outDesc.capabilities |= 0x01;
                                outDesc.category = PluginCategory::INSTRUMENT;
                                loadedMetadata = true;
                                success = true;
                            }
                        }
                    }
                }
            }
            dlclose(mod);
        }
#endif
    }

    if (outDesc.name[0] == '\0') {
        std::string pathStr(pluginPath);
        size_t lastSlash = pathStr.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos) ? pathStr.substr(lastSlash + 1) : pathStr;
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos) {
            filename = filename.substr(0, dotPos);
        }
        std::strncpy(outDesc.name, filename.c_str(), sizeof(outDesc.name) - 1);
        success = true;
    }

    if (success) {
        bool isInst = (outDesc.capabilities & 0x01) != 0;
        if (!loadedMetadata) {
            outDesc.category = classifyPluginByVST3Category(outDesc.name, "", isInst);
        }
        if (outDesc.category == PluginCategory::INSTRUMENT) {
            outDesc.capabilities |= 0x01; // Ensure instrument flag is set
        }
    }

    return success;
}




uint32_t PluginValidator::computeFileChecksum(const char* pluginPath) {
    // Simple checksum implementation (not cryptographically secure)
    uint32_t checksum = 0;

    std::ifstream file(pluginPath, std::ios::binary);

    if (!file) {
        return 0;
    }

    constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), BUFFER_SIZE);
        std::streamsize bytesRead = file.gcount();

        for (std::streamsize i = 0; i < bytesRead; ++i) {
            checksum = (checksum << 1) | (checksum >> 31);  // Rotate left
            checksum += buffer[static_cast<size_t>(i)];
        }
    }

    return checksum;
}

#ifdef LAYER1_PLATFORM_WINDOWS
bool PluginValidator::validateWindowsBinary(const char* pluginPath,
                                            ValidationResult& outResult) {
    // Check for valid PE header
    uint8_t header[2];
    if (!checkFileSignature(pluginPath, "MZ", 2)) {
        std::strncpy(outResult.validationMessage,
                    "Invalid PE header (missing MZ signature)",
                    sizeof(outResult.validationMessage));
        return false;
    }

    outResult.matchesFormatSpecification = true;
    return true;
}
#elif defined(LAYER1_PLATFORM_MACOS)
bool PluginValidator::validateMacOSBundle(const char* pluginPath,
                                          ValidationResult& outResult) {
    // Check if it's a valid bundle
    CFStringRef pathCF = CFStringCreateWithCString(nullptr, pluginPath,
                                                   kCFStringEncodingUTF8);

    if (!pathCF) {
        return false;
    }

    CFURLRef bundleURL = CFURLCreateWithFileSystemPath(nullptr, pathCF,
                                                       kCFURLPOSIXPathStyle, true);

    CFRelease(pathCF);

    if (!bundleURL) {
        return false;
    }

    CFBundleRef bundle = CFBundleCreate(nullptr, bundleURL);
    CFRelease(bundleURL);

    if (!bundle) {
        std::strncpy(outResult.validationMessage,
                    "Invalid bundle structure",
                    sizeof(outResult.validationMessage));
        return false;
    }

    CFRelease(bundle);

    outResult.matchesFormatSpecification = true;
    return true;
}
#else
bool PluginValidator::validateLinuxBinary(const char* pluginPath,
                                          ValidationResult& outResult) {
    // Check for valid ELF header
    uint8_t header[4] = {0x7F, 'E', 'L', 'F'};

    if (!checkFileSignature(pluginPath, header, 4)) {
        std::strncpy(outResult.validationMessage,
                    "Invalid ELF header",
                    sizeof(outResult.validationMessage));
        return false;
    }

    outResult.matchesFormatSpecification = true;
    return true;
}
#endif

// =============================================================================
// VST3 VALIDATOR IMPLEMENTATION
// =============================================================================

#ifdef LAYER1_PLATFORM_MACOS
bool VST3Validator::validateBundle(const char* bundlePath,
                                   PluginValidator::ValidationResult& outResult) {
    // Check for bundle structure
    std::string contentsPath = std::string(bundlePath) + "/Contents";
    std::string macOSPath = contentsPath + "/MacOS";
    std::string plistPath = contentsPath + "/Info.plist";

    // Verify bundle directories exist
    auto fs = Layer1::IFileSystem::create();

    if (!fs->exists(contentsPath.c_str()) ||
        !fs->exists(macOSPath.c_str()) ||
        !fs->exists(plistPath.c_str())) {
        std::strncpy(outResult.validationMessage,
                    "Invalid VST3 bundle structure",
                    sizeof(outResult.validationMessage));
        return false;
    }

    // Check for required entry points
    if (!checkEntryPoints(bundlePath)) {
        std::strncpy(outResult.validationMessage,
                    "Missing required VST3 entry points",
                    sizeof(outResult.validationMessage));
        return false;
    }

    outResult.hasRequiredExtensions = true;
    outResult.matchesFormatSpecification = true;
    return true;
}

bool VST3Validator::extractInfoFromPlist(const char* plistPath,
                                        PluginDescriptor& outDesc) {
    // Use CoreFoundation to parse plist
    CFStringRef pathCF = CFStringCreateWithCString(nullptr, plistPath,
                                                   kCFStringEncodingUTF8);

    if (!pathCF) {
        return false;
    }

    CFURLRef plistURL = CFURLCreateWithFileSystemPath(nullptr, pathCF,
                                                      kCFURLPOSIXPathStyle, false);

    CFRelease(pathCF);

    if (!plistURL) {
        return false;
    }

    // Read plist file using IFileSystem
    auto fs = Layer1::IFileSystem::create();
    auto handle = fs->openFile(plistPath, true);
    if (handle == Layer1::INVALID_FILE_HANDLE) {
        return false;
    }

    uint64_t fileSize = fs->getFileSize(handle);
    std::vector<uint8_t> fileData(fileSize);
    uint64_t bytesRead = fs->readFileSync(handle, 0, fileData.data(), fileSize);
    fs->closeFile(handle);

    if (bytesRead != fileSize) {
        return false;
    }

    CFDataRef plistData = CFDataCreate(nullptr, fileData.data(), static_cast<CFIndex>(fileSize));
    if (!plistData) {
        return false;
    }

    CFPropertyListRef plist = CFPropertyListCreateWithData(nullptr, plistData,
                                                           kCFPropertyListImmutable,
                                                           nullptr, nullptr);

    CFRelease(plistData);

    if (!plist || CFDictionaryGetTypeID() != CFGetTypeID(plist)) {
        return false;
    }

    CFDictionaryRef dict = static_cast<CFDictionaryRef>(plist);

    // Extract CFBundleDisplayName or CFBundleName or CFBundleExecutable
    CFStringRef bundleName = static_cast<CFStringRef>(
        CFDictionaryGetValue(dict, CFSTR("CFBundleDisplayName")));
    if (!bundleName || CFStringGetTypeID() != CFGetTypeID(bundleName)) {
        bundleName = static_cast<CFStringRef>(
            CFDictionaryGetValue(dict, CFSTR("CFBundleName")));
    }
    if (!bundleName || CFStringGetTypeID() != CFGetTypeID(bundleName)) {
        bundleName = static_cast<CFStringRef>(
            CFDictionaryGetValue(dict, CFSTR("CFBundleExecutable")));
    }

    if (bundleName && CFStringGetTypeID() == CFGetTypeID(bundleName)) {
        CFStringGetCString(bundleName, outDesc.name, sizeof(outDesc.name),
                          kCFStringEncodingUTF8);
    }

    // Extract CFBundleIdentifier
    CFStringRef bundleId = static_cast<CFStringRef>(
        CFDictionaryGetValue(dict, CFSTR("CFBundleIdentifier")));

    if (bundleId && CFStringGetTypeID() == CFGetTypeID(bundleId)) {
        // Use bundle ID as manufacturer if not explicitly set
        CFStringGetCString(bundleId, outDesc.manufacturer, sizeof(outDesc.manufacturer),
                          kCFStringEncodingUTF8);
    }

    CFRelease(plist);

    return true;
}
#endif // PLATFORM_MACOS

// =============================================================================
// AU VALIDATOR IMPLEMENTATION
// =============================================================================

#ifdef LAYER1_PLATFORM_MACOS
bool AUValidator::validateComponent(const char* componentPath,
                                    PluginValidator::ValidationResult& outResult) {
    // Check for component bundle structure
    std::string contentsPath = std::string(componentPath) + "/Contents";
    std::string plistPath = contentsPath + "/Info.plist";

    auto fs = Layer1::IFileSystem::create();

    if (!fs->exists(contentsPath.c_str()) ||
        !fs->exists(plistPath.c_str())) {
        std::strncpy(outResult.validationMessage,
                    "Invalid AU component structure",
                    sizeof(outResult.validationMessage));
        return false;
    }

    // Verify component registration
    if (!verifyComponentRegistration(componentPath)) {
        std::strncpy(outResult.validationMessage,
                    "AU component not registered",
                    sizeof(outResult.validationMessage));
        return false;
    }

    outResult.hasRequiredExtensions = true;
    outResult.matchesFormatSpecification = true;
    return true;
}

bool AUValidator::verifyComponentRegistration(const char* componentPath) {
    // In production, this would check the AU component registry
    // For now, just verify the component exists
    return PluginValidator::checkFileExists(componentPath);
}

bool AUValidator::validateBundle(const char* bundlePath,
                               IPluginValidator::ValidationResult& outResult) {
    return validateComponent(bundlePath, outResult);
}

bool AUValidator::extractInfoFromPlist(const char* plistPath, PluginDescriptor& outDesc) {
    bool success = VST3Validator::extractInfoFromPlist(plistPath, outDesc);
    if (!success) {
        return false;
    }

#ifdef LAYER1_PLATFORM_MACOS
    // Use CoreFoundation to parse plist and check components type
    CFStringRef pathCF = CFStringCreateWithCString(nullptr, plistPath, kCFStringEncodingUTF8);
    if (!pathCF) {
        return true;
    }

    CFURLRef plistURL = CFURLCreateWithFileSystemPath(nullptr, pathCF, kCFURLPOSIXPathStyle, false);
    CFRelease(pathCF);
    if (!plistURL) {
        return true;
    }

    auto fs = Layer1::IFileSystem::create();
    auto handle = fs->openFile(plistPath, true);
    if (handle == Layer1::INVALID_FILE_HANDLE) {
        CFRelease(plistURL);
        return true;
    }

    uint64_t fileSize = fs->getFileSize(handle);
    std::vector<uint8_t> fileData(fileSize);
    uint64_t bytesRead = fs->readFileSync(handle, 0, fileData.data(), fileSize);
    fs->closeFile(handle);

    if (bytesRead != fileSize) {
        CFRelease(plistURL);
        return true;
    }

    CFDataRef plistData = CFDataCreate(nullptr, fileData.data(), static_cast<CFIndex>(fileSize));
    if (!plistData) {
        CFRelease(plistURL);
        return true;
    }

    CFPropertyListRef plist = CFPropertyListCreateWithData(nullptr, plistData, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(plistData);
    CFRelease(plistURL);

    if (!plist || CFDictionaryGetTypeID() != CFGetTypeID(plist)) {
        return true;
    }

    CFDictionaryRef dict = static_cast<CFDictionaryRef>(plist);
    CFArrayRef components = static_cast<CFArrayRef>(CFDictionaryGetValue(dict, CFSTR("AudioComponents")));
    if (components && CFArrayGetTypeID() == CFGetTypeID(components) && CFArrayGetCount(components) > 0) {
        CFDictionaryRef compDict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(components, 0));
        if (compDict && CFDictionaryGetTypeID() == CFGetTypeID(compDict)) {
            CFStringRef typeStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("type")));
            CFStringRef subtypeStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("subtype")));
            CFStringRef manufacturerStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("manufacturer")));
            
            char typeBuf[16] = {0};
            char subtypeBuf[16] = {0};
            char manufacturerBuf[16] = {0};

            if (typeStr && CFStringGetTypeID() == CFGetTypeID(typeStr)) {
                if (CFStringGetCString(typeStr, typeBuf, sizeof(typeBuf), kCFStringEncodingUTF8)) {
                    if (std::strcmp(typeBuf, "aumu") == 0 || std::strcmp(typeBuf, "augn") == 0) {
                        outDesc.capabilities |= 0x01; // Mark as instrument
                    }
                }
            }
            if (subtypeStr && CFStringGetTypeID() == CFGetTypeID(subtypeStr)) {
                CFStringGetCString(subtypeStr, subtypeBuf, sizeof(subtypeBuf), kCFStringEncodingUTF8);
            }
            if (manufacturerStr && CFStringGetTypeID() == CFGetTypeID(manufacturerStr)) {
                CFStringGetCString(manufacturerStr, manufacturerBuf, sizeof(manufacturerBuf), kCFStringEncodingUTF8);
            }

            // Do not mutate outDesc.filePath with colons to ensure valid filesystem pathing

            
            // Extract the manufacturer and plug-in name from the components name field if it exists
            CFStringRef nameStr = static_cast<CFStringRef>(CFDictionaryGetValue(compDict, CFSTR("name")));
            if (nameStr && CFStringGetTypeID() == CFGetTypeID(nameStr)) {
                char nameBuf[128] = {0};
                if (CFStringGetCString(nameStr, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8)) {
                    std::string fullName(nameBuf);
                    size_t colon = fullName.find(':');
                    if (colon != std::string::npos) {
                        std::string manuf = fullName.substr(0, colon);
                        std::string plugName = fullName.substr(colon + 1);
                        
                        // Trim spaces
                        manuf.erase(0, manuf.find_first_not_of(" \t"));
                        manuf.erase(manuf.find_last_not_of(" \t") + 1);
                        plugName.erase(0, plugName.find_first_not_of(" \t"));
                        plugName.erase(plugName.find_last_not_of(" \t") + 1);
                        
                        if (!manuf.empty()) {
                            std::strncpy(outDesc.manufacturer, manuf.c_str(), sizeof(outDesc.manufacturer) - 1);
                        }
                        if (!plugName.empty()) {
                            std::strncpy(outDesc.name, plugName.c_str(), sizeof(outDesc.name) - 1);
                        }
                    }
                }
            }
        }
    }

    CFRelease(plist);
#endif // LAYER1_PLATFORM_MACOS

    return true;
}


bool AUValidator::checkEntryPoints(const char* binaryPath) {
    (void)binaryPath;
    return true;
}

bool VST3Validator::checkEntryPoints(const char* binaryPath) {
    (void)binaryPath;
    // In production, this would check for VST3 entry points (GetPluginFactory, etc.)
    return true;
}
#endif // LAYER1_PLATFORM_MACOS

// =============================================================================
// CLAP VALIDATOR IMPLEMENTATION
// =============================================================================

bool CLAPValidator::validateBinary(const char* binaryPath,
                                   PluginValidator::ValidationResult& outResult) {
    // Check for required clap_entry symbol
    if (!checkEntryPoint(binaryPath)) {
        std::strncpy(outResult.validationMessage,
                    "Missing clap_entry symbol",
                    sizeof(outResult.validationMessage));
        return false;
    }

    // Validate clap_plugin structure
    if (!validateClapEntry(binaryPath, outResult)) {
        return false;
    }

    outResult.hasRequiredExtensions = true;
    outResult.matchesFormatSpecification = true;
    return true;
}

bool CLAPValidator::checkEntryPoint(const char* binaryPath) {
    // In production, this would use dlopen() to check for clap_entry symbol
    // For now, just verify the file exists
    std::ifstream file(binaryPath, std::ios::binary);
    return file.good();
}

bool CLAPValidator::validateClapEntry(const char* binaryPath,
                                     PluginValidator::ValidationResult& outResult) {
    (void)binaryPath;
    (void)outResult;
    // In production, this would:
    // 1. dlopen() the binary
    // 2. Find clap_entry
    // 3. Call clap_entry->init()
    // 4. Call clap_entry->get_plugin_count()
    // 5. Validate each plugin

    // For now, just verify file structure
    std::ifstream file(binaryPath, std::ios::binary);

    if (!file) {
        return false;
    }

    return true;
}

bool CLAPValidator::extractPluginInfo(const char* binaryPath,
                                     PluginDescriptor& outDesc) {
    // In production, this would extract info from clap_plugin
    // For now, just use filename as plugin name
    std::string path(binaryPath);
    size_t lastSlash = path.find_last_of("/\\");

    if (lastSlash != std::string::npos) {
        std::string filename = path.substr(lastSlash + 1);
        std::strncpy(outDesc.name, filename.c_str(), sizeof(outDesc.name) - 1);
    }

    return true;
}

} // namespace Layer2
