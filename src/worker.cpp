#include "worker.h"

#include "Plausibel.h"

#include "i18n.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>

using namespace RawnesAddr;
namespace fs = std::filesystem;

namespace {
// Vektor-/Page-Offsets fuer die automatische Erkennung (rawnes_gui_v3_final.py).
constexpr uint32_t kVecOffset = 0x0800;
constexpr uint32_t kPageOffset = 0x0900;
constexpr uint32_t kSeenOffset = 0x0A00;
// Schreibmarkierungs-Bits (ab FPGA-Build 9).
constexpr uint32_t kRamMarkOffset = 0x1000;
// Wertespiegel des Cartridge-WRAM $6000-$7FFF und dessen Schreibmarkierungen.
constexpr uint32_t kWramOffset = 0x2000;
constexpr uint32_t kWramMarkOffset = 0x4000;

// Offset einer NES-Adresse im Snoop-Fenster -- 1:1 zu Pythons snoop_offset().
std::optional<uint32_t> snoopOffset(uint32_t nesAddr) {
    if (nesAddr <= 0x07FF) return nesAddr;
    if (nesAddr >= 0x6000 && nesAddr <= 0x7FFF) return kWramOffset + (nesAddr - 0x6000);
    return std::nullopt;
}

std::string Upper(std::string s) {
    for (char& c : s) c = (char)::toupper((unsigned char)c);
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    return s;
}

long long MsSeit(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t).count();
}

}  // namespace

MonitorWorker::MonitorWorker(std::shared_ptr<RaClient> client) : client_(std::move(client)) {
    thread_ = std::thread([this]() { run(); });
}

MonitorWorker::~MonitorWorker() {
    stop();
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        quit_ = true;
        queue_.clear();
    }
    queueCv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (ed_) ed_->close();
}

void MonitorWorker::post(std::function<void()> auftrag) {
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        queue_.push_back(std::move(auftrag));
    }
    queueCv_.notify_one();
}

