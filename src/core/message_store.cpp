#include "opengenesis/core/message_store.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::core {
namespace {
std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}
} // namespace

MessageStore::MessageStore(std::string path) : path_(std::move(path)) { load(); }

std::string MessageStore::clean_text(std::string text) {
    for (auto& ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    if (text.size() > 2000) text.resize(2000);
    return text;
}

std::optional<DirectMessage> MessageStore::send(std::string sender_id, std::string recipient_id,
                                                std::string text, std::string& reason) {
    text = clean_text(std::move(text));
    if (sender_id.empty() || recipient_id.empty() || sender_id == recipient_id) {
        reason = "invalid-recipient";
        return std::nullopt;
    }
    if (text.empty()) {
        reason = "empty-message";
        return std::nullopt;
    }
    DirectMessage message{.id = security::random_hex(16),
                          .sender_id = std::move(sender_id),
                          .recipient_id = std::move(recipient_id),
                          .text = std::move(text),
                          .sent_unix = unix_now()};
    std::scoped_lock lock(mutex_);
    messages_.push_back(message);
    persist_locked();
    reason.clear();
    return message;
}

std::vector<DirectMessage> MessageStore::list_for_user(const std::string_view user_id,
                                                       const std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    std::vector<DirectMessage> result;
    for (auto it = messages_.rbegin(); it != messages_.rend() && result.size() < limit; ++it) {
        if (it->sender_id == user_id || it->recipient_id == user_id) result.push_back(*it);
    }
    return result;
}

bool MessageStore::mark_read(const std::string_view user_id, const std::string_view message_id) {
    std::scoped_lock lock(mutex_);
    for (auto& message : messages_) {
        if (message.id == message_id && message.recipient_id == user_id) {
            if (message.read_unix == 0) {
                message.read_unix = unix_now();
                persist_locked();
            }
            return true;
        }
    }
    return false;
}

std::size_t MessageStore::unread_count(const std::string_view user_id) const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(messages_.begin(), messages_.end(), [&](const auto& message) {
        return message.recipient_id == user_id && message.read_unix == 0;
    }));
}

std::size_t MessageStore::count() const {
    std::scoped_lock lock(mutex_);
    return messages_.size();
}

void MessageStore::load() {
    std::scoped_lock lock(mutex_);
    messages_.clear();
    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 6) continue;
        try {
            DirectMessage message{.id = fields[0],
                                  .sender_id = fields[1],
                                  .recipient_id = fields[2],
                                  .text = fields[3],
                                  .sent_unix = std::stoll(fields[4]),
                                  .read_unix = std::stoll(fields[5])};
            if (!message.id.empty() && !message.sender_id.empty() && !message.recipient_id.empty()) {
                messages_.push_back(std::move(message));
            }
        } catch (...) {
        }
    }
}

void MessageStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write message store");
    output << "# OpenGenesisLINK direct-message store v1\n";
    for (const auto& message : messages_) {
        output << message.id << '\t' << message.sender_id << '\t' << message.recipient_id << '\t'
               << message.text << '\t' << message.sent_unix << '\t' << message.read_unix << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush message store");
    std::filesystem::rename(temp, path);
}

} // namespace opengenesis::core
