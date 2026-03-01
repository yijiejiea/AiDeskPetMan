#include <iostream>
#include <string>
#include <vector>

#include "ai/chat_context.hpp"
#include "app/app_error.hpp"

namespace {

bool Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using mikudesk::ai::ChatContext;
  using mikudesk::ai::ChatRole;

  ChatContext context("System Prompt", 2);

  {
    const auto bootstrap_messages = context.BuildMessagesForRequest("hello");
    if (!Expect(bootstrap_messages.size() == 2, "bootstrap messages should contain system + user")) {
      return 1;
    }
    if (!Expect(bootstrap_messages[0].role == ChatRole::kSystem, "first message should be system")) {
      return 1;
    }
    if (!Expect(bootstrap_messages[1].role == ChatRole::kUser, "second message should be user")) {
      return 1;
    }
  }

  {
    auto invalid_commit = context.CommitExchange("", "assistant");
    if (!Expect(!invalid_commit.has_value(), "empty user message should be rejected")) {
      return 1;
    }
    if (!Expect(invalid_commit.error() == mikudesk::app::AppError::kAiContextInvalid,
                "empty user message should return kAiContextInvalid")) {
      return 1;
    }
  }

  {
    if (!Expect(context.CommitExchange("u1", "a1").has_value(), "commit u1/a1 should pass")) {
      return 1;
    }
    if (!Expect(context.CommitExchange("u2", "a2").has_value(), "commit u2/a2 should pass")) {
      return 1;
    }
    if (!Expect(context.CommitExchange("u3", "a3").has_value(), "commit u3/a3 should pass")) {
      return 1;
    }

    const auto messages = context.BuildMessagesForRequest("u4");
    if (!Expect(messages.size() == 6, "sliding window should keep system + last 2 rounds + current user")) {
      return 1;
    }
    if (!Expect(messages[0].content == "System Prompt", "system prompt should be kept as first message")) {
      return 1;
    }
    if (!Expect(messages[1].content == "u2" && messages[2].content == "a2" &&
                    messages[3].content == "u3" && messages[4].content == "a3",
                "history should keep latest two rounds")) {
      return 1;
    }
    if (!Expect(messages[5].content == "u4", "last message should be the new user input")) {
      return 1;
    }
  }

  {
    context.Clear();
    const auto cleared = context.BuildMessagesForRequest("after_clear");
    if (!Expect(cleared.size() == 2, "clear should remove all history")) {
      return 1;
    }
    if (!Expect(cleared[1].content == "after_clear", "user message after clear mismatch")) {
      return 1;
    }
  }

  return 0;
}
