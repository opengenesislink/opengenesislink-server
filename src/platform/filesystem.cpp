#include "opengenesis/platform/filesystem.hpp"

#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace opengenesis::platform {

void replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination) {
#ifdef _WIN32
    const DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (::MoveFileExW(source.c_str(), destination.c_str(), flags) == 0) {
        throw std::runtime_error(
            "cannot replace persistent file: Windows error " +
            std::to_string(static_cast<unsigned long>(::GetLastError())));
    }
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw std::runtime_error("cannot replace persistent file: " + error.message());
    }
#endif
}

} // namespace opengenesis::platform
