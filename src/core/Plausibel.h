// Pruefung einer Abfrage, bevor sie ausgewertet wird -- Schutz gegen falsche
// Freischaltungen, wenn die Konsole ausgeschaltet oder das Spiel verlassen
// wird. Eigene Datei, damit RAW-NES und der Pruefstand denselben Code nutzen.
#pragma once

#include "ConditionEngine.h"

#include <deque>
#include <optional>
#include <string>

namespace rawnes {

// buildGelesen: Build-Kennung des Kerns in dieser Abfrage, -1 = nicht lesbar.
// buildErwartet: beim Start gelesene Kennung, -1 = unbekannt.
// Rueckgabe: leer = plausibel, sonst ein kurzer Grund fuers Log
// ("build ?", "build 0b", "FF", "00").
std::string unplausibelGrund(int buildGelesen, int buildErwartet, const RamMap& ram);

// Wie viele plausible Abfragen eine Abfrage abwarten muss, bevor sie
// ausgewertet wird. Faellt in dieser Zeit die Verbindung weg oder kommt eine
// unplausible Abfrage, werden alle wartenden verworfen. Gewaehlt mit einem
// Pruefstand, der aufgezeichnete Abfragen mit eingespielten Stoerungen auswertet.
constexpr int kAuswerteVerzoegerung = 3;

// Haelt plausible Abfragen zurueck, bis `verzoegerung` weitere plausible
// gefolgt sind; die Reihenfolge bleibt erhalten, es geht keine verloren.
class Abfragefilter {
public:
    explicit Abfragefilter(int verzoegerung = kAuswerteVerzoegerung) : k_(verzoegerung) {}
    // Neue plausible Abfrage; liefert die aelteste, die jetzt ausgewertet
    // werden darf (oder nichts, solange noch gewartet wird).
    std::optional<RamMap> weiter(const RamMap& ram);
    // Unplausible Abfrage, Verbindungsabbruch, Neustart: alles Wartende weg.
    void verwerfen() { warte_.clear(); }
    size_t wartend() const { return warte_.size(); }

private:
    int k_;
    std::deque<RamMap> warte_;
};

}  // namespace rawnes
