// rawnes_gui_config.json neben der Exe -- dieselbe Datei wie in der
// Qt-Fassung. Eine vorhandene Konfiguration wird weiterbenutzt.
#pragma once

#include <map>
#include <string>

struct Einstellungen {
    std::string raUser;
    // Gespeichert wird nur der Sitzungsschluessel, nie das Passwort.
    std::string raToken;
    // Passwort aus einer aelteren Konfiguration (Qt-Fassung, "ra_pass"). Wird
    // nur gelesen, um einmal anzumelden; beim naechsten Speichern faellt es weg.
    std::string raPassAlt;
    bool rememberLogin = true;
    std::string language = "de";
    std::string sdRoot;
    std::string coresDir;   // nur gesetzt, wenn bewusst gewaehlt
    long long messageRead = 0;   // zuletzt gezeigte Nachricht (nachricht_rawnes.json)
    std::map<std::string, std::string> remembered;  // Fingerabdruck -> Quellpfad
    bool dateiDa = false;   // false: erster Start (Ersteinrichtung)
    // Ersteinrichtung bis zum Ende durchlaufen. Fehlt der Eintrag in einer
    // vorhandenen Datei (aeltere Fassung), gilt sie als erledigt.
    bool einrichtungFertig = true;

    // Fehlende oder kaputte Datei: Standardwerte.
    void Laden(const std::string& pfad);
    void Speichern(const std::string& pfad) const;
};
