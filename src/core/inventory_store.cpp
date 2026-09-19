#include "opengenesis/core/inventory_store.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::core {
namespace {

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2] = digits[c >> 4U];
        output[i * 2 + 1] = digits[c & 0x0fU];
    }
    return output;
}

unsigned char nibble(const char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
    throw std::runtime_error("invalid inventory hex");
}

std::string unhex(std::string_view value) {
    if ((value.size() % 2U) != 0U) throw std::runtime_error("invalid inventory hex length");
    std::string output(value.size() / 2U, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>((nibble(value[i * 2U]) << 4U) |
                                      nibble(value[i * 2U + 1U]));
    }
    return output;
}

std::vector<std::string> tabs(const std::string& line) {
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

bool valid_name(const std::string_view value) {
    return !value.empty() && value.size() <= 128U &&
           std::none_of(value.begin(), value.end(), [](const unsigned char c) {
               return c < 0x20U || c == 0x7fU;
           });
}

bool valid_legacy_id(const std::string_view value) {
    if (value.empty()) return true;
    if (value.size() != 36U) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8U || i == 13U || i == 18U || i == 23U) {
            if (value[i] != '-') return false;
        } else {
            const auto c = static_cast<unsigned char>(value[i]);
            if (std::isxdigit(c) == 0) return false;
        }
    }
    return true;
}

} // namespace

InventoryStore::InventoryStore(std::string path) : path_(std::move(path)) {
    load();
}

InventoryFolder InventoryStore::ensure_root(std::string user_id) {
    std::scoped_lock lock(mutex_);
    if (const auto it = root_by_user_.find(user_id); it != root_by_user_.end()) {
        return folders_.at(it->second);
    }

    InventoryFolder folder{
        .id = security::random_hex(16),
        .owner_user_id = std::move(user_id),
        .parent_id = {},
        .name = "My Inventory",
        .legacy_id = {},
        .created_unix = now()};
    root_by_user_[folder.owner_user_id] = folder.id;
    folders_[folder.id] = folder;
    persist_locked();
    return folder;
}

bool InventoryStore::folder_belongs_locked(const std::string_view id,
                                           const std::string_view user) const {
    const auto it = folders_.find(std::string{id});
    return it != folders_.end() && it->second.owner_user_id == user;
}

bool InventoryStore::folder_is_descendant_locked(const std::string_view candidate_parent,
                                                 const std::string_view folder_id) const {
    std::string current{candidate_parent};
    for (std::size_t depth = 0; depth < folders_.size() + 1U && !current.empty(); ++depth) {
        if (current == folder_id) return true;
        const auto it = folders_.find(current);
        if (it == folders_.end()) return false;
        current = it->second.parent_id;
    }
    return false;
}

void InventoryStore::collect_descendants_locked(const std::string_view folder_id,
                                                std::vector<std::string>& output) const {
    for (const auto& [id, folder] : folders_) {
        if (folder.parent_id != folder_id) continue;
        output.push_back(id);
        collect_descendants_locked(id, output);
    }
}