void MonitorWorker::run() {
    for (;;) {
        std::function<void()> auftrag;
        {
            std::unique_lock<std::mutex> l(queueMutex_);
            const bool etwas = queueCv_.wait_for(l, std::chrono::seconds(3), [this]() {
                return quit_ || !queue_.empty() || !diagnoseQueue_.empty();
            });
            if (quit_) return;
            if (!etwas) {
                l.unlock();
                pruefeLeerlauf();
                continue;
            }
            if (!queue_.empty()) {
                auftrag = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        runPendingDiagnose();
        if (auftrag) {
            try {
                auftrag();
            } catch (const std::exception& e) {
                emitLog(std::string("Fehler: ") + e.what());
            }
        }
    }
}

// Ohne laufenden Monitor alle 3 s nachsehen, ob unser Kern laeuft. Meldet nur
// Aenderungen. Konsole aus und EverDrive-Menue sind dabei nicht zu
// unterscheiden: in beiden Faellen laeuft der Kern nicht, das EverDrive
// antwortet aber weiter ueber USB (Messung 04.11.2026).
void MonitorWorker::pruefeLeerlauf() {
    if (!ed_ || running_) return;
    // Systeminfo (CMD_SYS_INF): liefert das EverDrive nur bei eingeschalteter
    // Konsole (Messung 05.11.2026: im Menue pwr_sys=1, bei ausgeschalteter
    // Konsole keine Antwort). Bleibt sie aus, ist die alte Verbindung
    // ausserdem tot -- einmal neu oeffnen und erneut fragen.
    Bytes si = ed_->sysInfoNoRecover();
    if (si.size() != 64 && ed_->neuOeffnen()) si = ed_->sysInfoNoRecover();
    int zustand = 3;   // 3 = Konsole aus
    if (si.size() == 64) {
        zustand = 1;   // 1 = EverDrive-Menue (kein Spiel mit unserem Kern)
        try {
            Bytes b = ed_->memrdNoRecover(snoopBase_ + kBuildOffset, 1);
            if (!b.empty() && (b[0] == kFpgaBuildExpected || b[0] == 11)) zustand = 2;   // Spiel laeuft
        } catch (const std::exception&) {}
    }
    if (zustand != leerlaufZustand_) {
        leerlaufZustand_ = zustand;
        if (leerlaufStatus) leerlaufStatus(zustand);
    }
}

void MonitorWorker::runPendingDiagnose() {
    std::deque<long long> ids;
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        ids.swap(diagnoseQueue_);
    }
    for (long long id : ids) doDiagnose(id);
}

// ================= oeffentliche Auftraege =================

void MonitorWorker::connectToDevice(const std::string& preferred) {
    post([this, preferred]() { doConnect(preferred); });
}

void MonitorWorker::seedRemembered(const std::map<std::string, std::string>& remembered) {
    post([this, remembered]() {
        for (const auto& kv : remembered) remembered_[kv.first] = kv.second;
    });
}

void MonitorWorker::startMonitoring(std::shared_ptr<rawnes::RomIndexMap> romIndex,
                                    const std::string& user, const std::string& token, bool hardcore) {
    post([this, romIndex, user, token, hardcore]() {
        doStartMonitoring(romIndex, user, token, hardcore);
    });
}

void MonitorWorker::continueWithPath(const std::string& path) {
    post([this, path]() { doContinueWithPath(path); });
}

void MonitorWorker::continueWithChosenRom(const std::string& hexKey, const std::string& source) {
    post([this, hexKey, source]() { doContinueWithChosenRom(hexKey, source); });
}

void MonitorWorker::diagnoseAchievement(long long achievementId) {
    {
        std::lock_guard<std::mutex> l(queueMutex_);
        diagnoseQueue_.push_back(achievementId);
    }
    queueCv_.notify_one();
}

void MonitorWorker::stop() {
    running_ = false;
}

// ================= Verbindung (einmalig, unabhaengig vom Spielstart) =================

void MonitorWorker::doConnect(const std::string& preferred) {
    // Eine noch offene alte Verbindung (z. B. nach dem Ausschalten der
    // Konsole) haelt den Anschluss belegt -- vorher schliessen.
    if (ed_) {
        try { ed_->close(); } catch (...) {}
        ed_.reset();
    }
    if (connectAttempt) connectAttempt(1, 1);
    auto [edPtr, foundPort] = find_everdrive(preferred);
    if (!edPtr) {
        // foundPort enthaelt hier die Gruende je Anschluss.
        if (connectionFailed)
            connectionFailed(std::string("EverDrive nicht gefunden -- angeschlossen? "
                                         "Anderes Programm auf dem COM-Port?") + foundPort);
        return;
    }
    ed_ = std::move(edPtr);
    port_ = foundPort;
    leerlaufZustand_ = 0;
    if (connected) connected(port_);
    emitLog(std::string(T("Verbunden auf ")) + port_ + T("."));

    // FPGA-Build-Check direkt nach dem Verbinden, beide moegliche
    // Snoop-Basisadressen probieren (0x1808000 zuerst, dann 0x1804000).
    fpgaBuild_ = -1;
    for (uint32_t candidate : {kSnoopBase, kSnoopBaseAlt}) {
        try {
            // memrdNoRecover: Fehlschlag ist hier ein erwarteter Normalfall
            // (Konsole im EverDrive-Menue -> MCU-DMA blockiert den Bus).
            Bytes b = ed_->memrdNoRecover(candidate + kBuildOffset, 1);
            int build = b[0];
            // 0xFF und 0x00 sind Leerlaufwerte eines nicht antwortenden Busses.
            if (build == 0xFF || build == 0x00) continue;
            if (build == kFpgaBuildExpected || build == 11) {   // 11 = aeltere Kerne ohne Bildpuffer
                if (candidate != snoopBase_) {
                    emitLog(Fmt(T("Snoop-Region liegt bei 0x%1 (nicht 0x%2)."),
                                {i18n::Hex(candidate, 7), i18n::Hex(snoopBase_, 7)}));
                    snoopBase_ = candidate;
                }
                fpgaBuild_ = build;
                break;
            }
            if (fpgaBuild_ < 0) fpgaBuild_ = build;  // erster erfolgreicher Read als Anzeige
        } catch (const std::exception&) {
            // dieser Kandidat nicht erreichbar, naechsten probieren
        }
    }
    if (fpgaBuild_ >= 0) {
        if (fpgaBuild_ != kFpgaBuildExpected) {
            emitLog(Fmt(T("ACHTUNG: FPGA-Build %1 erwartet, %2 gefunden -- aktuelle top.rbf deployt?"),
                        {i18n::Zahl(kFpgaBuildExpected), i18n::Zahl(fpgaBuild_)}));
        } else {
            emitLog(Fmt(T("FPGA-Build %1 bestätigt."), {i18n::Zahl(fpgaBuild_)}));
        }
    } else {
        emitLog(T("FPGA-Build konnte nicht gelesen werden (Menü statt Spiel? "
                  "Bekanntes DMA-Verhalten, kein Fehler)."));
    }
}

// ================= Automatische Erkennung =================

std::optional<MonitorWorker::MapCfg> MonitorWorker::readMapCfg() {
    // Layout aus krikzz base_sv/sys_cfg.sv (siehe Python _read_map_cfg()).
    try {
        Bytes raw = ed_->memrd(kCfgBase, 16);
        if (raw.size() < 5) return std::nullopt;
        const uint8_t r0 = raw[0], r1 = raw[1], r2 = raw[2], r4 = raw[4];
        MapCfg c;
        c.mapper = r0 | ((r2 >> 4) << 8);
        c.prgShift = r1 & 0x0F;
        c.chrShift = r2 & 0x0F;
        c.chrRam = (r4 & 0x04) != 0;
        c.mirror = r4 & 0x03;
        return c;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int MonitorWorker::shiftFor(int units16k) {
    // sys_cfg.sv: maske = (1<<shift)-1; PRG in 8-KB-Baenken -> units16k * 2.
    if (units16k <= 0) return -1;
    const int banks = units16k * 2;
    int shift = 0;
    while ((1 << shift) < banks) ++shift;
    return shift;
}

std::vector<rawnes::RomIndexEntry> MonitorWorker::filterByMapper(
    std::vector<rawnes::RomIndexEntry> treffer) {
    auto cfg = readMapCfg();
    if (!cfg) {
        emitLog(T("  Mapper-Konfiguration nicht lesbar -- Filter übersprungen."));
        return treffer;
    }
    emitLog(Fmt(T("  laufendes Spiel: Mapper %1, PRG-Shift %2, CHR-Shift %3, CHR-RAM %4"),
                {i18n::Zahl(cfg->mapper), i18n::Zahl(cfg->prgShift), i18n::Zahl(cfg->chrShift),
                 cfg->chrRam ? T("ja") : T("nein")}));

    std::vector<rawnes::RomIndexEntry> passend;
    for (auto& t : treffer) if (t.mapper == cfg->mapper) passend.push_back(t);
    if (!passend.empty() && passend.size() < treffer.size()) {
        emitLog(Fmt(T("  Mapper %1 grenzt auf %2 ein."),
                    {i18n::Zahl(cfg->mapper), i18n::Zahl((long long)passend.size())}));
        treffer = passend;
    }

    if (treffer.size() > 1) {
        std::vector<rawnes::RomIndexEntry> enger;
        for (auto& t : treffer) if (shiftFor(t.prg) == cfg->prgShift) enger.push_back(t);
        if (!enger.empty() && enger.size() < treffer.size()) {
            emitLog(Fmt(T("  PRG-Größe (Shift %1) grenzt auf %2 ein."),
                        {i18n::Zahl(cfg->prgShift), i18n::Zahl((long long)enger.size())}));
            treffer = enger;
        }
    }
    if (treffer.size() > 1) {
        std::vector<rawnes::RomIndexEntry> enger;
        for (auto& t : treffer) if ((t.chr == 0) == cfg->chrRam) enger.push_back(t);
        if (!enger.empty() && enger.size() < treffer.size()) {
            emitLog(Fmt(T("  CHR-Typ grenzt auf %1 ein."), {i18n::Zahl((long long)enger.size())}));
            treffer = enger;
        }
    }
    return treffer;
}

MonitorWorker::DetectResult MonitorWorker::tryAutoDetect() {
    DetectResult result;
    if (!ed_) return result;
    try {
        Bytes vecRaw = ed_->memrd(snoopBase_ + kVecOffset, 8);
        std::array<uint8_t, 6> vec6{};
        for (int i = 0; i < 6; ++i) vec6[i] = vecRaw[2 + i];

        static const char* hexChars = "0123456789abcdef";
        std::string hexs;
        for (int i = 0; i < 6; ++i) {
            hexs.push_back(hexChars[vec6[i] >> 4]);
            hexs.push_back(hexChars[vec6[i] & 0xF]);
            if (i < 5) hexs.push_back(' ');
        }
        result.hexKey = hexs;

        // Ein Vektor aus lauter 0xFF (oder 0x00) ist kein Fingerabdruck,
        // sondern der Leerlaufwert des Busses.
        bool allFF = true, all00 = true;
        for (uint8_t b : vec6) {
            if (b != 0xFF) allFF = false;
            if (b != 0x00) all00 = false;
        }
        if (allFF || all00) {
            emitLog(Fmt(T("Fingerabdruck [%1] besteht nur aus %2 -- das ist keine "
                          "gültige Antwort aus dem Snoop-RAM, sondern der "
                          "Leerlaufwert des Busses."),
                        {result.hexKey, allFF ? T("FF") : T("00")}));
            emitLog(T("Erkennung abgebrochen. Typische Ursachen: die Konsole steht im "
                      "EverDrive-Menü statt im Spiel, das Spiel wurde gerade erst "
                      "gestartet, oder die aktuelle top.rbf ist nicht deployt."));
            return result;
        }

        // EXAKT: die vom EverDrive gestartete Datei (Pfad in
        // EDN8/sysdata/registry.bin) ueber USB von der SD-Karte lesen und ihren
        // RA-Hash bilden. Gilt nur, wenn ihre Sprungadressen zum laufenden
        // Spiel passen -- sonst (z. B. Start ueber USB) geht es wie bisher weiter.
        {
            const Bytes reg = ed_->fileReadNoRecover("EDN8/sysdata/registry.bin", 256);
            std::string pfad;
            for (uint8_t c : reg) { if (c == 0) break; pfad.push_back((char)c); }
            if (pfad.size() > 1 && pfad[0] == '/') {
                emitLog(std::string(T("  Gestartet laut EverDrive: ")) + pfad);
                const long long groesse = ed_->fileSizeNoRecover(pfad);
                if (groesse >= 16 + 8192 && groesse <= 8 * 1024 * 1024) {
                    const auto t0 = Clock::now();
                    const Bytes datei = ed_->fileReadAllNoRecover(pfad, (uint32_t)groesse);
                    const auto vecDatei = datei.empty() ? std::nullopt : rawnes::vectorsFromInes(datei);
                    // Passen die Sprungadressen (NMI + Reset) des laufenden Spiels
                    // zu irgendeinem 8-KB-Bereich der Datei? Mapper, die den ganzen
                    // Bereich $8000-$FFFF umschalten (AxROM, Color Dreams ...),
                    // zeigen sie oft aus einem anderen als dem letzten Bereich
                    // (Messung an 4020 ROMs: 63 Faelle, z. B. Wizards & Warriors).
                    bool passt = false;
                    if (vecDatei && datei.size() >= 16) {
                        const size_t start = 16 + ((datei[6] & 0x04) ? 512 : 0);
                        const size_t prg = (size_t)datei[4] * 16384;
                        for (size_t ende = 8192; !passt && ende <= prg && start + ende <= datei.size(); ende += 8192) {
                            const uint8_t* v = &datei[start + ende - 6];
                            passt = v[0] == vec6[0] && v[1] == vec6[1] && v[2] == vec6[2] && v[3] == vec6[3];
                        }
                    }
                    if (!vecDatei) {
                        emitLog(T("  Gestartete Datei nicht lesbar -- es geht ohne Hash weiter."));
                    } else if (!passt) {
                        emitLog(T("  Die gestartete Datei passt nicht zum laufenden Spiel -- es geht ohne Hash weiter."));
                    } else {
                        result.exactMd5 = rawnes::inesMd5(datei);
                        result.resolvedSource = pfad;
                        emitLog(Fmt(T("Eindeutig über den Hash der gestarteten Datei (%1 KB in %2 ms)."),
                                    {i18n::Zahl((long long)(groesse / 1024)), i18n::Zahl(MsSeit(t0))}));
                        return result;
                    }
                } else {
                    emitLog(T("  Gestartete Datei nicht lesbar -- es geht ohne Hash weiter."));
                }
            } else {
                emitLog(T("  Gestartetes ROM vom EverDrive nicht lesbar."));
            }
        }
        if (!romIndex_ || romIndex_->empty()) return result;

        auto treffer = rawnes::lookup(*romIndex_, vec6);
        if (treffer.empty()) {
            emitLog(std::string(T("Fingerabdruck [")) + result.hexKey + T("] nicht in der Sammlung."));
            return result;
        }
        if (treffer.size() == 1) {
            emitLog(std::string(T("Erkannt: ")) + treffer.front().name);
            result.resolvedSource = treffer.front().source;
            return result;
        }
        emitLog(Fmt(T("Fingerabdruck [%1] passt auf %2 ROMs."),
                    {result.hexKey, i18n::Zahl((long long)treffer.size())}));

        // 0) Das EverDrive merkt sich das gestartete ROM in
        //    EDN8/sysdata/registry.bin (so liest es auch Krikzz' edio-Beispiel).
        //    Verglichen wird nur der Dateiname, und nur unter den Treffern des
        //    Fingerabdrucks: der Pfad kann also nie ein Spiel waehlen, das gar
        //    nicht laeuft. Wurde das Spiel anders gestartet (z. B. ueber USB),
        //    passt kein Treffer, und es geht wie bisher weiter.
        {
            const Bytes reg = ed_->fileReadNoRecover("EDN8/sysdata/registry.bin", 256);
            std::string pfad;
            for (uint8_t c : reg) { if (c == 0) break; pfad.push_back((char)c); }
            if (!pfad.empty() && pfad[0] == '/') {
                auto dateiname = [](std::string s) {
                    const size_t p = s.find_last_of("/\\");
                    if (p != std::string::npos) s = s.substr(p + 1);
                    for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);
                    return s;
                };
                const std::string ziel = dateiname(pfad);
                std::vector<rawnes::RomIndexEntry> passend;
                for (auto& t : treffer)
                    if (dateiname(t.source) == ziel || dateiname(t.name) == ziel) passend.push_back(t);
                bool gleicherInhalt = !passend.empty();
                for (auto& t : passend)
                    if (t.md5.empty() || t.md5 != passend.front().md5) gleicherInhalt = false;
                if (passend.size() == 1 || gleicherInhalt) {
                    emitLog(std::string(T("Eindeutig über den Pfad des EverDrive: ")) + passend.front().name);
                    result.resolvedSource = passend.front().source;
                    return result;
                }
                emitLog(passend.empty()
                            ? T("  Dieses ROM gehört nicht zu den Treffern -- es geht ohne Pfad weiter.")
                            : T("  Mehrere ROMs mit diesem Namen -- es geht ohne Pfad weiter."));
            }
        }

        // 1) Mapper-/PRG-/CHR-Eingrenzung
        size_t vorher = treffer.size();
        treffer = filterByMapper(treffer);
        if (treffer.size() < vorher && treffer.size() == 1) {
            emitLog(std::string(T("Eindeutig über Mapper: ")) + treffer.front().name);
            result.resolvedSource = treffer.front().source;
            return result;
        }

        // 2) Page-Abgleich (tatsaechlich gelesener ROM-Code)
        if (treffer.size() > 1) {
            Bytes pageRaw = ed_->memrd(snoopBase_ + kPageOffset, 256);
            Bytes seenRaw = ed_->memrd(snoopBase_ + kSeenOffset, 256);
            std::array<uint8_t, 256> page{}, seen{};
            for (int i = 0; i < 256; ++i) {
                page[i] = pageRaw[i];
                seen[i] = seenRaw[i];
            }
            vorher = treffer.size();
            auto [eingegrenzt, nPos] = rawnes::matchPage(treffer, page, seen);
            if (nPos < 8) {
                emitLog(Fmt(T("  PRG-Page noch zu wenig erfasst (%1 Byte) -- kurz "
                              "spielen und erneut versuchen."), {i18n::Zahl(nPos)}));
            } else if (eingegrenzt.size() < vorher) {
                emitLog(Fmt(T("  ROM-Code (%1 Byte) grenzt von %2 auf %3 ein."),
                            {i18n::Zahl(nPos), i18n::Zahl((long long)vorher),
                             i18n::Zahl((long long)eingegrenzt.size())}));
            } else {
                emitLog(Fmt(T("  ROM-Code (%1 Byte) grenzt nicht weiter ein -- die "
                              "Kandidaten sind an den erfassten Stellen gleich."),
                            {i18n::Zahl(nPos)}));
            }
            treffer = eingegrenzt;
            if (treffer.size() == 1) {
                emitLog(std::string(T("Eindeutig über ROM-Code: ")) + treffer.front().name);
                result.resolvedSource = treffer.front().source;
                return result;
            }
        }

        // 3) RA-GameID-Eingrenzung
        emitLog(T("  prüfe RA-Zuordnung..."));
        std::map<std::string, std::optional<long long>> gids;
        bool gidLookupOk = true;
        bool verschiedeneRaSpiele = false;
        for (auto& t : treffer) {
            if (t.md5.empty()) { gidLookupOk = false; break; }
            gids[t.name] = client_->ra_gameid(t.md5);
        }
        if (gidLookupOk) {
            std::vector<rawnes::RomIndexEntry> mitSet;
            for (auto& t : treffer) if (gids[t.name].has_value()) mitSet.push_back(t);
            if (!mitSet.empty() && mitSet.size() < treffer.size()) {
                emitLog(Fmt(T("  %1 ohne RA-Eintrag verworfen, %2 verbleiben."),
                            {i18n::Zahl((long long)(treffer.size() - mitSet.size())),
                             i18n::Zahl((long long)mitSet.size())}));
                treffer = mitSet;
            }
            std::set<long long> eindeutige;
            for (auto& t : treffer) if (gids[t.name].has_value()) eindeutige.insert(*gids[t.name]);
            verschiedeneRaSpiele = (eindeutige.size() > 1);
            if (!eindeutige.empty() && eindeutige.size() == 1) {
                emitLog(Fmt(T("Alle %1 Varianten gehören zu RA-Spiel #%2 -- nehme '%3'."),
                            {i18n::Zahl((long long)treffer.size()), i18n::Zahl(*eindeutige.begin()),
                             treffer.front().name}));
                result.resolvedSource = treffer.front().source;
                return result;
            }
            if (treffer.size() == 1) {
                emitLog(std::string(T("Eindeutig nach RA-Abgleich: ")) + treffer.front().name);
                result.resolvedSource = treffer.front().source;
                return result;
            }
        } else {
            emitLog(T("  (RA-Abfrage übersprungen -- fehlende md5-Angabe im Index)"));
        }

        // 4) Fruehere Wahl fuer genau diesen Fingerabdruck -- NUR wenn nicht
        // feststeht, dass die Kandidaten zu verschiedenen RA-Spielen gehoeren.
        auto merk = remembered_.find(hexs);
        if (merk != remembered_.end() && !verschiedeneRaSpiele) {
            for (auto& t : treffer) {
                if (t.source == merk->second) {
                    emitLog(std::string(T("Frühere Wahl übernommen: ")) + t.name);
                    result.resolvedSource = t.source;
                    return result;
                }
            }
        } else if (merk != remembered_.end() && verschiedeneRaSpiele) {
            emitLog(T("Frühere Wahl NICHT übernommen -- die Kandidaten gehören zu "
                      "verschiedenen RA-Spielen. Bitte bewusst auswählen."));
        }

        // 5) Wirklich mehrdeutig -- die Oberflaeche soll den Nutzer fragen.
        for (auto& t : treffer) {
            std::string label = t.name;
            auto git = gids.find(t.name);
            if (git != gids.end() && git->second) label += " (RA #" + std::to_string(*git->second) + ")";
            result.ambiguous.emplace_back(label, t.source);
        }
        return result;
    } catch (const std::exception& e) {
        emitLog(std::string(T("Automatische Erkennung fehlgeschlagen: ")) + e.what());
        return result;
    }
}

void MonitorWorker::doStartMonitoring(std::shared_ptr<rawnes::RomIndexMap> romIndex,
                                      const std::string& user, const std::string& token,
                                      bool hardcore) {
    if (!ed_) {
        emitLog(T("Nicht verbunden -- erst mit dem EverDrive verbinden."));
        return;
    }
    romIndex_ = std::move(romIndex);
    user_ = user;
    token_ = token;
    hardcore_ = hardcore;

    auto det = tryAutoDetect();
    if (det.resolvedSource) {
        emitLog(T("Automatisch erkannt -- kein Dateidialog nötig."));
        doContinueWithPath(*det.resolvedSource, det.exactMd5);
    } else if (!det.ambiguous.empty()) {
        std::vector<std::string> labels, sources;
        for (auto& [label, source] : det.ambiguous) { labels.push_back(label); sources.push_back(source); }
        if (romAmbiguous) romAmbiguous(det.hexKey, labels, sources);
    } else {
        if (romNotDetected) romNotDetected();  // Oberflaeche zeigt Dateidialog
    }
}

void MonitorWorker::doContinueWithChosenRom(const std::string& hexKey, const std::string& source) {
    if (!hexKey.empty()) {
        remembered_[hexKey] = source;
        if (rememberedChanged) rememberedChanged(hexKey, source);
    }
    doContinueWithPath(source);
}

// ================= Laden + Poll-Loop =================

void MonitorWorker::doContinueWithPath(const std::string& path, const std::string& md5) {
    if (!ed_) {
        emitLog(T("Nicht verbunden -- erst mit dem EverDrive verbinden."));
        return;
    }
    // Steht die Datei im Index, ist ihr RA-Hash schon bekannt: dann wird sie
    // gar nicht geoeffnet. Wichtig, wenn die ROMs auf der SD-Karte liegen,
    // die jetzt im EverDrive steckt. Nur bei eindeutigem Hash (ein Archiv mit
    // mehreren ROMs faellt auf den alten Weg zurueck).
    std::string indexMd5;
    if (romIndex_) {
        bool mehrdeutig = false;
        for (const auto& kv : *romIndex_)
            for (const auto& e : kv.second)
                if (e.source == path && !e.md5.empty()) {
                    if (!indexMd5.empty() && indexMd5 != e.md5) mehrdeutig = true;
                    indexMd5 = e.md5;
                }
        if (mehrdeutig) indexMd5.clear();
    }
    if (!md5.empty()) indexMd5 = md5;   // exakt aus der gelesenen Datei

    const std::string ext = Lower(fs::u8path(path).extension().string());
    std::string effectivePath = path;
    std::string tempNesFile;

    if (indexMd5.empty() && ext != ".nes") {
        emitLog(T("Entpacke Archiv..."));
        std::string err;
        auto data = rawnes::extractSingleNesFromArchive(path, &err);
        if (data.empty()) {
            emitLog(std::string(T("Archiv konnte nicht gelesen werden: ")) + err);
            if (finished) finished();
            return;
        }
        // Wie QDir::temp().filePath("rawnes_sel_" + completeBaseName + ".nes").
        tempNesFile = (fs::temp_directory_path() /
                       fs::u8path("rawnes_sel_" + fs::u8path(path).stem().string() + ".nes")).string();
        std::ofstream f(fs::u8path(tempNesFile), std::ios::binary);
        if (!f || !f.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size())) {
            emitLog(T("Konnte entpackte ROM nicht zwischenspeichern."));
            if (finished) finished();
            return;
        }
        f.close();
        effectivePath = tempNesFile;
    }

    auto [gameOpt, err] = indexMd5.empty() ? client_->identify_and_load(effectivePath, user_, token_)
                                           : client_->identify_and_load_md5(indexMd5, user_, token_);
    if (!tempNesFile.empty()) {
        std::error_code ec;
        fs::remove(fs::u8path(tempNesFile), ec);
    }

    if (!gameOpt) {
        emitLog(std::string(T("Fehler: ")) + err);
        if (finished) finished();
        return;
    }
    game_ = *gameOpt;
    if (gameLoaded) gameLoaded(game_.name);

    if (game_.no_set || game_.achievements.empty()) {
        emitLog(game_.name + T(": kein Achievement-Set."));
        if (finished) finished();
        return;
    }

    tracked_.clear();
    for (const auto& ac : game_.achievements) {
        TrackedAchievement t;
        t.info = ac;
        t.rt = std::make_unique<rawnes::AchievementRuntime>(ac.mem);
        std::vector<std::pair<std::string, std::string>> single{{std::to_string(ac.id), ac.mem}};
        for (auto a : rawnes::collectAddresses(single)) {
            if (a <= 0xFFFF) t.needed.insert(static_cast<uint32_t>(a));
        }
        tracked_.push_back(std::move(t));
    }

    leaderboards_.clear();
    for (const auto& lb : game_.leaderboards) {
        leaderboards_.emplace_back(lb.id, lb.title, lb.format, lb.startMem,
                                   lb.cancelMem, lb.submitMem, lb.valueMem);
    }
    if (!leaderboards_.empty()) {
        int unsupportedCount = 0;
        for (auto& lb : leaderboards_) if (lb.unsupported()) ++unsupportedCount;
        emitLog(Fmt(T("Leaderboards: %1 geladen (%2 nicht auswertbar)."),
                    {i18n::Zahl((long long)leaderboards_.size()), i18n::Zahl(unsupportedCount)}));
    }
    {
        LeaderboardList lbList;
        for (const auto& lb : game_.leaderboards) lbList.emplace_back(lb.id, lb.title);
        if (leaderboardsReady) leaderboardsReady(lbList);
    }

    std::vector<std::pair<std::string, std::string>> idAndMem;
    for (const auto& ac : game_.achievements) idAndMem.emplace_back(std::to_string(ac.id), ac.mem);
    auto addrs32 = rawnes::collectAddresses(idAndMem);
    watchedAddrs_.clear();
    for (auto a : addrs32) if (a <= 0xFFFF) watchedAddrs_.push_back(static_cast<uint16_t>(a));
    changeCounts_.assign(watchedAddrs_.size(), 0);
    if (liveValuesInit) liveValuesInit(watchedAddrs_);
    emitLog(Fmt(T("%1 Adressen werden überwacht."), {i18n::Zahl((long long)watchedAddrs_.size())}));

    std::set<long long> validIds;
    for (const auto& a : game_.achievements) validIds.insert(a.id);

    // Checkpoint 1 von 3: direkt vor dem RA-Netzwerkaufruf.
    checkHardcoreAtStart();

    auto [ids, info] = client_->ra_unlocks(game_.gameid, user_, token_, validIds, hardcore_);
    (void)info;
    alreadyUnlocked_ = ids;
    for (auto& t : tracked_) t.unlocked = alreadyUnlocked_.count(t.info.id) > 0;
    emitLog(Fmt(T("%1 Achievements geladen (%2 bereits freigeschaltet)."),
                {i18n::Zahl((long long)tracked_.size()), i18n::Zahl((long long)alreadyUnlocked_.size())}));
    if (achievementsReady) achievementsReady(game_, alreadyUnlocked_);

    // Checkpoint 2 von 3: direkt vor Eintritt in den Poll-Loop.
    checkHardcoreAtStart();
    try {
        Bytes rc = ed_->memrd(snoopBase_ + kResetCtrOffset, 1);
        lastResetCtr_ = rc[0];
        haveResetCtr_ = true;
    } catch (const std::exception&) {
        haveResetCtr_ = false;
    }

    havePrevRam_ = false;
    sstSeenReported_ = false;
    confirmedAddrs_.clear();
    warmedUp_ = false;
    haveWarmSince_ = false;
    warned3s_ = false;
    waitingIds_.clear();
    sessionStart_ = Clock::now();

    // FPGA-Build HIER nochmal lesen -- jetzt laeuft das Spiel, der Read ist
    // zuverlaessig (1:1 zu Python: Build-Check fuer wr_bits_da vor dem Poll-Loop).
    try {
        Bytes b = ed_->memrd(snoopBase_ + kBuildOffset, 1);
        fpgaBuild_ = b[0];
    } catch (const std::exception&) {
        // bleibt auf dem letzten bekannten Stand
    }
    wrBitsAvailable_ = (fpgaBuild_ >= 9);
    if (wrBitsAvailable_) {
        emitLog(T("Schreibmarkierungen des FPGA verfügbar -- Achievements "
                  "werden exakt freigegeben."));
    } else {
        emitLog(T("FPGA ohne Schreibmarkierungen -- Warmlauf-Heuristik aktiv "
                  "(Achievements mit konstanten Adressen können ausbleiben)."));
    }
    running_ = true;
    leerlaufZustand_ = 0;   // nach dem Monitor neu melden
    unplausibelFolge_ = 0;
    filter_.verwerfen();
    runPollLoop();
}

// ================= Hardcore-Pruefung =================

std::optional<std::vector<std::string>> MonitorWorker::hardcoreViolations() {
    try {
        Bytes ctrl = ed_->memrd(kCfgBase + kCtrlByteOffset, 1);
        const uint8_t c = ctrl[0];
        std::vector<std::string> v;
        if (c & kCtSsOnBit) v.push_back("Savestates (\"In Game Menu\")");
        if (c & kCtGgOnBit) v.push_back("Cheats (\"Cheats\")");
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void MonitorWorker::reportViolations(const std::vector<std::string>& v, bool midSession) {
    std::string liste;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) liste += " + ";
        liste += v[i];
    }
    hardcore_ = false;
    if (midSession) {
        emitLog(std::string(T("ACHTUNG: Nach Spielneustart sind ")) + liste +
                T(" am EverDrive aktiviert -- Hardcore wird für den Rest der Session "
                  "beendet, ab jetzt Softcore."));
    } else {
        emitLog(std::string(T("ACHTUNG: ")) + liste +
                T(" sind am EverDrive aktiviert -- nur Softcore "
                  "verfügbar. Für Hardcore bitte im EverDrive-System-Menü (nicht im "
                  "Ingame-Menü) deaktivieren, dann Monitor neu starten."));
    }
    if (hardcoreDowngraded) hardcoreDowngraded(liste);
}

void MonitorWorker::checkHardcoreAtStart() {
    if (!hardcore_) return;
    auto v = hardcoreViolations();
    if (!v) {
        hardcore_ = false;
        emitLog(T("Hardcore-Prüfung fehlgeschlagen -- Ctrl-Byte nicht lesbar. "
                  "Nur Softcore verfügbar."));
        return;
    }
    if (v->empty()) {
        emitLog(T("Hardcore-Voraussetzung erfüllt: Savestates und Cheats sind am "
                  "EverDrive deaktiviert."));
        return;
    }
    reportViolations(*v, false);
}

void MonitorWorker::checkHardcoreOnReset() {
    if (!hardcore_) return;
    auto v = hardcoreViolations();
    if (!v || v->empty()) return;
    reportViolations(*v, true);
}

void MonitorWorker::doDiagnose(long long achievementId) {
    TrackedAchievement* found = nullptr;
    for (auto& t : tracked_) if (t.info.id == achievementId) { found = &t; break; }
    if (!found) return;

    emitLog(Fmt(T("\n--- %1 (%2P) ---"), {found->info.title, i18n::Zahl(found->info.points)}));
    emitLog(std::string(T("MemAddr: ")) + found->info.mem);

    std::vector<std::string> nichtSnoopbar;
    for (auto a : found->needed) {
        const bool snoopable = (a <= 0x07FF) || (a >= 0x6000 && a <= 0x7FFF);
        if (!snoopable) nichtSnoopbar.push_back("$" + Upper(i18n::Hex(a, 4)));
    }
    if (!nichtSnoopbar.empty()) {
        std::string s;
        for (size_t i = 0; i < nichtSnoopbar.size(); ++i) {
            if (i) s += T(", ");
            s += nichtSnoopbar[i];
        }
        emitLog(std::string(T("nicht snoopbare Adressen: ")) + s);
    }
    if (found->rt->unsupported()) {
        emitLog(T("Bedingung wird nicht unterstützt -- feuert nie."));
        return;
    }
    if (!havePrevRam_) {
        emitLog(T("noch keine Messwerte -- Monitor starten."));
        return;
    }
    std::string werte;
    bool erstes = true;
    for (auto a : found->needed) {
        auto it = prevRam_.find(a);
        const std::string val = (it != prevRam_.end()) ? std::to_string(it->second) : "?";
        if (!erstes) werte += T(", ");
        erstes = false;
        werte += Upper("$" + i18n::Hex(a, 4) + "=" + val);
    }
    emitLog(std::string(T("aktuelle Werte: ")) + werte);
    const bool frei = wrBitsAvailable_
        ? std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                        found->needed.begin(), found->needed.end())
        : (warmedUp_ || std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                      found->needed.begin(), found->needed.end()));
    emitLog(frei ? T("Adressen bestätigt -- wird ausgewertet.")
                 : T("wartet noch auf bestätigte Adressen (Schutz gegen Fehlbuchungen)."));
}

