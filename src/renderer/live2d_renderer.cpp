#include "renderer/live2d_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "renderer/cubism_skin_loader.hpp"
#include "spdlog/spdlog.h"

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <GL/gl.h>

#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QString>

#include "CubismDefaultParameterId.hpp"
#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "ICubismAllocator.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Model/CubismUserModel.hpp"
#include "Motion/CubismMotion.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"
#endif

namespace mikudesk::renderer {

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D

namespace Csm = Live2D::Cubism::Framework;
namespace CsmDefaultParameterId = Live2D::Cubism::Framework::DefaultParameterId;

namespace {

constexpr int kDefaultMotionPriority = 1;
constexpr int kMinModelDimensionPx = 64;

struct RenderViewport {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
};

class CubismAllocator final : public Csm::ICubismAllocator {
 public:
  void* Allocate(const Csm::csmSizeType size) override {
    return std::malloc(size);
  }

  void Deallocate(void* memory) override {
    std::free(memory);
  }

  void* AllocateAligned(const Csm::csmSizeType size,
                        const Csm::csmUint32 alignment) override {
    const std::size_t offset = static_cast<std::size_t>(alignment) - 1U + sizeof(void*);
    void* allocation = Allocate(size + static_cast<Csm::csmSizeType>(offset));
    if (allocation == nullptr) {
      return nullptr;
    }

    std::size_t aligned_address = reinterpret_cast<std::size_t>(allocation) + sizeof(void*);
    const std::size_t shift = aligned_address % alignment;
    if (shift != 0U) {
      aligned_address += (alignment - shift);
    }

    auto** preamble = reinterpret_cast<void**>(aligned_address);
    preamble[-1] = allocation;
    return reinterpret_cast<void*>(aligned_address);
  }

  void DeallocateAligned(void* aligned_memory) override {
    if (aligned_memory == nullptr) {
      return;
    }
    auto** preamble = static_cast<void**>(aligned_memory);
    Deallocate(preamble[-1]);
  }
};

CubismAllocator g_cubism_allocator;
Csm::CubismFramework::Option g_cubism_option{};
bool g_cubism_option_initialized = false;

void CubismLogFunction(const Csm::csmChar* message) {
  if (message == nullptr) {
    return;
  }
  spdlog::debug("[Cubism] {}", message);
}

std::filesystem::path GetExecutableDirectory() {
#if defined(_WIN32)
  std::vector<wchar_t> module_path(MAX_PATH, L'\0');
  DWORD length = GetModuleFileNameW(nullptr, module_path.data(),
                                    static_cast<DWORD>(module_path.size()));
  if (length == 0U) {
    return {};
  }
  std::filesystem::path executable_path(std::wstring(module_path.data(), length));
  return executable_path.parent_path();
#else
  return {};
#endif
}

std::optional<std::filesystem::path> ResolveReadablePath(const std::filesystem::path& file_path) {
  if (std::filesystem::exists(file_path)) {
    return file_path;
  }

  if (!file_path.is_relative()) {
    return std::nullopt;
  }

  const auto executable_directory = GetExecutableDirectory();
  if (!executable_directory.empty()) {
    const auto executable_relative_path = executable_directory / file_path;
    if (std::filesystem::exists(executable_relative_path)) {
      return executable_relative_path;
    }
  }

  const auto working_directory_relative_path = std::filesystem::current_path() / file_path;
  if (std::filesystem::exists(working_directory_relative_path)) {
    return working_directory_relative_path;
  }

  return std::nullopt;
}

Csm::csmByte* LoadFileBytesForCubism(const std::string file_path,
                                     Csm::csmSizeInt* out_size) {
  if (out_size == nullptr) {
    return nullptr;
  }

  auto resolved_path = ResolveReadablePath(std::filesystem::path(file_path));
  if (!resolved_path.has_value()) {
    return nullptr;
  }

  std::ifstream input(*resolved_path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    return nullptr;
  }

  const auto file_size = static_cast<std::size_t>(input.tellg());
  input.seekg(0, std::ios::beg);

  auto* data = new Csm::csmByte[file_size];
  input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(file_size));
  if (!input.good() && !input.eof()) {
    delete[] data;
    return nullptr;
  }

  *out_size = static_cast<Csm::csmSizeInt>(file_size);
  return data;
}

void ReleaseCubismBytes(Csm::csmByte* byte_data) {
  delete[] byte_data;
}

app::Result<void> EnsureCubismFrameworkInitialized() {
  if (!g_cubism_option_initialized) {
    g_cubism_option.LogFunction = CubismLogFunction;
    g_cubism_option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Info;
    g_cubism_option.LoadFileFunction = LoadFileBytesForCubism;
    g_cubism_option.ReleaseBytesFunction = ReleaseCubismBytes;
    g_cubism_option_initialized = true;
  }

  if (!Csm::CubismFramework::IsStarted()) {
    const bool started = Csm::CubismFramework::StartUp(&g_cubism_allocator, &g_cubism_option);
    if (!started) {
      return std::unexpected(app::AppError::kLive2dSdkInvalid);
    }
  }

  if (!Csm::CubismFramework::IsInitialized()) {
    Csm::CubismFramework::Initialize();
  }

  if (!Csm::CubismFramework::IsInitialized()) {
    return std::unexpected(app::AppError::kLive2dSdkInvalid);
  }

  return {};
}

std::optional<std::vector<Csm::csmByte>> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    return std::nullopt;
  }

  const auto file_size = static_cast<std::size_t>(input.tellg());
  input.seekg(0, std::ios::beg);

  std::vector<Csm::csmByte> data(file_size);
  if (file_size > 0U) {
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size));
    if (!input.good() && !input.eof()) {
      return std::nullopt;
    }
  }

  return data;
}

