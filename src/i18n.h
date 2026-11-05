// Uebersetzung Deutsch -> Englisch, im Code hinterlegt.
//
// Die Tabelle ist wortgleich aus der Qt-Fassung (i18n.h, rawnesTableEn)
// uebernommen. Schluessel ist immer der deutsche Text; fehlt ein Eintrag,
// bleibt der deutsche Text stehen -- wie dort.
//
// Weil Dear ImGui jedes Bild neu zeichnet, wird T() bei jedem Bild frisch
// ausgewertet. Ein Sprachwechsel wirkt damit sofort auf alles Sichtbare; die
// Sonderfaelle der Qt-Fassung (zur Laufzeit zusammengesetzte Texte, die beim
// Umschalten in der alten Sprache stehen blieben) entfallen. Bereits
// geschriebene Protokollzeilen bleiben, wie sie sind.
#pragma once

#include <initializer_list>
#include <string>

namespace i18n {

void SpracheSetzen(const std::string& kennung);  // "de" oder "en"
const std::string& Sprache();
bool Englisch();

// Deutscher Quelltext -> Text in der aktuellen Sprache.
const char* T(const char* deutsch);

// Wie QString::arg: ersetzt %1, %2, ... der Reihe nach.
std::string Fmt(const std::string& muster, std::initializer_list<std::string> werte);

// Zahl als Text, fuer Fmt().
std::string Zahl(long long n);
// Hex mit fester Breite, Kleinbuchstaben, fuehrende Nullen (QString::arg(n, w, 16, '0')).
std::string Hex(unsigned long long n, int breite);

}  // namespace i18n

using i18n::T;
using i18n::Fmt;
