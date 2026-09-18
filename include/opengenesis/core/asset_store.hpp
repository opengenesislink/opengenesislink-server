#pragma once
#include "opengenesis/core/permissions.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace opengenesis::core {
struct AssetInfo {
    std::string id;
    std::string owner_user_id;
    std::string name;
    std::string mime_type;
    std::string content_hash;
    std::uint64_t size{0};
    PermissionMask permissions{perm_all};
    PermissionMask next_owner_permissions{perm_copy | perm_transfer};
    std::int64_t created_unix{0};
};
class AssetStore final {
public:
    AssetStore(std::string metadata_path,std::filesystem::path blob_directory,std::size_t max_asset_bytes=1024U*1024U);
    [[nodiscard]] std::optional<AssetInfo> create(std::string owner_user_id,std::string name,std::string mime_type,std::string data,std::string& reason,PermissionMask permissions=perm_all,PermissionMask next_owner_permissions=perm_copy|perm_transfer);
    [[nodiscard]] std::optional<AssetInfo> transfer(std::string_view asset_id,std::string_view from_user,std::string to_user,bool keep_copy,std::string& reason);
    [[nodiscard]] std::optional<AssetInfo> find(std::string_view id) const;
    [[nodiscard]] std::optional<std::string> read(std::string_view id,std::string_view owner_user_id) const;
    [[nodiscard]] std::vector<AssetInfo> list_for_user(std::string_view owner_user_id) const;
    [[nodiscard]] std::vector<AssetInfo> list_all() const;
    [[nodiscard]] std::optional<std::string> read_exportable(std::string_view id) const;
    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] std::uint64_t total_bytes() const;
private:
    void load(); void persist_locked() const;
    std::string metadata_path_; std::filesystem::path blob_directory_; std::size_t max_asset_bytes_;
    mutable std::mutex mutex_; std::unordered_map<std::string,AssetInfo> assets_;
};
}