std::string BuildMotionKey(std::string_view group, int index) {
  return std::string(group) + ":" + std::to_string(index);
}

std::optional<std::string> PickRandomItem(const std::vector<std::string>& items,
                                          std::mt19937* random_engine) {
  if (items.empty() || random_engine == nullptr) {
    return std::nullopt;
  }

  std::uniform_int_distribution<std::size_t> distribution(0, items.size() - 1U);
  return items[distribution(*random_engine)];
}

std::optional<int> PickRandomMotionIndex(const std::vector<int>& motion_indices,
                                         std::mt19937* random_engine) {
  if (motion_indices.empty() || random_engine == nullptr) {
    return std::nullopt;
  }

  std::uniform_int_distribution<std::size_t> distribution(0, motion_indices.size() - 1U);
  return motion_indices[distribution(*random_engine)];
}

std::optional<std::pair<std::string, int>> PickRandomClickMotion(
    const std::vector<std::string>& motion_groups, const CubismSkinLoader* loader,
    std::mt19937* random_engine) {
  if (motion_groups.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> candidate_groups;
  candidate_groups.reserve(4);

  constexpr std::array<std::string_view, 4> kPreferredGroups = {
      "Tap", "tap", "Flick", "flick"};
  for (std::string_view preferred_group : kPreferredGroups) {
    const auto it = std::find(motion_groups.begin(), motion_groups.end(), preferred_group);
    if (it != motion_groups.end()) {
      candidate_groups.push_back(*it);
    }
  }

  if (candidate_groups.empty()) {
    candidate_groups = motion_groups;
  }

  auto group_name = PickRandomItem(candidate_groups, random_engine);
  if (!group_name.has_value()) {
    return std::nullopt;
  }

  int motion_index = 0;
  if (loader != nullptr) {
    auto motion_indices = loader->ListMotionIndices(*group_name);
    auto picked_index = PickRandomMotionIndex(motion_indices, random_engine);
    if (picked_index.has_value()) {
      motion_index = *picked_index;
    }
  }

  return std::make_pair(*group_name, motion_index);
}

RenderViewport BuildModelViewport(int viewport_width, int viewport_height, int model_width_px,
                                  int model_height_px) {
  RenderViewport render_viewport;
  render_viewport.width =
      std::clamp(model_width_px, kMinModelDimensionPx, std::max(viewport_width, 1));
  render_viewport.height =
      std::clamp(model_height_px, kMinModelDimensionPx, std::max(viewport_height, 1));
  render_viewport.x = (viewport_width - render_viewport.width) / 2;
  render_viewport.y = (viewport_height - render_viewport.height) / 2;
  return render_viewport;
}

class RuntimeCubismModel final : public Csm::CubismUserModel {
 public:
  RuntimeCubismModel() = default;

  ~RuntimeCubismModel() override {
    ReleaseGpuResources();
    ReleaseMotions();
    ReleaseExpressions();
  }

  app::Result<void> Load(const std::filesystem::path& model_directory,
                         const std::filesystem::path& model_json_path) {
    model_directory_ = model_directory;
    model_json_path_ = model_json_path;

    auto model_json_bytes = ReadFileBytes(model_json_path_);
    if (!model_json_bytes.has_value()) {
      return std::unexpected(app::AppError::kIoReadFailed);
    }

    model_setting_ =
        std::make_unique<Csm::CubismModelSettingJson>(model_json_bytes->data(),
                                                      static_cast<Csm::csmSizeInt>(model_json_bytes->size()));
    if (model_setting_ == nullptr) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    auto load_model_result = LoadModelFile();
    if (!load_model_result.has_value()) {
      return load_model_result;
    }

    InitializeParameterIds();
    CollectEffectParameterIds();

    auto load_subresources_result = LoadSubResources();
    if (!load_subresources_result.has_value()) {
      return load_subresources_result;
    }

    ApplyLayout();

    if (GetModel() != nullptr) {
      GetModel()->SaveParameters();
    }

    auto preload_motion_result = PreloadMotions();
    if (!preload_motion_result.has_value()) {
      return preload_motion_result;
    }

    if (_motionManager != nullptr) {
      _motionManager->StopAllMotions();
    }

    return {};
  }

  app::Result<void> EnsureGraphicsResources() {
    if (graphics_ready_) {
      return {};
    }

    if (GetModel() == nullptr || model_setting_ == nullptr) {
      return std::unexpected(app::AppError::kLive2dRenderFailed);
    }

    CreateRenderer();
    auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (renderer == nullptr) {
      return std::unexpected(app::AppError::kLive2dRenderFailed);
    }

    for (int texture_index = 0; texture_index < model_setting_->GetTextureCount(); ++texture_index) {
      const char* texture_name = model_setting_->GetTextureFileName(texture_index);
      if (texture_name == nullptr || std::string_view(texture_name).empty()) {
        continue;
      }

      const std::filesystem::path texture_path = model_directory_ / texture_name;
      QImage texture_image(QString::fromStdWString(texture_path.wstring()));
      if (texture_image.isNull()) {
        spdlog::error("Failed to load Live2D texture: {}", texture_path.string());
        return std::unexpected(app::AppError::kLive2dLoadFailed);
      }

      QImage rgba_image = texture_image.convertToFormat(QImage::Format_RGBA8888);

      GLuint texture_id = 0;
      glGenTextures(1, &texture_id);
      if (texture_id == 0U) {
        return std::unexpected(app::AppError::kLive2dRenderFailed);
      }

      glBindTexture(GL_TEXTURE_2D, texture_id);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba_image.width(), rgba_image.height(), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, rgba_image.constBits());
      glBindTexture(GL_TEXTURE_2D, 0);

      renderer->BindTexture(static_cast<Csm::csmUint32>(texture_index), texture_id);
      texture_ids_.push_back(texture_id);
    }

    renderer->IsPremultipliedAlpha(false);
    graphics_ready_ = true;
    return {};
  }

  void Update(double delta_seconds, float eye_ball_x, float eye_ball_y, float angle_x,
              float angle_y) {
    auto* model = GetModel();
    if (model == nullptr) {
      return;
    }

    const Csm::csmFloat32 delta =
        static_cast<Csm::csmFloat32>(std::max(delta_seconds, 0.0));

    model->LoadParameters();

    Csm::csmBool motion_updated = false;
    if (_motionManager != nullptr) {
      motion_updated = _motionManager->UpdateMotion(model, delta);
    }

    model->SaveParameters();

    if (!motion_updated && _eyeBlink != nullptr) {
      _eyeBlink->UpdateParameters(model, delta);
    }

    if (_expressionManager != nullptr) {
      _expressionManager->UpdateMotion(model, delta);
    }

    if (param_eye_ball_x_ != nullptr) {
      model->AddParameterValue(param_eye_ball_x_, eye_ball_x);
    }
    if (param_eye_ball_y_ != nullptr) {
      model->AddParameterValue(param_eye_ball_y_, eye_ball_y);
    }
    if (param_angle_x_ != nullptr) {
      model->AddParameterValue(param_angle_x_, angle_x);
    }
    if (param_angle_y_ != nullptr) {
      model->AddParameterValue(param_angle_y_, angle_y);
    }
    if (param_angle_z_ != nullptr) {
      model->AddParameterValue(param_angle_z_, -eye_ball_x * eye_ball_y * 30.0F);
    }
    if (param_body_angle_x_ != nullptr) {
      model->AddParameterValue(param_body_angle_x_, eye_ball_x * 10.0F);
    }

    if (_physics != nullptr) {
      _physics->Evaluate(model, delta);
    }

    if (_pose != nullptr) {
      _pose->UpdateParameters(model, delta);
    }

    model->Update();
    MaybeLogTrackingState(model, eye_ball_x, eye_ball_y, angle_x, angle_y);
  }

  app::Result<void> Render(int viewport_width, int viewport_height) {
    auto setup_result = EnsureGraphicsResources();
    if (!setup_result.has_value()) {
      return setup_result;
    }

    auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    if (renderer == nullptr || GetModel() == nullptr) {
      return std::unexpected(app::AppError::kLive2dRenderFailed);
    }

    Csm::CubismMatrix44 projection;
    projection.LoadIdentity();

    if (viewport_width > 0 && viewport_height > 0) {
      if (viewport_width > viewport_height) {
        projection.Scale(static_cast<float>(viewport_height) / static_cast<float>(viewport_width),
                         1.0F);
      } else {
        projection.Scale(1.0F,
                         static_cast<float>(viewport_width) / static_cast<float>(viewport_height));
      }
    }

    if (GetModelMatrix() != nullptr) {
      projection.MultiplyByMatrix(GetModelMatrix());
    }

    renderer->SetMvpMatrix(&projection);
    renderer->DrawModel();

    return {};
  }

  app::Result<void> SetExpression(std::string_view expression_name) {
    if (_expressionManager == nullptr) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    const auto it = expressions_.find(std::string(expression_name));
    if (it == expressions_.end()) {
      return std::unexpected(app::AppError::kInvalidArgument);
    }

    _expressionManager->StartMotion(it->second, false);
    return {};
  }

  app::Result<void> StartMotion(std::string_view group_name, int motion_index) {
    if (_motionManager == nullptr) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    const std::string key = BuildMotionKey(group_name, motion_index);
    const auto it = motions_.find(key);
    if (it == motions_.end()) {
      return std::unexpected(app::AppError::kInvalidArgument);
    }

    _motionManager->StartMotionPriority(it->second, false, kDefaultMotionPriority);
    return {};
  }

 private:
  bool ShouldEmitTrackingLog(float eye_ball_x, float eye_ball_y) {
    const bool has_pointer_input =
        std::abs(eye_ball_x) > 0.03F || std::abs(eye_ball_y) > 0.03F;
    if (!has_pointer_input) {
      return false;
    }

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kTrackingLogInterval = std::chrono::milliseconds(400);

    if (!tracking_log_initialized_) {
      tracking_log_initialized_ = true;
      last_tracking_log_time_ = now;
      return true;
    }

    if (now - last_tracking_log_time_ < kTrackingLogInterval) {
      return false;
    }

    last_tracking_log_time_ = now;
    return true;
  }

  void MaybeLogTrackingState(Csm::CubismModel* model, float eye_ball_x, float eye_ball_y,
                             float angle_x, float angle_y) {
    if (model == nullptr || !ShouldEmitTrackingLog(eye_ball_x, eye_ball_y)) {
      return;
    }

    const auto get_index = [model](Csm::CubismIdHandle id) -> int {
      if (id == nullptr) {
        return -1;
      }
      return model->GetParameterIndex(id);
    };

    const auto get_value = [model](Csm::CubismIdHandle id, int index) -> float {
      if (id == nullptr || index < 0) {
        return 0.0F;
      }
      return model->GetParameterValue(id);
    };

    const int eye_ball_x_index = get_index(param_eye_ball_x_);
    const int eye_ball_y_index = get_index(param_eye_ball_y_);
    const int angle_x_index = get_index(param_angle_x_);
    const int angle_y_index = get_index(param_angle_y_);
    const int angle_z_index = get_index(param_angle_z_);
    const int body_angle_x_index = get_index(param_body_angle_x_);

    const float eye_ball_x_value = get_value(param_eye_ball_x_, eye_ball_x_index);
    const float eye_ball_y_value = get_value(param_eye_ball_y_, eye_ball_y_index);
    const float angle_x_value = get_value(param_angle_x_, angle_x_index);
    const float angle_y_value = get_value(param_angle_y_, angle_y_index);
    const float angle_z_value = get_value(param_angle_z_, angle_z_index);
    const float body_angle_x_value = get_value(param_body_angle_x_, body_angle_x_index);

    spdlog::info(
        "[TRACK] input eye=({:.3f},{:.3f}) angle=({:.3f},{:.3f}) | idx eye=({},{}) "
        "angle=({},{},{}) body=({}) | value eye=({:.3f},{:.3f}) angle=({:.3f},{:.3f},{:.3f}) "
        "body=({:.3f})",
        eye_ball_x, eye_ball_y, angle_x, angle_y, eye_ball_x_index, eye_ball_y_index,
        angle_x_index, angle_y_index, angle_z_index, body_angle_x_index, eye_ball_x_value,
        eye_ball_y_value, angle_x_value, angle_y_value, angle_z_value, body_angle_x_value);
  }

  app::Result<void> LoadModelFile() {
    const char* model_name = model_setting_->GetModelFileName();
    if (model_name == nullptr || std::string_view(model_name).empty()) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    const std::filesystem::path model_path = model_directory_ / model_name;
    auto model_bytes = ReadFileBytes(model_path);
    if (!model_bytes.has_value()) {
      spdlog::error("Failed to read Live2D moc file: {}", model_path.string());
      return std::unexpected(app::AppError::kIoReadFailed);
    }

    LoadModel(model_bytes->data(), static_cast<Csm::csmSizeInt>(model_bytes->size()), false);

    if (GetModel() == nullptr || GetModelMatrix() == nullptr) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    return {};
  }

  void InitializeParameterIds() {
    auto* id_manager = Csm::CubismFramework::GetIdManager();
    auto* model = GetModel();
    if (id_manager == nullptr || model == nullptr) {
      return;
    }
    const int parameter_count = model->GetParameterCount();
    spdlog::info("Live2D model parameter count: {}", parameter_count);

    const auto resolve_parameter_id =
        [id_manager, model, parameter_count](std::initializer_list<const char*> candidates,
                                             std::string_view semantic_name) -> Csm::CubismIdHandle {
      for (const char* candidate_name : candidates) {
        if (candidate_name == nullptr || std::string_view(candidate_name).empty()) {
          continue;
        }

        Csm::CubismIdHandle candidate_id = id_manager->GetId(candidate_name);
        if (candidate_id == nullptr) {
          continue;
        }

        const int parameter_index = model->GetParameterIndex(candidate_id);
        const bool in_real_parameter_range =
            parameter_index >= 0 && parameter_index < parameter_count;
        if (in_real_parameter_range) {
          spdlog::info("Live2D parameter mapped: {} -> {} (index={})", semantic_name,
                       candidate_name, parameter_index);
          return candidate_id;
        }
      }

      spdlog::warn("Live2D parameter not found for {} (parameter_count={})", semantic_name,
                   parameter_count);
      return nullptr;
    };

    param_angle_x_ =
        resolve_parameter_id({CsmDefaultParameterId::ParamAngleX, "PARAM_ANGLE_X"}, "AngleX");
    param_angle_y_ =
        resolve_parameter_id({CsmDefaultParameterId::ParamAngleY, "PARAM_ANGLE_Y"}, "AngleY");
    param_angle_z_ =
        resolve_parameter_id({CsmDefaultParameterId::ParamAngleZ, "PARAM_ANGLE_Z"}, "AngleZ");
    param_eye_ball_x_ = resolve_parameter_id(
        {CsmDefaultParameterId::ParamEyeBallX, "PARAM_EYE_BALL_X"}, "EyeBallX");
    param_eye_ball_y_ = resolve_parameter_id(
        {CsmDefaultParameterId::ParamEyeBallY, "PARAM_EYE_BALL_Y"}, "EyeBallY");
    param_body_angle_x_ = resolve_parameter_id(
        {CsmDefaultParameterId::ParamBodyAngleX, "PARAM_BODY_ANGLE_X"}, "BodyAngleX");
  }

  void CollectEffectParameterIds() {
    if (model_setting_ == nullptr) {
      return;
    }

    if (model_setting_->GetEyeBlinkParameterCount() > 0) {
      _eyeBlink = Csm::CubismEyeBlink::Create(model_setting_.get());
    }

    eye_blink_ids_.Clear();
    const int eye_blink_count = model_setting_->GetEyeBlinkParameterCount();
    for (int index = 0; index < eye_blink_count; ++index) {
      eye_blink_ids_.PushBack(model_setting_->GetEyeBlinkParameterId(index));
    }

    lip_sync_ids_.Clear();
    const int lip_sync_count = model_setting_->GetLipSyncParameterCount();
    for (int index = 0; index < lip_sync_count; ++index) {
      lip_sync_ids_.PushBack(model_setting_->GetLipSyncParameterId(index));
    }
  }

  app::Result<void> LoadSubResources() {
    if (model_setting_ == nullptr) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    const int expression_count = model_setting_->GetExpressionCount();
    for (int expression_index = 0; expression_index < expression_count; ++expression_index) {
      const char* expression_name = model_setting_->GetExpressionName(expression_index);
      const char* expression_file = model_setting_->GetExpressionFileName(expression_index);
      if (expression_name == nullptr || expression_file == nullptr ||
          std::string_view(expression_file).empty()) {
        continue;
      }

      const std::filesystem::path expression_path = model_directory_ / expression_file;
      auto expression_bytes = ReadFileBytes(expression_path);
      if (!expression_bytes.has_value()) {
        spdlog::warn("Failed to read Live2D expression file: {}", expression_path.string());
        continue;
      }

      Csm::ACubismMotion* expression_motion =
          LoadExpression(expression_bytes->data(),
                         static_cast<Csm::csmSizeInt>(expression_bytes->size()), expression_name);
      if (expression_motion != nullptr) {
        expressions_.insert_or_assign(std::string(expression_name), expression_motion);
      }
    }

    const char* physics_file = model_setting_->GetPhysicsFileName();
    if (physics_file != nullptr && !std::string_view(physics_file).empty()) {
      const std::filesystem::path physics_path = model_directory_ / physics_file;
      auto physics_bytes = ReadFileBytes(physics_path);
      if (physics_bytes.has_value()) {
        LoadPhysics(physics_bytes->data(), static_cast<Csm::csmSizeInt>(physics_bytes->size()));
      }
    }

    const char* pose_file = model_setting_->GetPoseFileName();
    if (pose_file != nullptr && !std::string_view(pose_file).empty()) {
      const std::filesystem::path pose_path = model_directory_ / pose_file;
      auto pose_bytes = ReadFileBytes(pose_path);
      if (pose_bytes.has_value()) {
        LoadPose(pose_bytes->data(), static_cast<Csm::csmSizeInt>(pose_bytes->size()));
      }
    }

    const char* user_data_file = model_setting_->GetUserDataFile();
    if (user_data_file != nullptr && !std::string_view(user_data_file).empty()) {
      const std::filesystem::path user_data_path = model_directory_ / user_data_file;
      auto user_data_bytes = ReadFileBytes(user_data_path);
      if (user_data_bytes.has_value()) {
        LoadUserData(user_data_bytes->data(),
                     static_cast<Csm::csmSizeInt>(user_data_bytes->size()));
      }
    }

    return {};
  }

  void ApplyLayout() {
    if (model_setting_ == nullptr || GetModelMatrix() == nullptr) {
      return;
    }

    Csm::csmMap<Csm::csmString, Csm::csmFloat32> layout;
    model_setting_->GetLayoutMap(layout);
    GetModelMatrix()->SetupFromLayout(layout);
  }

  app::Result<void> PreloadMotions() {
    if (model_setting_ == nullptr) {
      return std::unexpected(app::AppError::kLive2dLoadFailed);
    }

    for (int group_index = 0; group_index < model_setting_->GetMotionGroupCount(); ++group_index) {
      const char* group_name = model_setting_->GetMotionGroupName(group_index);
      if (group_name == nullptr || std::string_view(group_name).empty()) {
        continue;
      }

      const int motion_count = model_setting_->GetMotionCount(group_name);
      for (int motion_index = 0; motion_index < motion_count; ++motion_index) {
        const char* motion_file = model_setting_->GetMotionFileName(group_name, motion_index);
        if (motion_file == nullptr || std::string_view(motion_file).empty()) {
          continue;
        }

        const std::filesystem::path motion_path = model_directory_ / motion_file;
        auto motion_bytes = ReadFileBytes(motion_path);
        if (!motion_bytes.has_value()) {
          spdlog::warn("Failed to read Live2D motion file: {}", motion_path.string());
          continue;
        }

        const std::string motion_key = BuildMotionKey(group_name, motion_index);
        auto* motion = static_cast<Csm::CubismMotion*>(
            LoadMotion(motion_bytes->data(), static_cast<Csm::csmSizeInt>(motion_bytes->size()),
                       motion_key.c_str(), nullptr, nullptr, model_setting_.get(), group_name,
                       motion_index, false));
        if (motion == nullptr) {
          continue;
        }

        motion->SetEffectIds(eye_blink_ids_, lip_sync_ids_);
        motions_.insert_or_assign(motion_key, motion);
      }
    }

    return {};
  }

  void ReleaseMotions() {
    for (auto& [_, motion] : motions_) {
      Csm::ACubismMotion::Delete(motion);
    }
    motions_.clear();
  }

  void ReleaseExpressions() {
    for (auto& [_, expression] : expressions_) {
      Csm::ACubismMotion::Delete(expression);
    }
    expressions_.clear();
  }

  void ReleaseGpuResources() {
    if (texture_ids_.empty()) {
      return;
    }

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (context != nullptr) {
      QOpenGLFunctions* functions = context->functions();
      for (GLuint texture_id : texture_ids_) {
        if (texture_id != 0U) {
          functions->glDeleteTextures(1, &texture_id);
        }
      }

      if (graphics_ready_) {
        DeleteRenderer();
      }
    }

    texture_ids_.clear();
    graphics_ready_ = false;
  }

  std::filesystem::path model_directory_;
  std::filesystem::path model_json_path_;
  std::unique_ptr<Csm::ICubismModelSetting> model_setting_;
  std::unordered_map<std::string, Csm::ACubismMotion*> motions_;
  std::unordered_map<std::string, Csm::ACubismMotion*> expressions_;
  std::vector<GLuint> texture_ids_;
  Csm::csmVector<Csm::CubismIdHandle> eye_blink_ids_;
  Csm::csmVector<Csm::CubismIdHandle> lip_sync_ids_;
  Csm::CubismIdHandle param_angle_x_ = nullptr;
  Csm::CubismIdHandle param_angle_y_ = nullptr;
  Csm::CubismIdHandle param_angle_z_ = nullptr;
  Csm::CubismIdHandle param_eye_ball_x_ = nullptr;
  Csm::CubismIdHandle param_eye_ball_y_ = nullptr;
  Csm::CubismIdHandle param_body_angle_x_ = nullptr;
  std::chrono::steady_clock::time_point last_tracking_log_time_{};
  bool tracking_log_initialized_ = false;
  bool graphics_ready_ = false;
};

}  // namespace

