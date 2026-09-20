#include "opengenesis/core/marketplace_store.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::core {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool safe_text(
    const std::string_view value,
    const std::size_t maximum,
    const bool allow_empty = false) {
    if ((!allow_empty && value.empty()) ||
        value.size() > maximum) {
        return false;
    }
    return value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(
            line.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

MarketplaceListingState parse_state(
    const std::string_view value) {
    if (value == "reserved") {
        return MarketplaceListingState::reserved;
    }
    if (value == "sold") {
        return MarketplaceListingState::sold;
    }
    if (value == "cancelled") {
        return MarketplaceListingState::cancelled;
    }
    return MarketplaceListingState::active;
}

} // namespace

std::string_view marketplace_listing_state_name(
    const MarketplaceListingState state) noexcept {
    switch (state) {
        case MarketplaceListingState::active:
            return "active";
        case MarketplaceListingState::reserved:
            return "reserved";
        case MarketplaceListingState::sold:
            return "sold";
        case MarketplaceListingState::cancelled:
            return "cancelled";
    }
    return "cancelled";
}

MarketplaceStore::MarketplaceStore(std::string path)
    : path_(std::move(path)) {
    load();
}

std::optional<MarketplaceListing> MarketplaceStore::create(
    std::string seller_user_id,
    std::string asset_id,
    std::string title,
    std::string description,
    const std::int64_t price_minor,
    std::string currency_code,
    std::string& reason) {
    if (!safe_text(seller_user_id, 256U) ||
        !safe_text(asset_id, 256U) ||
        !safe_text(title, 128U) ||
        !safe_text(description, 1024U, true) ||
        price_minor <= 0 ||
        !safe_text(currency_code, 16U)) {
        reason = "invalid-marketplace-listing";
        return std::nullopt;
    }

    const auto now = unix_now();
    MarketplaceListing listing{
        .id = security::random_hex(16),
        .seller_user_id = std::move(seller_user_id),
        .asset_id = std::move(asset_id),
        .title = std::move(title),
        .description = std::move(description),
        .price_minor = price_minor,
        .currency_code = std::move(currency_code),
        .state = MarketplaceListingState::active,
        .buyer_user_id = {},
        .sale_reference = {},
        .created_unix = now,
        .updated_unix = now};

    std::scoped_lock lock(mutex_);
    listings_[listing.id] = listing;
    persist_locked();
    reason.clear();
    return listing;
}

bool MarketplaceStore::cancel(
    const std::string_view seller_user_id,
    const std::string_view listing_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = listings_.find(std::string{listing_id});
    if (it == listings_.end()) {
        reason = "listing-not-found";
        return false;
    }
    if (it->second.seller_user_id != seller_user_id) {
        reason = "permission-denied";
        return false;
    }
    if (it->second.state != MarketplaceListingState::active) {
        reason = "listing-not-active";
        return false;
    }
    it->second.state = MarketplaceListingState::cancelled;
    it->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

bool MarketplaceStore::reserve_purchase(
    const std::string_view listing_id,
    std::string buyer_user_id,
    std::string sale_reference,
    std::string& reason) {
    if (!safe_text(buyer_user_id, 256U) ||
        !safe_text(sale_reference, 128U)) {
        reason = "invalid-sale";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto it = listings_.find(std::string{listing_id});
    if (it == listings_.end()) {
        reason = "listing-not-found";
        return false;
    }
    if (it->second.state != MarketplaceListingState::active) {
        reason = "listing-not-active";
        return false;
    }
    if (it->second.seller_user_id == buyer_user_id) {
        reason = "cannot-buy-own-listing";
        return false;
    }
    it->second.state = MarketplaceListingState::reserved;
    it->second.buyer_user_id = std::move(buyer_user_id);
    it->second.sale_reference = std::move(sale_reference);
    it->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

bool MarketplaceStore::complete_purchase(
    const std::string_view listing_id,
    const std::string_view buyer_user_id,
    const std::string_view sale_reference,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = listings_.find(std::string{listing_id});
    if (it == listings_.end()) {
        reason = "listing-not-found";
        return false;
    }
    if (it->second.state != MarketplaceListingState::reserved ||
        it->second.buyer_user_id != buyer_user_id ||
        it->second.sale_reference != sale_reference) {
        reason = "listing-reservation-mismatch";
        return false;
    }
    it->second.state = MarketplaceListingState::sold;
    it->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

bool MarketplaceStore::release_purchase(
    const std::string_view listing_id,
    const std::string_view buyer_user_id,
    const std::string_view sale_reference,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = listings_.find(std::string{listing_id});
    if (it == listings_.end()) {
        reason = "listing-not-found";
        return false;
    }
    if (it->second.state != MarketplaceListingState::reserved ||
        it->second.buyer_user_id != buyer_user_id ||
        it->second.sale_reference != sale_reference) {
        reason = "listing-reservation-mismatch";
        return false;
    }
    it->second.state = MarketplaceListingState::active;
    it->second.buyer_user_id.clear();
    it->second.sale_reference.clear();
    it->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

std::optional<MarketplaceListing> MarketplaceStore::find(
    const std::string_view listing_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = listings_.find(std::string{listing_id});
    return it == listings_.end()
               ? std::nullopt
               : std::optional<MarketplaceListing>{it->second};
}

std::vector<MarketplaceListing> MarketplaceStore::active(
    const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<MarketplaceListing> result;
    for (const auto& [_, listing] : listings_) {
        if (listing.state == MarketplaceListingState::active) {
            result.push_back(listing);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const MarketplaceListing& left,
           const MarketplaceListing& right) {
            return left.created_unix > right.created_unix;
        });
    if (result.size() > std::min<std::size_t>(limit, 500U)) {
        result.resize(std::min<std::size_t>(limit, 500U));
    }
    return result;
}

std::vector<MarketplaceListing> MarketplaceStore::for_seller(
    const std::string_view seller_user_id,
    const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<MarketplaceListing> result;
    for (const auto& [_, listing] : listings_) {
        if (listing.seller_user_id == seller_user_id) {
            result.push_back(listing);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const MarketplaceListing& left,
           const MarketplaceListing& right) {
            return left.created_unix > right.created_unix;
        });
    if (result.size() > std::min<std::size_t>(limit, 500U)) {
        result.resize(std::min<std::size_t>(limit, 500U));
    }
    return result;
}

std::size_t MarketplaceStore::active_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(
        std::count_if(
            listings_.begin(), listings_.end(),
            [](const auto& item) {
                return item.second.state ==
                       MarketplaceListingState::active;
            }));
}

std::size_t MarketplaceStore::total_count() const {
    std::scoped_lock lock(mutex_);
    return listings_.size();
}

void MarketplaceStore::load() {
    std::scoped_lock lock(mutex_);
    listings_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 12U) continue;
        try {
            MarketplaceListing listing{
                .id = fields[0],
                .seller_user_id = fields[1],
                .asset_id = fields[2],
                .title = fields[3],
                .description = fields[4],
                .price_minor = std::stoll(fields[5]),
                .currency_code = fields[6],
                .state = parse_state(fields[7]),
                .buyer_user_id = fields[8],
                .sale_reference = fields[9],
                .created_unix = std::stoll(fields[10]),
                .updated_unix = std::stoll(fields[11])};
            if (!listing.id.empty() &&
                listing.price_minor > 0) {
                listings_[listing.id] =
                    std::move(listing);
            }
        } catch (...) {
        }
    }
}

void MarketplaceStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot write marketplace store");
    }
    output << "# OpenGenesisLINK marketplace v1\n";

    std::vector<MarketplaceListing> rows;
    rows.reserve(listings_.size());
    for (const auto& [_, listing] : listings_) {
        rows.push_back(listing);
    }
    std::sort(
        rows.begin(), rows.end(),
        [](const MarketplaceListing& left,
           const MarketplaceListing& right) {
            return left.created_unix < right.created_unix;
        });

    for (const auto& listing : rows) {
        output << listing.id << '\t'
               << listing.seller_user_id << '\t'
               << listing.asset_id << '\t'
               << listing.title << '\t'
               << listing.description << '\t'
               << listing.price_minor << '\t'
               << listing.currency_code << '\t'
               << marketplace_listing_state_name(
                      listing.state)
               << '\t'
               << listing.buyer_user_id << '\t'
               << listing.sale_reference << '\t'
               << listing.created_unix << '\t'
               << listing.updated_unix << '\n';
    }

    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush marketplace store");
    }
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
