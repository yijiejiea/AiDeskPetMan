#pragma once

#include <coroutine>
#include <exception>
#include <utility>

namespace mikudesk::core {

template <typename T>
class Task;

template <>
class Task<void> {
 public:
  struct promise_type {
    using Handle = std::coroutine_handle<promise_type>;

    Task get_return_object() {
      return Task{Handle::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept {
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {
      exception_ = std::current_exception();
    }

    std::exception_ptr exception_;
  };

  using Handle = std::coroutine_handle<promise_type>;

  Task() = default;

  explicit Task(Handle handle) : handle_(handle) {}

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  Task& operator=(Task&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Destroy();
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }

  ~Task() {
    Destroy();
  }

  bool IsDone() const {
    return !handle_ || handle_.done();
  }

  void Resume() {
    if (!handle_ || handle_.done()) {
      return;
    }
    handle_.resume();
    if (handle_.promise().exception_ != nullptr) {
      std::rethrow_exception(handle_.promise().exception_);
    }
  }

 private:
  void Destroy() {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

  Handle handle_;
};

}  // namespace mikudesk::core