#endif

struct Live2DRenderer::RuntimeState {
#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  std::unique_ptr<RuntimeCubismModel> model;
#endif
};

Live2DRenderer::Live2DRenderer() = default;

Live2DRenderer::~Live2DRenderer() = default;

app::Result<void> Live2DRenderer::Initialize(const app::SkinConfig& skin_config) {
  skin_config_ = skin_config;
  skin_loader_ = std::make_unique<CubismSkinLoader>();
  runtime_state_.reset();
  ready_ = false;
  idle_elapsed_seconds_ = 0.0;
  next_expression_index_ = 0;
  expression_unavailable_logged_ = false;
  render_failure_logged_ = false;
  eye_tracking_enabled_ = skin_config_.enable_eye_tracking;
  idle_animation_enabled_ = skin_config_.enable_idle_animation;

  if (!skin_config_.enable_live2d) {
    return std::unexpected(app::AppError::kLive2dNotEnabled);
  }

  std::filesystem::path model_directory = skin_config_.directory / skin_config_.current;
  auto load_result = skin_loader_->Load(model_directory);
  if (!load_result.has_value()) {
    return load_result;
  }

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  auto runtime_result = LoadRuntimeModel(model_directory);
  if (!runtime_result.has_value()) {
    return runtime_result;
  }
#else
  spdlog::warn("Live2D SDK is disabled at compile time. Metadata mode only.");
  return std::unexpected(app::AppError::kLive2dNotEnabled);
#endif

  ready_ = true;
  return {};
}

