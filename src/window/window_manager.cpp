#include "window/window_manager.hpp"

#include <QCoreApplication>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QFont>
#include <QPixmap>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QColor>
#include <QPainter>

#include <utility>

#include "spdlog/spdlog.h"
#include "window/desk_pet_window.hpp"

namespace mikudesk::window {

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() {
  Destroy();
}

app::Result<void> WindowManager::Create(const app::AppConfig& config,
                                        const std::filesystem::path& config_path) {
  if (QCoreApplication::instance() == nullptr) {
    spdlog::error("QCoreApplication is not initialized.");
    return std::unexpected(app::AppError::kWindowCreateFailed);
  }

  if (window_ != nullptr) {
    running_ = true;
    return {};
  }

  auto on_close = [this]() { running_ = false; };
  window_ = std::make_unique<DeskPetWindow>(config, config_path, std::move(on_close));
  window_->move(120, 120);
  window_->show();
  window_->raise();
  window_->activateWindow();
  window_->setFocus();
  spdlog::info("Qt transparent window created at ({}, {}), size={}x{}.", window_->x(), window_->y(),
               window_->width(), window_->height());

  EnsureTrayIcon();
  running_ = true;
  return {};
}

void WindowManager::EnsureTrayIcon() {
  if (tray_icon_ != nullptr) {
    return;
  }
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    spdlog::warn("System tray is not available on this environment.");
    return;
  }

  tray_icon_ = std::make_unique<QSystemTrayIcon>();
  tray_menu_ = std::make_unique<QMenu>();

  // Use an app-specific tray icon so it is easy to identify in Windows tray.
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(26, 188, 214));
  painter.drawEllipse(1, 1, 30, 30);
  QFont font = painter.font();
  font.setBold(true);
  font.setPointSize(16);
  painter.setFont(font);
  painter.setPen(Qt::white);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, "M");
  painter.end();
  QIcon tray_icon(pixmap);
  tray_icon_->setIcon(tray_icon);
  tray_icon_->setToolTip("MikuDesk");

  QAction* open_settings_action = tray_menu_->addAction("打开设置");
  QAction* show_window_action = tray_menu_->addAction("显示主窗口");
  QAction* hide_window_action = tray_menu_->addAction("隐藏主窗口");
  tray_menu_->addSeparator();
  QAction* exit_action = tray_menu_->addAction("退出");

  QObject::connect(open_settings_action, &QAction::triggered, [this]() {
    if (window_ == nullptr) {
      return;
    }
    window_->ShowSettingsWindow();
  });

  QObject::connect(show_window_action, &QAction::triggered, [this]() {
    if (window_ == nullptr) {
      return;
    }
    window_->show();
    window_->raise();
    window_->activateWindow();
    window_->setFocus();
  });

  QObject::connect(hide_window_action, &QAction::triggered, [this]() {
    if (window_ == nullptr) {
      return;
    }
    window_->hide();
  });

  QObject::connect(exit_action, &QAction::triggered, [this]() {
    running_ = false;
    if (window_ != nullptr) {
      window_->close();
    }
  });

  QObject::connect(tray_icon_.get(), &QSystemTrayIcon::activated,
                   [this](QSystemTrayIcon::ActivationReason reason) {
                     if (window_ == nullptr) {
                       return;
                     }
                     if (reason == QSystemTrayIcon::Trigger) {
                       if (window_->isVisible()) {
                         window_->hide();
                       } else {
                         window_->show();
                         window_->raise();
                         window_->activateWindow();
                         window_->setFocus();
                       }
                       return;
                     }
                     if (reason == QSystemTrayIcon::DoubleClick) {
                       window_->ShowSettingsWindow();
                     }
                   });

  tray_icon_->setContextMenu(tray_menu_.get());
  tray_icon_->show();
  spdlog::info("System tray icon initialized. Right click tray icon to open menu.");
}

void WindowManager::PollEvents() {
  if (!running_ || window_ == nullptr) {
    return;
  }
}

void WindowManager::BeginFrame() {
  if (!running_ || window_ == nullptr) {
    return;
  }
  window_->AdvanceFrame();
}

void WindowManager::EndFrame() {}

bool WindowManager::IsRunning() const {
  return running_;
}

void WindowManager::Destroy() {
  running_ = false;
  if (tray_icon_ != nullptr) {
    tray_icon_->hide();
    tray_icon_.reset();
  }
  tray_menu_.reset();

  if (window_ != nullptr) {
    window_->close();
    window_.reset();
  }
}

}  // namespace mikudesk::window
