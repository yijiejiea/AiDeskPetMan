#pragma once

#include <stop_token>
#include <string_view>

#include "ai/inference_types.hpp"
#include "app/app_error.hpp"
#include "core/task.hpp"

namespace mikudesk::ai {

class IInferenceEngine {
 public:
  virtual ~IInferenceEngine() = default;

  virtual core::Task<app::Result<ChatReply>> Chat(const ChatRequest& request,
                                                   StreamChunkCallback on_chunk,
                                                   std::stop_token stop_token) = 0;
  virtual std::string_view EngineName() const = 0;
  virtual bool IsReady() const = 0;
};

}  // namespace mikudesk::ai

