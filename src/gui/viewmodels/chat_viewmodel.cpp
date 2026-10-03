#include "chat_viewmodel.hpp"

namespace vinox::gui {

ChatViewModel::ChatViewModel(std::shared_ptr<IVinoxBackend> backend)
    : backend_(std::move(backend)) {}

void ChatViewModel::set_backend(std::shared_ptr<IVinoxBackend> backend) {
    backend_ = std::move(backend);
}

void ChatViewModel::send_message(const std::string& user_text, float temperature, float top_p, uint64_t max_tokens) {
    if (!backend_ || is_generating_ || user_text.empty()) return;

    // Append user message
    ChatMessage user_msg;
    user_msg.id = "msg_" + std::to_string(messages_.size() + 1);
    user_msg.role = "user";
    user_msg.content = user_text;
    user_msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    messages_.push_back(user_msg);
    if (on_message_added) on_message_added();

    // Prepare assistant message
    ChatMessage asst_msg;
    asst_msg.id = "msg_" + std::to_string(messages_.size() + 1);
    asst_msg.role = "assistant";
    asst_msg.content = "";
    asst_msg.reasoning_content = "";
    asst_msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    messages_.push_back(asst_msg);
    if (on_message_added) on_message_added();

    is_generating_ = true;
    if (on_generating_changed) on_generating_changed(true);

    auto start_time = std::chrono::steady_clock::now();
    uint64_t tokens_in_turn = 0;

    auto token_cb = [this, &tokens_in_turn, start_time](const std::string& token, bool is_reasoning) {
        if (messages_.empty()) return;
        auto& current_msg = messages_.back();

        if (is_reasoning) {
            current_msg.reasoning_content += token;
        } else {
            current_msg.content += token;
        }

        tokens_in_turn++;
        total_tokens_++;
        current_msg.token_count++;

        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
        if (elapsed_sec > 0.05) {
            token_rate_ = static_cast<double>(tokens_in_turn) / elapsed_sec;
        }

        if (on_token_delta) on_token_delta(token, is_reasoning);
    };

    std::string err;
    backend_->generate_chat_stream(messages_, temperature, top_p, max_tokens, token_cb, err);
    last_error_ = err;

    is_generating_ = false;
    if (on_generating_changed) on_generating_changed(false);
}

void ChatViewModel::cancel() {
    if (backend_ && is_generating_) {
        backend_->cancel_generation();
    }
    is_generating_ = false;
    if (on_generating_changed) on_generating_changed(false);
}

void ChatViewModel::clear() {
    cancel();
    messages_.clear();
    total_tokens_ = 0;
    token_rate_ = 0.0;
    last_error_.clear();
    if (on_message_added) on_message_added();
}

} // namespace vinox::gui
