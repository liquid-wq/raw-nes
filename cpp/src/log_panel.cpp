#include "log_panel.h"
#include "colors.h"

#include <QPlainTextEdit>
#include <QVBoxLayout>

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    text_ = new QPlainTextEdit(this);
    text_->setReadOnly(true);
    text_->setMaximumBlockCount(2000); // verhindert unbegrenztes Wachstum bei langen Sessions
    text_->setStyleSheet(QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; font-family: Consolas; font-size: 11px; }")
        .arg(RawnesColors::kPanel.name(), RawnesColors::kTextMain.name()));
    layout->addWidget(text_);
}

void LogPanel::append(const QString& text) {
    text_->appendPlainText(text);
}

void LogPanel::clear() {
    text_->clear();
}
