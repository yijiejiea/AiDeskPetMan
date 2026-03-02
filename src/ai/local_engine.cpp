#include "ai/local_engine.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "spdlog/spdlog.h"

#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
#include "llama.h"
#endif

namespace mikudesk::ai {

namespace {

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool HasSupportedModelExtension(const std::filesystem::path& file_path) {
  const std::string extension = ToLowerAscii(file_path.extension().string());
  return extension == ".gguf" || extension == ".bin";
}

std::optional<std::filesystem::path> ResolveModelFilePath(std::string_view configured_path) {
  if (configured_path.empty()) {
    return std::nullopt;
  }

  const std::filesystem::path input_path(configured_path);
  std::error_code error_code;
  if (std::filesystem::exists(input_path, error_code) &&
      std::filesystem::is_regular_file(input_path, error_code)) {
    return input_path;
  }

  if (!std::filesystem::exists(input_path, error_code) ||
      !std::filesystem::is_directory(input_path, error_code)) {
    return std::nullopt;
  }

  std::vector<std::filesystem::path> candidates;
  std::filesystem::recursive_directory_iterator iterator(
      input_path, std::filesystem::directory_options::skip_permission_denied, error_code);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error_code && iterator != end; iterator.increment(error_code)) {
    if (!iterator->is_regular_file(error_code)) {
      continue;
    }
    const std::filesystem::path candidate_path = iterator->path();
    if (!HasSupportedModelExtension(candidate_path)) {
      continue;
    }
    candidates.push_back(candidate_path);
  }

  if (candidates.empty()) {
    return std::nullopt;
  }
  std::sort(candidates.begin(), candidates.end());
  return candidates.front();
}

const char* ToLlamaChatRole(ChatRole role) {
  switch (role) {
    case ChatRole::kSystem:
      return "system";
    case ChatRole::kAssistant:
      return "assistant";
    case ChatRole::kUser:
      return "user";
  }
  return "user";
}

std::string BuildFallbackPrompt(const std::vector<ChatMessage>& messages) {
  std::ostringstream prompt_stream;
  for (const ChatMessage& message : messages) {
    prompt_stream << "<|im_start|>" << ToLlamaChatRole(message.role) << '\n';
    prompt_stream << message.content << '\n';
    prompt_stream << "<|im_end|>\n";
  }
  prompt_stream << "<|im_start|>assistant\n";
  return prompt_stream.str();
}

#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
app::Result<std::string> BuildPromptFromChatTemplate(
    const llama_model* model, const std::vector<ChatMessage>& messages) {
  if (model == nullptr) {
    return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  const char* chat_template = llama_model_chat_template(model, nullptr);
  if (chat_template == nullptr || std::strlen(chat_template) == 0) {
    return BuildFallbackPrompt(messages);
  }

  std::vector<std::string> roles;
  std::vector<std::string> contents;
  std::vector<llama_chat_message> llama_messages;
  roles.reserve(messages.size());
  contents.reserve(messages.size());
  llama_messages.reserve(messages.size());
  for (const ChatMessage& message : messages) {
    roles.emplace_back(ToLlamaChatRole(message.role));
    contents.emplace_back(message.content);
    llama_messages.push_back({roles.back().c_str(), contents.back().c_str()});
  }

  std::vector<char> prompt_buffer(4096);
  int32_t prompt_size = llama_chat_apply_template(chat_template, llama_messages.data(),
                                                  llama_messages.size(), true,
                                                  prompt_buffer.data(),
                                                  static_cast<int32_t>(prompt_buffer.size()));
  if (prompt_size < 0) {
    spdlog::warn("llama_chat_apply_template failed, fallback formatter will be used.");
    return BuildFallbackPrompt(messages);
  }

  if (prompt_size >= static_cast<int32_t>(prompt_buffer.size())) {
    prompt_buffer.resize(static_cast<std::size_t>(prompt_size) + 1U);
    prompt_size = llama_chat_apply_template(chat_template, llama_messages.data(),
                                            llama_messages.size(), true,
                                            prompt_buffer.data(),
                                            static_cast<int32_t>(prompt_buffer.size()));
    if (prompt_size < 0 || prompt_size >= static_cast<int32_t>(prompt_buffer.size())) {
      return std::unexpected(app::AppError::kLlamaInferenceFailed);
    }
  }

  return std::string(prompt_buffer.data(), static_cast<std::size_t>(prompt_size));
}
#endif

void TrimAsciiWhitespace(std::string& text) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
}

std::size_t FindFirstRoleMarker(std::string_view text) {
  static constexpr std::array<std::string_view, 5> kStopMarkers = {
      "<|im_start|>", "<|assistant|>", "<|user|>", "<|system|>", "<assistant>"};

  std::size_t first_marker_pos = std::string::npos;
  for (const std::string_view marker : kStopMarkers) {
    const std::size_t marker_pos = text.find(marker);
    if (marker_pos == std::string::npos) {
      continue;
    }
    first_marker_pos = std::min(first_marker_pos, marker_pos);
  }
  return first_marker_pos;
}

void StripRoleMarkers(std::string& text) {
  const std::size_t marker_pos = FindFirstRoleMarker(text);
  if (marker_pos != std::string::npos) {
    text.erase(marker_pos);
  }
  TrimAsciiWhitespace(text);
}

}  // namespace

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

  const auto resolved_model_path = ResolveModelFilePath(config_.model_path);
  if (!resolved_model_path.has_value()) {
    spdlog::warn("LocalEngine model path is invalid or no supported model found: {}",
                 config_.model_path);
    return std::unexpected(app::AppError::kLlamaLoadFailed);
  }
  const std::string resolved_model_path_string = resolved_model_path->string();

  if (runtime_state_ == nullptr) {
    runtime_state_ = std::make_unique<RuntimeState>();
  }

  ggml_backend_load_all();
  llama_backend_init();

  const bool supports_gpu_offload = llama_supports_gpu_offload();
  const std::size_t backend_count = ggml_backend_reg_count();
  const std::size_t device_count = ggml_backend_dev_count();
  std::size_t gpu_device_count = 0;
  for (std::size_t index = 0; index < device_count; ++index) {
    ggml_backend_dev_t device = ggml_backend_dev_get(index);
    if (device == nullptr) {
      continue;
    }
    const auto device_type = ggml_backend_dev_type(device);
    if (device_type == GGML_BACKEND_DEVICE_TYPE_GPU ||
        device_type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
      ++gpu_device_count;
    }
  }
  spdlog::info(
      "LocalEngine backend probe: supports_gpu_offload={}, backend_count={}, device_count={}, "
      "gpu_device_count={}, requested_gpu_layers={}",
      supports_gpu_offload, backend_count, device_count, gpu_device_count, config_.gpu_layers);
  if (config_.gpu_layers > 0 && (!supports_gpu_offload || gpu_device_count == 0)) {
    spdlog::warn(
        "LocalEngine requested GPU offload (local_gpu_layers={}) but no available GPU backend "
        "device was detected. Inference will run on CPU.",
        config_.gpu_layers);
  } else if (config_.gpu_layers <= 0) {
    spdlog::info("LocalEngine local_gpu_layers={} -> CPU-only inference.", config_.gpu_layers);
  }

  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = config_.gpu_layers;
  runtime_state_->model =
      llama_model_load_from_file(resolved_model_path_string.c_str(), model_params);
  if (runtime_state_->model == nullptr) {
    return std::unexpected(app::AppError::kLlamaLoadFailed);
  }

  llama_context_params context_params = llama_context_default_params();
  const bool use_gpu_offload =
      supports_gpu_offload && gpu_device_count > 0 && config_.gpu_layers > 0;
  const int clamped_threads = std::clamp(config_.threads, 1, 128);
  context_params.n_ctx =
      static_cast<uint32_t>(std::clamp(config_.context_length, 512, 65536));
  // Avoid oversized logical batches that can increase CPU-side scheduling overhead.
  const int max_batch = use_gpu_offload ? 512 : 1024;
  context_params.n_batch =
      static_cast<uint32_t>(std::clamp(config_.context_length, 32, max_batch));
  context_params.n_threads = clamped_threads;
  // When offloading layers to GPU, reduce batch worker threads to avoid pegging CPU at 90%+.
  context_params.n_threads_batch =
      use_gpu_offload ? std::clamp(clamped_threads / 2, 1, 4) : clamped_threads;
  // Explicitly bias llama.cpp toward GPU execution paths when GPU backend is available.
  context_params.offload_kqv = true;
  context_params.op_offload = true;
  context_params.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
  runtime_state_->context = llama_init_from_model(runtime_state_->model, context_params);
  if (runtime_state_->context == nullptr) {
    return std::unexpected(app::AppError::kLlamaLoadFailed);
  }

  spdlog::info(
      "LocalEngine context config: n_ctx={}, n_batch={}, n_threads={}, n_threads_batch={}, "
      "use_gpu_offload={}, offload_kqv={}, op_offload={}, flash_attn={}",
      context_params.n_ctx, context_params.n_batch, context_params.n_threads,
      context_params.n_threads_batch, use_gpu_offload,
      context_params.offload_kqv, context_params.op_offload,
      static_cast<int>(context_params.flash_attn_type));

  spdlog::info("LocalEngine loaded model: {} (configured path: {})", resolved_model_path_string,
               config_.model_path);
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