// ================= Bildpuffer (FPGA-Build >= 12) =================
//
// Der FPGA kopiert bei jedem NMI die ueberwachten Werte in einen Ring aus 32
// Abschnitten (rawnes_snoop.sv). Ausgewertet wird dann jedes Bild genau
// einmal und vollstaendig aus einem Moment -- statt einzeln gelesener Werte
// aus mehreren Bildern.

bool MonitorWorker::richteRingEin() {
    if (fpgaBuild_ < kRingBuild) return false;
    if (snoopBase_ != RawnesAddr::kSnoopBase) return false;   // Ring nur im 0x1808000-Fenster
    ringAddrs_.clear();
    Bytes lo, hi;
    for (uint16_t a : watchedAddrs_) {
        uint16_t e;
        if (a <= 0x07FF) e = a;
        else if (a >= 0x6000 && a <= 0x7FFF) e = (uint16_t)(0x2000 | (a - 0x6000));
        else continue;
        ringAddrs_.push_back(a);
        lo.push_back((uint8_t)(e & 0xFF));
        hi.push_back((uint8_t)(e >> 8));
    }
    if (ringAddrs_.empty()) return false;
    if (ringAddrs_.size() > kRingMaxAdressen) {
        emitLog(Fmt(T("Bildpuffer: %1 Adressen, höchstens %2 -- Einzelabfrage."),
                    {i18n::Zahl((long long)ringAddrs_.size()), i18n::Zahl((long long)kRingMaxAdressen)}));
        return false;
    }
    try {
        ed_->memwr(snoopBase_ + kRingCtrlOffset, Bytes{0});
        ::Sleep(40);
        ed_->memwr(snoopBase_ + kRingLoOffset, lo);
        ed_->memwr(snoopBase_ + kRingHiOffset, hi);
        ed_->memwr(snoopBase_ + kRingCntOffset, Bytes{(uint8_t)ringAddrs_.size()});
        Bytes lo2 = ed_->memrd(snoopBase_ + kRingLoOffset, (uint32_t)lo.size());
        Bytes hi2 = ed_->memrd(snoopBase_ + kRingHiOffset, (uint32_t)hi.size());
        Bytes n2 = ed_->memrd(snoopBase_ + kRingCntOffset, 1);
        if (lo2 != lo || hi2 != hi || n2.empty() || n2[0] != (uint8_t)ringAddrs_.size()) {
            emitLog(T("Bildpuffer: Adressliste kam nicht richtig an -- Einzelabfrage."));
            return false;
        }
        Bytes g0 = ed_->memrd(snoopBase_ + kRingGenOffset, 1);
        ed_->memwr(snoopBase_ + kRingCtrlOffset, Bytes{1});
        ::Sleep(300);
        Bytes g1 = ed_->memrd(snoopBase_ + kRingGenOffset, 1);
        if (g0.empty() || g1.empty() || g0[0] == g1[0]) {
            ed_->memwr(snoopBase_ + kRingCtrlOffset, Bytes{0});
            emitLog(T("Bildpuffer: keine Bilder (Spiel ohne NMI?) -- Einzelabfrage."));
            return false;
        }
        rawnes::RamMap probe;
        if (!leseRingBild((uint8_t)(g1[0] - 1), probe)) {
            ed_->memwr(snoopBase_ + kRingCtrlOffset, Bytes{0});
            emitLog(T("Bildpuffer: Prüfwert falsch -- Einzelabfrage."));
            return false;
        }
        ringGen_ = g1[0];
        emitLog(Fmt(T("Bildgenaue Auswertung aktiv: %1 Adressen, %2 Bilder in 0,3 s."),
                    {i18n::Zahl((long long)ringAddrs_.size()), i18n::Zahl((long long)(uint8_t)(g1[0] - g0[0]))}));
        return true;
    } catch (const std::exception& e) {
        emitLog(std::string(T("Bildpuffer: ")) + e.what() + T(" -- Einzelabfrage."));
        try { ed_->memwr(snoopBase_ + kRingCtrlOffset, Bytes{0}); } catch (...) {}
        return false;
    }
}