app::Result<void> Live2DRenderer::LoadRuntimeModel(
    const std::filesystem::path& model_directory) {
#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  auto framework_result = EnsureCubismFrameworkInitialized();
  if (!framework_result.has_value()) {
    return framework_result;
  }

  const auto* cubism_loader = dynamic_cast<const CubismSkinLoader*>(skin_loader_.get());
  if (cubism_loader == nullptr) {
    return std::unexpected(app::AppError::kLive2dLoadFailed);
  }

  const std::filesystem::path model_json_path = cubism_loader->GetModelJsonPath();
  if (model_json_path.empty()) {
    return std::unexpected(app::AppError::kLive2dModelNotFound);
  }

  runtime_state_ = std::make_unique<RuntimeState>();
  runtime_state_->model = std::make_unique<RuntimeCubismModel>();

  auto load_result = runtime_state_->model->Load(model_directory, model_json_path);
  if (!load_result.has_value()) {
    runtime_state_.reset();
    return load_result;
  }

  spdlog::info("Live2D runtime model loaded: {}", model_json_path.string());
  return {};
#else
  static_cast<void>(model_directory);
  return std::unexpected(app::AppError::kLive2dNotEnabled);
#endif
}

app::Result<void> Live2DRenderer::SwitchSkin(const std::filesystem::path& model_directory) {
  if (skin_loader_ == nullptr) {
    return std::unexpected(app::AppError::kLive2dLoadFailed);
  }

  auto load_result = skin_loader_->Load(model_directory);
  if (!load_result.has_value()) {
    ready_ = false;
    return load_result;
  }

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  auto runtime_result = LoadRuntimeModel(model_directory);
  if (!runtime_result.has_value()) {
    ready_ = false;
    return runtime_result;
  }
#endif

  ready_ = true;
  idle_elapsed_seconds_ = 0.0;
  next_expression_index_ = 0;
  expression_unavailable_logged_ = false;
  render_failure_logged_ = false;
  return {};
}

