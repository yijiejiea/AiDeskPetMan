#pragma once

#include <functional>
#include <string>

#include <QString>

namespace mikudesk::ui {

class ImGuiLayer final {
 public:
  using SubmitHandler = std::function<void(std::string)>;

  explicit ImGuiLayer(SubmitHandler on_submit);
  ~ImGuiLayer();

  ImGuiLayer(const ImGuiLayer&) = delete;
  ImGuiLayer& operator=(const ImGuiLayer&) = delete;

  bool Initialize();
  void Shutdown();
  void BeginFrame(int width, int height, float delta_seconds);
  void Render();

  bool HandleKeyPress(int key, int modifiers, const QString& text);
  void ToggleInput();
  bool IsInputVisible() const;
  bool IsInitialized() const;

 private:
  void DrawInputWindow();
  void SubmitCurrentInput();

  SubmitHandler on_submit_;
  QString input_text_;
  bool initialized_ = false;
  bool input_visible_ = false;
};

}  // namespace mikudesk::ui
