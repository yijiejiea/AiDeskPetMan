#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mikudesk::ai {

class SseParser {
 public:
  std::vector<std::string> Feed(std::string_view chunk);
  void Reset();

 private:
  std::string line_buffer_;
  std::string event_data_buffer_;
};

}  // namespace mikudesk::ai

