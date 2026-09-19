#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

struct InventoryFolder {
    std::string id;
    std::string owner_user_id;
    std::string parent_id;
    std::string name;
    std::string legacy_id;
    std::int64_t created_unix{0};
};

struct InventoryItem {
    std::string id;
    std::string owner_user_id;
    std::string parent_id;
    std::string asset_id;
    std::string name;
    std::string legacy_id;
    std::int64_t created_unix{0};
};

struct UserInventory {
    InventoryFolder root;
    std::vector<InventoryFolder> folders;
    std::vector<InventoryItem> items;
};

class InventoryStore final {
public:
    explicit InventoryStore(std::string path);

    [[nodiscard]] InventoryFolder ensure_root(std::string user_id);

    [[nodiscard]] std::optional<InventoryFolder> create_folder(
        std::string user_id,
        std::string parent_id,
        std::string name,
        std::string& reason,
        std::string legacy_id = {});

    [[nodiscard]] std::optional<InventoryItem> create_item(
        std::string user_id,
        std::string parent_id,
        std::string asset_id,
        std::string name,
        std::string& reason,
        std::string legacy_id = {});

    [[nodiscard]] bool update_folder(std::string_view user_id,
                                     std::string_view folder_id,
                                     std::string parent_id,
                                     std::string name,
                                     std::string& reason);
    [[nodiscard]] bool move_folder(std::string_view user_id,
                                   std::string_view folder_id,
                                   std::string parent_id,
                                   std::string& reason);
    [[nodiscard]] bool delete_folder(std::string_view user_id,
                                     std::string_view folder_id,
                                     bool delete_self,
                                     std::string& reason);

    [[nodiscard]] bool update_item(std::string_view user_id,
                                   std::string_view item_id,
                                   std::string parent_id,
                                   std::string asset_id,
                                   std::string name,
                                   std::string& reason);
    [[nodiscard]] bool move_item(std::string_view user_id,
                                 std::string_view item_id,
                                 std::string parent_id,
                                 std::string& reason);
    [[nodiscard]] bool delete_item(std::string_view user_id,
                                   std::string_view item_id,
                                   std::string& reason);

    [[nodiscard]] std::optional<InventoryFolder> find_folder(
        std::string_view user_id,
        std::string_view folder_id) const;
    [[nodiscard]] std::optional<InventoryItem> find_item(
        std::string_view user_id,
        std::string_view item_id) const;

    [[nodiscard]] UserInventory list(std::string_view user_id);
    [[nodiscard]] std::size_t folder_count() const;
    [[nodiscard]] std::size_t item_count() const;

private:
    void load();
    void persist_locked() const;
    [[nodiscard]] bool folder_belongs_locked(std::string_view folder_id,
                                             std::string_view user_id) const;
    [[nodiscard]] bool folder_is_descendant_locked(std::string_view candidate_parent,
                                                   std::string_view folder_id) const;
    void collect_descendants_locked(std::string_view folder_id,
                                    std::vector<std::string>& output) const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, InventoryFolder> folders_;
    std::unordered_map<std::string, InventoryItem> items_;
    std::unordered_map<std::string, std::string> root_by_user_;
};

} // namespace opengenesis::core
