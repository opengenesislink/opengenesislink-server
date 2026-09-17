#pragma once

#include <filesystem>

namespace opengenesis::platform {

// Replaces destination with source using the native operating-system primitive.
// source and destination must reside on the same filesystem/volume.
void replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination);

} // namespace opengenesis::platform
