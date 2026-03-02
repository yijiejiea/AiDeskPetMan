#include "ui/chat_bubble.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QVBoxLayout>

#include <algorithm>

namespace mikudesk::ui {

namespace {

constexpr int kBubbleHorizontalPaddingPx = 14;
constexpr int kBubbleVerticalPaddingPx = 12;
constexpr int kBubbleMaxWidthPx = 420;
constexpr int kBubbleMinWidthPx = 180;
constexpr int kBubbleMinHeightPx = 62;
constexpr int kBubbleMaxHeightPx = 280;
constexpr int kBubbleMinTextHeightPx = 34;
constexpr int kBubbleOffsetAboveModelPx = 14;
constexpr int kBubbleRadiusPx = 16;
constexpr int kOuterPaddingPx = 10;
constexpr int kTailWidthPx = 22;
constexpr int kTailHeightPx = 12;
constexpr int kBubbleSideGapPx = 12;
constexpr int kScreenPaddingPx = 10;
constexpr int kAutoHideDelayMs = 15000;

}  // namespace

ChatBubble::ChatBubble(QWidget* parent) : QWidget(parent) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                 Qt::NoDropShadowWindowHint);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setFocusPolicy(Qt::NoFocus);

  auto* bubble_layout = new QVBoxLayout(this);
  bubble_layout->setContentsMargins(kOuterPaddingPx + kBubbleHorizontalPaddingPx,
                                    kOuterPaddingPx + kBubbleVerticalPaddingPx,
                                    kOuterPaddingPx + kBubbleHorizontalPaddingPx,
                                    kOuterPaddingPx + kBubbleVerticalPaddingPx + kTailHeightPx);
  bubble_layout->setSpacing(0);

  text_view_ = new QTextBrowser(this);
  text_view_->setFrameShape(QFrame::NoFrame);
  text_view_->setReadOnly(true);
  text_view_->setOpenLinks(false);
  text_view_->setOpenExternalLinks(false);
  text_view_->setUndoRedoEnabled(false);
  text_view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  text_view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  text_view_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  text_view_->document()->setDocumentMargin(0.0);
  text_view_->setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11));
  text_view_->setStyleSheet(
      "QTextBrowser { background: transparent; color: rgb(20, 30, 44); border: none; "
      "selection-background-color: rgba(72, 128, 196, 120); }"
      "QScrollBar:vertical { width: 8px; background: transparent; margin: 4px 0 4px 0; }"
      "QScrollBar::handle:vertical { background: rgba(74, 109, 156, 168); border-radius: 4px; min-height: 22px; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }");
  bubble_layout->addWidget(text_view_);

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
    if (text_view_ != nullptr) {
      text_view_->clear();
    }
    return;
  }

  bool was_scrolled_to_bottom = true;
  if (text_view_ != nullptr && text_view_->verticalScrollBar() != nullptr) {
    QScrollBar* scrollbar = text_view_->verticalScrollBar();
    was_scrolled_to_bottom = scrollbar->value() >= scrollbar->maximum() - 4;
  }

  text_ = std::move(text);

  int max_width = kBubbleMaxWidthPx;
  int max_height = kBubbleMaxHeightPx;
  if (parentWidget() != nullptr) {
    max_width = std::min(kBubbleMaxWidthPx, std::max(kBubbleMinWidthPx, parentWidget()->width() - 8));
    max_height =
        std::min(kBubbleMaxHeightPx, std::max(kBubbleMinHeightPx, parentWidget()->height() - 12));
  }
  resize(ComputeBubbleSize(max_width, max_height));

  if (text_view_ != nullptr) {
    text_view_->setPlainText(text_);
    if (text_view_->verticalScrollBar() != nullptr) {
      if (!streaming_) {
        text_view_->verticalScrollBar()->setValue(0);
      } else if (was_scrolled_to_bottom) {
        text_view_->verticalScrollBar()->setValue(text_view_->verticalScrollBar()->maximum());
      }
    }
  }

  show();
  raise();

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
  if (text_view_ != nullptr) {
    text_view_->clear();
  }
  auto_hide_timer_.stop();
  hide();
}

void ChatBubble::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRectF card_rect(kOuterPaddingPx, kOuterPaddingPx, width() - (kOuterPaddingPx * 2),
                         height() - (kOuterPaddingPx * 2) - kTailHeightPx);
  if (card_rect.width() <= 0.0 || card_rect.height() <= 0.0) {
    return;
  }

  QPainterPath bubble_path;
  bubble_path.addRoundedRect(card_rect, kBubbleRadiusPx, kBubbleRadiusPx);
  const double tail_center_x = card_rect.center().x() + 18.0;
  const QPointF tail_tip(tail_center_x, card_rect.bottom() + kTailHeightPx);
  const QPointF tail_left(tail_tip.x() - (kTailWidthPx / 2.0), card_rect.bottom() - 0.5);
  const QPointF tail_right(tail_tip.x() + (kTailWidthPx / 2.0), card_rect.bottom() - 0.5);
  bubble_path.moveTo(tail_left);
  bubble_path.lineTo(tail_tip);
  bubble_path.lineTo(tail_right);
  bubble_path.closeSubpath();

  const QPainterPath shadow_path = bubble_path.translated(0.0, 3.0);
  painter.fillPath(shadow_path, QColor(6, 16, 34, 84));

  QLinearGradient fill_gradient(card_rect.topLeft(), card_rect.bottomLeft());
  fill_gradient.setColorAt(0.0, QColor(255, 255, 255, 248));
  fill_gradient.setColorAt(0.65, QColor(238, 246, 255, 240));
  fill_gradient.setColorAt(1.0, QColor(224, 237, 252, 235));
  painter.setBrush(fill_gradient);
  painter.setPen(QPen(QColor(157, 190, 228, 210), 1.0));
  painter.drawPath(bubble_path);

  QPen highlight_pen(QColor(255, 255, 255, 180), 1.0);
  painter.setPen(highlight_pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(card_rect.adjusted(1.0, 1.0, -1.0, -1.0), kBubbleRadiusPx - 2,
                          kBubbleRadiusPx - 2);
}

