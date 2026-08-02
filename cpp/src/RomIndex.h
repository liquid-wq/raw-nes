// RomIndex.h -- Portierung von rom_index.py.
//
// .nes und .zip werden unterstuetzt (ZIP: eigener minimaler Central-
// Directory-Parser + zlib fuer Deflate-Eintraege -- kein externer
// Zip-Reader noetig, via miniz). .7z/.rar/.tar/.gz werden ueber eine
// extern installierte 7-Zip-EXE entpackt (7z.exe/7za.exe, per
// Prozessaufruf -- 7-Zip kann RAR mit-entpacken, kein Extra-Tool
// noetig). Entspricht py7zr im Python-Original (rom_index.py), das
// .7z ebenfalls unterstuetzt. Ist keine 7-Zip-Installation auffindbar,
// wird die Archivdatei uebersprungen und gezaehlt (skippedArchives) --
// die GUI zeigt dann einen Hinweis "bitte 7-Zip installieren".
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rawnes {

struct RomIndexEntry {
    std::string name;
    std::string source;    // Dateipfad (bei ZIP/Archiv: Pfad des Archivs, nicht des Eintrags)
    std::string vec6Hex;    // 6 Vektorbytes als Hex
    std::string md5;
    std::string pageHex;    // 256-Byte letzte Seite als Hex, leer wenn ROM zu klein
    int mapper = -1;
    int prg = -1;
    int chr = -1;
    int mirror = -1;
    int four = -1;
};

// index_key(vec6) -- nur NMI+Reset (erste 4 Bytes), IRQ-Vektor absichtlich
// ausgeklammert: MMC1-Titel wie Metroid nutzen keine IRQs, dort bleibt der
// Vektor auf dem Init-Wert.
using RomIndexMap = std::map<std::string, std::vector<RomIndexEntry>>;

std::optional<std::array<uint8_t, 6>> vectorsFromInes(const std::vector<uint8_t>& data);
std::optional<std::array<uint8_t, 256>> lastPageFromInes(const std::vector<uint8_t>& data);

struct InesHeaderInfo {
    int mapper = -1, prg = -1, chr = -1, mirror = -1, four = -1;
};
std::optional<InesHeaderInfo> inesHeaderInfo(const std::vector<uint8_t>& data);

std::string inesMd5(const std::vector<uint8_t>& data);
std::string indexKey(const std::array<uint8_t, 6>& vec6);

// Baut den Index durch rekursives Durchsuchen von `base` (.nes direkt,
// .nes in .zip/.7z/.rar/.tar/.gz). `skippedArchives` zaehlt Archive,
// die mangels installierter 7-Zip-EXE nicht gelesen werden konnten.
// `progress` wird nach jedem indizierten ROM aufgerufen (Anzahl bisher).
RomIndexMap buildIndex(const std::string& base, int* skippedArchives = nullptr,
                       const std::function<void(int, const std::string&)>& progress = nullptr);

void saveIndex(const RomIndexMap& index, const std::string& path);
RomIndexMap loadIndex(const std::string& path);

// Kandidaten ueber die erfasste PRG-Page eingrenzen. seenBits: 256 Werte,
// je Position 0/1 (welche der 256 Snooper-Bytes tatsaechlich gelesen
// wurden). Gibt (eingegrenzte_oder_urspruengliche_Liste, Anzahl_geprueft) zurueck.
std::pair<std::vector<RomIndexEntry>, int> matchPage(
    const std::vector<RomIndexEntry>& treffer,
    const std::array<uint8_t, 256>& pageBytes,
    const std::array<uint8_t, 256>& seenBits);

std::vector<RomIndexEntry> lookup(const RomIndexMap& index,
                                  const std::array<uint8_t, 6>& vec6);

// Fuer die manuelle Einzeldatei-Auswahl ("ROM waehlen..."): entpackt die
// EINE .nes-Datei aus einem Archiv (.zip via miniz, .7z/.rar/.tar/.gz
// via externe 7z.exe). Leerer Vektor, wenn das Archiv keine oder mehr
// als eine .nes-Datei enthaelt, oder (bei .7z/.rar/...) keine 7-Zip-
// Installation gefunden wurde. `outError` bekommt bei leerem Ergebnis
// einen kurzen, menschenlesbaren Grund (fuer die GUI-Logausgabe).
std::vector<uint8_t> extractSingleNesFromArchive(const std::string& archivePath,
                                                  std::string* outError = nullptr);

} // namespace rawnes
