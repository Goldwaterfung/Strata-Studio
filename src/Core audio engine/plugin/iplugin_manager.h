// src/Core audio engine/plugin/iplugin_manager.h
#pragma once

#include "iplugin.h"
#include <vector>
#include <string>

namespace Layer3 {

class IPluginManager {
public:
    virtual ~IPluginManager() = default;

    // Create plugin manager instance
    static std::unique_ptr<IPluginManager> create();

    // Scan directories for new plugins
    // Should ideally spawn external scanner process for stability
    virtual void scanForPlugins(const std::vector<std::string>& paths) = 0;

    // Get list of all discovered and valid plugins
    virtual std::vector<PluginDescriptor> getAvailablePlugins() const = 0;

    // Instantiation
    virtual std::unique_ptr<IPlugin> instantiatePlugin(const PluginDescriptor& descriptor) = 0;

    // Scan progress and status queries
    virtual bool isScanning() const = 0;
    virtual float getScanProgress() const = 0;
    virtual std::string getCurrentlyScanningPlugin() const = 0;
};

} // namespace Layer3
