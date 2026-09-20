#include "opengenesis/core/economy_ledger.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
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

bool safe_field(
    const std::string_view value,
    const std::size_t maximum = 512U,
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

EconomyEntryKind parse_kind(const std::string_view value) {
    if (value == "mint") return EconomyEntryKind::mint;
    if (value == "burn") return EconomyEntryKind::burn;
    if (value == "escrow_reserve") {
        return EconomyEntryKind::escrow_reserve;
    }
    if (value == "escrow_commit") {
        return EconomyEntryKind::escrow_commit;
    }
    if (value == "escrow_release") {
        return EconomyEntryKind::escrow_release;
    }
    return EconomyEntryKind::transfer;
}

EscrowState parse_escrow_state(const std::string_view value) {
    if (value == "committed") return EscrowState::committed;
    if (value == "released") return EscrowState::released;
    return EscrowState::reserved;
}

bool add_overflows(
    const std::int64_t current,
    const std::int64_t amount) {
    return amount > 0 &&
           current > std::numeric_limits<std::int64_t>::max() -
                         amount;
}

} // namespace

std::string_view economy_entry_kind_name(
    const EconomyEntryKind kind) noexcept {
    switch (kind) {
        case EconomyEntryKind::transfer: return "transfer";
        case EconomyEntryKind::mint: return "mint";
        case EconomyEntryKind::burn: return "burn";
        case EconomyEntryKind::escrow_reserve:
            return "escrow_reserve";
        case EconomyEntryKind::escrow_commit:
            return "escrow_commit";
        case EconomyEntryKind::escrow_release:
            return "escrow_release";
    }
    return "transfer";
}

std::string_view escrow_state_name(
    const EscrowState state) noexcept {
    switch (state) {
        case EscrowState::reserved: return "reserved";
        case EscrowState::committed: return "committed";
        case EscrowState::released: return "released";
    }
    return "released";
}

