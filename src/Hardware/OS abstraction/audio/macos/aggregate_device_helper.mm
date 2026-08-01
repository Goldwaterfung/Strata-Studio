// aggregate_device_helper.mm
// Layer 1: Hardware/OS Abstraction - macOS Aggregate Device Helper

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <vector>
#include <string>

namespace Layer1 {

class AggregateDeviceHelper {
public:
    static std::vector<AudioDeviceID> getSubDevices(AudioDeviceID aggregateID) {
        std::vector<AudioDeviceID> subDevices;
        
        AudioObjectPropertyAddress propertyAddress = {
            kAudioAggregateDevicePropertyActiveSubDeviceList,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        
        UInt32 dataSize = 0;
        OSStatus status = AudioObjectGetPropertyDataSize(aggregateID, &propertyAddress, 0, nullptr, &dataSize);
        if (status != noErr) return subDevices;
        
        uint32_t count = dataSize / sizeof(AudioDeviceID);
        subDevices.resize(count);
        status = AudioObjectGetPropertyData(aggregateID, &propertyAddress, 0, nullptr, &dataSize, subDevices.data());
        
        return subDevices;
    }

    static std::string getDeviceName(AudioDeviceID deviceID) {
        CFStringRef cfName = nullptr;
        UInt32 dataSize = sizeof(CFStringRef);
        AudioObjectPropertyAddress propertyAddress = {
            kAudioObjectPropertyName,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        
        OSStatus status = AudioObjectGetPropertyData(deviceID, &propertyAddress, 0, nullptr, &dataSize, &cfName);
        if (status != noErr || !cfName) return "Unknown Device";
        
        char buffer[256];
        CFStringGetCString(cfName, buffer, sizeof(buffer), kCFStringEncodingUTF8);
        CFRelease(cfName);
        
        return std::string(buffer);
    }
};

} // namespace Layer1
