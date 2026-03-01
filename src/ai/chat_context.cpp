#include "ai/chat_context.hpp"

#include <algorithm>

namespace mikudesk::ai {

ChatContext::ChatContext(std::string system_prompt, int context_rounds) {
  Configure(std::move(system_prompt), context_rounds);
}

void ChatContext::Configure(std::string system_prompt, int context_rounds) {
  if (!system_prompt.empty()) {
    system_prompt_ = std::move(system_prompt);
  }
  context_rounds_ = std::clamp(context_rounds, 1, 64);
  TrimToSlidingWindow();
}

void ChatContext::Clear() {
  history_.clear();
}

std::vector<ChatMessage> ChatContext::BuildMessagesForRequest(std::string_view user_message) const {
  std::vector<ChatMessage> messages;
  messages.reserve(history_.size() + 2);
  messages.push_back(ChatMessage{.role = ChatRole::kSystem, .content = system_prompt_});
  messages.insert(messages.end(), history_.begin(), history_.end());
  messages.push_back(ChatMessage{.role = ChatRole::kUser, .content = std::string(user_message)});
  return messages;
}

app::Result<void> ChatContext::CommitExchange(std::string user_message, std::string assistant_message) {
  if (user_message.empty()) {
    return std::unexpected(app::AppError::kAiContextInvalid);
  }

  history_.push_back(ChatMessage{.role = ChatRole::kUser, .content = std::move(user_message)});
  if (!assistant_message.empty()) {
    history_.push_back(
        ChatMessage{.role = ChatRole::kAssistant, .content = std::move(assistant_message)});
  }
  TrimToSlidingWindow();
  return {};
}

void ChatContext::TrimToSlidingWindow() {
  const std::size_t max_messages = static_cast<std::size_t>(context_rounds_) * 2U;
  if (history_.size() <= max_messages) {
    return;
  }

  const std::size_t remove_count = history_.size() - max_messages;
  history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(remove_count));
}

}  // namespace mikudesk::ai

