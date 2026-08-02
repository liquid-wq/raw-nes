#pragma once
#include <QWidget>
#include <map>

class QListWidget;
class QPushButton;

// Leaderboard-Panel: Toggle-Button + Liste (Titel + Status). Entspricht
// lb_toggle_btn/lb_frame/lb_list in der Python-GUI.
//
// WICHTIG (Design-Entscheidung): Das Panel soll sich
// automatisch einblenden, sobald Hardcore aktiviert wird (Leaderboards
// zaehlen nur im Hardcore-Modus). Das wird NICHT hier verdrahtet,
// sondern auf MainWindow-Ebene (HardcorePanel::toggled(true) ->
// LeaderboardPanel::setVisiblePanel(true)) -- sauberere Trennung, dieses
// Widget kennt HardcorePanel nicht.
class LeaderboardPanel : public QWidget {
    Q_OBJECT
public:
    explicit LeaderboardPanel(QWidget* parent = nullptr);

    // Baut die Liste komplett neu auf (bei Spielstart). Jeder Eintrag
    // startet im Status "wartet auf Start".
    void setLeaderboards(const std::vector<std::pair<long long, QString>>& idsAndTitles);

    // Aktualisiert den Statustext einer Zeile (laeuft/abgegeben/nicht
    // unterstuetzt), ohne die Liste neu aufzubauen.
    void setStatus(long long lbId, const QString& statusText);

    // In der Sidebar waere der eigene Toggle-Knopf doppelt gemoppelt --
    // dort uebernimmt der Griff am Fensterrand das Ein-/Ausklappen.
    void setToggleButtonVisible(bool on);

    void setVisiblePanel(bool visible); // zeigt/versteckt die Liste, aktualisiert Button-Text
    bool isPanelVisible() const { return visible_; }

private:
    void updateToggleButtonText();
    void rebuildListItem(long long lbId);

    struct Entry {
        QString title;
        QString status;
        int row = -1;
    };

    QPushButton* toggleBtn_;
    QListWidget* list_;
    std::map<long long, Entry> entries_;
    bool visible_ = false;
};
