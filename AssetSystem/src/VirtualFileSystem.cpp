#include "assets/VirtualFileSystem.h"

#include <algorithm>
#include <iostream>

namespace ce::assets {

juce::String VirtualFileSystem::NormalizePath(const juce::String& virtualPath) {
    return virtualPath.startsWith("/") ? virtualPath.substring(1) : virtualPath;
}

bool VirtualFileSystem::Mount(const juce::File& zipFile, int priority) {
    if (!zipFile.existsAsFile()) {
        std::cout << "[vfs] mount failed, file not found: " << zipFile.getFullPathName() << std::endl;
        return false;
    }

    auto zip = std::make_unique<juce::ZipFile>(zipFile);
    if (zip->getNumEntries() <= 0) {
        std::cout << "[vfs] mount failed, no entries (not a zip, or empty): " << zipFile.getFullPathName()
                   << std::endl;
        return false;
    }

    const int entryCount = zip->getNumEntries();

    const juce::ScopedLock lock(lock_);
    mounts_.push_back(MountedArchive{ std::move(zip), priority, zipFile });
    std::stable_sort(mounts_.begin(), mounts_.end(),
                      [](const MountedArchive& a, const MountedArchive& b) { return a.priority > b.priority; });

    std::cout << "[vfs] mounted " << zipFile.getFileName() << " (" << entryCount << " entries, priority "
               << priority << ")" << std::endl;
    return true;
}

void VirtualFileSystem::UnmountAll() {
    const juce::ScopedLock lock(lock_);
    mounts_.clear();
}

bool VirtualFileSystem::Exists(const juce::String& virtualPath) const {
    const auto normalized = NormalizePath(virtualPath);

    const juce::ScopedLock lock(lock_);
    for (const auto& mount : mounts_) {
        if (mount.zip->getEntry(normalized) != nullptr) {
            return true;
        }
    }
    return false;
}

bool VirtualFileSystem::ReadFile(const juce::String& virtualPath, juce::MemoryBlock& outData) const {
    const auto normalized = NormalizePath(virtualPath);

    const juce::ScopedLock lock(lock_);
    for (const auto& mount : mounts_) {
        const auto* entry = mount.zip->getEntry(normalized);
        if (entry == nullptr) {
            continue;
        }

        std::unique_ptr<juce::InputStream> stream(mount.zip->createStreamForEntry(*entry));
        if (stream == nullptr) {
            std::cout << "[vfs] found " << virtualPath << " in " << mount.sourceFile.getFileName()
                       << " but couldn't open a stream for it" << std::endl;
            continue;
        }

        outData.reset();
        stream->readIntoMemoryBlock(outData);
        std::cout << "[vfs] read " << virtualPath << " from " << mount.sourceFile.getFileName() << " (priority "
                   << mount.priority << ", " << outData.getSize() << " bytes)" << std::endl;
        return true;
    }

    std::cout << "[vfs] not found in any mount: " << virtualPath << std::endl;
    return false;
}

} // namespace ce::assets
