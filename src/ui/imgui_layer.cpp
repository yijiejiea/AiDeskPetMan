#include "ui/imgui_layer.hpp"

#include <algorithm>
#include <utility>

#include <Qt>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "spdlog/spdlog.h"

namespace mikudesk::ui {

namespace {

constexpr float kInputWindowWidthPx = 440.0F;
constexpr float kInputWindowBottomMarginPx = 20.0F;

bool IsControlKey(int key) {
  return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
         key == Qt::Key_Meta;
}

}  // namespace

ImGuiLayer::ImGuiLayer(SubmitHandler on_submit) : on_submit_(std::move(on_submit)) {}

ImGuiLayer::~ImGuiLayer() {
  Shutdown();
}

bool ImGuiLayer::Initialize() {
  if (initialized_) {
    return true;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;

  ImGui::StyleColorsDark();

  if (!ImGui_ImplOpenGL3_Init("#version 130")) {
    spdlog::error("ImGui OpenGL backend initialization failed.");
    ImGui::DestroyContext();
    return false;
  }

  initialized_ = true;
  return true;
}

void ImGuiLayer::Shutdown() {
  if (!initialized_) {
    return;
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui::DestroyContext();
  initialized_ = false;
}

void ImGuiLayer::BeginFrame(int width, int height, float delta_seconds) {
  if (!initialized_) {
    return;
  }

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(std::max(width, 1)),
                          static_cast<float>(std::max(height, 1)));
  io.DeltaTime = std::max(delta_seconds, 1.0F / 120.0F);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui::NewFrame();
  DrawInputWindow();
  ImGui::Render();
}

void ImGuiLayer::Render() {
  if (!initialized_) {
    return;
  }
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::HandleKeyPress(int key, int modifiers, const QString& text) {
  const bool ctrl_pressed = (modifiers & Qt::ControlModifier) != 0;
  const bool shift_pressed = (modifiers & Qt::ShiftModifier) != 0;

  if (ctrl_pressed && key == Qt::Key_Return) {
    ToggleInput();
    return true;
  }
  if (ctrl_pressed && key == Qt::Key_Enter) {
    ToggleInput();
    return true;
  }

  if (!input_visible_) {
    return false;
  }

  if (key == Qt::Key_Escape) {
    input_visible_ = false;
    return true;
  }

  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    if (shift_pressed) {
      input_text_.append('\n');
    } else {
      SubmitCurrentInput();
    }
    return true;
  }

  if (key == Qt::Key_Backspace) {
    if (!input_text_.isEmpty()) {
      input_text_.chop(1);
    }
    return true;
  }

  if (!text.isEmpty() && !IsControlKey(key) && !ctrl_pressed) {
    input_text_.append(text);
    return true;
  }

  return true;
}

void ImGuiLayer::ToggleInput() {
  input_visible_ = !input_visible_;
  if (!input_visible_) {
    input_text_.clear();
  }
}

bool ImGuiLayer::IsInputVisible() const {
  return input_visible_;
}

bool ImGuiLayer::IsInitialized() const {
  return initialized_;
}

void ImGuiLayer::DrawInputWindow() {
  if (!input_visible_) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(kInputWindowWidthPx, 220.0F), ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      ImVec2(ImGui::GetIO().DisplaySize.x * 0.5F,
             ImGui::GetIO().DisplaySize.y - kInputWindowBottomMarginPx),
      ImGuiCond_Always, ImVec2(0.5F, 1.0F));
  ImGui::SetNextWindowBgAlpha(0.78F);

  constexpr ImGuiWindowFlags kWindowFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

  if (!ImGui::Begin("MikuDeskChatInput", nullptr, kWindowFlags)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Chat");
  ImGui::Separator();

  const QByteArray utf8 = input_text_.toUtf8();
  ImGui::BeginChild("InputPreview", ImVec2(0.0F, 130.0F), true,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);
  if (utf8.isEmpty()) {
    ImGui::TextDisabled("Press Enter to send, Shift+Enter for newline, Esc to close.");
  } else {
    ImGui::TextUnformatted(utf8.constData());
  }
  ImGui::EndChild();

  ImGui::TextDisabled("Ctrl+Enter: toggle input panel");
  ImGui::End();
}

void ImGuiLayer::SubmitCurrentInput() {
  const QString trimmed = input_text_.trimmed();
  if (!trimmed.isEmpty() && on_submit_) {
    on_submit_(trimmed.toStdString());
  }
  input_text_.clear();
  input_visible_ = false;
}

}  // namespace mikudesk::ui
