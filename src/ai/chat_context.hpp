#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ai/inference_types.hpp"
#include "app/app_error.hpp"

namespace mikudesk::ai {

class ChatContext {
 public:
  ChatContext() = default;
  explicit ChatContext(std::string system_prompt, int context_rounds);

  void Configure(std::string system_prompt, int context_rounds);
  void Clear();

  std::vector<ChatMessage> BuildMessagesForRequest(std::string_view user_message) const;
  app::Result<void> CommitExchange(std::string user_message, std::string assistant_message);

 private:
  void TrimToSlidingWindow();

  std::string system_prompt_ = "你是 MikuDesk 桌面助手。";
  int context_rounds_ = 10;
  std::vector<ChatMessage> history_;
};

}  // namespace mikudesk::ai

