#pragma once

#include <stop_token>
#include <string>

#include "ai/inference_engine.hpp"

namespace mikudesk::ai {

struct CloudEngineConfig {
  std::string api_base_url;
  std::string api_model;
  std::string api_key;
  bool stream = true;
  int request_timeout_ms = 60000;
};

class CloudEngine final : public IInferenceEngine {
 public:
  explicit CloudEngine(CloudEngineConfig config);

  core::Task<app::Result<ChatReply>> Chat(const ChatRequest& request, StreamChunkCallback on_chunk,
                                           std::stop_token stop_token) override;
  std::string_view EngineName() const override;
  bool IsReady() const override;

 private:
  CloudEngineConfig config_;
};

}  // namespace mikudesk::ai

