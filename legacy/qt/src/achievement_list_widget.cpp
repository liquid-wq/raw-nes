#include "achievement_list_widget.h"
#include "colors.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

AchievementListWidget::AchievementListWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setStyleSheet(QString(
        "QListWidget { background: %1; border: none; }"
        "QListWidget::item { border: none; padding: 2px 0; }")
        .arg(RawnesColors::kDark.name()));
    layout->addWidget(list_);
}

void AchievementListWidget::clear() {
    list_->clear();
    rows_.clear();
    selectedId_ = -1;
}

void AchievementListWidget::setGame(const GameData& game,
                                    const std::set<long long>& already_unlocked) {
    clear();
    for (const auto& ac : game.achievements) {
        auto* row = new AchievementRow();
        row->setContent(QString::fromStdString(ac.title),
                        QString::fromStdString(ac.desc), ac.points);

        bool unlocked = already_unlocked.count(ac.id) > 0;
        row->setState(unlocked ? AchievementRow::State::Unlocked
                               : AchievementRow::State::Locked);

        if (!ac.badge.empty()) {
            QString badgeName = QString::fromStdString(ac.badge);
            QPixmap pm = badges_.get(unlocked ? badgeName : badgeName + "_lock");
            row->setBadge(pm);
        }

        auto* item = new QListWidgetItem(list_);
        item->setSizeHint(QSize(0, 56));
        item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(ac.id));
        list_->addItem(item);
        list_->setItemWidget(item, row);

        connect(row, &AchievementRow::doubleClicked, this, [this, id = ac.id]() {
            emit achievementDoubleClicked(id);
        });
        connect(row, &AchievementRow::clicked, this, [this, id = ac.id]() {
            // Vorherige Auswahl aufheben, diese Zeile hervorheben.
            if (selectedId_ != -1) {
                auto prev = rows_.find(selectedId_);
                if (prev != rows_.end()) prev->second.row->setSelected(false);
            }
            auto cur = rows_.find(id);
            if (cur != rows_.end()) cur->second.row->setSelected(true);
            selectedId_ = id;
            emit achievementClicked(id);
        });

        rows_[ac.id] = {row, item, QString::fromStdString(ac.desc),
                        QString::fromStdString(ac.badge)};
    }
}

void AchievementListWidget::setUnlocked(long long achievementId, bool justNow) {
    auto it = rows_.find(achievementId);
    if (it == rows_.end()) return;
    it->second.row->setState(justNow ? AchievementRow::State::JustUnlocked
                                     : AchievementRow::State::Unlocked);
}

void AchievementListWidget::setProgress(long long achievementId, int hitsCurrent, int hitsTarget) {
    auto it = rows_.find(achievementId);
    if (it == rows_.end()) return;
    it->second.row->setProgress(hitsCurrent, hitsTarget);
}

void AchievementListWidget::setUnsupported(long long achievementId) {
    auto it = rows_.find(achievementId);
    if (it == rows_.end()) return;
    it->second.row->setState(AchievementRow::State::Unsupported);
}

void AchievementListWidget::setWaiting(long long achievementId, bool waiting) {
    auto it = rows_.find(achievementId);
    if (it == rows_.end()) return;
    auto cur = it->second.row->state();
    if (cur == AchievementRow::State::Unlocked || cur == AchievementRow::State::JustUnlocked) {
        return; // schon freigeschaltet -- nicht zurueckfallen lassen
    }
    it->second.row->setState(waiting ? AchievementRow::State::Waiting
                                     : AchievementRow::State::Locked);
}

QString AchievementListWidget::descriptionFor(long long achievementId) const {
    auto it = rows_.find(achievementId);
    if (it == rows_.end()) return QString();
    return it->second.desc;
}

QPixmap AchievementListWidget::badgeFor(long long achievementId, int maxWidth) {
    auto it = rows_.find(achievementId);
    if (it == rows_.end()) return QPixmap();
    // Immer die ungesperrte Variante: in der Liste steht bei gesperrten
    // Zeilen das "_lock"-Bild, im Popup ist das freigeschaltete gemeint.
    return badges_.get(it->second.badgeName, maxWidth);
}