void Live2DRenderer::Update(double delta_seconds) {
  if (!ready_) {
    return;
  }

  if (eye_tracking_enabled_) {
    eye_ball_x_ = std::clamp(pointer_x_, -1.0F, 1.0F);
    eye_ball_y_ = std::clamp(pointer_y_, -1.0F, 1.0F);
  } else {
    eye_ball_x_ = 0.0F;
    eye_ball_y_ = 0.0F;
  }
  angle_x_ = std::clamp(eye_ball_x_ * 30.0F, -30.0F, 30.0F);
  angle_y_ = std::clamp(eye_ball_y_ * 30.0F, -30.0F, 30.0F);

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  if (runtime_state_ != nullptr && runtime_state_->model != nullptr) {
    runtime_state_->model->Update(delta_seconds, eye_ball_x_, eye_ball_y_, angle_x_, angle_y_);
  }
#endif

  if (std::abs(eye_ball_x_) > 0.01F || std::abs(eye_ball_y_) > 0.01F) {
    spdlog::debug(
        "Live2D pointer mapped: ParamEyeBallX={}, ParamEyeBallY={}, ParamAngleX={}, ParamAngleY={}",
        eye_ball_x_, eye_ball_y_, angle_x_, angle_y_);
  }
}

void Live2DRenderer::Render() {
  if (!ready_) {
    return;
  }

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  if (runtime_state_ == nullptr || runtime_state_->model == nullptr) {
    return;
  }

  GLint viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport);

  const RenderViewport model_viewport = BuildModelViewport(
      viewport[2], viewport[3], skin_config_.model_width_px, skin_config_.model_height_px);
  glViewport(model_viewport.x, model_viewport.y, model_viewport.width, model_viewport.height);

  auto render_result = runtime_state_->model->Render(model_viewport.width, model_viewport.height);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  if (!render_result.has_value() && !render_failure_logged_) {
    spdlog::error("Live2D render failed: {}", app::AppErrorToString(render_result.error()));
    render_failure_logged_ = true;
  }
