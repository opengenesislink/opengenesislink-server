#include "opengenesis/core/social_policy_store.hpp"

#include "opengenesis/platform/filesystem.hpp"

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

bool safe_id(const std::string_view value) {
    return !value.empty() && value.size() <= 256U &&
           value.find('\t') == std::string_view::npos &&
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

} // namespace

SocialPolicyStore::SocialPolicyStore(std::string path)
    : path_(std::move(path)) {
    load();
}

std::string SocialPolicyStore::key(
    const std::string_view owner_user_id,
    const std::string_view target_user_id) {
    return std::string{owner_user_id} + "|" +
           std::string{target_user_id};
}

bool SocialPolicyStore::set_blocked(
    std::string owner_user_id,
    std::string target_user_id,
    const bool blocked,
    std::string& reason) {
    if (!safe_id(owner_user_id) ||
        !safe_id(target_user_id) ||
        owner_user_id == target_user_id) {
        reason = "invalid-social-policy";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto policy_key = key(owner_user_id, target_user_id);
    auto& policy = policies_[policy_key];
    policy.owner_user_id = std::move(owner_user_id);
    policy.target_user_id = std::move(target_user_id);
    policy.blocked = blocked;
    policy.updated_unix = unix_now();
    if (!policy.blocked && !policy.muted) {
        policies_.erase(policy_key);
    }
    persist_locked();
    reason.clear();
    return true;
}

bool SocialPolicyStore::set_muted(
    std::string owner_user_id,
    std::string target_user_id,
    const bool muted_value,
    std::string& reason) {
    if (!safe_id(owner_user_id) ||
        !safe_id(target_user_id) ||
        owner_user_id == target_user_id) {
        reason = "invalid-social-policy";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto policy_key = key(owner_user_id, target_user_id);
    auto& policy = policies_[policy_key];
    policy.owner_user_id = std::move(owner_user_id);
    policy.target_user_id = std::move(target_user_id);
    policy.muted = muted_value;
    policy.updated_unix = unix_now();
    if (!policy.blocked && !policy.muted) {
        policies_.erase(policy_key);
    }
    persist_locked();
    reason.clear();
    return true;
}

bool SocialPolicyStore::blocks(
    const std::string_view owner_user_id,
    const std::string_view target_user_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = policies_.find(
        key(owner_user_id, target_user_id));
    return it != policies_.end() && it->second.blocked;
}

bool SocialPolicyStore::muted(
    const std::string_view owner_user_id,
    const std::string_view target_user_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = policies_.find(
        key(owner_user_id, target_user_id));
    return it != policies_.end() && it->second.muted;
}

std::vector<SocialPolicy> SocialPolicyStore::list_for_user(
    const std::string_view owner_user_id) const {
    std::scoped_lock lock(mutex_);
    std::vector<SocialPolicy> result;
    for (const auto& [_, policy] : policies_) {
        if (policy.owner_user_id == owner_user_id) {
            result.push_back(policy);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const SocialPolicy& left,
           const SocialPolicy& right) {
            if (left.updated_unix != right.updated_unix) {
                return left.updated_unix > right.updated_unix;
            }
            return left.target_user_id < right.target_user_id;
        });
    return result;
}

std::size_t SocialPolicyStore::count() const {
    std::scoped_lock lock(mutex_);
    return policies_.size();
}

void SocialPolicyStore::load() {
    std::scoped_lock lock(mutex_);
    policies_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 5U) continue;
        try {
            SocialPolicy policy{
                .owner_user_id = fields[0],
                .target_user_id = fields[1],
                .blocked = fields[2] == "1",
                .muted = fields[3] == "1",
                .updated_unix = std::stoll(fields[4])};
            if (safe_id(policy.owner_user_id) &&
                safe_id(policy.target_user_id) &&
                policy.owner_user_id != policy.target_user_id &&
                (policy.blocked || policy.muted)) {
                policies_[key(
                    policy.owner_user_id,
                    policy.target_user_id)] = std::move(policy);
            }
        } catch (...) {
        }
    }
}

void SocialPolicyStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot write social policies");
    }
    output << "# OpenGenesisLINK social policies v1\n";

    std::vector<SocialPolicy> rows;
    rows.reserve(policies_.size());
    for (const auto& [_, policy] : policies_) {
        rows.push_back(policy);
    }
    std::sort(
        rows.begin(), rows.end(),
        [](const SocialPolicy& left,
           const SocialPolicy& right) {
            if (left.owner_user_id != right.owner_user_id) {
                return left.owner_user_id <
                       right.owner_user_id;
            }
            return left.target_user_id <
                   right.target_user_id;
        });

    for (const auto& policy : rows) {
        output << policy.owner_user_id << '\t'
               << policy.target_user_id << '\t'
               << (policy.blocked ? 1 : 0) << '\t'
               << (policy.muted ? 1 : 0) << '\t'
               << policy.updated_unix << '\n';
    }

    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush social policies");
    }
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
