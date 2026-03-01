#include <iostream>
#include <string>
#include <vector>

#include "ai/sse_parser.hpp"

namespace {

bool Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using mikudesk::ai::SseParser;

  SseParser parser;

  {
    const auto events = parser.Feed("data: hello\n\n");
    if (!Expect(events.size() == 1, "single event count should be 1")) {
      return 1;
    }
    if (!Expect(events[0] == "hello", "single event payload mismatch")) {
      return 1;
    }
  }

  {
    const auto partial_events = parser.Feed("data: split");
    if (!Expect(partial_events.empty(), "partial chunk should not emit event")) {
      return 1;
    }

    const auto completed_events = parser.Feed("_event\n\n");
    if (!Expect(completed_events.size() == 1, "completed split event count should be 1")) {
      return 1;
    }
    if (!Expect(completed_events[0] == "split_event", "split event payload mismatch")) {
      return 1;
    }
  }

  {
    const auto events = parser.Feed("data: line1\ndata: line2\n\n");
    if (!Expect(events.size() == 1, "multi data event count should be 1")) {
      return 1;
    }
    if (!Expect(events[0] == "line1\nline2", "multi data event payload mismatch")) {
      return 1;
    }
  }

  {
    const auto ignored = parser.Feed("event: ping\nid: 1\n\n");
    if (!Expect(ignored.empty(), "non-data lines should be ignored")) {
      return 1;
    }
  }

  {
    parser.Reset();
    const auto after_reset = parser.Feed("data: reset_ok\n\n");
    if (!Expect(after_reset.size() == 1, "reset should clear parser state")) {
      return 1;
    }
    if (!Expect(after_reset[0] == "reset_ok", "reset event payload mismatch")) {
      return 1;
    }
  }

  return 0;
}