#endif
}

void Live2DRenderer::SetPointerPosition(float normalized_x, float normalized_y) {
  pointer_x_ = std::clamp(normalized_x, -1.0F, 1.0F);
  pointer_y_ = std::clamp(normalized_y, -1.0F, 1.0F);

  const auto now = std::chrono::steady_clock::now();
  constexpr auto kPointerLogInterval = std::chrono::milliseconds(200);

  bool should_log = !pointer_log_initialized_;
  if (!should_log) {
    const float delta_x = std::abs(pointer_x_ - last_logged_pointer_x_);
    const float delta_y = std::abs(pointer_y_ - last_logged_pointer_y_);
    const bool changed_enough = delta_x > 0.08F || delta_y > 0.08F;
    if (changed_enough && (now - last_pointer_log_time_ >= kPointerLogInterval)) {
      should_log = true;
    }
  }

  if (should_log) {
    pointer_log_initialized_ = true;
    last_pointer_log_time_ = now;
    last_logged_pointer_x_ = pointer_x_;
    last_logged_pointer_y_ = pointer_y_;
    spdlog::info("[TRACK] pointer normalized=({:.3f}, {:.3f})", pointer_x_, pointer_y_);
  }
}

void Live2DRenderer::TriggerClickExpression() {
  if (!ready_ || skin_loader_ == nullptr) {
    return;
  }

  const auto expressions = skin_loader_->ListExpressions();
  if (expressions.empty()) {
    if (!expression_unavailable_logged_) {
      spdlog::warn(
          "No expressions available in current Live2D model. Click interaction falls back to "
          "motion.");
      expression_unavailable_logged_ = true;
    }

    const auto motions = skin_loader_->ListMotions();
    const auto* cubism_loader = dynamic_cast<const CubismSkinLoader*>(skin_loader_.get());
    auto click_motion = PickRandomClickMotion(motions, cubism_loader, &random_engine_);
    if (!click_motion.has_value()) {
      return;
    }

    const auto& [group_name, motion_index] = *click_motion;
    auto motion_result = skin_loader_->StartMotion(group_name, motion_index);
    if (!motion_result.has_value()) {
      spdlog::warn("Failed to trigger click motion {}:{}", group_name, motion_index);
      return;
    }

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
    if (runtime_state_ != nullptr && runtime_state_->model != nullptr) {
      auto runtime_motion_result = runtime_state_->model->StartMotion(group_name, motion_index);
      if (!runtime_motion_result.has_value()) {
        spdlog::warn("Live2D runtime click motion failed: {}",
                     app::AppErrorToString(runtime_motion_result.error()));
      }
    }
#endif
    return;
  }

  expression_unavailable_logged_ = false;
  const std::size_t expression_index = next_expression_index_ % expressions.size();
  const auto set_result = skin_loader_->SetExpression(expressions[expression_index]);
  if (!set_result.has_value()) {
    spdlog::warn("Failed to set expression index {}", expression_index);
    return;
  }

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  if (runtime_state_ != nullptr && runtime_state_->model != nullptr) {
    auto runtime_result = runtime_state_->model->SetExpression(expressions[expression_index]);
    if (!runtime_result.has_value()) {
      spdlog::warn("Live2D runtime expression update failed: {}",
                   app::AppErrorToString(runtime_result.error()));
    }
  }
#endif

  next_expression_index_ = (expression_index + 1U) % expressions.size();
}

