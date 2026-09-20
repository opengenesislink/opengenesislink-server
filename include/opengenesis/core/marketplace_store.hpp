#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

enum class MarketplaceListingState {
    active,
    reserved,
    sold,
    cancelled
};

struct MarketplaceListing {
    std::string id;
    std::string seller_user_id;
    std::string asset_id;
    std::string title;
    std::string description;
    std::int64_t price_minor{0};
    std::string currency_code;
    MarketplaceListingState state{MarketplaceListingState::active};
    std::string buyer_user_id;
    std::string sale_reference;
    std::int64_t created_unix{0};
    std::int64_t updated_unix{0};
};

class MarketplaceStore final {
public:
    explicit MarketplaceStore(std::string path);

    [[nodiscard]] std::optional<MarketplaceListing> create(
        std::string seller_user_id,
        std::string asset_id,
        std::string title,
        std::string description,
        std::int64_t price_minor,
        std::string currency_code,
        std::string& reason);

    [[nodiscard]] bool cancel(
        std::string_view seller_user_id,
        std::string_view listing_id,
        std::string& reason);

    [[nodiscard]] bool reserve_purchase(
        std::string_view listing_id,
        std::string buyer_user_id,
        std::string sale_reference,
        std::string& reason);

    [[nodiscard]] bool complete_purchase(
        std::string_view listing_id,
        std::string_view buyer_user_id,
        std::string_view sale_reference,
        std::string& reason);

    [[nodiscard]] bool release_purchase(
        std::string_view listing_id,
        std::string_view buyer_user_id,
        std::string_view sale_reference,
        std::string& reason);

    [[nodiscard]] bool rollback_purchase(
        std::string_view listing_id,
        std::string_view buyer_user_id,
        std::string_view sale_reference,
        std::string& reason);

    [[nodiscard]] std::optional<MarketplaceListing> find(
        std::string_view listing_id) const;

    [[nodiscard]] std::vector<MarketplaceListing> active(
        std::size_t limit = 200U) const;
    [[nodiscard]] std::vector<MarketplaceListing> for_seller(
        std::string_view seller_user_id,
        std::size_t limit = 200U) const;

    [[nodiscard]] std::size_t active_count() const;
    [[nodiscard]] std::size_t total_count() const;

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MarketplaceListing> listings_;
};

[[nodiscard]] std::string_view marketplace_listing_state_name(
    MarketplaceListingState state) noexcept;

} // namespace opengenesis::core
