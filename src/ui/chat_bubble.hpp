#pragma once

#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

class QEnterEvent;
class QEvent;
class QPaintEvent;
class QTextBrowser;

namespace mikudesk::ui {

class ChatBubble final : public QWidget {
 public:
  explicit ChatBubble(QWidget* parent = nullptr);
  ~ChatBubble() override = default;

  void SetText(QString text, bool streaming);
  void Clear();
  void Reposition(const QRect& model_rect_in_window);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  QSize ComputeBubbleSize(int max_width, int max_height) const;

  QString text_;
  bool streaming_ = false;
  QTimer auto_hide_timer_;
  QTextBrowser* text_view_ = nullptr;
};

}  // namespace mikudesk::ui
