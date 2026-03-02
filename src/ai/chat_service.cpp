#include "ai/chat_service.hpp"

#include <utility>

#include "security/dpapi_secret_store.hpp"

namespace mikudesk::ai {

ChatService::ChatService(app::AppConfig app_config) : app_config_(std::move(app_config)) {
  std::scoped_lock lock(mutex_);
  context_.Configure(app_config_.ai.system_prompt, app_config_.ai.context_rounds);
  active_mode_ = app_config_.ai.inference_mode;
  (void)RebuildEnginesLocked();
}

void ChatService::UpdateConfig(const app::AppConfig& app_config) {
  std::scoped_lock lock(mutex_);
  app_config_ = app_config;
  context_.Configure(app_config_.ai.system_prompt, app_config_.ai.context_rounds);
  active_mode_ = app_config_.ai.inference_mode;
  (void)RebuildEnginesLocked();
}

void ChatService::ClearContext() {
  std::scoped_lock lock(mutex_);
  context_.Clear();
}

app::InferenceMode ChatService::GetInferenceMode() const {
  std::scoped_lock lock(mutex_);
  return active_mode_;
}

app::Result<void> ChatService::SetInferenceMode(app::InferenceMode inference_mode) {
  std::scoped_lock lock(mutex_);
  active_mode_ = inference_mode;
  return {};
}

std::string_view ChatService::GetCurrentEngineName() const {
  std::scoped_lock lock(mutex_);
  std::shared_ptr<IInferenceEngine> engine =
      const_cast<ChatService*>(this)->GetCurrentEngineLocked();
  if (engine == nullptr) {
    return "none";
  }
  return engine->EngineName();
}

app::Result<ChatReply> ChatService::SendMessage(std::string user_message, StreamChunkCallback on_chunk,
                                                std::stop_token stop_token) {
  if (user_message.empty()) {
    return std::unexpected(app::AppError::kAiContextInvalid);
  }

  ChatRequest request;
  std::shared_ptr<IInferenceEngine> engine;
  {
    std::scoped_lock lock(mutex_);
    if (active_mode_ == app::InferenceMode::kLocalModel) {
#if !defined(MIKUDESK_ENABLE_LLAMA) || !MIKUDESK_ENABLE_LLAMA
      return std::unexpected(app::AppError::kLlamaNotEnabled);
#else
      if (local_engine_ == nullptr || !local_engine_->IsReady()) {
        return std::unexpected(app::AppError::kLlamaLoadFailed);
      }
#endif
    } else if (active_mode_ == app::InferenceMode::kTokenApi) {
      if (cloud_engine_ == nullptr || !cloud_engine_->IsReady()) {
        if (app_config_.security.encrypted_api_key.empty()) {
          return std::unexpected(app::AppError::kApiKeyMissing);
        }
        return std::unexpected(app::AppError::kAiEngineNotReady);
      }
    }

    engine = GetCurrentEngineLocked();
    if (engine == nullptr || !engine->IsReady()) {
      return std::unexpected(app::AppError::kAiEngineNotReady);
    }

    request.messages = context_.BuildMessagesForRequest(user_message);
    request.max_tokens = app_config_.ai.max_tokens;
    request.temperature = app_config_.ai.temperature;
    request.top_p = app_config_.ai.top_p;
    request.stream = app_config_.ai.stream;
  }

  auto task = engine->Chat(request, std::move(on_chunk), stop_token);
  auto result = task.TakeResult();
  if (!result.has_value()) {
    return std::unexpected(result.error());
  }

  {
    std::scoped_lock lock(mutex_);
    auto commit_result = context_.CommitExchange(user_message, result->text);
    if (!commit_result.has_value()) {
      return std::unexpected(commit_result.error());
    }
  }

  return result;
}

app::Result<void> ChatService::RebuildEnginesLocked() {
  security::DpapiSecretStore secret_store;
  auto decrypted_key = secret_store.Decrypt(app_config_.security.encrypted_api_key);
  if (!decrypted_key.has_value() && !app_config_.security.encrypted_api_key.empty()) {
    return std::unexpected(app::AppError::kApiKeyDecryptFailed);
  }

  CloudEngineConfig cloud_config;
  cloud_config.api_base_url = ResolveApiBaseUrlLocked();
  cloud_config.api_model = app_config_.ai.api_model;
  cloud_config.stream = app_config_.ai.stream;
  cloud_config.request_timeout_ms = app_config_.ai.request_timeout_ms;
  if (decrypted_key.has_value()) {
    cloud_config.api_key = *decrypted_key;
  }
  cloud_engine_ = std::make_shared<CloudEngine>(std::move(cloud_config));

  LocalEngineConfig local_config;
  local_config.model_path = app_config_.ai.local_model_path.string();
  local_config.gpu_layers = app_config_.ai.local_gpu_layers;
  local_config.threads = app_config_.ai.local_threads;
  local_config.context_length = app_config_.ai.local_context_length;
  local_engine_ = std::make_shared<LocalEngine>(std::move(local_config));

  return {};
}

std::shared_ptr<IInferenceEngine> ChatService::GetCurrentEngineLocked() {
  std::shared_ptr<IInferenceEngine> preferred = nullptr;
  std::shared_ptr<IInferenceEngine> fallback = nullptr;

  if (active_mode_ == app::InferenceMode::kLocalModel) {
    preferred = local_engine_;
    fallback = cloud_engine_;
  } else {
    preferred = cloud_engine_;
    fallback = local_engine_;
  }

  if (preferred != nullptr && preferred->IsReady()) {
    return preferred;
  }
  if (fallback != nullptr && fallback->IsReady()) {
    return fallback;
  }
  return preferred != nullptr ? preferred : fallback;
}

std::string ChatService::ResolveApiBaseUrlLocked() const {
  switch (app_config_.ai.provider) {
    case app::AiProvider::kDeepSeek:
      return app_config_.ai.api_base_url.empty() ? "https://api.deepseek.com/v1"
                                                  : app_config_.ai.api_base_url;
    case app::AiProvider::kCustom:
      return app_config_.ai.api_base_url;
    case app::AiProvider::kOpenAi:
      return app_config_.ai.api_base_url.empty() ? "https://api.openai.com/v1"
                                                  : app_config_.ai.api_base_url;
  }
  return app_config_.ai.api_base_url;
}

}  // namespace mikudesk::ai