#if defined(MIKUDESK_ENABLE_LLAMA) && MIKUDESK_ENABLE_LLAMA
  llama_model* model = runtime_state_->model;
  llama_context* context = runtime_state_->context;
  if (model == nullptr || context == nullptr) {
    co_return std::unexpected(app::AppError::kLlamaInferenceFailed);
  }

  auto prompt_result = BuildPromptFromChatTemplate(model, request.messages);
  if (!prompt_result.has_value()) {
    co_return std::unexpected(prompt_result.error());
  }
  const std::string prompt = std::move(*prompt_result);

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
    llama_sampler_chain_add(sampler, llama_sampler_init_penalties(128, 1.10F, 0.0F, 0.0F));
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

    const std::size_t reply_size_before_append = full_reply.size();
    full_reply.append(piece_buffer, static_cast<std::size_t>(piece_length));

    const std::size_t marker_pos = FindFirstRoleMarker(full_reply);
    if (marker_pos != std::string::npos) {
      if (on_chunk && marker_pos > reply_size_before_append) {
        const std::string delta =
            full_reply.substr(reply_size_before_append, marker_pos - reply_size_before_append);
        on_chunk(delta);
      }
      full_reply.erase(marker_pos);
      break;
    }

    if (on_chunk) {
      on_chunk(std::string_view(piece_buffer, static_cast<std::size_t>(piece_length)));
    }

    batch = llama_batch_get_one(&next_token, 1);
    ++generated_tokens;
  }

  llama_sampler_free(sampler);

  ChatReply reply;
  StripRoleMarkers(full_reply);
  reply.text = std::move(full_reply);
  reply.finish_reason = generated_tokens >= max_new_tokens ? "length" : "stop";
  spdlog::info("LocalEngine generated {} token(s), processed {} token(s).", generated_tokens,
               processed_tokens);
  co_return reply;
#else
  (void)request;
  (void)on_chunk;
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
  return ResolveModelFilePath(config_.model_path).has_value();
#endif
}

}  // namespace mikudesk::ai
