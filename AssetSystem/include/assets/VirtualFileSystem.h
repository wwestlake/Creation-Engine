#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

namespace ce::assets {

// Mounts one or more .zip archives into a single virtual directory tree
// and streams entries straight into memory — no disk extraction. This is
// the foundation spec section 5.2's layered content (base + override
// patches) will eventually build on: Mount() takes a priority, and a
// higher-priority mount shadows a same-path file from a lower-priority
// one (see VFS3 in the render-pipeline follow-on plan for a worked
// example).
//
// Thread-safety: every method takes lock_ for its duration. JUCE's
// juce::ZipFile is itself safe for concurrent entry reads when
// constructed from a File (per its own header docs), but mounts_ (the
// list of archives) is ours to protect, and doing that consistently
// through every method — not just the ones that "obviously" need it —
// is what the interface promises callers.
class VirtualFileSystem final {
public:
    VirtualFileSystem() = default;
    ~VirtualFileSystem() = default;

    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // Opens zipFile and adds it to the mount list. Higher priority is
    // searched first. Returns false (logged) if the archive can't be
    // opened or has no entries.
    bool Mount(const juce::File& zipFile, int priority = 0);
    void UnmountAll();

    bool Exists(const juce::String& virtualPath) const;

    // Reads the full contents of virtualPath from the highest-priority
    // mount that has it. Returns false (logged) if not found anywhere.
    bool ReadFile(const juce::String& virtualPath, juce::MemoryBlock& outData) const;

private:
    struct MountedArchive {
        std::unique_ptr<juce::ZipFile> zip;
        int priority = 0;
        juce::File sourceFile;
    };

    static juce::String NormalizePath(const juce::String& virtualPath);

    mutable juce::CriticalSection lock_;
    std::vector<MountedArchive> mounts_; // kept sorted by priority, highest first.
};

} // namespace ce::assets
