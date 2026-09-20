#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

enum class EconomyEntryKind {
    transfer,
    mint,
    burn,
    escrow_reserve,
    escrow_commit,
    escrow_release
};

struct EconomyAccount {
    std::string user_id;
    std::int64_t balance_minor{0};
    std::int64_t created_unix{0};
    std::int64_t updated_unix{0};
};

struct EconomyEntry {
    std::string id;
    EconomyEntryKind kind{EconomyEntryKind::transfer};
    std::string reference;
    std::string from_user;
    std::string to_user;
    std::int64_t amount_minor{0};
    std::string memo;
    std::int64_t created_unix{0};
};

enum class EscrowState {
    reserved,
    committed,
    released
};

struct EconomyEscrow {
    std::string id;
    std::string reference;
    std::string buyer_user;
    std::string seller_user;
    std::int64_t amount_minor{0};
    EscrowState state{EscrowState::reserved};
    std::int64_t created_unix{0};
    std::int64_t updated_unix{0};
};

class EconomyLedger final {
public:
    EconomyLedger(std::string path, std::string currency_code);

    [[nodiscard]] const std::string& currency_code() const noexcept {
        return currency_code_;
    }

    [[nodiscard]] EconomyAccount ensure_account(std::string user_id);
    [[nodiscard]] std::optional<EconomyAccount> account(
        std::string_view user_id) const;

    [[nodiscard]] std::optional<EconomyEntry> transfer(
        std::string from_user,
        std::string to_user,
        std::int64_t amount_minor,
        std::string reference,
        std::string memo,
        std::string& reason);

    [[nodiscard]] std::optional<EconomyEntry> mint(
        std::string to_user,
        std::int64_t amount_minor,
        std::string reference,
        std::string memo,
        std::string& reason);

    [[nodiscard]] std::optional<EconomyEntry> burn(
        std::string from_user,
        std::int64_t amount_minor,
        std::string reference,
        std::string memo,
        std::string& reason);

    [[nodiscard]] std::optional<EconomyEscrow> reserve(
        std::string buyer_user,
        std::string seller_user,
        std::int64_t amount_minor,
        std::string reference,
        std::string& reason);

    [[nodiscard]] bool commit_escrow(
        std::string_view escrow_id,
        std::string& reason);
    [[nodiscard]] bool release_escrow(
        std::string_view escrow_id,
        std::string& reason);

    [[nodiscard]] std::optional<EconomyEscrow> escrow(
        std::string_view escrow_id) const;

    [[nodiscard]] std::vector<EconomyEntry> entries_for(
        std::string_view user_id,
        std::size_t limit = 100U) const;
    [[nodiscard]] std::vector<EconomyEscrow> escrows_for(
        std::string_view user_id,
        std::size_t limit = 100U) const;

    [[nodiscard]] std::int64_t total_supply_minor() const;
    [[nodiscard]] std::size_t account_count() const;
    [[nodiscard]] std::size_t entry_count() const;

private:
    void load();
    void persist_locked() const;
    EconomyAccount& ensure_account_locked(const std::string& user_id);
    [[nodiscard]] bool reference_exists_locked(
        std::string_view reference) const;
    EconomyEntry append_locked(
        EconomyEntryKind kind,
        std::string reference,
        std::string from_user,
        std::string to_user,
        std::int64_t amount_minor,
        std::string memo);

    std::string path_;
    std::string currency_code_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, EconomyAccount> accounts_;
    std::unordered_map<std::string, EconomyEscrow> escrows_;
    std::vector<EconomyEntry> entries_;
};

[[nodiscard]] std::string_view economy_entry_kind_name(
    EconomyEntryKind kind) noexcept;
[[nodiscard]] std::string_view escrow_state_name(
    EscrowState state) noexcept;

} // namespace opengenesis::core