EconomyLedger::EconomyLedger(
    std::string path,
    std::string currency_code)
    : path_(std::move(path)),
      currency_code_(std::move(currency_code)) {
    if (!safe_field(currency_code_, 16U) ||
        !std::all_of(
            currency_code_.begin(), currency_code_.end(),
            [](const unsigned char c) {
                return (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9');
            })) {
        throw std::invalid_argument(
            "invalid economy currency code");
    }
    load();
}

EconomyAccount& EconomyLedger::ensure_account_locked(
    const std::string& user_id) {
    auto it = accounts_.find(user_id);
    if (it != accounts_.end()) return it->second;
    const auto now = unix_now();
    return accounts_
        .emplace(
            user_id,
            EconomyAccount{
                .user_id = user_id,
                .balance_minor = 0,
                .created_unix = now,
                .updated_unix = now})
        .first->second;
}

EconomyAccount EconomyLedger::ensure_account(
    std::string user_id) {
    if (!safe_field(user_id, 256U)) {
        throw std::invalid_argument("invalid economy user");
    }
    std::scoped_lock lock(mutex_);
    auto& account = ensure_account_locked(user_id);
    persist_locked();
    return account;
}

std::optional<EconomyAccount> EconomyLedger::account(
    const std::string_view user_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = accounts_.find(std::string{user_id});
    return it == accounts_.end()
               ? std::nullopt
               : std::optional<EconomyAccount>{it->second};
}

bool EconomyLedger::reference_exists_locked(
    const std::string_view reference) const {
    return std::any_of(
        entries_.begin(), entries_.end(),
        [&](const EconomyEntry& entry) {
            return entry.reference == reference;
        });
}

EconomyEntry EconomyLedger::append_locked(
    const EconomyEntryKind kind,
    std::string reference,
    std::string from_user,
    std::string to_user,
    const std::int64_t amount_minor,
    std::string memo) {
    EconomyEntry entry{
        .id = security::random_hex(16),
        .kind = kind,
        .reference = std::move(reference),
        .from_user = std::move(from_user),
        .to_user = std::move(to_user),
        .amount_minor = amount_minor,
        .memo = std::move(memo),
        .created_unix = unix_now()};
    entries_.push_back(entry);
    return entry;
}

std::optional<EconomyEntry> EconomyLedger::transfer(
    std::string from_user,
    std::string to_user,
    const std::int64_t amount_minor,
    std::string reference,
    std::string memo,
    std::string& reason) {
    if (!safe_field(from_user, 256U) ||
        !safe_field(to_user, 256U) ||
        from_user == to_user ||
        amount_minor <= 0 ||
        !safe_field(reference, 128U) ||
        !safe_field(memo, 512U, true)) {
        reason = "invalid-transfer";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    if (reference_exists_locked(reference)) {
        reason = "duplicate-reference";
        return std::nullopt;
    }
    auto& from = ensure_account_locked(from_user);
    auto& to = ensure_account_locked(to_user);
    if (from.balance_minor < amount_minor) {
        reason = "insufficient-funds";
        return std::nullopt;
    }
    if (add_overflows(to.balance_minor, amount_minor)) {
        reason = "balance-overflow";
        return std::nullopt;
    }

    from.balance_minor -= amount_minor;
    to.balance_minor += amount_minor;
    const auto now = unix_now();
    from.updated_unix = now;
    to.updated_unix = now;
    const auto entry = append_locked(
        EconomyEntryKind::transfer,
        std::move(reference),
        std::move(from_user),
        std::move(to_user),
        amount_minor,
        std::move(memo));
    persist_locked();
    reason.clear();
    return entry;
}

std::optional<EconomyEntry> EconomyLedger::mint(
    std::string to_user,
    const std::int64_t amount_minor,
    std::string reference,
    std::string memo,
    std::string& reason) {
    if (!safe_field(to_user, 256U) ||
        amount_minor <= 0 ||
        !safe_field(reference, 128U) ||
        !safe_field(memo, 512U, true)) {
        reason = "invalid-mint";
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    if (reference_exists_locked(reference)) {
        reason = "duplicate-reference";
        return std::nullopt;
    }
    auto& to = ensure_account_locked(to_user);
    if (add_overflows(to.balance_minor, amount_minor)) {
        reason = "balance-overflow";
        return std::nullopt;
    }
    to.balance_minor += amount_minor;
    to.updated_unix = unix_now();
    const auto entry = append_locked(
        EconomyEntryKind::mint,
        std::move(reference),
        {},
        std::move(to_user),
        amount_minor,
        std::move(memo));
    persist_locked();
    reason.clear();
    return entry;
}

std::optional<EconomyEntry> EconomyLedger::burn(
    std::string from_user,
    const std::int64_t amount_minor,
    std::string reference,
    std::string memo,
    std::string& reason) {
    if (!safe_field(from_user, 256U) ||
        amount_minor <= 0 ||
        !safe_field(reference, 128U) ||
        !safe_field(memo, 512U, true)) {
        reason = "invalid-burn";
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    if (reference_exists_locked(reference)) {
        reason = "duplicate-reference";
        return std::nullopt;
    }
    auto& from = ensure_account_locked(from_user);
    if (from.balance_minor < amount_minor) {
        reason = "insufficient-funds";
        return std::nullopt;
    }
    from.balance_minor -= amount_minor;
    from.updated_unix = unix_now();
    const auto entry = append_locked(
        EconomyEntryKind::burn,
        std::move(reference),
        std::move(from_user),
        {},
        amount_minor,
        std::move(memo));
    persist_locked();
    reason.clear();
    return entry;
}

std::optional<EconomyEscrow> EconomyLedger::reserve(
    std::string buyer_user,
    std::string seller_user,
    const std::int64_t amount_minor,
    std::string reference,
    std::string& reason) {
    if (!safe_field(buyer_user, 256U) ||
        !safe_field(seller_user, 256U) ||
        buyer_user == seller_user ||
        amount_minor <= 0 ||
        !safe_field(reference, 128U)) {
        reason = "invalid-escrow";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    if (reference_exists_locked(reference) ||
        std::any_of(
            escrows_.begin(), escrows_.end(),
            [&](const auto& item) {
                return item.second.reference == reference;
            })) {
        reason = "duplicate-reference";
        return std::nullopt;
    }

    auto& buyer = ensure_account_locked(buyer_user);
    (void)ensure_account_locked(seller_user);
    if (buyer.balance_minor < amount_minor) {
        reason = "insufficient-funds";
        return std::nullopt;
    }

    buyer.balance_minor -= amount_minor;
    buyer.updated_unix = unix_now();
    const auto now = unix_now();
    EconomyEscrow escrow{
        .id = security::random_hex(16),
        .reference = reference,
        .buyer_user = buyer_user,
        .seller_user = seller_user,
        .amount_minor = amount_minor,
        .state = EscrowState::reserved,
        .created_unix = now,
        .updated_unix = now};
    escrows_[escrow.id] = escrow;
    (void)append_locked(
        EconomyEntryKind::escrow_reserve,
        reference + ":reserve",
        buyer_user,
        {},
        amount_minor,
        "escrow reserve");
    persist_locked();
    reason.clear();
    return escrow;
}

bool EconomyLedger::commit_escrow(
    const std::string_view escrow_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = escrows_.find(std::string{escrow_id});
    if (it == escrows_.end()) {
        reason = "escrow-not-found";
        return false;
    }
    if (it->second.state != EscrowState::reserved) {
        reason = "escrow-not-reserved";
        return false;
    }
    auto& seller =
        ensure_account_locked(it->second.seller_user);
    if (add_overflows(
            seller.balance_minor,
            it->second.amount_minor)) {
        reason = "balance-overflow";
        return false;
    }
    seller.balance_minor += it->second.amount_minor;
    seller.updated_unix = unix_now();
    it->second.state = EscrowState::committed;
    it->second.updated_unix = unix_now();
    (void)append_locked(
        EconomyEntryKind::escrow_commit,
        it->second.reference + ":commit",
        {},
        it->second.seller_user,
        it->second.amount_minor,
        "escrow commit");
    persist_locked();
    reason.clear();
    return true;
}

bool EconomyLedger::release_escrow(
    const std::string_view escrow_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = escrows_.find(std::string{escrow_id});
    if (it == escrows_.end()) {
        reason = "escrow-not-found";
        return false;
    }
    if (it->second.state != EscrowState::reserved) {
        reason = "escrow-not-reserved";
        return false;
    }
    auto& buyer =
        ensure_account_locked(it->second.buyer_user);
    if (add_overflows(
            buyer.balance_minor,
            it->second.amount_minor)) {
        reason = "balance-overflow";
        return false;
    }
    buyer.balance_minor += it->second.amount_minor;
    buyer.updated_unix = unix_now();
    it->second.state = EscrowState::released;
    it->second.updated_unix = unix_now();
    (void)append_locked(
        EconomyEntryKind::escrow_release,
        it->second.reference + ":release",
        {},
        it->second.buyer_user,
        it->second.amount_minor,
        "escrow release");
    persist_locked();
    reason.clear();
    return true;
}

std::optional<EconomyEscrow> EconomyLedger::escrow(
    const std::string_view escrow_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = escrows_.find(std::string{escrow_id});
    return it == escrows_.end()
               ? std::nullopt
               : std::optional<EconomyEscrow>{it->second};
}

std::vector<EconomyEntry> EconomyLedger::entries_for(
    const std::string_view user_id,
    const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<EconomyEntry> result;
    const auto capped = std::min<std::size_t>(limit, 500U);
    for (auto it = entries_.rbegin();
         it != entries_.rend() && result.size() < capped;
         ++it) {
        if (it->from_user == user_id ||
            it->to_user == user_id) {
            result.push_back(*it);
        }
    }
    return result;
}

std::vector<EconomyEscrow> EconomyLedger::escrows_for(
    const std::string_view user_id,
    const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<EconomyEscrow> result;
    for (const auto& [_, escrow] : escrows_) {
        if (escrow.buyer_user == user_id ||
            escrow.seller_user == user_id) {
            result.push_back(escrow);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const EconomyEscrow& left,
           const EconomyEscrow& right) {
            return left.created_unix > right.created_unix;
        });
    if (result.size() > std::min<std::size_t>(limit, 500U)) {
        result.resize(std::min<std::size_t>(limit, 500U));
    }
    return result;
}

std::int64_t EconomyLedger::total_supply_minor() const {
    std::scoped_lock lock(mutex_);
    std::int64_t total = 0;
    for (const auto& [_, account] : accounts_) {
        if (account.balance_minor > 0 &&
            add_overflows(total, account.balance_minor)) {
            return std::numeric_limits<std::int64_t>::max();
        }
        total += account.balance_minor;
    }
    for (const auto& [_, escrow] : escrows_) {
        if (escrow.state == EscrowState::reserved) {
            if (add_overflows(total, escrow.amount_minor)) {
                return std::numeric_limits<std::int64_t>::max();
            }
            total += escrow.amount_minor;
        }
    }
    return total;
}

std::size_t EconomyLedger::account_count() const {
    std::scoped_lock lock(mutex_);
    return accounts_.size();
}

std::size_t EconomyLedger::entry_count() const {
    std::scoped_lock lock(mutex_);
    return entries_.size();
}

void EconomyLedger::load() {
    std::scoped_lock lock(mutex_);
    accounts_.clear();
    escrows_.clear();
    entries_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        try {
            if (fields.size() == 5U &&
                fields[0] == "A") {
                EconomyAccount account{
                    .user_id = fields[1],
                    .balance_minor = std::stoll(fields[2]),
                    .created_unix = std::stoll(fields[3]),
                    .updated_unix = std::stoll(fields[4])};
                if (account.balance_minor >= 0 &&
                    safe_field(account.user_id, 256U)) {
                    accounts_[account.user_id] =
                        std::move(account);
                }
            } else if (fields.size() == 9U &&
                       fields[0] == "E") {
                EconomyEntry entry{
                    .id = fields[1],
                    .kind = parse_kind(fields[2]),
                    .reference = fields[3],
                    .from_user = fields[4],
                    .to_user = fields[5],
                    .amount_minor = std::stoll(fields[6]),
                    .memo = fields[7],
                    .created_unix = std::stoll(fields[8])};
                if (entry.amount_minor > 0) {
                    entries_.push_back(std::move(entry));
                }
            } else if (fields.size() == 9U &&
                       fields[0] == "S") {
                EconomyEscrow escrow{
                    .id = fields[1],
                    .reference = fields[2],
                    .buyer_user = fields[3],
                    .seller_user = fields[4],
                    .amount_minor = std::stoll(fields[5]),
                    .state = parse_escrow_state(fields[6]),
                    .created_unix = std::stoll(fields[7]),
                    .updated_unix = std::stoll(fields[8])};
                if (escrow.amount_minor > 0) {
                    escrows_[escrow.id] =
                        std::move(escrow);
                }
            }
        } catch (...) {
        }
    }
}

void EconomyLedger::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot write economy ledger");
    }

    output << "# OpenGenesisLINK economy ledger v1 "
           << currency_code_ << '\n';

    std::vector<EconomyAccount> accounts;
    accounts.reserve(accounts_.size());
    for (const auto& [_, account] : accounts_) {
        accounts.push_back(account);
    }
    std::sort(
        accounts.begin(), accounts.end(),
        [](const EconomyAccount& left,
           const EconomyAccount& right) {
            return left.user_id < right.user_id;
        });
    for (const auto& account : accounts) {
        output << "A\t" << account.user_id << '\t'
               << account.balance_minor << '\t'
               << account.created_unix << '\t'
               << account.updated_unix << '\n';
    }

    for (const auto& entry : entries_) {
        output << "E\t" << entry.id << '\t'
               << economy_entry_kind_name(entry.kind) << '\t'
               << entry.reference << '\t'
               << entry.from_user << '\t'
               << entry.to_user << '\t'
               << entry.amount_minor << '\t'
               << entry.memo << '\t'
               << entry.created_unix << '\n';
    }

    std::vector<EconomyEscrow> escrows;
    escrows.reserve(escrows_.size());
    for (const auto& [_, escrow] : escrows_) {
        escrows.push_back(escrow);
    }
    std::sort(
        escrows.begin(), escrows.end(),
        [](const EconomyEscrow& left,
           const EconomyEscrow& right) {
            return left.created_unix < right.created_unix;
        });
    for (const auto& escrow : escrows) {
        output << "S\t" << escrow.id << '\t'
               << escrow.reference << '\t'
               << escrow.buyer_user << '\t'
               << escrow.seller_user << '\t'
               << escrow.amount_minor << '\t'
               << escrow_state_name(escrow.state) << '\t'
               << escrow.created_unix << '\t'
               << escrow.updated_unix << '\n';
    }

    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush economy ledger");
    }
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
