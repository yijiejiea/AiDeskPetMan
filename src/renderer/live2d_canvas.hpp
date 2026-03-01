#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include "renderer/live2d_renderer.hpp"

namespace mikudesk::renderer {

class Live2DCanvas final : public QOpenGLWidget, protected QOpenGLFunctions {
 public:
  explicit Live2DCanvas(Live2DRenderer* renderer, QWidget* parent = nullptr);
  ~Live2DCanvas() override = default;

 protected:
  void initializeGL() override;
  void paintGL() override;

 private:
  Live2DRenderer* renderer_ = nullptr;
};

}  // namespace mikudesk::renderer
