#include "ai/cloud_engine.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "ai/sse_parser.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace mikudesk::ai {

namespace {

std::string ChatRoleToApiRole(ChatRole role) {
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

app::Result<std::string> ParseStreamChunk(std::string_view event_data, std::string* finish_reason) {
  if (event_data == "[DONE]") {
    return std::string();
  }

  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(event_data);
  } catch (...) {
    return std::unexpected(app::AppError::kAiSseParseFailed);
  }

  const auto& choices = json_data.value("choices", nlohmann::json::array());
  if (!choices.is_array() || choices.empty()) {
    return std::string();
  }

  const auto& choice0 = choices.at(0);
  if (finish_reason != nullptr && choice0.contains("finish_reason") &&
      choice0.at("finish_reason").is_string()) {
    *finish_reason = choice0.at("finish_reason").get<std::string>();
  }

  if (!choice0.contains("delta") || !choice0.at("delta").is_object()) {
    return std::string();
  }

  const auto& delta = choice0.at("delta");
  if (!delta.contains("content") || !delta.at("content").is_string()) {
    return std::string();
  }

  return delta.at("content").get<std::string>();
}

app::Result<ChatReply> ParseNonStreamReply(std::string_view response_body) {
  nlohmann::json json_data;
  try {
    json_data = nlohmann::json::parse(response_body);
  } catch (...) {
    return std::unexpected(app::AppError::kAiRequestFailed);
  }

  const auto& choices = json_data.value("choices", nlohmann::json::array());
  if (!choices.is_array() || choices.empty()) {
    return std::unexpected(app::AppError::kAiRequestFailed);
  }

  const auto& choice0 = choices.at(0);
  const auto& message = choice0.value("message", nlohmann::json::object());
  if (!message.is_object() || !message.contains("content") || !message.at("content").is_string()) {
    return std::unexpected(app::AppError::kAiRequestFailed);
  }

  ChatReply reply;
  reply.text = message.at("content").get<std::string>();
  if (choice0.contains("finish_reason") && choice0.at("finish_reason").is_string()) {
    reply.finish_reason = choice0.at("finish_reason").get<std::string>();
  }
  return reply;
}

}  // namespace

CloudEngine::CloudEngine(CloudEngineConfig config) : config_(std::move(config)) {}

core::Task<app::Result<ChatReply>> CloudEngine::Chat(const ChatRequest& request,
                                                      StreamChunkCallback on_chunk,
                                                      std::stop_token stop_token) {
  if (!IsReady()) {
    co_return std::unexpected(app::AppError::kAiEngineNotReady);
  }

  if (stop_token.stop_requested()) {
    co_return std::unexpected(app::AppError::kAiRequestFailed);
  }

  nlohmann::json payload{
      {"model", config_.api_model},
      {"stream", config_.stream && request.stream},
      {"max_tokens", request.max_tokens},
      {"temperature", request.temperature},
      {"top_p", request.top_p},
      {"messages", nlohmann::json::array()},
  };
  for (const ChatMessage& message : request.messages) {
    payload["messages"].push_back({
        {"role", ChatRoleToApiRole(message.role)},
        {"content", message.content},
    });
  }

  QNetworkAccessManager manager;
  QNetworkRequest network_request(QUrl(QString::fromStdString(config_.api_base_url + "/chat/completions")));
  network_request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  network_request.setRawHeader("Authorization",
                               QByteArray::fromStdString("Bearer " + config_.api_key));
  network_request.setTransferTimeout(config_.request_timeout_ms);

  QNetworkReply* reply =
      manager.post(network_request, QByteArray::fromStdString(payload.dump()));
  if (reply == nullptr) {
    co_return std::unexpected(app::AppError::kAiRequestFailed);
  }

  QEventLoop loop;
  QTimer cancel_timer;
  cancel_timer.setInterval(50);
  QObject::connect(&cancel_timer, &QTimer::timeout, [&]() {
    if (!stop_token.stop_requested()) {
      return;
    }
    if (reply->isRunning()) {
      reply->abort();
    }
  });

  std::string full_text;
  std::string finish_reason;
  std::string raw_response;
  bool parse_failed = false;
  SseParser parser;
  const bool stream_enabled = config_.stream && request.stream;

  QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
    const QByteArray chunk = reply->readAll();
    if (chunk.isEmpty()) {
      return;
    }

    if (!stream_enabled) {
      raw_response.append(chunk.constData(), static_cast<std::size_t>(chunk.size()));
      return;
    }

    auto events = parser.Feed(std::string_view(chunk.constData(), static_cast<std::size_t>(chunk.size())));
    for (const std::string& event_data : events) {
      auto parsed_chunk = ParseStreamChunk(event_data, &finish_reason);
      if (!parsed_chunk.has_value()) {
        parse_failed = true;
        return;
      }
      if (parsed_chunk->empty()) {
        continue;
      }
      full_text += *parsed_chunk;
      if (on_chunk) {
        on_chunk(*parsed_chunk);
      }
    }
  });

  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  cancel_timer.start();
  loop.exec();
  cancel_timer.stop();

  const QNetworkReply::NetworkError reply_error = reply->error();
  const std::string error_message = reply->errorString().toStdString();
  reply->deleteLater();

  if (stop_token.stop_requested()) {
    co_return std::unexpected(app::AppError::kAiRequestFailed);
  }

  if (reply_error != QNetworkReply::NoError) {
    spdlog::warn("CloudEngine request failed: {}", error_message);
    co_return std::unexpected(app::AppError::kAiRequestFailed);
  }

  if (parse_failed) {
    co_return std::unexpected(app::AppError::kAiSseParseFailed);
  }

  if (!stream_enabled) {
    co_return ParseNonStreamReply(raw_response);
  }

  ChatReply reply_data;
  reply_data.text = std::move(full_text);
  reply_data.finish_reason = std::move(finish_reason);
  co_return reply_data;
}

std::string_view CloudEngine::EngineName() const {
  return "cloud";
}

bool CloudEngine::IsReady() const {
  return !config_.api_base_url.empty() && !config_.api_model.empty() && !config_.api_key.empty();
}

}  // namespace mikudesk::ai

