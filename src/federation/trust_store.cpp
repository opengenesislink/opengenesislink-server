#include "opengenesis/federation/trust_store.hpp"
#include "opengenesis/platform/filesystem.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::federation {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool safe_text(const std::string_view value) {
    return !value.empty() && value.size() <= 2048 &&
           value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

bool hex_key(const std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    });
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

} // namespace

FederationTrustStore::FederationTrustStore(std::string path) : path_(std::move(path)) {
    load();
}

bool FederationTrustStore::trust(FederationPeer peer, std::string& reason) {
    if (!safe_text(peer.grid_id) || !safe_text(peer.base_url) ||
        !peer.base_url.starts_with("https://") || !hex_key(peer.public_key_hex)) {
        reason = "invalid-federation-peer";
        return false;
    }

    peer.trusted = true;
    peer.revoked = false;
    peer.updated_unix = unix_now();

    std::scoped_lock lock(mutex_);
    peers_[peer.grid_id] = std::move(peer);
    persist_locked();
    reason.clear();
    return true;
}

bool FederationTrustStore::revoke(const std::string_view grid_id) {
    std::scoped_lock lock(mutex_);
    const auto it = peers_.find(std::string{grid_id});
    if (it == peers_.end()) return false;
    it->second.trusted = false;
    it->second.revoked = true;
    it->second.updated_unix = unix_now();
    persist_locked();
    return true;
}

std::optional<FederationPeer> FederationTrustStore::find(const std::string_view grid_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = peers_.find(std::string{grid_id});
    return it == peers_.end() ? std::nullopt : std::optional<FederationPeer>{it->second};
}

bool FederationTrustStore::is_trusted(const std::string_view grid_id,
                                      const std::string_view public_key_hex) const {
    const auto peer = find(grid_id);
    return peer && peer->trusted && !peer->revoked &&
           peer->public_key_hex == public_key_hex;
}

std::vector<FederationPeer> FederationTrustStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<FederationPeer> output;
    output.reserve(peers_.size());
    for (const auto& [_, peer] : peers_) output.push_back(peer);
    std::sort(output.begin(), output.end(), [](const FederationPeer& a, const FederationPeer& b) {
        return a.grid_id < b.grid_id;
    });
    return output;
}

void FederationTrustStore::load() {
    std::scoped_lock lock(mutex_);
    peers_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 6) continue;
        try {
            FederationPeer peer{
                .grid_id = fields[0],
                .base_url = fields[1],
                .public_key_hex = fields[2],
                .trusted = fields[3] == "1",
                .revoked = fields[4] == "1",
                .updated_unix = std::stoll(fields[5])};
            if (!safe_text(peer.grid_id) || !safe_text(peer.base_url) ||
                !hex_key(peer.public_key_hex)) {
                continue;
            }
            peers_[peer.grid_id] = std::move(peer);
        } catch (...) {
        }
    }
}

void FederationTrustStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write federation trust store");

    output << "# OpenGenesisLINK OGL-FED trust store v1\n";
    std::vector<FederationPeer> peers;
    peers.reserve(peers_.size());
    for (const auto& [_, peer] : peers_) peers.push_back(peer);
    std::sort(peers.begin(), peers.end(), [](const FederationPeer& a, const FederationPeer& b) {
        return a.grid_id < b.grid_id;
    });
    for (const auto& peer : peers) {
        output << peer.grid_id << '\t' << peer.base_url << '\t' << peer.public_key_hex << '\t'
               << (peer.trusted ? '1' : '0') << '\t' << (peer.revoked ? '1' : '0') << '\t'
               << peer.updated_unix << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush federation trust store");

    opengenesis::platform::replace_file(temp, path);
}

} // namespace opengenesis::federation
