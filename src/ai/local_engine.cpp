#include "ai/local_engine.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
#include "llama.h"
#endif

namespace mikudesk::ai {

struct LocalEngine::RuntimeState {
#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
  llama_model* model = nullptr;
  llama_context* context = nullptr;
#endif
};

LocalEngine::LocalEngine(LocalEngineConfig config) : config_(std::move(config)) {}

LocalEngine::~LocalEngine() {
#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
  if (runtime_state_ != nullptr) {
    if (runtime_state_->context != nullptr) {
      llama_free(runtime_state_->context);
      runtime_state_->context = nullptr;
    }
    if (runtime_state_->model != nullptr) {
      llama_model_free(runtime_state_->model);
      runtime_state_->model = nullptr;
    }
  }
  llama_backend_free();
#endif
}

app::Result<void> LocalEngine::EnsureLoaded() {
#if !defined(MIKUDESK_ENABLE_LLAMA) || !MIKUDESK_ENABLE_LLAMA
  return std::unexpected(app::AppError::kLlamaNotEnabled);
#else
  if (runtime_state_ != nullptr && runtime_state_->model != nullptr &&
      runtime_state_->context != nullptr) {
    return {};
  }

  if (config_.model_path.empty() || !std::filesystem::exists(config_.model_path)) {
    spdlog::warn("LocalEngine GGUF model path is invalid: {}", config_.model_path);
    return std::unexpected(app::AppError::kLlamaLoadFailed);
  }

  if (runtime_state_ == nullptr) {
    runtime_state_ = std::make_unique<RuntimeState>();
  }

  ggml_backend_load_all();
  llama_backend_init();

  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = config_.gpu_layers;
  runtime_state_->model = llama_model_load_from_file(config_.model_path.c_str(), model_params);
  if (runtime_state_->model == nullptr) {
    return std::unexpected(app::AppError::kLlamaLoadFailed);
  }

  llama_context_params context_params = llama_context_default_params();
  context_params.n_ctx =
      static_cast<uint32_t>(std::clamp(config_.context_length, 512, 65536));
  context_params.n_batch = static_cast<uint32_t>(std::clamp(config_.context_length, 32, 4096));
  context_params.n_threads = std::clamp(config_.threads, 1, 128);
  context_params.n_threads_batch = context_params.n_threads;
  runtime_state_->context = llama_init_from_model(runtime_state_->model, context_params);
  if (runtime_state_->context == nullptr) {
    return std::unexpected(app::AppError::kLlamaLoadFailed);
  }

  spdlog::info("LocalEngine loaded GGUF model: {}", config_.model_path);
  return {};
#endif
}

core::Task<app::Result<ChatReply>> LocalEngine::Chat(const ChatRequest& request,
                                                      StreamChunkCallback on_chunk,
                                                      std::stop_token stop_token) {
  auto loaded_result = EnsureLoaded();
  if (!loaded_result.has_value()) {
    co_return std::unexpected(loaded_result.error());
  }

  if (stop_token.stop_requested()) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  std::ostringstream prompt_stream;
  for (const ChatMessage& message : request.messages) {
    switch (message.role) {
      case ChatRole::kSystem:
        prompt_stream << "<|system|>\n";
        break;
      case ChatRole::kAssistant:
        prompt_stream << "<|assistant|>\n";
        break;
      case ChatRole::kUser:
        prompt_stream << "<|user|>\n";
        break;
    }
    prompt_stream << message.content << '\n';
  }
  prompt_stream << "<|assistant|>\n";
  const std::string prompt = prompt_stream.str();

#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
  llama_model* model = runtime_state_->model;
  llama_context* context = runtime_state_->context;
  if (model == nullptr || context == nullptr) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  const llama_vocab* vocab = llama_model_get_vocab(model);
  if (vocab == nullptr) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  const int32_t token_count_estimate = -llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                                        nullptr, 0, true, true);
  if (token_count_estimate <= 0) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  std::vector<llama_token> prompt_tokens(static_cast<std::size_t>(token_count_estimate));
  const int32_t prompt_token_count = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                                    prompt_tokens.data(),
                                                    static_cast<int32_t>(prompt_tokens.size()),
                                                    true, true);
  if (prompt_token_count <= 0) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }
  prompt_tokens.resize(static_cast<std::size_t>(prompt_token_count));

  llama_memory_t memory = llama_get_memory(context);
  if (memory != nullptr) {
    llama_memory_clear(memory, true);
  }

  auto sampler_params = llama_sampler_chain_default_params();
  llama_sampler* sampler = llama_sampler_chain_init(sampler_params);
  if (sampler == nullptr) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  const float clamped_temperature = static_cast<float>(std::clamp(request.temperature, 0.0, 2.0));
  const float clamped_top_p = static_cast<float>(std::clamp(request.top_p, 0.0, 1.0));
  if (clamped_temperature <= 0.0F) {
    llama_sampler_chain_add(sampler, llama_sampler_init_greedy());
  } else {
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(clamped_top_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(clamped_temperature));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
  }

  llama_batch batch = llama_batch_get_one(prompt_tokens.data(),
                                          static_cast<int32_t>(prompt_tokens.size()));
  if (llama_model_has_encoder(model)) {
    if (llama_encode(context, batch) != 0) {
      llama_sampler_free(sampler);
      co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
    }

    llama_token decoder_start_token = llama_model_decoder_start_token(model);
    if (decoder_start_token == LLAMA_TOKEN_NULL) {
      decoder_start_token = llama_vocab_bos(vocab);
    }
    batch = llama_batch_get_one(&decoder_start_token, 1);
  }

  const int32_t n_ctx = static_cast<int32_t>(llama_n_ctx(context));
  const int32_t available_generation_budget =
      std::max<int32_t>(1, n_ctx - static_cast<int32_t>(prompt_tokens.size()) - 1);
  const int32_t max_new_tokens = std::clamp<int32_t>(request.max_tokens, 1,
                                                      available_generation_budget);

  std::string full_reply;
  full_reply.reserve(static_cast<std::size_t>(max_new_tokens) * 4U);
  int32_t generated_tokens = 0;
  int32_t processed_tokens = 0;
  llama_token next_token = LLAMA_TOKEN_NULL;

  while (generated_tokens < max_new_tokens) {
    if (stop_token.stop_requested()) {
      llama_sampler_free(sampler);
      co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
    }

    if (llama_decode(context, batch) != 0) {
      llama_sampler_free(sampler);
      co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
    }
    processed_tokens += batch.n_tokens;

    next_token = llama_sampler_sample(sampler, context, -1);
    if (llama_vocab_is_eog(vocab, next_token)) {
      break;
    }

    char piece_buffer[512];
    const int piece_length =
        llama_token_to_piece(vocab, next_token, piece_buffer, sizeof(piece_buffer), 0, true);
    if (piece_length < 0) {
      llama_sampler_free(sampler);
      co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
    }

    const std::string chunk(piece_buffer, static_cast<std::size_t>(piece_length));
    full_reply += chunk;
    if (on_chunk) {
      on_chunk(chunk);
    }

    batch = llama_batch_get_one(&next_token, 1);
    ++generated_tokens;
  }

  llama_sampler_free(sampler);

  ChatReply reply;
  reply.text = std::move(full_reply);
  reply.finish_reason = generated_tokens >= max_new_tokens ? "length" : "stop";
  spdlog::info("LocalEngine generated {} token(s), processed {} token(s).", generated_tokens,
               processed_tokens);
  co_return reply;
#else
  (void)request;
  (void)on_chunk;
  (void)prompt;
  co_return std::unexpected(app::AppError::kLlamaNotEnabled);
#endif
}

std::string_view LocalEngine::EngineName() const {
  return "local";
}

bool LocalEngine::IsReady() const {
#if !defined(MIKUDESK_ENABLE_LLAMA) || !MIKUDESK_ENABLE_LLAMA
  return false;
#else
  return !config_.model_path.empty() && std::filesystem::exists(config_.model_path);
#endif
}

}  // namespace mikudesk::ai
