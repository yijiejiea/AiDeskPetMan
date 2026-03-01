#pragma once

#include <memory>
#include <stop_token>
#include <string>

#include "ai/inference_engine.hpp"

namespace mikudesk::ai {

struct LocalEngineConfig {
  std::string model_path;
  int gpu_layers = 0;
  int threads = 8;
  int context_length = 4096;
};

class LocalEngine final : public IInferenceEngine {
 public:
  explicit LocalEngine(LocalEngineConfig config);
  ~LocalEngine() override;

  core::Task<app::Result<ChatReply>> Chat(const ChatRequest& request, StreamChunkCallback on_chunk,
                                           std::stop_token stop_token) override;
  std::string_view EngineName() const override;
  bool IsReady() const override;

 private:
  app::Result<void> EnsureLoaded();

  struct RuntimeState;
  LocalEngineConfig config_;
  std::unique_ptr<RuntimeState> runtime_state_;
};

}  // namespace mikudesk::ai

