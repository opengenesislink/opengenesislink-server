#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opengenesis::core {

struct DirectMessage {
    std::string id;
    std::string sender_id;
    std::string recipient_id;
    std::string text;
    std::int64_t sent_unix{0};
    std::int64_t read_unix{0};
};

class MessageStore final {
public:
    explicit MessageStore(std::string path);

    [[nodiscard]] std::optional<DirectMessage> send(std::string sender_id,
                                                    std::string recipient_id,
                                                    std::string text,
                                                    std::string& reason);
    [[nodiscard]] std::vector<DirectMessage> list_for_user(std::string_view user_id,
                                                           std::size_t limit = 100) const;
    bool mark_read(std::string_view user_id, std::string_view message_id);
    [[nodiscard]] std::size_t unread_count(std::string_view user_id) const;
    [[nodiscard]] std::size_t count() const;

private:
    void load();
    void persist_locked() const;
    static std::string clean_text(std::string text);

    std::string path_;
    mutable std::mutex mutex_;
    std::vector<DirectMessage> messages_;
};

} // namespace opengenesis::core
