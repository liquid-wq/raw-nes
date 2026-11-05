#include "achievement_row.h"
#include "colors.h"

#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QVBoxLayout>

AchievementRow::AchievementRow(QWidget* parent) : QWidget(parent) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 3px farbiger Streifen links -- Statusfarbe.
    leftBar_ = new QFrame(this);
    leftBar_->setFixedWidth(3);
    outer->addWidget(leftBar_);

    auto* content = new QWidget(this);
    content->setObjectName("rowContent");
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(10, 8, 10, 8);
    contentLayout->setSpacing(10);
    outer->addWidget(content, 1);

    badgeLabel_ = new QLabel(content);
    badgeLabel_->setFixedSize(36, 36);
    badgeLabel_->setScaledContents(true);
    badgeLabel_->setStyleSheet(
        QString("background: %1; border-radius: 4px;").arg(RawnesColors::kLockedIconBg.name()));
    contentLayout->addWidget(badgeLabel_);

    auto* textCol = new QVBoxLayout();
    textCol->setSpacing(2);
    titleLabel_ = new QLabel(content);
    titleLabel_->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(RawnesColors::kTextMain.name()));
    descLabel_ = new QLabel(content);
    descLabel_->setWordWrap(true);
    descLabel_->setStyleSheet(
        QString("color: %1; font-size: 10px;").arg(RawnesColors::kTextMuted.name()));
    textCol->addWidget(titleLabel_);
    textCol->addWidget(descLabel_);
    contentLayout->addLayout(textCol, 1);

    auto* rightCol = new QVBoxLayout();
    rightCol->setSpacing(2);
    pointsLabel_ = new QLabel(content);
    pointsLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pointsLabel_->setStyleSheet("font-size: 11px;");
    rightCol->addWidget(pointsLabel_);

    progressBar_ = new QProgressBar(content);
    progressBar_->setFixedWidth(70);
    progressBar_->setFixedHeight(6);
    progressBar_->setTextVisible(false);
    progressBar_->setStyleSheet(QString(
        "QProgressBar { background: %1; border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: %2; border-radius: 3px; }")
        .arg(RawnesColors::kLockedIconBg.name(), RawnesColors::kInProgress.name()));
    progressBar_->hide(); // nur sichtbar, wenn setProgress() mit target>1 aufgerufen wurde
    rightCol->addWidget(progressBar_);
    contentLayout->addLayout(rightCol);

    applyStateStyle();
}

void AchievementRow::setContent(const QString& title, const QString& desc, int points) {
    titleLabel_->setText(title);
    descLabel_->setText(desc);
    pointsLabel_->setText(tr("%1 Pkt").arg(points));
}

void AchievementRow::setBadge(const QPixmap& pixmap) {
    if (pixmap.isNull()) return; // Platzhalter-Hintergrund bleibt sichtbar
    badgeLabel_->setPixmap(pixmap);
}

void AchievementRow::setState(State state) {
    state_ = state;
    applyStateStyle();
}

void AchievementRow::setProgress(int hitsCurrent, int hitsTarget) {
    if (hitsTarget <= 1) {
        progressBar_->hide();
        return;
    }
    progressBar_->show();
    progressBar_->setRange(0, hitsTarget);
    progressBar_->setValue(hitsCurrent);
    pointsLabel_->setText(QString("%1/%2").arg(hitsCurrent).arg(hitsTarget));
}

void AchievementRow::setSelected(bool selected) {
    selected_ = selected;
    applyStateStyle();
}

void AchievementRow::applyStateStyle() {
    QColor accent;
    QColor pointsColor;
    double opacity = 1.0;
    switch (state_) {
        case State::Unlocked:
            accent = RawnesColors::kUnlocked;
            pointsColor = RawnesColors::kUnlocked;
            break;
        case State::JustUnlocked:
            accent = RawnesColors::kJustUnlocked;
            pointsColor = RawnesColors::kJustUnlocked;
            break;
        case State::Locked:
            accent = RawnesColors::kTextMuted;
            pointsColor = RawnesColors::kTextMuted;
            opacity = 0.7;
            break;
        case State::Unsupported:
            accent = RawnesColors::kInProgress;
            pointsColor = RawnesColors::kInProgress;
            opacity = 0.5;
            break;
        case State::Waiting:
            // Exakter Hex-Wert aus Python (self.ra_list.itemconfig(idx,
            // fg="#6080a0")) -- "wartet auf bestaetigte Adressen", Schutz
            // gegen Fehlbuchungen. Kein RawnesColors-Konstantenname dafuer
            // bekannt, daher direkt der Python-Hex-Wert.
            accent = QColor("#6080a0");
            pointsColor = QColor("#6080a0");
            opacity = 0.85;
            break;
    }
    leftBar_->setStyleSheet(QString("background: %1;").arg(accent.name()));
    pointsLabel_->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pointsColor.name()));

    QWidget* content = findChild<QWidget*>("rowContent");
    if (content) {
        if (selected_) {
            // Angeklickte Zeile hervorheben: dunklerer Grund + heller
            // Rahmen -- dezent, aber klar als Auswahl erkennbar.
            content->setStyleSheet(QString(
                "#rowContent { background: %1; border: 1px solid %2; border-radius: 4px; }")
                .arg(RawnesColors::kLockedIconBg.name(), RawnesColors::kUnlocked.name()));
        } else {
            content->setStyleSheet(QString(
                "#rowContent { background: %1; border-radius: 4px; }")
                .arg(RawnesColors::kPanel.name()));
        }
        // QGraphicsOpacityEffect statt QWidget::setWindowOpacity(), da
        // Letzteres nur auf Top-Level-Fenster wirkt, nicht auf
        // Kind-Widgets innerhalb einer Liste.
        auto* opacityEffect = qobject_cast<QGraphicsOpacityEffect*>(content->graphicsEffect());
        if (!opacityEffect) {
            opacityEffect = new QGraphicsOpacityEffect(content);
            content->setGraphicsEffect(opacityEffect);
        }
        opacityEffect->setOpacity(opacity);
    }
}

void AchievementRow::mouseDoubleClickEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    emit doubleClicked();
}

void AchievementRow::mousePressEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    emit clicked();
}
