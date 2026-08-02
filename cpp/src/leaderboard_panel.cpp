#include "leaderboard_panel.h"
#include "colors.h"

#include <QListWidget>
#include <QSizePolicy>
#include <QPushButton>
#include <QVBoxLayout>

LeaderboardPanel::LeaderboardPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    toggleBtn_ = new QPushButton(this);
    toggleBtn_->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: none; padding: 4px 10px; text-align: left; }")
        .arg(RawnesColors::kPanel.name(), RawnesColors::kTextMain.name()));
    layout->addWidget(toggleBtn_);

    list_ = new QListWidget(this);
    list_->setStyleSheet(QString(
        "QListWidget { background: %1; color: %2; border: none; font-size: 11px; }")
        .arg(RawnesColors::kPanel.name(), RawnesColors::kTextMain.name()));
    // Frueher setFixedHeight(90) (~3-4 Zeilen, wie Pythons height=4). In der
    // Sidebar ist die volle Fensterhoehe verfuegbar -- dort waere eine feste
    // Hoehe reine Verschwendung, bei 37 Leaderboards muesste man staendig
    // scrollen. Mindesthoehe bleibt, damit das Panel im Fluss nicht kollabiert.
    list_->setMinimumHeight(90);
    list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    list_->hide(); // standardmaessig eingeklappt, wie im Original
    layout->addWidget(list_);

    connect(toggleBtn_, &QPushButton::clicked, this, [this]() { setVisiblePanel(!visible_); });
    updateToggleButtonText();
}

void LeaderboardPanel::updateToggleButtonText() {
    toggleBtn_->setText(visible_ ? tr("Leaderboards ausblenden") : tr("Leaderboards anzeigen"));
}

void LeaderboardPanel::setVisiblePanel(bool visible) {
    visible_ = visible;
    list_->setVisible(visible_);
    updateToggleButtonText();
}

void LeaderboardPanel::setLeaderboards(
    const std::vector<std::pair<long long, QString>>& idsAndTitles) {
    list_->clear();
    entries_.clear();
    for (const auto& [id, title] : idsAndTitles) {
        Entry e;
        // Liefert RA fuer ein Spiel keinen Leaderboard-Titel, stand hier
        // vorher eine leere Zeile mit fuehrendem " -- ". Dann lieber die ID
        // zeigen, damit der Eintrag ueberhaupt unterscheidbar bleibt.
        e.title = title.trimmed().isEmpty() ? tr("Leaderboard #%1").arg(id) : title;
        e.status = tr("wartet auf Start");
        entries_[id] = e;
        rebuildListItem(id);
    }
}

void LeaderboardPanel::rebuildListItem(long long lbId) {
    auto it = entries_.find(lbId);
    if (it == entries_.end()) return;
    QString text = QString("%1 -- %2").arg(it->second.title, it->second.status);
    if (it->second.row >= 0 && it->second.row < list_->count()) {
        list_->item(it->second.row)->setText(text);
    } else {
        it->second.row = list_->count();
        list_->addItem(text);
    }
}

void LeaderboardPanel::setStatus(long long lbId, const QString& statusText) {
    auto it = entries_.find(lbId);
    if (it == entries_.end()) return;
    it->second.status = statusText;
    rebuildListItem(lbId);
}

void LeaderboardPanel::setToggleButtonVisible(bool on) {
    if (toggleBtn_) toggleBtn_->setVisible(on);
}
