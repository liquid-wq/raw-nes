// RichPresence.h -- Portierung von ra_richpresence_nes.py.
//
// Bewusst minimal, wie im Python-Original: RAs Rich-Presence-Sprache ist
// eine eigene, umfangreiche Skriptsprache (Lookup-Tabellen, Format-
// Funktionen, bedingte Anzeige). Ein voller Interpreter waere ein
// eigenes Projekt und traegt zur Kernfunktion (Achievements) nichts bei.
// Dieses Modul liefert stattdessen einen statischen Aktivitaetstext,
// damit der RA-Ping nicht leer ist (Spiel taucht im Profil auf).
#pragma once

#include <optional>
#include <string>

namespace rawnes {

struct RichPresenceScript {
    bool present = false;
    std::string raw;
};

// Entspricht parse_script() -- merkt sich nur, dass ein Script existiert.
inline RichPresenceScript parseRichPresenceScript(const std::string& script) {
    RichPresenceScript s;
    if (!script.empty()) { s.present = true; s.raw = script; }
    return s;
}

// Entspricht evaluate() -- Platzhalter-Aktivitaetstext. Hier waere der
// Anknuepfpunkt fuer einen echten Interpreter, falls der volle
// Funktionsumfang spaeter gebraucht wird.
inline std::optional<std::string> evaluateRichPresence(const RichPresenceScript& compiled) {
    if (!compiled.present) return std::nullopt;
    return "Spielt (RAW-NES)";
}

} // namespace rawnes
