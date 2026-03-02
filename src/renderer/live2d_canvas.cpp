#include "renderer/live2d_canvas.hpp"

#include <algorithm>
#include <cmath>

#include <QSurfaceFormat>

namespace mikudesk::renderer {

Live2DCanvas::Live2DCanvas(Live2DRenderer* renderer, QWidget* parent)
    : QOpenGLWidget(parent), renderer_(renderer) {
  setAutoFillBackground(false);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  setAttribute(Qt::WA_OpaquePaintEvent, false);
  setAttribute(Qt::WA_TranslucentBackground, true);

  QSurfaceFormat surface_format = format();
  if (surface_format.alphaBufferSize() < 8) {
    surface_format.setAlphaBufferSize(8);
    setFormat(surface_format);
  }
}

void Live2DCanvas::initializeGL() {
  initializeOpenGLFunctions();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_STENCIL_TEST);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
}

void Live2DCanvas::paintGL() {
  const qreal pixel_ratio = devicePixelRatioF();
  const int viewport_width =
      std::max(1, static_cast<int>(std::lround(static_cast<double>(width()) * pixel_ratio)));
  const int viewport_height =
      std::max(1, static_cast<int>(std::lround(static_cast<double>(height()) * pixel_ratio)));

  glViewport(0, 0, viewport_width, viewport_height);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (renderer_ != nullptr) {
    renderer_->Render();
  }
}

}  // namespace mikudesk::renderer
