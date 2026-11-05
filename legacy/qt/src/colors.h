#pragma once
#include <QColor>

// Farbpalette aus einem echten NES-Foto (EU-Version) extrahiert.
// Grün/Rot für Achievement-Status bleiben bewusst nicht konsolentreu --
// Statusfarbcodierung (frei/gesperrt) ist wichtiger als reine
// Optik-Treue.
namespace RawnesColors {
    inline const QColor kLight      = QColor("#d7d3cc"); // Gehäuse oben (Header)
    inline const QColor kDark       = QColor("#534c4d"); // Gehäuse unten (Hauptfläche)
    inline const QColor kPanel      = QColor("#3f3a3b"); // abgesetzte Flächen (Zeilen, Log)
    inline const QColor kRed        = QColor("#790d10"); // Nintendo-Logo, Akzent/Warnung
    inline const QColor kTextMain   = QColor("#d7d3cc"); // Haupttext auf dunklem Grund
    inline const QColor kTextMuted  = QColor("#a9a49c"); // Nebentext (Beschreibungen)

    inline const QColor kUnlocked      = QColor("#6fbf6f"); // freigeschaltet
    inline const QColor kInProgress    = QColor("#dd9999"); // Hit-Count läuft
    inline const QColor kLockedIconBg  = QColor("#2a2726"); // Platzhalter-Hintergrund gesperrtes Badge
    inline const QColor kJustUnlocked  = QColor("#ffd75e"); // goldener Blitz beim Freischalten (kurzzeitig)
}
