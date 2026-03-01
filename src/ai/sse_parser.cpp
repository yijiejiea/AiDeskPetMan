#include "ai/sse_parser.hpp"

#include <string>
#include <vector>

namespace mikudesk::ai {

std::vector<std::string> SseParser::Feed(std::string_view chunk) {
  std::vector<std::string> events;
  if (chunk.empty()) {
    return events;
  }

  line_buffer_.append(chunk.data(), chunk.size());

  std::size_t line_end = line_buffer_.find('\n');
  while (line_end != std::string::npos) {
    std::string line = line_buffer_.substr(0, line_end);
    line_buffer_.erase(0, line_end + 1);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty()) {
      if (!event_data_buffer_.empty()) {
        events.push_back(event_data_buffer_);
        event_data_buffer_.clear();
      }
      line_end = line_buffer_.find('\n');
      continue;
    }

    if (!line.starts_with("data:")) {
      line_end = line_buffer_.find('\n');
      continue;
    }

    std::string data = line.substr(5);
    if (!data.empty() && data.front() == ' ') {
      data.erase(data.begin());
    }

    if (!event_data_buffer_.empty()) {
      event_data_buffer_.push_back('\n');
    }
    event_data_buffer_.append(data);

    line_end = line_buffer_.find('\n');
  }

  return events;
}

void SseParser::Reset() {
  line_buffer_.clear();
  event_data_buffer_.clear();
}

}  // namespace mikudesk::ai

