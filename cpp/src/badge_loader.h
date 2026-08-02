#pragma once
#include <QString>
#include <QPixmap>
#include <QHash>

// auf ~32px skaliert. Leeres QPixmap bei Fehler/ohne Badge.
class BadgeLoader {
public:
    // maxWidth: gewuenschte Zielbreite. Das Freischalt-Popup zeichnet das
    // Badge deutlich groesser als die 32 px der Listenzeile, deshalb geht die
    // Groesse in den Speicher-Cache-Schluessel ein -- sonst kaeme fuer beide
    // Aufrufer dasselbe (kleine) Pixmap zurueck. Die Datei im badge_cache
    // bleibt unveraendert in Originalgroesse liegen.
    QPixmap get(const QString& badgeName, int maxWidth = 32);
private:
    QHash<QString, QPixmap> mem_;
};
