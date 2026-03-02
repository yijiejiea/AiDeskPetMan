#pragma once

#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>

#include "ai/chat_context.hpp"
#include "ai/cloud_engine.hpp"
#include "ai/inference_engine.hpp"
#include "ai/local_engine.hpp"
#include "app/config.hpp"

namespace mikudesk::ai {

class ChatService {
 public:
  explicit ChatService(app::AppConfig app_config);

  void UpdateConfig(const app::AppConfig& app_config);
  void ClearContext();
  app::InferenceMode GetInferenceMode() const;
  app::Result<void> SetInferenceMode(app::InferenceMode inference_mode);
  std::string_view GetCurrentEngineName() const;

  app::Result<ChatReply> SendMessage(std::string user_message, StreamChunkCallback on_chunk,
                                     std::stop_token stop_token = {});

 private:
  app::Result<void> RebuildEnginesLocked();
  std::shared_ptr<IInferenceEngine> GetCurrentEngineLocked();
  std::string ResolveApiBaseUrlLocked() const;

  mutable std::mutex mutex_;
  app::AppConfig app_config_;
  ChatContext context_;
  app::InferenceMode active_mode_ = app::InferenceMode::kTokenApi;
  std::shared_ptr<CloudEngine> cloud_engine_;
  std::shared_ptr<LocalEngine> local_engine_;
};

}  // namespace mikudesk::ai