std::optional<InventoryFolder> InventoryStore::create_folder(
    std::string user_id,
    std::string parent_id,
    std::string name,
    std::string& reason,
    std::string legacy_id) {
    if (!valid_name(name) || !valid_legacy_id(legacy_id)) {
        reason = "invalid-folder";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    if (parent_id.empty()) {
        const auto root = root_by_user_.find(user_id);
        if (root == root_by_user_.end()) {
            reason = "missing-root";
            return std::nullopt;
        }
        parent_id = root->second;
    }
    if (!folder_belongs_locked(parent_id, user_id)) {
        reason = "invalid-parent";
        return std::nullopt;
    }
    if (!legacy_id.empty()) {
        for (const auto& [_, folder] : folders_) {
            if (folder.owner_user_id == user_id && folder.legacy_id == legacy_id) {
                reason = "legacy-folder-exists";
                return std::nullopt;
            }
        }
    }

    InventoryFolder folder{
        .id = security::random_hex(16),
        .owner_user_id = std::move(user_id),
        .parent_id = std::move(parent_id),
        .name = std::move(name),
        .legacy_id = std::move(legacy_id),
        .created_unix = now()};
    folders_[folder.id] = folder;
    persist_locked();
    reason.clear();
    return folder;
}

std::optional<InventoryItem> InventoryStore::create_item(
    std::string user_id,
    std::string parent_id,
    std::string asset_id,
    std::string name,
    std::string& reason,
    std::string legacy_id) {
    if (asset_id.empty() || !valid_name(name) || !valid_legacy_id(legacy_id)) {
        reason = "invalid-item";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    if (parent_id.empty()) {
        const auto root = root_by_user_.find(user_id);
        if (root == root_by_user_.end()) {
            reason = "missing-root";
            return std::nullopt;
        }
        parent_id = root->second;
    }
    if (!folder_belongs_locked(parent_id, user_id)) {
        reason = "invalid-parent";
        return std::nullopt;
    }
    if (!legacy_id.empty()) {
        for (const auto& [_, item] : items_) {
            if (item.owner_user_id == user_id && item.legacy_id == legacy_id) {
                reason = "legacy-item-exists";
                return std::nullopt;
            }
        }
    }

    InventoryItem item{
        .id = security::random_hex(16),
        .owner_user_id = std::move(user_id),
        .parent_id = std::move(parent_id),
        .asset_id = std::move(asset_id),
        .name = std::move(name),
        .legacy_id = std::move(legacy_id),
        .created_unix = now()};
    items_[item.id] = item;
    persist_locked();
    reason.clear();
    return item;
}

bool InventoryStore::update_folder(const std::string_view user_id,
                                   const std::string_view folder_id,
                                   std::string parent_id,
                                   std::string name,
                                   std::string& reason) {
    if (!valid_name(name)) {
        reason = "invalid-name";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto it = folders_.find(std::string{folder_id});
    if (it == folders_.end() || it->second.owner_user_id != user_id ||
        it->second.parent_id.empty()) {
        reason = "folder-not-found-or-root";
        return false;
    }
    if (!folder_belongs_locked(parent_id, user_id) ||
        parent_id == folder_id ||
        folder_is_descendant_locked(parent_id, folder_id)) {
        reason = "invalid-parent";
        return false;
    }
    it->second.parent_id = std::move(parent_id);
    it->second.name = std::move(name);
    persist_locked();
    reason.clear();
    return true;
}

bool InventoryStore::move_folder(const std::string_view user_id,
                                 const std::string_view folder_id,
                                 std::string parent_id,
                                 std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = folders_.find(std::string{folder_id});
    if (it == folders_.end() || it->second.owner_user_id != user_id ||
        it->second.parent_id.empty()) {
        reason = "folder-not-found-or-root";
        return false;
    }
    if (!folder_belongs_locked(parent_id, user_id) ||
        parent_id == folder_id ||
        folder_is_descendant_locked(parent_id, folder_id)) {
        reason = "invalid-parent";
        return false;
    }
    it->second.parent_id = std::move(parent_id);
    persist_locked();
    reason.clear();
    return true;
}

bool InventoryStore::delete_folder(const std::string_view user_id,
                                   const std::string_view folder_id,
                                   const bool delete_self,
                                   std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = folders_.find(std::string{folder_id});
    if (it == folders_.end() || it->second.owner_user_id != user_id ||
        it->second.parent_id.empty()) {
        reason = "folder-not-found-or-root";
        return false;
    }

    std::vector<std::string> folders_to_remove;
    collect_descendants_locked(folder_id, folders_to_remove);
    if (delete_self) folders_to_remove.push_back(std::string{folder_id});

    for (auto item = items_.begin(); item != items_.end();) {
        const bool in_target = item->second.owner_user_id == user_id &&
            (item->second.parent_id == folder_id ||
             std::find(folders_to_remove.begin(), folders_to_remove.end(),
                       item->second.parent_id) != folders_to_remove.end());
        if (in_target) item = items_.erase(item);
        else ++item;
    }

    for (const auto& id : folders_to_remove) folders_.erase(id);
    persist_locked();
    reason.clear();
    return true;
}

bool InventoryStore::update_item(const std::string_view user_id,
                                 const std::string_view item_id,
                                 std::string parent_id,
                                 std::string asset_id,
                                 std::string name,
                                 std::string& reason) {
    if (asset_id.empty() || !valid_name(name)) {
        reason = "invalid-item";
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = items_.find(std::string{item_id});
    if (it == items_.end() || it->second.owner_user_id != user_id) {
        reason = "item-not-found";
        return false;
    }
    if (!folder_belongs_locked(parent_id, user_id)) {
        reason = "invalid-parent";
        return false;
    }
    it->second.parent_id = std::move(parent_id);
    it->second.asset_id = std::move(asset_id);
    it->second.name = std::move(name);
    persist_locked();
    reason.clear();
    return true;
}

bool InventoryStore::move_item(const std::string_view user_id,
                               const std::string_view item_id,
                               std::string parent_id,
                               std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = items_.find(std::string{item_id});
    if (it == items_.end() || it->second.owner_user_id != user_id) {
        reason = "item-not-found";
        return false;
    }
    if (!folder_belongs_locked(parent_id, user_id)) {
        reason = "invalid-parent";
        return false;
    }
    it->second.parent_id = std::move(parent_id);
    persist_locked();
    reason.clear();
    return true;
}

bool InventoryStore::delete_item(const std::string_view user_id,
                                 const std::string_view item_id,
                                 std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = items_.find(std::string{item_id});
    if (it == items_.end() || it->second.owner_user_id != user_id) {
        reason = "item-not-found";
        return false;
    }
    items_.erase(it);
    persist_locked();
    reason.clear();
    return true;
}

std::optional<InventoryFolder> InventoryStore::find_folder(
    const std::string_view user_id,
    const std::string_view folder_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = folders_.find(std::string{folder_id});
    if (it == folders_.end() || it->second.owner_user_id != user_id) return std::nullopt;
    return it->second;
}

std::optional<InventoryItem> InventoryStore::find_item(
    const std::string_view user_id,
    const std::string_view item_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = items_.find(std::string{item_id});
    if (it == items_.end() || it->second.owner_user_id != user_id) return std::nullopt;
    return it->second;
}

UserInventory InventoryStore::list(const std::string_view user_id) {
    const auto root = ensure_root(std::string{user_id});
    std::scoped_lock lock(mutex_);
    UserInventory inventory{.root = root, .folders = {}, .items = {}};
    for (const auto& [_, folder] : folders_) {
        if (folder.owner_user_id == user_id && folder.id != root.id) {
            inventory.folders.push_back(folder);
        }
    }
    for (const auto& [_, item] : items_) {
        if (item.owner_user_id == user_id) inventory.items.push_back(item);
    }
    std::sort(inventory.folders.begin(), inventory.folders.end(),
              [](const auto& a, const auto& b) { return a.created_unix < b.created_unix; });
    std::sort(inventory.items.begin(), inventory.items.end(),
              [](const auto& a, const auto& b) { return a.created_unix < b.created_unix; });
    return inventory;
}

std::size_t InventoryStore::folder_count() const {
    std::scoped_lock lock(mutex_);
    return folders_.size();
}

std::size_t InventoryStore::item_count() const {
    std::scoped_lock lock(mutex_);
    return items_.size();
}

void InventoryStore::load() {
    std::scoped_lock lock(mutex_);
    folders_.clear();
    items_.clear();
    root_by_user_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = tabs(line);
        try {
            if ((fields.size() == 6U || fields.size() == 7U) && fields[0] == "F") {
                InventoryFolder folder{
                    .id = fields[1],
                    .owner_user_id = fields[2],
                    .parent_id = fields[3],
                    .name = unhex(fields[4]),
                    .legacy_id = fields.size() == 7U ? fields[6] : std::string{},
                    .created_unix = std::stoll(fields[5])};
                folders_[folder.id] = folder;
                if (folder.parent_id.empty()) root_by_user_[folder.owner_user_id] = folder.id;
            } else if ((fields.size() == 7U || fields.size() == 8U) && fields[0] == "I") {
                InventoryItem item{
                    .id = fields[1],
                    .owner_user_id = fields[2],
                    .parent_id = fields[3],
                    .asset_id = fields[4],
                    .name = unhex(fields[5]),
                    .legacy_id = fields.size() == 8U ? fields[7] : std::string{},
                    .created_unix = std::stoll(fields[6])};
                items_[item.id] = item;
            }
        } catch (...) {
        }
    }
}

void InventoryStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";

    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write inventory");
    output << "# OpenGenesisLINK inventory v2\n";

    for (const auto& [_, folder] : folders_) {
        output << "F\t" << folder.id << '\t' << folder.owner_user_id << '\t'
               << folder.parent_id << '\t' << hex(folder.name) << '\t'
               << folder.created_unix << '\t' << folder.legacy_id << '\n';
    }
    for (const auto& [_, item] : items_) {
        output << "I\t" << item.id << '\t' << item.owner_user_id << '\t'
               << item.parent_id << '\t' << item.asset_id << '\t'
               << hex(item.name) << '\t' << item.created_unix << '\t'
               << item.legacy_id << '\n';
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush inventory");
    platform::replace_file(temp, path);
}

} // namespace opengenesis::core
