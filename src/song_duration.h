#pragma once

#include <filesystem>
#include <optional>

namespace meteor {

// Reads only the final note tick for the standalone UI preview.
std::optional<int> songLastTick(const std::filesystem::path& path);

} // namespace meteor
