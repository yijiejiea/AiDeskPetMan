#include "ui/chat_input_dialog.hpp"

#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QPushButton>

#include "ui_chat_input_dialog.h"

namespace mikudesk::ui {

namespace {

constexpr int kDefaultDialogWidthPx = 460;
constexpr int kDefaultDialogHeightPx = 260;
constexpr int kDialogBottomMarginPx = 12;

}  // namespace

ChatInputDialog::ChatInputDialog(QWidget* parent)
    : QDialog(parent), ui_(std::make_unique<Ui::ChatInputDialog>()) {
  ui_->setupUi(this);
  setModal(false);
  setWindowFlag(Qt::Tool, true);
  setWindowFlag(Qt::WindowStaysOnTopHint, true);

  ui_->message_text_edit->installEventFilter(this);

  connect(ui_->send_button, &QPushButton::clicked, this, [this]() { SubmitCurrentText(); });
  connect(ui_->cancel_button, &QPushButton::clicked, this, [this]() { hide(); });
}

ChatInputDialog::~ChatInputDialog() = default;

void ChatInputDialog::SetSubmitHandler(SubmitHandler on_submit) {
  on_submit_ = std::move(on_submit);
}

void ChatInputDialog::ShowNear(const QPoint& global_anchor) {
  resize(kDefaultDialogWidthPx, kDefaultDialogHeightPx);

  const QPoint top_left(global_anchor.x() - (width() / 2),
                        global_anchor.y() - height() - kDialogBottomMarginPx);
  move(top_left);
  show();
  raise();
  activateWindow();
  ui_->message_text_edit->setFocus();
}

bool ChatInputDialog::eventFilter(QObject* watched, QEvent* event) {
  if (watched != ui_->message_text_edit || event == nullptr) {
    return QDialog::eventFilter(watched, event);
  }

  if (event->type() != QEvent::KeyPress) {
    return QDialog::eventFilter(watched, event);
  }

  auto* key_event = static_cast<QKeyEvent*>(event);
  const int key = key_event->key();
  const bool shift_pressed = key_event->modifiers().testFlag(Qt::ShiftModifier);

  if (key == Qt::Key_Escape) {
    hide();
    return true;
  }

  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    if (shift_pressed) {
      return false;
    }
    SubmitCurrentText();
    return true;
  }

  return QDialog::eventFilter(watched, event);
}

void ChatInputDialog::closeEvent(QCloseEvent* event) {
  ui_->message_text_edit->clear();
  QDialog::closeEvent(event);
}

void ChatInputDialog::SubmitCurrentText() {
  const QString message = ui_->message_text_edit->toPlainText().trimmed();
  if (message.isEmpty()) {
    return;
  }

  if (on_submit_) {
    on_submit_(message);
  }

  ui_->message_text_edit->clear();
  hide();
}

}  // namespace mikudesk::ui
