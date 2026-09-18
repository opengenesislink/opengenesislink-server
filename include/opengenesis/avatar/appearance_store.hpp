#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::avatar {

struct WearableRef {
    std::string slot;
    std::string item_id;
    std::string asset_id;
};

struct AttachmentRef {
    std::string point;
    std::string item_id;
    std::string asset_id;
};

struct AvatarAppearance {
    std::string user_id;
    std::uint64_t revision{0};
    double avatar_height{1.9};
    std::string visual_params_csv;
    std::vector<WearableRef> wearables;
    std::vector<AttachmentRef> attachments;
    std::int64_t updated_unix{0};
};

class AppearanceStore final {
public:
    explicit AppearanceStore(std::string path);

    [[nodiscard]] AvatarAppearance ensure(std::string user_id);
    [[nodiscard]] std::optional<AvatarAppearance> find(std::string_view user_id) const;

    [[nodiscard]] std::optional<AvatarAppearance> set_legacy_body(
        std::string user_id,
        double avatar_height,
        std::string visual_params_csv,
        std::string& reason);

    [[nodiscard]] std::optional<AvatarAppearance> set_wearable(
        std::string user_id,
        std::string slot,
        std::string item_id,
        std::string asset_id,
        std::string& reason);

    [[nodiscard]] std::optional<AvatarAppearance> remove_wearable(
        std::string_view user_id,
        std::string_view slot);

    [[nodiscard]] std::optional<AvatarAppearance> attach(
        std::string user_id,
        std::string point,
        std::string item_id,
        std::string asset_id,
        std::string& reason);

    [[nodiscard]] std::optional<AvatarAppearance> detach(
        std::string_view user_id,
        std::string_view point,
        std::string_view item_id = {});

    [[nodiscard]] std::size_t count() const;

private:
    void load();
    void persist_locked() const;
    static bool valid_field(std::string_view value, std::size_t max_size);

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, AvatarAppearance> by_user_;
};

} // namespace opengenesis::avatar
