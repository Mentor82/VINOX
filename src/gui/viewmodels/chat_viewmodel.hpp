#pragma once

#include "../backend_interface.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace vinox::gui {

class ChatViewModel {
public:
    explicit ChatViewModel(std::shared_ptr<IVinoxBackend> backend);

    void set_backend(std::shared_ptr<IVinoxBackend> backend);

    const std::vector<ChatMessage>& messages() const { return messages_; }
    bool is_generating() const { return is_generating_; }
    double token_rate() const { return token_rate_; }
    uint64_t total_tokens() const { return total_tokens_; }
    const std::string& last_error() const { return last_error_; }

    void send_message(const std::string& user_text, float temperature = 0.7f, float top_p = 0.9f, uint64_t max_tokens = 512);
    void cancel();
    void clear();

    // Callback hooks for UI notification
    std::function<void()> on_message_added;
    std::function<void(const std::string& chunk, bool is_reasoning)> on_token_delta;
    std::function<void(bool generating)> on_generating_changed;

private:
    std::shared_ptr<IVinoxBackend> backend_;
    std::vector<ChatMessage> messages_;
    bool is_generating_{false};
    double token_rate_{0.0};
    uint64_t total_tokens_{0};
    std::string last_error_;
};

} // namespace vinox::gui
