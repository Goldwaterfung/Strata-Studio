// filesystem_factory.cpp
// Layer 1: Hardware/OS Abstraction - File System Factory

#include "ifile_system.h"
#include <memory>

// Include concrete implementation
// Currently we only have std_file_system.cpp which is cross-platform
// In the future we might have platform-specific ones
#include "std_file_system.cpp"

namespace Layer1 {

std::unique_ptr<IFileSystem> IFileSystem::create() {
    return std::make_unique<StdFileSystem>();
}

} // namespace Layer1
