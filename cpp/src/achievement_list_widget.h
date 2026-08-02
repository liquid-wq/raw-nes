#pragma once
#include "achievement_row.h"
#include "badge_loader.h"
#include "ra_client.h"
#include <QWidget>
#include <map>

class QListWidget;
class QListWidgetItem;

// Verwaltet die komplette Achievement-Liste: baut sie aus einem GameData
// auf, lädt Badges nach, und bietet eine einfache API zum Aktualisieren
// des Status einzelner Achievements (freigeschaltet, Fortschritt) während
// des Pollings, ohne die ganze Liste neu aufzubauen.
class AchievementListWidget : public QWidget {
    Q_OBJECT
public:
    explicit AchievementListWidget(QWidget* parent = nullptr);

    // Baut die Liste komplett neu auf (bei Spielstart). already_unlocked:
    // IDs, die laut RA-Server schon freigeschaltet sind (State::Unlocked
    // statt Locked von Anfang an).
    void setGame(const GameData& game, const std::set<long long>& already_unlocked);
    void clear();

    // Aktualisiert eine einzelne Zeile, ohne die Liste neu zu bauen --
    // fuer den Poll-Loop (Fortschritt/Freischaltung waehrend des Spielens).
    void setUnlocked(long long achievementId, bool justNow);
    void setProgress(long long achievementId, int hitsCurrent, int hitsTarget);
    void setUnsupported(long long achievementId);
    // "Wartet auf bestaetigte Adressen" -- Schutz-gegen-Fehlbuchungen-Status
    // aus MonitorWorker. Wird ignoriert, wenn die Zeile schon (Just)Unlocked
    // ist, damit ein spaeter eintreffendes Waiting-Signal keine bereits
    // freigeschaltete Zeile zuruecksetzt.
    void setWaiting(long long achievementId, bool waiting);

    // Fuer das Freischalt-Popup, das nur Titel, Punkte und ID aus dem
    // unlocked-Signal bekommt. badgeFor() ist bewusst nicht const: der
    // BadgeLoader laedt beim ersten Zugriff nach und schreibt seinen Cache.
    QString descriptionFor(long long achievementId) const;
    QPixmap badgeFor(long long achievementId, int maxWidth = 128);

signals:
    // Fuer Doppelklick -> "auf RetroAchievements oeffnen" (die GUI baut
    // daraus die URL, dieses Widget kennt RA-URLs nicht selbst).
    void achievementDoubleClicked(long long achievementId);
    // Fuer Einfachklick -> Diagnose (entspricht Pythons
    // _erklaere_achievement_click). Die GUI/der Worker liefert den Text.
    void achievementClicked(long long achievementId);

private:
    struct RowEntry {
        AchievementRow* row;
        QListWidgetItem* item;
        QString desc;      // Beschreibung fuers Popup
        QString badgeName; // Basisname OHNE "_lock"
    };

    QListWidget* list_;
    BadgeLoader badges_;
    std::map<long long, RowEntry> rows_;
    long long selectedId_ = -1; // aktuell hervorgehobene Zeile (-1 = keine)
};