void ChatBubble::enterEvent(QEnterEvent* event) {
  auto_hide_timer_.stop();
  QWidget::enterEvent(event);
}

void ChatBubble::leaveEvent(QEvent* event) {
  if (!streaming_ && !text_.isEmpty()) {
    auto_hide_timer_.start(kAutoHideDelayMs);
  }
  QWidget::leaveEvent(event);
}

void ChatBubble::Reposition(const QRect& model_rect_in_window) {
  if (parentWidget() == nullptr || !isVisible()) {
    return;
  }

  const QPoint anchor_global =
      parentWidget()->mapToGlobal(QPoint(model_rect_in_window.center().x(), model_rect_in_window.top()));
  const QPoint model_top_left = parentWidget()->mapToGlobal(model_rect_in_window.topLeft());
  const QRect model_rect_global(model_top_left, model_rect_in_window.size());

  QScreen* screen = QGuiApplication::screenAt(anchor_global);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  const QRect available =
      screen != nullptr
          ? screen->availableGeometry()
          : QRect(anchor_global.x() - 500, anchor_global.y() - 400, 1000, 800);

  int x = anchor_global.x() - (width() / 2);
  int y = anchor_global.y() - height() - kBubbleOffsetAboveModelPx;
  const int min_x = available.left() + kScreenPaddingPx;
  const int max_x = available.right() - width() - kScreenPaddingPx;
  const int min_y = available.top() + kScreenPaddingPx;
  const int max_y = available.bottom() - height() - kScreenPaddingPx;
  x = std::clamp(x, min_x, std::max(min_x, max_x));

  bool use_side_dock = y < min_y;
  if (use_side_dock) {
    const int right_x = model_rect_global.right() + kBubbleSideGapPx;
    const int left_x = model_rect_global.left() - width() - kBubbleSideGapPx;
    const int side_y =
        std::clamp(model_rect_global.top() - (height() / 5), min_y, std::max(min_y, max_y));

    if (right_x + width() <= available.right() - kScreenPaddingPx) {
      x = right_x;
      y = side_y;
    } else if (left_x >= available.left() + kScreenPaddingPx) {
      x = left_x;
      y = side_y;
    } else {
      y = min_y;
    }
  } else {
    y = std::clamp(y, min_y, std::max(min_y, max_y));
  }

  QRect target_rect(x, y, width(), height());
  if (target_rect.intersects(model_rect_global)) {
    const int above_y = model_rect_global.top() - height() - kBubbleOffsetAboveModelPx;
    if (above_y >= min_y) {
      y = above_y;
    } else {
      const int right_x = model_rect_global.right() + kBubbleSideGapPx;
      const int left_x = model_rect_global.left() - width() - kBubbleSideGapPx;
      if (right_x + width() <= available.right() - kScreenPaddingPx) {
        x = right_x;
      } else if (left_x >= available.left() + kScreenPaddingPx) {
        x = left_x;
      }
      y = std::clamp(model_rect_global.top() - (height() / 4), min_y, std::max(min_y, max_y));
    }
  }

  move(x, y);
}

QSize ChatBubble::ComputeBubbleSize(int max_width, int max_height) const {
  QFontMetrics metrics(text_view_ != nullptr ? text_view_->font() : font());

  const int horizontal_chrome = (kOuterPaddingPx + kBubbleHorizontalPaddingPx) * 2;
  const int vertical_chrome = (kOuterPaddingPx + kBubbleVerticalPaddingPx) * 2 + kTailHeightPx;

  const int text_width_budget = std::max(40, max_width - horizontal_chrome);
  const QRect text_bounds =
      metrics.boundingRect(QRect(0, 0, text_width_budget, 10000),
                           Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text_);

  const int width =
      std::clamp(text_bounds.width() + horizontal_chrome, kBubbleMinWidthPx, max_width);

  const int max_text_height = std::max(kBubbleMinTextHeightPx, max_height - vertical_chrome);
  const int text_height =
      std::clamp(text_bounds.height(), kBubbleMinTextHeightPx, max_text_height);
  const int height =
      std::clamp(text_height + vertical_chrome, kBubbleMinHeightPx, max_height);

  return {width, height};
}

}  // namespace mikudesk::ui