void Live2DRenderer::TickIdle(double delta_seconds) {
  if (!ready_ || skin_loader_ == nullptr) {
    return;
  }
  if (!idle_animation_enabled_) {
    return;
  }

  idle_elapsed_seconds_ += delta_seconds;
  const int idle_interval = std::max(skin_config_.idle_interval_seconds, 1);
  if (idle_elapsed_seconds_ < static_cast<double>(idle_interval)) {
    return;
  }
  idle_elapsed_seconds_ = 0.0;

  const auto motions = skin_loader_->ListMotions();
  if (motions.empty()) {
    spdlog::warn("No motions available for idle playback.");
    return;
  }

  std::string group_name = skin_config_.idle_motion_group;
  bool group_found = std::find(motions.begin(), motions.end(), group_name) != motions.end();
  if (!group_found) {
    group_name = "idle";
    group_found = std::find(motions.begin(), motions.end(), group_name) != motions.end();
  }
  if (!group_found) {
    group_name = motions.front();
  }

  auto motion_result = skin_loader_->StartMotion(group_name, 0);
  if (!motion_result.has_value()) {
    spdlog::warn("Failed to trigger idle motion for group {}", group_name);
    return;
  }

#if defined(MIKUDESK_ENABLE_LIVE2D) && MIKUDESK_ENABLE_LIVE2D
  if (runtime_state_ != nullptr && runtime_state_->model != nullptr) {
    auto runtime_motion_result = runtime_state_->model->StartMotion(group_name, 0);
    if (!runtime_motion_result.has_value()) {
      spdlog::warn("Live2D runtime motion trigger failed: {}",
                   app::AppErrorToString(runtime_motion_result.error()));
    }
  }
#endif
}

