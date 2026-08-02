#pragma once
#include <QWidget>
#include <QColor>
#include <QDateTime>
#include <map>
#include <vector>

class QLabel;
class QScrollArea;
class QTimer;

// RAM-Live-Anzeige im MEGA-RAW-Stil, aber dreispaltig: ein RichText-QLabel
// in einem Scrollbereich, das alle beobachteten Adressen als Hex-Paare
// rendert. Geaenderte Werte leuchten gold auf und glimmen ueber 1s zum
// Ruhe-Gruen zurueck.
//
// Warum dreispaltig statt einer Adresse pro Zeile (frueherer Stand):
// eine Zeile brauchte rund 100 von 700 Pixel Breite, waehrend die Liste
// vertikal aus dem Fenster lief und ohne Scrollbereich einfach abgeschnitten
// wurde. Drei Spalten zeigen bei gleicher Hoehe dreimal so viele Werte.
//
// Oeffentliches Interface (clearAll/addAddress/updateValue) unveraendert,
// damit MainWindow nicht angepasst werden muss.
class LiveValuesWidget : public QWidget {
    Q_OBJECT
public:
    explicit LiveValuesWidget(QWidget* parent = nullptr);

    void clearAll();
    void addAddress(int slot, uint16_t addr);
    void updateValue(int slot, uint8_t value, int changeCount);

private:
    void rebuildHtml();
    // Startet/stoppt den Nachzieh-Timer je nachdem, ob gerade ein Wert
    // am Abglimmen ist. Ohne ihn wurde die Farbe nur bei einem neuen
    // Wert-Update berechnet -- eine Adresse, die sich einmal aenderte und
    // dann ruhte, blieb dauerhaft gold stehen statt zurueckzufaden.
    void updateFadeTimer();

    struct Row {
        uint16_t addr = 0;
        int value = -1;          // -1 = noch kein Wert
        int changeCount = 0;
        qint64 lastChangeMs = 0; // fuer den gold->gruen-Fade
    };

    QScrollArea* scroll_;
    QLabel* label_;
    QTimer* fadeTimer_;
    std::map<int, Row> rows_;
};
