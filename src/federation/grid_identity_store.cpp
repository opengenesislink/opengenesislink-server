#include "opengenesis/federation/grid_identity_store.hpp"

#include "opengenesis/platform/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::federation {
namespace {

bool valid_text(const std::string_view value, const std::size_t max_size) {
    return !value.empty() && value.size() <= max_size &&
           value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\t') == std::string_view::npos;
}

bool valid_grid_id(const std::string_view value) {
    return valid_text(value, 255) &&
           value.find(' ') == std::string_view::npos;
}

bool valid_base_url(const std::string_view value) {
    return valid_text(value, 2048) &&
           (value.starts_with("https://") || value.starts_with("http://"));
}

} // namespace

GridIdentityStore::GridIdentityStore(std::string path, std::string grid_id, std::string base_url)
    : path_(std::move(path)) {
    if (!valid_grid_id(grid_id)) throw std::invalid_argument("invalid federation.grid_id");
    if (!valid_base_url(base_url)) throw std::invalid_argument("invalid federation.base_url");
    identity_.grid_id = std::move(grid_id);
    identity_.base_url = std::move(base_url);
    load_or_create();
}

LocalGridIdentity GridIdentityStore::identity() const {
    std::scoped_lock lock(mutex_);
    return identity_;
}

void GridIdentityStore::rotate_keys() {
    std::scoped_lock lock(mutex_);
    identity_.keys = generate_grid_key_pair();
    persist_locked();
}

void GridIdentityStore::load_or_create() {
    std::scoped_lock lock(mutex_);
    std::ifstream input(path_);
    if (input) {
        std::string stored_grid;
        std::string stored_url;
        std::string public_key;
        std::string private_key;
        std::getline(input, stored_grid);
        std::getline(input, stored_url);
        std::getline(input, public_key);
        std::getline(input, private_key);
        if (!input.eof() && input.fail()) throw std::runtime_error("cannot read federation identity");
        if (stored_grid != identity_.grid_id || stored_url != identity_.base_url) {
            throw std::runtime_error("federation identity does not match configured grid");
        }
        if (public_key.size() != 64 || private_key.size() != 64) {
            throw std::runtime_error("invalid persisted federation key pair");
        }
        identity_.keys = {.public_key_hex = std::move(public_key),
                          .private_key_hex = std::move(private_key)};
        return;
    }

    identity_.keys = generate_grid_key_pair();
    persist_locked();
}

void GridIdentityStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write federation identity");
    output << identity_.grid_id << '\n'
           << identity_.base_url << '\n'
           << identity_.keys.public_key_hex << '\n'
           << identity_.keys.private_key_hex << '\n';
    output.close();
    if (!output) throw std::runtime_error("cannot flush federation identity");
    platform::replace_file(temp, path);
}

} // namespace opengenesis::federation
