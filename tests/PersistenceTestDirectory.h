#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace persistence_test
{
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::filesystem::path& parent)
    {
        std::filesystem::create_directories(parent);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = parent / ("persistence-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Could not reserve a persistence test directory.");
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};
} // namespace persistence_test
