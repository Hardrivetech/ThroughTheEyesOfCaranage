#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace carnage::assets {

// Directory holding shaders/ and meshes/. Points at the source tree (set by
// CMake) so edits are picked up without copying; falls back to ./assets.
const std::filesystem::path& root();

std::expected<std::string, std::string> readText(const std::filesystem::path& relative);

} // namespace carnage::assets
