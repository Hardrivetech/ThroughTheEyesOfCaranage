#include "gfx/Assets.hpp"

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

#ifndef CARNAGE_ASSET_DIR
#define CARNAGE_ASSET_DIR "assets"
#endif

namespace carnage::assets {

const std::filesystem::path& root() {
    static const std::filesystem::path path = [] {
        std::error_code ec;
        const std::filesystem::path configured{CARNAGE_ASSET_DIR};
        if (std::filesystem::is_directory(configured, ec)) return configured;
        return std::filesystem::path{"assets"};
    }();
    return path;
}

std::expected<std::string, std::string> readText(const std::filesystem::path& relative) {
    const std::filesystem::path full = root() / relative;
    std::ifstream in(full, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot open '{}'", full.string()));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace carnage::assets
