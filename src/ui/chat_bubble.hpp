#pragma once

#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>

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

 private:
  QSize ComputeBubbleSize(int max_width) const;

  QString text_;
  bool streaming_ = false;
  QTimer auto_hide_timer_;
};

}  // namespace mikudesk::ui
