#include "opengenesis/avatar/appearance_store.hpp"

#include "opengenesis/platform/filesystem.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::avatar {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2] = digits[(c >> 4U) & 0x0fU];
        output[i * 2 + 1] = digits[c & 0x0fU];
    }
    return output;
}

unsigned char nibble(const char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
    throw std::runtime_error("invalid appearance hex");
}

std::string unhex(std::string_view value) {
    if ((value.size() % 2) != 0) throw std::runtime_error("invalid appearance hex length");
    std::string output(value.size() / 2, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>((nibble(value[i * 2]) << 4U) | nibble(value[i * 2 + 1]));
    }
    return output;
}

std::vector<std::string> tabs(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        result.push_back(line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

} // namespace

AppearanceStore::AppearanceStore(std::string path) : path_(std::move(path)) {
    load();
}

bool AppearanceStore::valid_field(const std::string_view value, const std::size_t max_size) {
    return !value.empty() && value.size() <= max_size &&
           value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

AvatarAppearance AppearanceStore::ensure(std::string user_id) {
    if (!valid_field(user_id, 256)) throw std::invalid_argument("invalid appearance user");
    std::scoped_lock lock(mutex_);
    auto& appearance = by_user_[user_id];
    if (appearance.user_id.empty()) {
        appearance.user_id = std::move(user_id);
        appearance.revision = 1;
        appearance.updated_unix = unix_now();
        persist_locked();
    }
    return appearance;
}

std::optional<AvatarAppearance> AppearanceStore::find(const std::string_view user_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = by_user_.find(std::string{user_id});
    return it == by_user_.end() ? std::nullopt : std::optional<AvatarAppearance>{it->second};
}

std::optional<AvatarAppearance> AppearanceStore::set_wearable(
    std::string user_id,
    std::string slot,
    std::string item_id,
    std::string asset_id,
    std::string& reason) {
    if (!valid_field(user_id, 256) || !valid_field(slot, 64) ||
        !valid_field(item_id, 256) || !valid_field(asset_id, 256)) {
        reason = "invalid-wearable";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    auto& appearance = by_user_[user_id];
    if (appearance.user_id.empty()) appearance.user_id = user_id;

    auto it = std::find_if(appearance.wearables.begin(), appearance.wearables.end(),
                           [&](const WearableRef& wearable) { return wearable.slot == slot; });
    WearableRef value{.slot = std::move(slot),
                      .item_id = std::move(item_id),
                      .asset_id = std::move(asset_id)};
    if (it == appearance.wearables.end()) appearance.wearables.push_back(std::move(value));
    else *it = std::move(value);

    ++appearance.revision;
    appearance.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return appearance;
}

std::optional<AvatarAppearance> AppearanceStore::remove_wearable(
    const std::string_view user_id,
    const std::string_view slot) {
    std::scoped_lock lock(mutex_);
    const auto it = by_user_.find(std::string{user_id});
    if (it == by_user_.end()) return std::nullopt;
    auto& appearance = it->second;
    const auto old_size = appearance.wearables.size();
    std::erase_if(appearance.wearables,
                  [&](const WearableRef& wearable) { return wearable.slot == slot; });
    if (appearance.wearables.size() == old_size) return appearance;
    ++appearance.revision;
    appearance.updated_unix = unix_now();
    persist_locked();
    return appearance;
}

std::optional<AvatarAppearance> AppearanceStore::attach(
    std::string user_id,
    std::string point,
    std::string item_id,
    std::string asset_id,
    std::string& reason) {
    if (!valid_field(user_id, 256) || !valid_field(point, 64) ||
        !valid_field(item_id, 256) || !valid_field(asset_id, 256)) {
        reason = "invalid-attachment";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    auto& appearance = by_user_[user_id];
    if (appearance.user_id.empty()) appearance.user_id = user_id;

    auto it = std::find_if(appearance.attachments.begin(), appearance.attachments.end(),
                           [&](const AttachmentRef& attachment) {
                               return attachment.point == point && attachment.item_id == item_id;
                           });
    AttachmentRef value{.point = std::move(point),
                        .item_id = std::move(item_id),
                        .asset_id = std::move(asset_id)};
    if (it == appearance.attachments.end()) appearance.attachments.push_back(std::move(value));
    else *it = std::move(value);

    ++appearance.revision;
    appearance.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return appearance;
}

std::optional<AvatarAppearance> AppearanceStore::detach(
    const std::string_view user_id,
    const std::string_view point,
    const std::string_view item_id) {
    std::scoped_lock lock(mutex_);
    const auto it = by_user_.find(std::string{user_id});
    if (it == by_user_.end()) return std::nullopt;
    auto& appearance = it->second;
    const auto old_size = appearance.attachments.size();
    std::erase_if(appearance.attachments, [&](const AttachmentRef& attachment) {
        if (attachment.point != point) return false;
        return item_id.empty() || attachment.item_id == item_id;
    });
    if (appearance.attachments.size() == old_size) return appearance;
    ++appearance.revision;
    appearance.updated_unix = unix_now();
    persist_locked();
    return appearance;
}

std::size_t AppearanceStore::count() const {
    std::scoped_lock lock(mutex_);
    return by_user_.size();
}

void AppearanceStore::load() {
    std::scoped_lock lock(mutex_);
    by_user_.clear();
    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = tabs(line);
        try {
            if (fields.size() == 4 && fields[0] == "A") {
                auto& appearance = by_user_[unhex(fields[1])];
                appearance.user_id = unhex(fields[1]);
                appearance.revision = std::stoull(fields[2]);
                appearance.updated_unix = std::stoll(fields[3]);
            } else if (fields.size() == 5 && fields[0] == "W") {
                const auto user = unhex(fields[1]);
                auto& appearance = by_user_[user];
                appearance.user_id = user;
                appearance.wearables.push_back(
                    {.slot = unhex(fields[2]),
                     .item_id = unhex(fields[3]),
                     .asset_id = unhex(fields[4])});
            } else if (fields.size() == 5 && fields[0] == "T") {
                const auto user = unhex(fields[1]);
                auto& appearance = by_user_[user];
                appearance.user_id = user;
                appearance.attachments.push_back(
                    {.point = unhex(fields[2]),
                     .item_id = unhex(fields[3]),
                     .asset_id = unhex(fields[4])});
            }
        } catch (...) {
        }
    }
}

void AppearanceStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write appearance store");
    output << "# OpenGenesisLINK avatar appearance v1\n";

    std::vector<AvatarAppearance> rows;
    rows.reserve(by_user_.size());
    for (const auto& [_, appearance] : by_user_) rows.push_back(appearance);
    std::sort(rows.begin(), rows.end(), [](const AvatarAppearance& a, const AvatarAppearance& b) {
        return a.user_id < b.user_id;
    });

    for (const auto& appearance : rows) {
        output << "A\t" << hex(appearance.user_id) << '\t' << appearance.revision << '\t'
               << appearance.updated_unix << '\n';
        for (const auto& wearable : appearance.wearables) {
            output << "W\t" << hex(appearance.user_id) << '\t' << hex(wearable.slot) << '\t'
                   << hex(wearable.item_id) << '\t' << hex(wearable.asset_id) << '\n';
        }
        for (const auto& attachment : appearance.attachments) {
            output << "T\t" << hex(appearance.user_id) << '\t' << hex(attachment.point) << '\t'
                   << hex(attachment.item_id) << '\t' << hex(attachment.asset_id) << '\n';
        }
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush appearance store");
    platform::replace_file(temp, path);
}

} // namespace opengenesis::avatar