void MonitorWorker::ringAus() {
    if (!ringAktiv_) return;
    ringAktiv_ = false;
    try {
        Bytes b = ed_->memrdNoRecover(snoopBase_ + kBuildOffset, 1);
        if (!b.empty() && b[0] == fpgaBuild_) ed_->memwr(snoopBase_ + kRingCtrlOffset, Bytes{0});
    } catch (...) {}
}

bool MonitorWorker::leseRingBild(uint8_t f, rawnes::RamMap& ram) {
    const uint32_t n = (uint32_t)ringAddrs_.size();
    Bytes b = ed_->memrd(snoopBase_ + kRingBufOffset + (uint32_t)(f % kRingAbschnitte) * 256, n + 4);
    if (b.size() != n + 4) return false;
    if (b[0] != f || b[1] != 0xA5 || b[n + 2] != 0x5A || b[n + 3] != f) return false;
    for (uint32_t i = 0; i < n; ++i) ram[ringAddrs_[i]] = b[2 + i];
    return true;
}

// ================= Poll-Loop =================

// Diagnosemodus (Standard: aus). Schreibt jede Abfrage mit Zeit,
// Build-Kennung, Neustartzaehler, allen Werten und Entscheidung nach
// messung_polls.txt neben der Exe.
constexpr bool kMessung = false;

