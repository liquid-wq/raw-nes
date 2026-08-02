#pragma once
#include <QPixmap>
#include <QWidget>

class QLabel;
class QProgressBar;
class QFrame;

// Eine Achievement-Zeile: Badge-Bild, Titel, Beschreibung, Punkte,
// Fortschrittsbalken bei Hit-Count-Achievements. Bildstärker als reiner
// Text, aber übersichtlicher als eine Kachel-Grid-Ansicht bei Spielen mit
// vielen Achievements.
//
// Wird per QListWidget::setItemWidget() in eine QListWidget-Zeile gesetzt
// (Standard-Qt-Muster für reichhaltige Listenzeilen mit eingebautem
// Scrollen/Auswählen).
class AchievementRow : public QWidget {
    Q_OBJECT
public:
    // Waiting neu ergaenzt (Python-Pendant: blaue Zeile "#6080a0" waehrend
    // die Bedingung auf bestaetigte Adressen wartet -- SICHERUNG GEGEN
    // FEHLBUCHUNGEN, siehe MonitorWorker). Nur gesetzt, solange nicht
    // Unlocked/JustUnlocked.
    enum class State { Locked, Unlocked, JustUnlocked, Unsupported, Waiting };

    explicit AchievementRow(QWidget* parent = nullptr);

    void setContent(const QString& title, const QString& desc, int points);
    void setBadge(const QPixmap& pixmap);
    void setState(State state);

    // Hebt die aktuell angeklickte Zeile optisch hervor (heller Rahmen).
    // Kein Signal/Slot -> keine MOC-Neugenerierung noetig.
    void setSelected(bool selected);

    // hitsTarget <= 1: kein Fortschrittsbalken (Ziel ist "nur einmal wahr").
    // hitsTarget > 1: zeigt "current/target" + Balken.
    void setProgress(int hitsCurrent, int hitsTarget);

    State state() const { return state_; }

signals:
    void doubleClicked();
    // Einfachklick -- Diagnose (entspricht Pythons _erklaere_achievement_click).
    void clicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void applyStateStyle();

    QFrame* leftBar_;
    QLabel* badgeLabel_;
    QLabel* titleLabel_;
    QLabel* descLabel_;
    QLabel* pointsLabel_;
    QProgressBar* progressBar_;
    State state_ = State::Locked;
    bool selected_ = false;
};