void Live2DRenderer::SetEyeTrackingEnabled(bool enabled) {
  eye_tracking_enabled_ = enabled;
  skin_config_.enable_eye_tracking = enabled;
  if (!enabled) {
    pointer_x_ = 0.0F;
    pointer_y_ = 0.0F;
  }
}

void Live2DRenderer::SetIdleAnimationEnabled(bool enabled) {
  idle_animation_enabled_ = enabled;
  skin_config_.enable_idle_animation = enabled;
  idle_elapsed_seconds_ = 0.0;
}

void Live2DRenderer::SetIdleMotionGroup(std::string group_name) {
  if (!group_name.empty()) {
    skin_config_.idle_motion_group = std::move(group_name);
  }
}

void Live2DRenderer::SetIdleIntervalSeconds(int interval_seconds) {
  skin_config_.idle_interval_seconds = std::max(interval_seconds, 1);
  idle_elapsed_seconds_ = 0.0;
}

void Live2DRenderer::SetModelRenderSize(int model_width_px, int model_height_px) {
  skin_config_.model_width_px = std::clamp(model_width_px, 64, 4096);
  skin_config_.model_height_px = std::clamp(model_height_px, 64, 4096);
}

bool Live2DRenderer::IsReady() const {
  return ready_;
}

std::vector<std::string> Live2DRenderer::ListExpressions() const {
  if (skin_loader_ == nullptr) {
    return {};
  }
  return skin_loader_->ListExpressions();
}

std::vector<std::string> Live2DRenderer::ListMotions() const {
  if (skin_loader_ == nullptr) {
    return {};
  }
  return skin_loader_->ListMotions();
}

std::string Live2DRenderer::GetActiveExpression() const {
  const auto* cubism_loader = dynamic_cast<const CubismSkinLoader*>(skin_loader_.get());
  if (cubism_loader == nullptr) {
    return {};
  }
  return cubism_loader->GetActiveExpression();
}

std::string Live2DRenderer::GetActiveMotion() const {
  const auto* cubism_loader = dynamic_cast<const CubismSkinLoader*>(skin_loader_.get());
  if (cubism_loader == nullptr) {
    return {};
  }
  return cubism_loader->GetActiveMotion();
}

std::filesystem::path Live2DRenderer::GetCurrentModelDirectory() const {
  const auto* cubism_loader = dynamic_cast<const CubismSkinLoader*>(skin_loader_.get());
  if (cubism_loader == nullptr) {
    return {};
  }
  return cubism_loader->GetModelDirectory();
}

}  // namespace mikudesk::renderer