void MonitorWorker::runPollLoop() {
    Clock::time_point pingTimer = Clock::now();
    constexpr long long kPingIntervalMs = 120000;

    std::ofstream mess;
    long long messZeilen = 0;
    const Clock::time_point messStart = Clock::now();
    if (kMessung) {
        wchar_t exe[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const fs::path datei = fs::path(exe).parent_path() / L"messung_polls.txt";
        mess.open(datei, std::ios::app);
        if (mess) {
            mess << "=== Monitorstart: " << game_.name << " | Build " << fpgaBuild_ << " | Adressen:";
            for (uint16_t a : watchedAddrs_) { char h[8]; std::snprintf(h, sizeof(h), " %04X", a); mess << h; }
            mess << "\n";
            // Fuer den Pruefstand: jedes Achievement mit Bedingung und Zustand.
            for (const auto& ta : tracked_)
                mess << "ACH #" << ta.info.id << " " << (ta.unlocked ? "frei" : "offen")
                     << (ta.rt->unsupported() ? " nicht-auswertbar" : "") << " \"" << ta.info.title
                     << "\" | " << ta.info.mem << "\n";
        }
    }
    // Diagnosemodus: test.ini neben der Exe mit "buchen=0" ->
    // auswerten wie immer, aber NICHTS an RetroAchievements buchen.
    bool probelauf = false;
    if (kMessung) {
        wchar_t exe[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::ifstream ini(fs::path(exe).parent_path() / L"test.ini");
        std::string zeile;
        while (std::getline(ini, zeile)) {
            zeile.erase(std::remove_if(zeile.begin(), zeile.end(), [](char c) { return c == ' ' || c == '\r'; }),
                        zeile.end());
            if (zeile == "buchen=0") probelauf = true;
        }
        if (probelauf) {
            emitLog("PROBELAUF: Freischaltungen werden NICHT an RetroAchievements gebucht (test.ini).");
            if (mess) mess << "PROBELAUF aktiv (buchen=0)\n";
        }
    }

    // Wiederverbinden und Lesefehler der seriellen Schicht mitschreiben.
    if (kMessung && mess) {
        g_serialProtokoll = [&](const std::string& s) {
            mess << MsSeit(messStart) << " SERIELL: " << s << "\n";
            mess.flush();
        };
    }
    // Eine Zeile je Abfrage: ms, Build, Neustartzaehler, Entscheidung, Werte.
    auto messZeile = [&](int build, const rawnes::RamMap& werte, const std::string& entscheidung,
                         int rcBekannt = -2) {
        if (!kMessung || !mess || messZeilen > 60000) return;
        ++messZeilen;
        int rc = rcBekannt;
        if (rc == -2) {
            rc = -1;
            try {
                Bytes r = ed_->memrd(snoopBase_ + kResetCtrOffset, 1);
                if (!r.empty()) rc = r[0];
            } catch (...) {}
        }
        char kopf[96];
        std::snprintf(kopf, sizeof(kopf), "%8lld build=%s rc=%s %-22s", MsSeit(messStart),
                      build < 0 ? "??" : i18n::Hex((unsigned)build, 2).c_str(),
                      rc < 0 ? "??" : i18n::Hex((unsigned)rc, 2).c_str(), entscheidung.c_str());
        mess << kopf;
        for (const auto& kv : werte) {
            char w[12];
            std::snprintf(w, sizeof(w), " %04X=%02X", (unsigned)kv.first, (unsigned)kv.second);
            mess << w;
        }
        mess << "\n";
        mess.flush();
    };

    // Netzwerkaufrufe (Buchung, Aktivitaet) im Hintergrund, damit die Abfrage
    // nicht stehen bleibt. Am Ende der Schleife wird auf alle gewartet.
    std::vector<std::future<void>> netz;
    auto imHintergrund = [&](std::function<void()> auftrag) {
        netz.erase(std::remove_if(netz.begin(), netz.end(), [](std::future<void>& f) {
                       return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                   }), netz.end());
        netz.push_back(std::async(std::launch::async, std::move(auftrag)));
    };
    auto pingImHintergrund = [&]() {
        std::string richPresence;
        if (!game_.richPresenceScript.empty()) {
            auto compiled = rawnes::parseRichPresenceScript(game_.richPresenceScript);
            if (auto text = rawnes::evaluateRichPresence(compiled)) richPresence = *text;
        }
        imHintergrund([client = client_, logFn = log, gid = game_.gameid, user = user_, token = token_, richPresence]() {
            auto [ok, info] = client->ra_ping(gid, user, token, richPresence);
            // Erfolg nicht ins Log (alle 2 min, nur Laerm) -- nur Fehler.
            if (!ok && logFn) logFn(Fmt(T("RA-Aktivität: %1"), {std::string(T("Fehler: ")) + info}));
        });
    };

    // Neustartzaehler und Savestate-Merker (je Abfrage).
    auto neustartUndSst = [&]() {
            try {
                Bytes rcBytes = ed_->memrd(snoopBase_ + kResetCtrOffset, 1);
                const uint8_t rc = rcBytes[0];
                if (haveResetCtr_ && rc != lastResetCtr_) {
                    emitLog(T("Spiel wurde neu gestartet -- Achievement-Zustand "
                              "zurückgesetzt."));
                    for (auto& t : tracked_) t.rt->reset();
                    for (auto& lb : leaderboards_) lb.reset();
                    havePrevRam_ = false;
                    sstSeenReported_ = false;
                    confirmedAddrs_.clear();
                    warmedUp_ = false;
                    haveWarmSince_ = false;
                    waitingIds_.clear();
                    checkHardcoreOnReset();
                }
                lastResetCtr_ = rc;
                haveResetCtr_ = true;
            } catch (const std::exception&) {}

            if (hardcore_ && !sstSeenReported_) {
                try {
                    Bytes sst = ed_->memrd(snoopBase_ + kSstStatusOffset, 1);
                    if (sst[0] & 0x02) {
                        sstSeenReported_ = true;
                        emitLog(T("ACHTUNG: Savestate-Aktivität erkannt (Audit-Bit) -- "
                                  "Hardcore-Bedingung möglicherweise verletzt."));
                    }
                } catch (const std::exception&) {}
            }

    };

    // Ein Abbild auswerten. ring: aus dem Bildpuffer (Neustart/Markierungen
    // werden dann einmal je Abfrage gelesen, nicht je Bild); anzeigen: Live-Werte melden.
    auto werteAus = [&](rawnes::RamMap& ram, bool ring, bool anzeigen) {
            if (!ring) neustartUndSst();

            // Live-Werte + Change-Detection ZUERST (wie in Python).
            for (size_t slot = 0; slot < watchedAddrs_.size(); ++slot) {
                const uint32_t addr = watchedAddrs_[slot];
                auto it = ram.find(addr);
                const uint8_t val = (it != ram.end()) ? it->second : 0;
                if (havePrevRam_) {
                    auto pit = prevRam_.find(addr);
                    const uint8_t prevVal = (pit != prevRam_.end()) ? pit->second : 0;
                    if (pit != prevRam_.end() && prevVal != val) {
                        ++changeCounts_[slot];
                        confirmedAddrs_.insert(addr);  // hat sich geaendert -> kein Init-Wert
                    }
                }
                if (anzeigen && liveValueUpdate) liveValueUpdate((int)slot, val, changeCounts_[slot]);
            }

            // Warmlauf-Heuristik (Fallback ohne Schreibmarkierungen).
            if (!warmedUp_) {
                if ((int)confirmedAddrs_.size() >= kWarmupMinActive) {
                    if (!haveWarmSince_) {
                        warmSince_ = Clock::now();
                        haveWarmSince_ = true;
                    } else if (MsSeit(warmSince_) >= (long long)(kWarmupSeconds * 1000)) {
                        warmedUp_ = true;
                        emitLog(Fmt(T("Spiel läuft (%1 Adressen aktiv) -- alle "
                                      "Achievements scharf."),
                                    {i18n::Zahl((long long)confirmedAddrs_.size())}));
                    }
                } else {
                    haveWarmSince_ = false;
                }
            }

            // Nach 3s einmalig Bilanz ziehen.
            if (!warned3s_ && MsSeit(sessionStart_) > 3000) {
                warned3s_ = true;
                int totalChanges = 0;
                for (int c : changeCounts_) totalChanges += c;
                if (totalChanges == 0) {
                    bool allFF = true;
                    for (size_t slot = 0; slot < watchedAddrs_.size(); ++slot) {
                        auto it = ram.find(watchedAddrs_[slot]);
                        if (it == ram.end() || it->second != 0xFF) { allFF = false; break; }
                    }
                    emitLog(Fmt(T("WARNUNG: nach 3s keine einzige Wertänderung%1.\n"
                                  "  Mögliche Gründe:\n"
                                  "  1. Läuft am Gerät die aktuelle top.rbf mit dem Spiegel-Snooper?\n"
                                  "  2. Läuft überhaupt ein Spiel (nicht das Menü)?\n"
                                  "  3. Beschreibt dieses Spiel die Adressen überhaupt?"),
                                {allFF ? T(" -- alle Slots stehen auf $FF") : ""}));
                } else {
                    int aktiv = 0;
                    for (int c : changeCounts_) if (c > 0) ++aktiv;
                    int wartet = 0;
                    for (auto& t : tracked_) {
                        if (!t.rt->unsupported() &&
                            !std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                           t.needed.begin(), t.needed.end())) {
                            ++wartet;
                        }
                    }
                    emitLog(Fmt(T("Snooper liefert Daten: %1/%2 Adressen ändern sich."),
                                {i18n::Zahl(aktiv), i18n::Zahl((long long)watchedAddrs_.size())}));
                    if (wartet > 0) {
                        emitLog(Fmt(T("%1 Achievement(s) warten noch auf echte Daten -- "
                                      "werden erst geprüft, wenn ihre Adressen bestätigt "
                                      "sind. Schutz gegen Fehlbuchungen an RA."),
                                    {i18n::Zahl(wartet)}));
                    }
                }
            }

            if (havePrevRam_) {
                for (auto& t : tracked_) {
                    if (t.unlocked || t.rt->unsupported()) continue;

                    // SICHERUNG GEGEN FEHLBUCHUNGEN: harte Schreibmarkierung
                    // pruefen (falls verfuegbar), sonst Warmlauf-Heuristik.
                    if (wrBitsAvailable_ && !ring) {   // Bildpuffer: Markierungen je Abfrage (ringMarken)
                        for (auto addr : t.needed) {
                            if (confirmedAddrs_.count(addr)) continue;
                            uint32_t off;
                            if (addr <= 0x07FF) off = kRamMarkOffset + addr;
                            else if (addr >= 0x6000 && addr <= 0x7FFF) off = kWramMarkOffset + (addr - 0x6000);
                            else continue;
                            try {
                                Bytes wb = ed_->memrd(snoopBase_ + off, 1);
                                if (wb[0] & 0x01) confirmedAddrs_.insert(addr);
                            } catch (const std::exception&) {}
                        }
                    }
                    const bool frei = wrBitsAvailable_
                        ? std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                        t.needed.begin(), t.needed.end())
                        : (warmedUp_ || std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                                      t.needed.begin(), t.needed.end()));
                    const bool wasWaiting = waitingIds_.count(t.info.id) > 0;
                    if (!frei && !wasWaiting) {
                        waitingIds_.insert(t.info.id);
                        if (achievementWaitingChanged) achievementWaitingChanged(t.info.id, true);
                    } else if (frei && wasWaiting) {
                        waitingIds_.erase(t.info.id);
                        if (achievementWaitingChanged) achievementWaitingChanged(t.info.id, false);
                    }
                    if (!frei) continue;

                    const bool fired = t.rt->update(ram, &prevRam_);
                    auto prog = t.rt->progress();
                    if (prog.hasProgress && progressChanged)
                        progressChanged(t.info.id, prog.current, prog.target);
                    if (fired) {
                        t.unlocked = true;
                        // Diagnosemodus: jede Freischaltung nachpruefbar festhalten --
                        // Bedingung und die Werte jeder benoetigten Adresse,
                        // jetzt und in der Abfrage davor.
                        if (kMessung && mess) {
                            mess << MsSeit(messStart) << " TRIGGER #" << t.info.id << " \"" << t.info.title
                                 << "\"\n    Bedingung: " << t.info.mem << "\n   ";
                            for (uint32_t a : t.needed) {
                                auto jetzt = ram.find(a);
                                auto vorher = prevRam_.find(a);
                                char w[48];
                                std::snprintf(w, sizeof(w), " %04X:%s->%s", (unsigned)a,
                                              vorher == prevRam_.end() ? "--" : i18n::Hex(vorher->second, 2).c_str(),
                                              jetzt == ram.end() ? "--" : i18n::Hex(jetzt->second, 2).c_str());
                                mess << w;
                            }
                            mess << "\n";
                            mess.flush();
                        }
                        if (unlocked) unlocked(t.info.title, t.info.points, t.info.id, hardcore_);
                        if (probelauf) {
                            emitLog(Fmt(T("Achievement freigeschaltet: %1 (%2)"),
                                        {t.info.title, std::string(T("Fehler: ")) + "PROBELAUF -- nicht gebucht"}));
                        } else {
                            // Buchung im Hintergrund: die Abfrage laeuft weiter, sonst
                            // laeuft der Bildpuffer waehrend der Netzwerkzeit ueber.
                            imHintergrund([client = client_, logFn = log, id = t.info.id, titel = t.info.title,
                                           user = user_, token = token_, hc = (bool)hardcore_]() {
                                auto [ok, info] = client->ra_award(id, user, token, hc);
                                if (logFn) logFn(Fmt(T("Achievement freigeschaltet: %1 (%2)"),
                                                     {titel, ok ? std::string(T("gebucht"))
                                                                : std::string(T("Fehler: ")) + info}));
                            });
                        }
                    }
                }
            }

            // Leaderboards: submit nur im Hardcore-Modus (RA-Regel).
            if (havePrevRam_) {
                for (auto& lb : leaderboards_) {
                    double val = 0;
                    auto ev = lb.update(ram, &prevRam_, &val);
                    switch (ev) {
                        case rawnes::LeaderboardRuntime::Event::Start:
                            if (leaderboardStarted) leaderboardStarted(lb.id(), lb.title());
                            emitLog(std::string(T("  Leaderboard läuft: ")) + lb.title());
                            break;
                        case rawnes::LeaderboardRuntime::Event::Cancel:
                            if (leaderboardCanceled) leaderboardCanceled(lb.id(), lb.title());
                            emitLog(std::string(T("  Leaderboard abgebrochen: ")) + lb.title());
                            break;
                        case rawnes::LeaderboardRuntime::Event::Submit: {
                            char buf[64];
                            std::snprintf(buf, sizeof(buf), "%.0f", val);  // QString::number(val, 'f', 0)
                            const std::string formatted = buf;
                            if (!hardcore_) {
                                emitLog(Fmt(T("  Leaderboard %1: Wert %2 -- NICHT eingereicht "
                                              "(nur im Hardcore-Modus gültig)."),
                                            {lb.title(), formatted}));
                                break;
                            }
                            auto [ok, info] = client_->ra_submit_lb(lb.id(), user_, token_,
                                                                    static_cast<uint32_t>(val));
                            if (leaderboardSubmitted) leaderboardSubmitted(lb.id(), lb.title(), formatted);
                            emitLog(Fmt(T("  *** Leaderboard %1: %2 (%3) ***"),
                                        {lb.title(), formatted,
                                         ok ? std::string(T("eingereicht"))
                                            : std::string(T("Fehler: ")) + info}));
                            break;
                        }
                        case rawnes::LeaderboardRuntime::Event::None:
                            break;
                    }
                }
            }

            prevRam_ = ram;
            havePrevRam_ = true;
    };


    // ---- Bildpuffer (FPGA-Build >= 12) ----
    bool ringErlaubt = true;
    if (kMessung) {
        wchar_t exe[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::ifstream ini(fs::path(exe).parent_path() / L"test.ini");
        std::string zeile;
        while (std::getline(ini, zeile)) {
            zeile.erase(std::remove_if(zeile.begin(), zeile.end(), [](char c) { return c == ' ' || c == '\r'; }),
                        zeile.end());
            if (zeile == "ring=0") ringErlaubt = false;
        }
    }
    ringAktiv_ = ringErlaubt && richteRingEin();
    if (kMessung && mess) mess << "BILDPUFFER " << (ringAktiv_ ? "an" : "aus") << "\n";
    ringBilder_ = 0;
    ringLuecken_ = 0;
    Clock::time_point ringBericht = Clock::now();
    Clock::time_point markenZeit = Clock::now();
    bool unplausibelLaeuft = false;
    Clock::time_point unplausibelSeit = Clock::now();
    Clock::time_point letztesBild = Clock::now();

    // Fehlt ein Bild, wird nichts ueber die Luecke hinweg verglichen und alle
    // Trefferzaehler beginnen neu -- eine Luecke kann so nur verzoegern, nie
    // ausloesen (wie MEGA-RAW).
    auto ringLuecke = [&](const char* grund) {
        ++ringLuecken_;
        for (auto& tr : tracked_) tr.rt->reset();
        havePrevRam_ = false;
        filter_.verwerfen();
        if (ringLuecken_ <= 5) emitLog(Fmt(T("Bildlücke (%1) -- Trefferzähler zurückgesetzt."), {T(grund)}));
        if (kMessung && mess) mess << MsSeit(messStart) << " LUECKE " << grund << "\n";
    };
    // Schreibmarkierungen der noch unbestaetigten Adressen, hoechstens 16 je Abfrage.
    auto ringMarken = [&]() {
        if (!wrBitsAvailable_ || MsSeit(markenZeit) < 200) return;
        markenZeit = Clock::now();
        int gelesen = 0;
        for (auto& tr : tracked_) {
            if (tr.unlocked || tr.rt->unsupported()) continue;
            for (auto addr : tr.needed) {
                if (confirmedAddrs_.count(addr) || gelesen >= 16) continue;
                uint32_t off;
                if (addr <= 0x07FF) off = kRamMarkOffset + addr;
                else if (addr >= 0x6000 && addr <= 0x7FFF) off = kWramMarkOffset + (addr - 0x6000);
                else continue;
                ++gelesen;
                try {
                    Bytes wb = ed_->memrd(snoopBase_ + off, 1);
                    if (!wb.empty() && (wb[0] & 0x01)) confirmedAddrs_.insert(addr);
                } catch (const std::exception&) {
                    return;   // Verbindung gestoert: die naechste Abfrage merkt es
                }
            }
        }
    };
    while (running_) {
        // Diagnose-Klicks auch waehrend der Ueberwachung beantworten.
        runPendingDiagnose();
        try {
            if (ringAktiv_) {
                // Nicht abfangen: ist die Verbindung weg (Konsole aus), endet die
                // Abfrage sofort ueber den Verbindungsverlust, statt jeden weiteren
                // Lesezugriff einzeln auf die Zeitschranke laufen zu lassen.
                int messBuild = -1;
                {
                    Bytes bb = ed_->memrd(snoopBase_ + kBuildOffset, 1);
                    if (!bb.empty()) messBuild = bb[0];
                }
                Bytes gb = ed_->memrd(snoopBase_ + kRingGenOffset, 1);
                if (gb.empty()) throw std::runtime_error("memrd: 0/1 Byte erhalten");
                const uint8_t gen = gb[0];
                neustartUndSst();

                bool plausibel = (messBuild == fpgaBuild_);
                uint8_t neu = (uint8_t)(gen - ringGen_);
                if (plausibel && neu >= kRingAbschnitte) {   // laenger nicht gelesen, als der Ring haelt
                    ringLuecke("Ring übergelaufen");
                    ringGen_ = (uint8_t)(gen - 1);
                    neu = 1;
                }
                if (neu > 0) letztesBild = Clock::now();
                else if (plausibel && MsSeit(letztesBild) > 2000) {
                    // Seit 2 s kein Bild: Spiel ohne NMI (Ladebild) oder der Kern
                    // wurde neu geladen und der Bildpuffer ist aus. Nur im zweiten
                    // Fall neu einrichten.
                    letztesBild = Clock::now();
                    Bytes st = ed_->memrd(snoopBase_ + kRingStatOffset, 1);
                    if (!st.empty() && !(st[0] & 0x01)) {
                        emitLog(T("Bildpuffer war aus -- wird neu eingerichtet."));
                        ringAktiv_ = richteRingEin();
                        if (!ringAktiv_) emitLog(T("Weiter mit Einzelabfrage."));
                        continue;
                    }
                }
                bool angezeigt = false;
                for (uint8_t i = 0; plausibel && i < neu && running_; ++i) {
                    const uint8_t f = (uint8_t)(ringGen_ + i);
                    rawnes::RamMap ram;
                    if (!leseRingBild(f, ram)) { ringLuecke("Bild überschrieben"); continue; }
                    ++ringBilder_;
                    // Keine Pruefung auf "alle Werte 00/FF": die Bilder kopiert der
                    // FPGA selbst, beim Ausschalten kommen schlicht keine mehr
                    // (Build-Kennung und Verbindung zeigen das). Im Titelbild stehen
                    // bei kleinen Sets aber oft wirklich alle Werte auf 00
                    // (California Games, 05.11.2026) -- das ist kein Fehler.
                    messZeile(messBuild, ram, "bild", lastResetCtr_);
                    auto frei = filter_.weiter(ram);
                    if (!frei) continue;
                    werteAus(*frei, true, false);
                    angezeigt = true;
                }
                if (plausibel) ringGen_ = gen;

                if (!plausibel) {
                    if (!unplausibelLaeuft) {
                        unplausibelLaeuft = true;
                        unplausibelSeit = Clock::now();
                        emitLog(Fmt(T("Unplausible Werte (%1) -- nicht ausgewertet. Konsole aus oder Spiel verlassen?"),
                                    {rawnes::unplausibelGrund(messBuild, fpgaBuild_, rawnes::RamMap())}));
                    }
                    if (MsSeit(unplausibelSeit) >= (kMessung ? 10000 : 1000)) {
                        emitLog(T("Kein Spiel mehr erkannt -- Monitor gestoppt. Spiel starten, dann erneut auf "
                                  "\"Spiel erkennen & Monitor starten\" klicken."));
                        if (spielVerlassen) spielVerlassen();
                        running_ = false;
                        break;
                    }
                    ::Sleep(50);
                    continue;
                }
                unplausibelLaeuft = false;

                if (angezeigt && liveValueUpdate) {
                    for (size_t slot = 0; slot < watchedAddrs_.size(); ++slot) {
                        auto it = prevRam_.find(watchedAddrs_[slot]);
                        liveValueUpdate((int)slot, it != prevRam_.end() ? it->second : 0, changeCounts_[slot]);
                    }
                }
                ringMarken();
                if (kMessung && mess && MsSeit(ringBericht) >= 5000) {
                    ringBericht = Clock::now();
                    mess << MsSeit(messStart) << " RING bilder=" << ringBilder_ << " luecken=" << ringLuecken_ << "\n";
                }
                if (MsSeit(pingTimer) > kPingIntervalMs) {
                    pingImHintergrund();
                    pingTimer = Clock::now();
                }
                ::Sleep(15);
                continue;
            }

            // Byteweise lesen, und nur die tatsaechlich gebrauchten Adressen --
            // 1:1 zu rawnes_gui_v3_final.py (getakteter Lesepfad; WRAM).
            rawnes::RamMap ram;
            for (uint16_t a : watchedAddrs_) {
                auto off = snoopOffset(a);
                if (!off) continue;  // nicht snoopbar -- bleibt ungesetzt
                Bytes b1 = ed_->memrd(snoopBase_ + *off, 1);
                if (!b1.empty()) ram[a] = b1[0];
            }

            // SCHUTZ GEGEN FEHLBUCHUNGEN beim Ausschalten oder Verlassen des
            // Spiels (Hardwaretest 04.11.2026: zwei falsche Freischaltungen
            // beim Ausschalten). Eine Abfrage wird NICHT ausgewertet, wenn
            //  a) die Build-Kennung des Kerns nicht mehr stimmt (Kern laeuft
            //     nicht mehr) oder
            //  b) alle Werte gleichzeitig FF oder 00 sind (Leerlauf des Busses).
            // Bleibt das rund eine Sekunde so, stoppt der Monitor.
            int messBuild = -1;
            {
                try {
                    Bytes bb = ed_->memrd(snoopBase_ + kBuildOffset, 1);
                    if (!bb.empty()) messBuild = bb[0];
                } catch (const std::exception&) {}
                // Gemeinsamer Baustein mit dem Pruefstand (core/Plausibel).
                const std::string grund = rawnes::unplausibelGrund(messBuild, fpgaBuild_, ram);
                if (!grund.empty()) {
                    messZeile(messBuild, ram, "VERWORFEN(" + grund + ")");
                    filter_.verwerfen();   // wartende Abfragen sind damit auch nicht bestaetigt
                    ++unplausibelFolge_;
                    if (unplausibelFolge_ == 1)
                        emitLog(Fmt(T("Unplausible Werte (%1) -- nicht ausgewertet. Konsole aus oder Spiel verlassen?"),
                                    {grund}));
                    if (unplausibelFolge_ >= (kMessung ? 200 : 20)) {   // ca. 1 s (Diagnosemodus: 10 s)
                        emitLog(T("Kein Spiel mehr erkannt -- Monitor gestoppt. Spiel starten, dann erneut auf "
                                  "\"Spiel erkennen & Monitor starten\" klicken."));
                        if (spielVerlassen) spielVerlassen();
                        running_ = false;
                        break;
                    }
                    ::Sleep(50);
                    continue;
                }
                unplausibelFolge_ = 0;
                messZeile(messBuild, ram, (int)filter_.wartend() >= rawnes::kAuswerteVerzoegerung
                                              ? "ok(aelteste ausgewertet)" : "ok(wartet)");
            }

            // Ausgewertet wird eine Abfrage erst, wenn kAuswerteVerzoegerung
            // weitere plausible gefolgt sind (core/Plausibel). Beim Ausschalten
            // koennen einzelne Abfragen schon Unsinn enthalten, waehrend der
            // Kern noch kurz laeuft -- der Abbruch verwirft sie dann. Gewaehlt
            // mit dem Pruefstand: ab 3 kam keine Stoerung mehr durch.
            {
                auto frei = filter_.weiter(ram);
                if (!frei) { ::Sleep(50); continue; }
                ram = std::move(*frei);
            }

            werteAus(ram, false, true);

            if (MsSeit(pingTimer) > kPingIntervalMs) {
                pingImHintergrund();
                pingTimer = Clock::now();
            }

            ::Sleep(50);
        } catch (const std::exception& e) {
            if (kMessung && mess) {
                mess << MsSeit(messStart) << " AUSNAHME: " << e.what() << "\n";
                mess.flush();
            }
            // memrd hat bereits einmal neu verbunden und erneut gelesen. Klappt
            // es trotzdem nicht (Konsole aus, Kabel ab), Monitor stoppen statt
            // endlos weiter zu versuchen (Messung 04.11.2026: alle 6-7 s ein
            // neuer Versuch, ohne Aussicht).
            if (connectionLost) connectionLost(e.what());
            if (spielVerlassen) spielVerlassen();
            emitLog(T("Monitor gestoppt -- keine Verbindung zum EverDrive. Konsole wieder an? Dann Spiel "
                      "starten und erneut auf \"Spiel erkennen & Monitor starten\" klicken."));
            running_ = false;
        }
    }
    for (auto& f : netz) f.wait();   // laufende Buchungen abschliessen
    ringAus();   // Kern laeuft noch: Bildpuffer abschalten, damit der naechste Start sauber einrichtet
    g_serialProtokoll = nullptr;
    if (kMessung && mess) {
        mess << MsSeit(messStart) << " RING bilder=" << ringBilder_ << " luecken=" << ringLuecken_ << "\n";
        mess << MsSeit(messStart) << " SCHLEIFE ENDE\n";
        mess.flush();
    }
    emitLog(T("Monitor gestoppt."));
    if (finished) finished();
}
