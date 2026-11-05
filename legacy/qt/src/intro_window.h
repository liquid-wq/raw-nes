#pragma once
#include <QPixmap>
#include <QRect>
#include <QVector>
#include <QWidget>

class QTimer;
class QElapsedTimer;
class QEventLoop;
class QPushButton;

// Natives Intro (QPainter) -- 1:1-Portierung der Choreografie aus
// rawnes_intro.html (HTML5-Canvas). Alle Zeiten, Groessen, Farben und
// Kurven stammen direkt aus dem dortigen JavaScript; nichts davon ist
// geschaetzt.
//
// Der fruehere Stand dieser Datei war ausdruecklich eine Annaeherung
// (ein einzelnes Standbild, geratene Timings, rote Palette). Er ist
// vollstaendig ersetzt.
//
// Kernpunkte der Vorlage:
//  - EINE Uhr t in ms steuert Text und Katze gemeinsam (haelt das
//    Bruellen synchron mit dem Buchstaben "R" bei t=800ms).
//  - 12 Katzen-Frames mit eigener Geometrie (FMETA): Ankerpunkt ist
//    NICHT die Bildmitte, sondern cx/bottom -- die Katze steht damit
//    bodenverankert, egal wie unterschiedlich gross die Frames sind.
//  - Textverlauf: RAW -> RAWNESS -> RAWNES -> Cursor wandert nach
//    links -> RAW-NES.
class IntroWindow : public QWidget {
    Q_OBJECT
public:
    // Groesse des Intro-Fensters -- bewusst identisch zum Hauptfenster,
    // damit das Intro genau dort und genau so gross erscheint, wo danach
    // die GUI aufgeht (kein Sprung zwischen beiden).
    static constexpr int kWinW = 760;
    static constexpr int kWinH = 900;

    // Zeigt das Intro modal (blockiert, bis die Animation durch ist oder
    // der Nutzer klickt, eine Taste drueckt oder SKIP waehlt).
    // Rueckgabe: die tatsaechlich benutzte Fenstergeometrie -- main.cpp
    // setzt sie auf das Hauptfenster, damit es an derselben Stelle
    // erscheint.
    static QRect showBlocking();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void tick();
    void startSequence();
    void finish();

    struct Star { double x, y, z, s; };

    QVector<QPixmap> frames_;
    QVector<Star> stars_;
    QElapsedTimer* clock_ = nullptr;
    QTimer* timer_ = nullptr;
    QEventLoop* loop_ = nullptr;
    QPushButton* skipBtn_ = nullptr;
    double shockT_ = -1.0;   // Startzeit der Shockwave, <0 = noch nicht ausgeloest
    double shockX_ = 0.0, shockY_ = 0.0;
    bool running_ = false;
};
