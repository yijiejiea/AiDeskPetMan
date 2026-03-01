#include "ui/chat_bubble.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <algorithm>

namespace mikudesk::ui {

namespace {

constexpr int kBubbleHorizontalPaddingPx = 14;
constexpr int kBubbleVerticalPaddingPx = 10;
constexpr int kBubbleMaxWidthPx = 360;
constexpr int kBubbleMinWidthPx = 120;
constexpr int kBubbleMinHeightPx = 40;
constexpr int kBubbleOffsetAboveModelPx = 12;
constexpr int kBubbleRoundRadiusPx = 14;
constexpr int kAutoHideDelayMs = 6000;

}  // namespace

ChatBubble::ChatBubble(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  hide();

  auto_hide_timer_.setSingleShot(true);
  connect(&auto_hide_timer_, &QTimer::timeout, this, [this]() {
    if (!streaming_) {
      hide();
    }
  });
}

void ChatBubble::SetText(QString text, bool streaming) {
  text = text.trimmed();
  streaming_ = streaming;

  if (text.isEmpty()) {
    if (!streaming_) {
      hide();
    }
    text_.clear();
    return;
  }

  text_ = std::move(text);
  const QSize target_size = ComputeBubbleSize(kBubbleMaxWidthPx);
  resize(target_size);
  show();

  if (streaming_) {
    auto_hide_timer_.stop();
  } else {
    auto_hide_timer_.start(kAutoHideDelayMs);
  }

  update();
}

void ChatBubble::Clear() {
  text_.clear();
  streaming_ = false;
  auto_hide_timer_.stop();
  hide();
}

void ChatBubble::Reposition(const QRect& model_rect_in_window) {
  if (parentWidget() == nullptr || !isVisible()) {
    return;
  }

  const int x = std::clamp(model_rect_in_window.center().x() - (width() / 2), 0,
                           std::max(parentWidget()->width() - width(), 0));
  const int y = std::clamp(model_rect_in_window.top() - height() - kBubbleOffsetAboveModelPx, 0,
                           std::max(parentWidget()->height() - height(), 0));
  move(x, y);
}

void ChatBubble::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);

  if (text_.isEmpty()) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(15, 15, 18, 205));
  painter.drawRoundedRect(rect(), kBubbleRoundRadiusPx, kBubbleRoundRadiusPx);

  painter.setPen(palette().color(QPalette::BrightText));
  const QRect text_rect = rect().adjusted(kBubbleHorizontalPaddingPx, kBubbleVerticalPaddingPx,
                                          -kBubbleHorizontalPaddingPx, -kBubbleVerticalPaddingPx);
  painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text_);
}

QSize ChatBubble::ComputeBubbleSize(int max_width) const {
  QFontMetrics metrics(font());
  const int text_width_budget = std::max(max_width - (kBubbleHorizontalPaddingPx * 2), 40);
  const QRect text_bounds =
      metrics.boundingRect(QRect(0, 0, text_width_budget, 2000),
                           Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text_);

  const int width = std::clamp(text_bounds.width() + (kBubbleHorizontalPaddingPx * 2),
                               kBubbleMinWidthPx, max_width);
  const int height = std::max(text_bounds.height() + (kBubbleVerticalPaddingPx * 2),
                              kBubbleMinHeightPx);
  return {width, height};
}

}  // namespace mikudesk::ui
