// Windows-Anbindung fuer die Oberflaeche: Programmordner, Datei- und
// Ordnerauswahl, Webseiten oeffnen.
//
// Alle Pfade sind UTF-8 -- so, wie die Qt-Fassung sie mit toStdString()
// weitergab und wie std::filesystem sie liest.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace plattform {

std::string Utf8(const std::wstring& w);
std::wstring Breit(const std::string& s);

// Ordner der laufenden Exe (QCoreApplication::applicationDirPath()).
std::string ExeOrdner();

// Ordnerauswahl (QFileDialog::getExistingDirectory). Leer bei Abbruch.
std::string OrdnerWaehlen(void* fenster, const std::string& titel, const std::string& start = std::string());

// Dateiauswahl (QFileDialog::getOpenFileName). filter: Paare aus
// Beschreibung und Muster ("*.nes;*.zip"). Leer bei Abbruch.
std::string DateiWaehlen(void* fenster, const std::string& titel,
                         const std::vector<std::pair<std::string, std::string>>& filter);

// QDesktopServices::openUrl
void UrlOeffnen(const std::string& url);

}  // namespace plattform
