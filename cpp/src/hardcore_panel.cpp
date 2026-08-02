#include "hardcore_panel.h"
#include "colors.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>

HardcorePanel::HardcorePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    checkbox_ = new QCheckBox(tr("Hardcore-Modus"), this);
    checkbox_->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(RawnesColors::kTextMain.name()));
    layout->addWidget(checkbox_);

    hint_ = new QLabel(this);
    hint_->setStyleSheet(
        QString("color: %1; font-size: 10px;").arg(RawnesColors::kTextMuted.name()));
    layout->addWidget(hint_);
    layout->addStretch(1);

    connect(checkbox_, &QCheckBox::toggled, this, &HardcorePanel::toggled);
}

bool HardcorePanel::isChecked() const {
    return checkbox_->isChecked();
}

void HardcorePanel::setChecked(bool checked) {
    checkbox_->setChecked(checked); // loest QCheckBox::toggled aus, wenn sich der Wert aendert
}

void HardcorePanel::setHint(const QString& text) {
    hint_->setText(text);
}
