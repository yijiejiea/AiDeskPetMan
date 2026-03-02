#pragma once

#include <functional>
#include <memory>

#include <QDialog>
#include <QPoint>
#include <QString>

namespace Ui {
class ChatInputDialog;
}

namespace mikudesk::ui {

class ChatInputDialog final : public QDialog {
 public:
  using SubmitHandler = std::function<void(QString)>;

  explicit ChatInputDialog(QWidget* parent = nullptr);
  ~ChatInputDialog() override;

  ChatInputDialog(const ChatInputDialog&) = delete;
  ChatInputDialog& operator=(const ChatInputDialog&) = delete;

  void SetSubmitHandler(SubmitHandler on_submit);
  void ShowNear(const QPoint& global_anchor);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  void SubmitCurrentText();

  std::unique_ptr<Ui::ChatInputDialog> ui_;
  SubmitHandler on_submit_;
};

}  // namespace mikudesk::ui
