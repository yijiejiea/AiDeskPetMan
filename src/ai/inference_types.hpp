#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mikudesk::ai {

enum class ChatRole {
  kSystem,
  kUser,
  kAssistant,
};

struct ChatMessage {
  ChatRole role = ChatRole::kUser;
  std::string content;
};

struct ChatRequest {
  std::vector<ChatMessage> messages;
  int max_tokens = 1024;
  double temperature = 0.7;
  double top_p = 0.9;
  bool stream = true;
};

struct ChatReply {
  std::string text;
  std::string finish_reason;
};

using StreamChunkCallback = std::function<void(std::string_view)>;

}  // namespace mikudesk::ai

