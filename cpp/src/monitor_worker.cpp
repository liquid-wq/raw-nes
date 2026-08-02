#include "monitor_worker.h"
#include "rawnes_addr.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QElapsedTimer>

using namespace RawnesAddr;

// Vektor-/Page-Offsets fuer die automatische Erkennung (aus
// rawnes_gui_v3_final.py, Kommentar ueber ADDR_SNOOP). Falls diese
// bereits in rawnes_addr.h stehen, diesen Block entfernen (Duplikat-
// Definition sonst Compile-Fehler).
namespace {
constexpr uint32_t kVecOffset = 0x0800;
constexpr uint32_t kPageOffset = 0x0900;
constexpr uint32_t kSeenOffset = 0x0A00;
// Schreibmarkierungs-Bits (ab FPGA-Build 9): pro Adresse ein Bit, ob das
// Spiel dort jemals geschrieben hat -- ersetzt die Warmlauf-Heuristik mit
// einer harten Auskunft, wo verfuegbar.
constexpr uint32_t kRamMarkOffset = 0x1000;
// Wertespiegel des Cartridge-WRAM $6000-$7FFF. Fehlte im C++-Port komplett;
// Belegung laut rawnes_gui_v3_final.py (WRAM_OFFSET, Kommentarblock ueber
// ADDR_SNOOP): 0x0000-0x07FF RAM-Werte, 0x2000-0x3FFF WRAM-Werte,
// 0x4000-0x5FFF WRAM-Schreibmarkierungen.
constexpr uint32_t kWramOffset = 0x2000;
constexpr uint32_t kWramMarkOffset = 0x4000;

// Offset einer NES-Adresse im Snoop-Fenster -- 1:1 zu Pythons
// snoop_offset(). Leer, wenn die Adresse nicht gespiegelt wird.
std::optional<uint32_t> snoopOffset(uint32_t nesAddr) {
    if (nesAddr <= 0x07FF) return nesAddr;
    if (nesAddr >= 0x6000 && nesAddr <= 0x7FFF) return kWramOffset + (nesAddr - 0x6000);
    return std::nullopt;
}
} // namespace

MonitorWorker::MonitorWorker(std::shared_ptr<RaClient> client)
    : client_(std::move(client)) {
    qRegisterMetaType<std::vector<uint16_t>>("std::vector<uint16_t>");
    qRegisterMetaType<std::shared_ptr<rawnes::RomIndexMap>>("std::shared_ptr<rawnes::RomIndexMap>");
    qRegisterMetaType<GameData>("GameData");
    qRegisterMetaType<std::set<long long>>("std::set<long long>");
    qRegisterMetaType<LeaderboardList>("LeaderboardList");
}

// ================= Verbindung (einmalig, unabhaengig vom Spielstart) =================

void MonitorWorker::connectToDevice(QString preferred) {
    emit connectAttempt(1, 1);
    auto [edPtr, foundPort] = find_everdrive(preferred);
    if (!edPtr) {
        emit connectionFailed("EverDrive nicht gefunden -- angeschlossen? "
                              "Anderes Programm auf dem COM-Port?");
        return;
    }
    ed_ = std::move(edPtr);
    port_ = foundPort;
    emit connected(port_);
    emit log(tr("Verbunden auf ")+ port_ + tr("."));

    // FPGA-Build-Check passiert in Python direkt nach dem Verbinden
    // (_check_builds() in _connect()), nicht erst beim Spielstart.
    // ZUSAETZLICH: beide moegliche Snoop-Basisadressen probieren (0x1808000
    // zuerst, dann 0x1804000 als Fallback), 1:1 zu Pythons Vorgehen --
    // welche Adresse die MCU tatsaechlich erreicht, war zwischen FPGA-
    // Firmware-Versionen schon unterschiedlich (siehe rawnes_addr.h).
    fpgaBuild_ = -1;
    for (uint32_t candidate : {kSnoopBase, kSnoopBaseAlt}) {
        try {
            // memrdNoRecover: Fehlschlag ist hier ein erwarteter Normalfall
            // (Konsole im EverDrive-Menue -> MCU-DMA blockiert den Bus).
            // Mit memrd() loeste jeder Kandidat eine recover()-Kaskade ueber
            // alle COM-Ports aus.
            QByteArray b = ed_->memrdNoRecover(candidate + kBuildOffset, 1);
            int build = static_cast<uint8_t>(b[0]);
            // 0xFF und 0x00 sind Leerlaufwerte eines nicht antwortenden
            // Busses, keine Build-Nummern.
            if (build == 0xFF || build == 0x00) continue;
            if (build == kFpgaBuildExpected) {
                if (candidate != snoopBase_) {
                    emit log(tr("Snoop-Region liegt bei 0x%1 (nicht 0x%2).")
                             .arg(candidate, 7, 16, QChar('0')).arg(snoopBase_, 7, 16, QChar('0')));
                    snoopBase_ = candidate;
                }
                fpgaBuild_ = build;
                break;
            }
            if (fpgaBuild_ < 0) fpgaBuild_ = build; // erster erfolgreicher Read als Fallback-Anzeige
        } catch (const std::exception&) {
            // dieser Kandidat nicht erreichbar, naechsten probieren
        }
    }
    if (fpgaBuild_ >= 0) {
        if (fpgaBuild_ != kFpgaBuildExpected) {
            emit log(tr("ACHTUNG: FPGA-Build %1 erwartet, %2 gefunden -- "
                             "aktuelle top.rbf deployt?").arg(kFpgaBuildExpected).arg(fpgaBuild_));
        } else {
            emit log(tr("FPGA-Build %1 bestaetigt.").arg(fpgaBuild_));
        }
    } else {
        emit log(tr("FPGA-Build konnte nicht gelesen werden (Menue statt Spiel? "
                 "Bekanntes DMA-Verhalten, kein Fehler)."));
    }
}

// ================= Automatische Erkennung =================

std::optional<MonitorWorker::MapCfg> MonitorWorker::readMapCfg() {
    // Layout aus krikzz base_sv/sys_cfg.sv (siehe Python _read_map_cfg()):
    //   raw[0] map_idx low, raw[1] prg_mask, raw[2] high-Bits + chr_mask,
    //   raw[4] map_cfg (mirroring/chr_ram).
    try {
        QByteArray raw = ed_->memrd(kCfgBase, 16);
        if (raw.size() < 5) return std::nullopt;
        uint8_t r0 = static_cast<uint8_t>(raw[0]), r1 = static_cast<uint8_t>(raw[1]),
                r2 = static_cast<uint8_t>(raw[2]), r4 = static_cast<uint8_t>(raw[4]);
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
    // sys_cfg.sv: maske = (1<<shift)-1, deckt die Bankanzahl ab. PRG wird
    // in 8-KB-Baenken adressiert -- units16k * 2 = Baenke.
    if (units16k <= 0) return -1;
    int banks = units16k * 2;
    int shift = 0;
    while ((1 << shift) < banks) ++shift;
    return shift;
}

std::vector<rawnes::RomIndexEntry> MonitorWorker::filterByMapper(
    std::vector<rawnes::RomIndexEntry> treffer) {
    auto cfg = readMapCfg();
    if (!cfg) {
        emit log(tr("  Mapper-Konfiguration nicht lesbar -- Filter uebersprungen."));
        return treffer;
    }
    emit log(tr("  laufendes Spiel: Mapper %1, PRG-Shift %2, CHR-Shift %3, CHR-RAM %4")
             .arg(cfg->mapper).arg(cfg->prgShift).arg(cfg->chrShift).arg(cfg->chrRam ? tr("ja"): tr("nein")));

    std::vector<rawnes::RomIndexEntry> passend;
    for (auto& t : treffer) if (t.mapper == cfg->mapper) passend.push_back(t);
    if (!passend.empty() && passend.size() < treffer.size()) {
        emit log(tr("  Mapper %1 grenzt auf %2 ein.").arg(cfg->mapper).arg(passend.size()));
        treffer = passend;
    }

    if (treffer.size() > 1) {
        std::vector<rawnes::RomIndexEntry> enger;
        for (auto& t : treffer) if (shiftFor(t.prg) == cfg->prgShift) enger.push_back(t);
        if (!enger.empty() && enger.size() < treffer.size()) {
            emit log(tr("  PRG-Groesse (Shift %1) grenzt auf %2 ein.")
                     .arg(cfg->prgShift).arg(enger.size()));
            treffer = enger;
        }
    }
    if (treffer.size() > 1) {
        std::vector<rawnes::RomIndexEntry> enger;
        for (auto& t : treffer) if ((t.chr == 0) == cfg->chrRam) enger.push_back(t);
        if (!enger.empty() && enger.size() < treffer.size()) {
            emit log(tr("  CHR-Typ grenzt auf %1 ein.").arg(enger.size()));
            treffer = enger;
        }
    }
    return treffer;
}

MonitorWorker::DetectResult MonitorWorker::tryAutoDetect() {
    DetectResult result;
    if (!ed_ || !romIndex_ || romIndex_->empty()) return result;
    try {
        QByteArray vecRaw = ed_->memrd(snoopBase_ + kVecOffset, 8);
        std::array<uint8_t, 6> vec6{};
        for (int i = 0; i < 6; ++i) vec6[i] = static_cast<uint8_t>(vecRaw[2 + i]);

        static const char* hexChars = "0123456789abcdef";
        std::string hexs;
        for (int i = 0; i < 6; ++i) {
            hexs.push_back(hexChars[vec6[i] >> 4]);
            hexs.push_back(hexChars[vec6[i] & 0xF]);
            if (i < 5) hexs.push_back(' ');
        }
        result.hexKey = QString::fromStdString(hexs);

        // Ein Vektor aus lauter 0xFF (oder 0x00) ist kein Fingerabdruck,
        // sondern der Leerlaufwert des Busses. Ohne diese Pruefung lief die
        // Erkennung weiter und "erkannte" ein voellig fremdes Spiel.
        bool allFF = true, all00 = true;
        for (uint8_t b : vec6) {
            if (b != 0xFF) allFF = false;
            if (b != 0x00) all00 = false;
        }
        if (allFF || all00) {
            emit log(tr("Fingerabdruck [%1] besteht nur aus %2 -- das ist keine "
                             "gueltige Antwort aus dem Snoop-RAM, sondern der "
                             "Leerlaufwert des Busses.")
                     .arg(result.hexKey).arg(allFF ? tr("FF"): tr("00")));
            emit log(tr("Erkennung abgebrochen. Typische Ursachen: die Konsole steht im "
                     "EverDrive-Menue statt im Spiel, das Spiel wurde gerade erst "
                     "gestartet, oder die aktuelle top.rbf ist nicht deployt."));
            return result;
        }

        auto treffer = rawnes::lookup(*romIndex_, vec6);
        if (treffer.empty()) {
            emit log(tr("Fingerabdruck [")+ result.hexKey + tr("] nicht in der Sammlung."));
            return result;
        }
        if (treffer.size() == 1) {
            emit log(tr("Erkannt: ")+ QString::fromStdString(treffer.front().name));
            result.resolvedSource = QString::fromStdString(treffer.front().source);
            return result;
        }
        emit log(tr("Fingerabdruck [%1] passt auf %2 ROMs.")
                 .arg(result.hexKey).arg(treffer.size()));

        // 1) Mapper-/PRG-/CHR-Eingrenzung
        size_t vorher = treffer.size();
        treffer = filterByMapper(treffer);
        if (treffer.size() < vorher && treffer.size() == 1) {
            emit log(tr("Eindeutig ueber Mapper: ")+ QString::fromStdString(treffer.front().name));
            result.resolvedSource = QString::fromStdString(treffer.front().source);
            return result;
        }

        // 2) Page-Abgleich (tatsaechlich gelesener ROM-Code)
        if (treffer.size() > 1) {
            QByteArray pageRaw = ed_->memrd(snoopBase_ + kPageOffset, 256);
            QByteArray seenRaw = ed_->memrd(snoopBase_ + kSeenOffset, 256);
            std::array<uint8_t, 256> page{}, seen{};
            for (int i = 0; i < 256; ++i) {
                page[i] = static_cast<uint8_t>(pageRaw[i]);
                seen[i] = static_cast<uint8_t>(seenRaw[i]);
            }
            vorher = treffer.size();
            auto [eingegrenzt, nPos] = rawnes::matchPage(treffer, page, seen);
            if (nPos < 8) {
                emit log(tr("  PRG-Page noch zu wenig erfasst (%1 Byte) -- kurz "
                                 "spielen und erneut versuchen.").arg(nPos));
            } else if (eingegrenzt.size() < vorher) {
                emit log(tr("  ROM-Code (%1 Byte) grenzt von %2 auf %3 ein.")
                         .arg(nPos).arg(vorher).arg(eingegrenzt.size()));
            } else {
                // Vorher stumm: ob der Page-Abgleich ueberhaupt Daten hatte,
                // war im Log nicht zu sehen. Fuer die Fehlersuche wichtig.
                emit log(tr("  ROM-Code (%1 Byte) grenzt nicht weiter ein -- die "
                            "Kandidaten sind an den erfassten Stellen gleich.")
                         .arg(nPos));
            }
            treffer = eingegrenzt;
            if (treffer.size() == 1) {
                emit log(tr("Eindeutig ueber ROM-Code: ")+ QString::fromStdString(treffer.front().name));
                result.resolvedSource = QString::fromStdString(treffer.front().source);
                return result;
            }
        }

        // 3) RA-GameID-Eingrenzung: Kandidaten ohne RA-Eintrag verwerfen,
        // bei genau EINER eindeutigen GameID unter allen verbleibenden
        // Kandidaten ist die Auswahl egal (gleiches Spiel, andere Dump-
        // Variante) -- nimm den ersten.
        emit log(tr("  pruefe RA-Zuordnung..."));
        std::map<std::string, std::optional<long long>> gids;
        bool gidLookupOk = true;
        // Steht am Ende auf true, wenn nachweislich mehrere VERSCHIEDENE
        // RA-Spiele unter den Kandidaten sind. Dann ist die Auswahl keine
        // Geschmacksfrage mehr, sondern entscheidet ueber das Achievement-Set.
        bool verschiedeneRaSpiele = false;
        for (auto& t : treffer) {
            if (t.md5.empty()) { gidLookupOk = false; break; }
            gids[t.name] = client_->ra_gameid(t.md5);
        }
        if (gidLookupOk) {
            std::vector<rawnes::RomIndexEntry> mitSet;
            for (auto& t : treffer) if (gids[t.name].has_value()) mitSet.push_back(t);
            if (!mitSet.empty() && mitSet.size() < treffer.size()) {
                emit log(tr("  %1 ohne RA-Eintrag verworfen, %2 verbleiben.")
                         .arg(treffer.size() - mitSet.size()).arg(mitSet.size()));
                treffer = mitSet;
            }
            std::set<long long> eindeutige;
            for (auto& t : treffer) if (gids[t.name].has_value()) eindeutige.insert(*gids[t.name]);
            verschiedeneRaSpiele = (eindeutige.size() > 1);
            if (!eindeutige.empty() && eindeutige.size() == 1) {
                emit log(tr("Alle %1 Varianten gehoeren zu RA-Spiel #%2 -- nehme '%3'.")
                         .arg(treffer.size()).arg(*eindeutige.begin())
                         .arg(QString::fromStdString(treffer.front().name)));
                result.resolvedSource = QString::fromStdString(treffer.front().source);
                return result;
            }
            if (treffer.size() == 1) {
                emit log(tr("Eindeutig nach RA-Abgleich: ")+ QString::fromStdString(treffer.front().name));
                result.resolvedSource = QString::fromStdString(treffer.front().source);
                return result;
            }
        } else {
            emit log(tr("  (RA-Abfrage uebersprungen -- fehlende md5-Angabe im Index)"));
        }

        // 4) Fruehere Wahl fuer genau diesen Fingerabdruck.
        // NUR wenn NICHT feststeht, dass die Kandidaten zu verschiedenen
        // RA-Spielen gehoeren. Andernfalls wurde eine alte Wahl (z. B. die
        // Europe-Fassung) stillschweigend auf ein anderes gestartetes ROM
        // (z. B. USA) angewendet -- Folge: falsches oder gar kein
        // Achievement-Set, ohne dass je gefragt wurde. Der Fingerabdruck
        // allein kann die Varianten hier nicht trennen, deshalb darf er
        // die Entscheidung auch nicht ersetzen.
        auto merk = remembered_.find(hexs);
        if (merk != remembered_.end() && !verschiedeneRaSpiele) {
            for (auto& t : treffer) {
                if (t.source == merk->second) {
                    emit log(tr("Fruehere Wahl uebernommen: ")+ QString::fromStdString(t.name));
                    result.resolvedSource = QString::fromStdString(t.source);
                    return result;
                }
            }
        } else if (merk != remembered_.end() && verschiedeneRaSpiele) {
            emit log(tr("Fruehere Wahl NICHT uebernommen -- die Kandidaten gehoeren zu "
                        "verschiedenen RA-Spielen. Bitte bewusst auswaehlen."));
        }

        // 5) Wirklich mehrdeutig -- MainWindow soll den Nutzer fragen.
        for (auto& t : treffer) {
            QString label = QString::fromStdString(t.name);
            auto git = gids.find(t.name);
            if (git != gids.end() && git->second) label += QString(" (RA #%1)").arg(*git->second);
            result.ambiguous.emplace_back(label, QString::fromStdString(t.source));
        }
        return result;
    } catch (const std::exception& e) {
        emit log(tr("Automatische Erkennung fehlgeschlagen: ") + e.what());
        return result;
    }
}

void MonitorWorker::startMonitoring(std::shared_ptr<rawnes::RomIndexMap> romIndex,
                                    QString user, QString token, bool hardcore) {
    if (!ed_) {
        emit log(tr("Nicht verbunden -- erst mit dem EverDrive verbinden."));
        return;
    }
    romIndex_ = std::move(romIndex);
    user_ = std::move(user);
    token_ = std::move(token);
    hardcore_ = hardcore;

    auto det = tryAutoDetect();
    if (det.resolvedSource) {
        emit log(tr("Automatisch erkannt -- kein Dateidialog noetig."));
        continueWithPath(*det.resolvedSource);
    } else if (!det.ambiguous.empty()) {
        QStringList labels, sources;
        for (auto& [label, source] : det.ambiguous) { labels << label; sources << source; }
        emit romAmbiguous(det.hexKey, labels, sources);
    } else {
        emit romNotDetected(); // MainWindow zeigt Dateidialog, ruft continueWithPath()
    }
}

void MonitorWorker::seedRemembered(QStringList keys, QStringList sources) {
    for (int i = 0; i < keys.size() && i < sources.size(); ++i) {
        remembered_[keys[i].toStdString()] = sources[i].toStdString();
    }
}

void MonitorWorker::continueWithChosenRom(QString hexKey, QString source) {
    if (!hexKey.isEmpty()) {
        remembered_[hexKey.toStdString()] = source.toStdString();
        emit rememberedChanged(hexKey, source);
    }
    continueWithPath(source);
}

// ================= Laden + Poll-Loop =================

void MonitorWorker::continueWithPath(QString path) {
    if (!ed_) {
        emit log(tr("Nicht verbunden -- erst mit dem EverDrive verbinden."));
        return;
    }
    QString ext = QFileInfo(path).suffix().toLower();
    QString effectivePath = path;
    QString tempNesFile;

    if (ext != "nes") {
        emit log(tr("Entpacke Archiv..."));
        std::string err;
        auto data = rawnes::extractSingleNesFromArchive(path.toStdString(), &err);
        if (data.empty()) {
            emit log(tr("Archiv konnte nicht gelesen werden: ")+ QString::fromStdString(err));
            emit finished();
            return;
        }
        tempNesFile = QDir::temp().filePath(
            "rawnes_sel_" + QFileInfo(path).completeBaseName() + ".nes");
        QFile f(tempNesFile);
        if (!f.open(QIODevice::WriteOnly) ||
            f.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<qint64>(data.size())) != static_cast<qint64>(data.size())) {
            emit log(tr("Konnte entpackte ROM nicht zwischenspeichern."));
            emit finished();
            return;
        }
        f.close();
        effectivePath = tempNesFile;
    }

    auto [gameOpt, err] = client_->identify_and_load(
        effectivePath.toStdString(), user_.toStdString(), token_.toStdString());
    if (!tempNesFile.isEmpty()) QFile::remove(tempNesFile);

    if (!gameOpt) {
        emit log(tr("Fehler: ")+ QString::fromStdString(err));
        emit finished();
        return;
    }
    game_ = *gameOpt;
    emit gameLoaded(QString::fromStdString(game_.name));

    if (game_.no_set || game_.achievements.empty()) {
        emit log(QString::fromStdString(game_.name) + tr(": kein Achievement-Set."));
        emit finished();
        return;
    }

    tracked_.clear();
    for (const auto& ac : game_.achievements) {
        TrackedAchievement t;
        t.info = ac;
        t.rt = std::make_unique<rawnes::AchievementRuntime>(ac.mem);
        // Adressen NUR dieser einen Bedingung -- fuer die Bestaetigungs-
        // pruefung unten (SICHERUNG GEGEN FEHLBUCHUNGEN).
        std::vector<std::pair<std::string, std::string>> single{
            {std::to_string(ac.id), ac.mem}};
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
        emit log(tr("Leaderboards: %1 geladen (%2 nicht auswertbar).")
                 .arg(leaderboards_.size()).arg(unsupportedCount));
    }
    // Panel-Liste fuellen (auch wenn leer -> Panel zeigt dann "keine").
    {
        std::vector<std::pair<long long, QString>> lbList;
        for (const auto& lb : game_.leaderboards)
            lbList.emplace_back(lb.id, QString::fromStdString(lb.title));
        emit leaderboardsReady(lbList);
    }

    std::vector<std::pair<std::string, std::string>> idAndMem;
    for (const auto& ac : game_.achievements) idAndMem.emplace_back(std::to_string(ac.id), ac.mem);
    auto addrs32 = rawnes::collectAddresses(idAndMem);
    watchedAddrs_.clear();
    for (auto a : addrs32) if (a <= 0xFFFF) watchedAddrs_.push_back(static_cast<uint16_t>(a));
    changeCounts_.assign(watchedAddrs_.size(), 0);
    emit liveValuesInit(watchedAddrs_);
    emit log(tr("%1 Adressen werden ueberwacht.").arg(watchedAddrs_.size()));

    std::set<long long> validIds;
    for (const auto& a : game_.achievements) validIds.insert(a.id);

    // Checkpoint 1 von 3: direkt vor dem RA-Netzwerkaufruf (kann je nach
    // Verbindung spuerbar dauern) -- Zeitfenster-Minimierung wie im
    // Python-Original (dort insgesamt drei Pruefpunkte: hier, direkt vor
    // dem Poll-Loop, und bei jedem erkannten Reset).
    checkHardcoreAtStart();

    auto [ids, info] = client_->ra_unlocks(game_.gameid, user_.toStdString(),
                                           token_.toStdString(), validIds, hardcore_);
    alreadyUnlocked_ = ids;
    for (auto& t : tracked_) t.unlocked = alreadyUnlocked_.count(t.info.id) > 0;
    emit log(tr("%1 Achievements geladen (%2 bereits freigeschaltet).")
             .arg(tracked_.size()).arg(alreadyUnlocked_.size()));
    // NEU: fehlte bisher -- ohne dieses Signal blieb die Achievement-
    // Liste in der GUI leer, obwohl die Runtime-Objekte hier korrekt
    // aufgebaut wurden.
    emit achievementsReady(game_, alreadyUnlocked_);

    // Checkpoint 2 von 3: direkt vor Eintritt in den Poll-Loop.
    checkHardcoreAtStart();
    try {
        QByteArray rc = ed_->memrd(snoopBase_ + kResetCtrOffset, 1);
        lastResetCtr_ = static_cast<uint8_t>(rc[0]);
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
    sessionClock_.start();

    // FPGA-Build HIER nochmal lesen, nicht nur beim fruehen Connect --
    // dort laeuft die Konsole meist noch im Menue (Read schlaegt bekannt
    // fehl), jetzt sollte das Spiel bereits laufen und der Read zuverlaessig
    // sein (1:1 zu Python: der Build-Check fuer wr_bits_da passiert direkt
    // vor dem Poll-Loop, nicht beim Verbinden).
    try {
        QByteArray b = ed_->memrd(snoopBase_ + kBuildOffset, 1);
        fpgaBuild_ = static_cast<uint8_t>(b[0]);
    } catch (const std::exception&) {
        // bleibt auf dem letzten bekannten Stand (ggf. -1 vom Connect)
    }
    wrBitsAvailable_ = (fpgaBuild_ >= 9);
    if (wrBitsAvailable_) {
        emit log(tr("Schreibmarkierungen des FPGA verfuegbar -- Achievements "
                 "werden exakt freigegeben."));
    } else {
        emit log(tr("FPGA ohne Schreibmarkierungen -- Warmlauf-Heuristik aktiv "
                 "(Achievements mit konstanten Adressen koennen ausbleiben)."));
    }
    running_ = true;
    runPollLoop();
}

// ================= Hardcore-Pruefung =================

std::optional<std::vector<std::string>> MonitorWorker::hardcoreViolations() {
    try {
        QByteArray ctrl = ed_->memrd(kCfgBase + kCtrlByteOffset, 1);
        uint8_t c = static_cast<uint8_t>(ctrl[0]);
        std::vector<std::string> v;
        if (c & kCtSsOnBit) v.push_back("Savestates (\"In Game Menu\")");
        if (c & kCtGgOnBit) v.push_back("Cheats (\"Cheats\")");
        return v;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void MonitorWorker::reportViolations(const std::vector<std::string>& v, bool midSession) {
    QString liste;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) liste += " + ";
        liste += QString::fromStdString(v[i]);
    }
    hardcore_ = false;
    if (midSession) {
        emit log(tr("ACHTUNG: Nach Spielneustart sind ")+ liste +
                 tr(" am EverDrive aktiviert -- Hardcore wird fuer den Rest der Session "
                 "beendet, ab jetzt Softcore."));
    } else {
        emit log(tr("ACHTUNG: ")+ liste + tr(" sind am EverDrive aktiviert -- nur Softcore "
                 "verfuegbar. Fuer Hardcore bitte im EverDrive-System-Menue (nicht im "
                 "Ingame-Menue) deaktivieren, dann Monitor neu starten."));
    }
    emit hardcoreDowngraded(liste);
}

void MonitorWorker::checkHardcoreAtStart() {
    if (!hardcore_) return;
    auto v = hardcoreViolations();
    if (!v) {
        hardcore_ = false;
        emit log(tr("Hardcore-Pruefung fehlgeschlagen -- Ctrl-Byte nicht lesbar. "
                 "Nur Softcore verfuegbar."));
        return;
    }
    if (v->empty()) {
        emit log(tr("Hardcore-Voraussetzung erfuellt: Savestates und Cheats sind am "
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

void MonitorWorker::stop() {
    running_ = false;
}

void MonitorWorker::diagnoseAchievement(qlonglong achievementId) {
    TrackedAchievement* found = nullptr;
    for (auto& t : tracked_) if (t.info.id == achievementId) { found = &t; break; }
    if (!found) return;

    emit log(tr("\n--- %1 (%2P) ---").arg(QString::fromStdString(found->info.title))
             .arg(found->info.points));
    emit log(tr("MemAddr: ")+ QString::fromStdString(found->info.mem));

    QStringList nichtSnoopbar;
    for (auto a : found->needed) {
        bool snoopable = (a <= 0x07FF) || (a >= 0x6000 && a <= 0x7FFF);
        if (!snoopable) nichtSnoopbar << QString("$%1").arg(a, 4, 16, QChar('0')).toUpper();
    }
    if (!nichtSnoopbar.isEmpty()) {
        emit log(tr("nicht snoopbare Adressen: ")+ nichtSnoopbar.join(tr(", ")));
    }
    if (found->rt->unsupported()) {
        emit log(tr("Bedingung wird nicht unterstuetzt -- feuert nie."));
        return;
    }
    if (!havePrevRam_) {
        emit log(tr("noch keine Messwerte -- Monitor starten."));
        return;
    }
    QStringList werte;
    for (auto a : found->needed) {
        auto it = prevRam_.find(a);
        QString val = (it != prevRam_.end()) ? QString::number(it->second) : "?";
        werte << QString("$%1=%2").arg(a, 4, 16, QChar('0')).toUpper().arg(val);
    }
    emit log(tr("aktuelle Werte: ")+ werte.join(tr(", ")));
    bool frei = wrBitsAvailable_
        ? std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                        found->needed.begin(), found->needed.end())
        : (warmedUp_ || std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                      found->needed.begin(), found->needed.end()));
    emit log(frei ? tr("Adressen bestaetigt -- wird ausgewertet."): tr("wartet noch auf bestaetigte Adressen (Schutz gegen Fehlbuchungen)."));
    // HINWEIS: die feine Pro-Bedingung-Erklaerung (Python: ra_condition_nes.
    // erklaere() -- zeigt jede Zeile der Bedingung einzeln mit aktuellem
    // Hit-Stand) fehlt hier. ConditionEngine.h exponiert dafuer keine
    // Funktion und keinen Zugriff auf die internen Bedingungsgruppen --
    // ohne das wuerde ich nur raten, wie Python das intern aufbereitet.
}

// ================= Poll-Loop =================

void MonitorWorker::runPollLoop() {
    QElapsedTimer pingTimer;
    pingTimer.start();
    constexpr qint64 kPingIntervalMs = 120000;

    while (running_) {
        try {
            // Byteweise lesen, und nur die tatsaechlich gebrauchten Adressen
            // -- 1:1 zu rawnes_gui_v3_final.py. Zwei Gruende, beide aus der
            // Python-Fassung belegt:
            //  1. Der Lesepfad des Spiegels ist getaktet (ein Takt Latenz).
            //     Ein Blocktransfer memrd(..., n) liefert deshalb NICHT
            //     zuverlaessig die richtigen Werte. Der bisherige 2-KB-
            //     Blockread war genau das.
            //  2. Der Block deckte nur $0000-$07FF ab. Achievement-Adressen
            //     im Cartridge-WRAM $6000-$7FFF landeten nie in der Map und
            //     wurden von der Auswertung still als 0 gelesen -- sie
            //     konnten sich nie aendern und nie ausloesen.
            rawnes::RamMap ram;
            for (uint16_t a : watchedAddrs_) {
                auto off = snoopOffset(a);
                if (!off) continue; // nicht snoopbar -- bleibt ungesetzt
                QByteArray b1 = ed_->memrd(snoopBase_ + *off, 1);
                if (!b1.isEmpty()) ram[a] = static_cast<uint8_t>(b1[0]);
            }

            try {
                QByteArray rcBytes = ed_->memrd(snoopBase_ + kResetCtrOffset, 1);
                uint8_t rc = static_cast<uint8_t>(rcBytes[0]);
                if (haveResetCtr_ && rc != lastResetCtr_) {
                    emit log(tr("Spiel wurde neu gestartet -- Achievement-Zustand "
                            "zurueckgesetzt."));
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
                    QByteArray sst = ed_->memrd(snoopBase_ + kSstStatusOffset, 1);
                    if (static_cast<uint8_t>(sst[0]) & 0x02) {
                        sstSeenReported_ = true;
                        emit log(tr("ACHTUNG: Savestate-Aktivitaet erkannt (Audit-Bit) -- "
                                "Hardcore-Bedingung moeglicherweise verletzt."));
                    }
                } catch (const std::exception&) {}
            }

            // Live-Werte + Change-Detection ZUERST (wie in Python): eine
            // Adresse gilt als bestaetigt, sobald sich ihr Wert einmal
            // geaendert hat -- das muss vor der Achievement-Auswertung im
            // selben Zyklus schon passiert sein.
            for (size_t slot = 0; slot < watchedAddrs_.size(); ++slot) {
                uint32_t addr = watchedAddrs_[slot];
                auto it = ram.find(addr);
                uint8_t val = (it != ram.end()) ? it->second : 0;
                if (havePrevRam_) {
                    auto pit = prevRam_.find(addr);
                    uint8_t prevVal = (pit != prevRam_.end()) ? pit->second : 0;
                    if (pit != prevRam_.end() && prevVal != val) {
                        ++changeCounts_[slot];
                        confirmedAddrs_.insert(addr); // hat sich geaendert -> kein Init-Wert
                    }
                }
                emit liveValueUpdate(static_cast<int>(slot), static_cast<uchar>(val),
                                    changeCounts_[slot]);
            }

            // Warmlauf-Heuristik (Fallback ohne Schreibmarkierungen): sobald
            // genug Adressen sich eine Weile bewegt haben, gilt das Spiel
            // als initialisiert -- auch konstant bleibende Adressen (z.B.
            // Item-Flags) werden dann ausgewertet.
            if (!warmedUp_) {
                if (static_cast<int>(confirmedAddrs_.size()) >= kWarmupMinActive) {
                    if (!haveWarmSince_) {
                        warmSinceClock_.start();
                        haveWarmSince_ = true;
                    } else if (warmSinceClock_.elapsed() >= static_cast<qint64>(kWarmupSeconds * 1000)) {
                        warmedUp_ = true;
                        emit log(tr("Spiel laeuft (%1 Adressen aktiv) -- alle "
                                         "Achievements scharf.").arg(confirmedAddrs_.size()));
                    }
                } else {
                    haveWarmSince_ = false;
                }
            }

            // Nach 3s einmalig Bilanz ziehen -- sonst sitzt man vor lauter
            // $FF und weiss nicht, ob das normal ist.
            if (!warned3s_ && sessionClock_.elapsed() > 3000) {
                warned3s_ = true;
                int totalChanges = 0;
                for (int c : changeCounts_) totalChanges += c;
                if (totalChanges == 0) {
                    bool allFF = true;
                    for (size_t slot = 0; slot < watchedAddrs_.size(); ++slot) {
                        auto it = ram.find(watchedAddrs_[slot]);
                        if (it == ram.end() || it->second != 0xFF) { allFF = false; break; }
                    }
                    emit log(tr("WARNUNG: nach 3s keine einzige Wertaenderung%1.\n"
                                     "  Moegliche Gruende:\n"
                                     "  1. Laeuft am Geraet die aktuelle top.rbf mit dem Spiegel-Snooper?\n"
                                     "  2. Laeuft ueberhaupt ein Spiel (nicht das Menue)?\n"
                                     "  3. Beschreibt dieses Spiel die Adressen ueberhaupt?")
                             .arg(allFF ? tr(" -- alle Slots stehen auf $FF"): tr("")));
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
                    emit log(tr("Snooper liefert Daten: %1/%2 Adressen aendern sich.")
                             .arg(aktiv).arg(watchedAddrs_.size()));
                    if (wartet > 0) {
                        emit log(tr("%1 Achievement(s) warten noch auf echte Daten -- "
                                         "werden erst geprueft, wenn ihre Adressen bestaetigt "
                                         "sind. Schutz gegen Fehlbuchungen an RA.").arg(wartet));
                    }
                }
            }

            if (havePrevRam_) {
                for (auto& t : tracked_) {
                    if (t.unlocked || t.rt->unsupported()) continue;

                    // SICHERUNG GEGEN FEHLBUCHUNGEN: harte Schreibmarkierung
                    // pruefen (falls verfuegbar), sonst Warmlauf-Heuristik.
                    // Ohne das koennte z.B. "$6877==0" beim allerersten Poll
                    // faelschlich ausloesen, weil der Spiegel bis zum ersten
                    // echten Schreibzugriff auf $00 steht.
                    if (wrBitsAvailable_) {
                        for (auto addr : t.needed) {
                            if (confirmedAddrs_.count(addr)) continue;
                            uint32_t off;
                            if (addr <= 0x07FF) off = kRamMarkOffset + addr;
                            else if (addr >= 0x6000 && addr <= 0x7FFF) off = kWramMarkOffset + (addr - 0x6000);
                            else continue;
                            try {
                                QByteArray wb = ed_->memrd(snoopBase_ + off, 1);
                                if (static_cast<uint8_t>(wb[0]) & 0x01) confirmedAddrs_.insert(addr);
                            } catch (const std::exception&) {}
                        }
                    }
                    bool frei = wrBitsAvailable_
                        ? std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                        t.needed.begin(), t.needed.end())
                        : (warmedUp_ || std::includes(confirmedAddrs_.begin(), confirmedAddrs_.end(),
                                                      t.needed.begin(), t.needed.end()));
                    bool wasWaiting = waitingIds_.count(t.info.id) > 0;
                    if (!frei && !wasWaiting) {
                        waitingIds_.insert(t.info.id);
                        emit achievementWaitingChanged(t.info.id, true);
                    } else if (frei && wasWaiting) {
                        waitingIds_.erase(t.info.id);
                        emit achievementWaitingChanged(t.info.id, false);
                    }
                    if (!frei) continue;

                    bool fired = t.rt->update(ram, &prevRam_);
                    auto prog = t.rt->progress();
                    if (prog.hasProgress) emit progressChanged(t.info.id, prog.current, prog.target);
                    if (fired) {
                        t.unlocked = true;
                        auto [ok, info] = client_->ra_award(t.info.id, user_.toStdString(),
                                                            token_.toStdString(), hardcore_);
                        emit unlocked(QString::fromStdString(t.info.title), t.info.points,
                                     t.info.id, hardcore_);
                        emit log(tr("Achievement freigeschaltet: %1 (%2)")
                                 .arg(QString::fromStdString(t.info.title))
                                 .arg(ok ? tr("gebucht"): (tr("Fehler: ") + QString::fromStdString(info))));
                    }
                }
            }

            // Leaderboards auswerten. RA-Regel: Eintraege zaehlen nur im
            // Hardcore-Modus -- start/cancel werden trotzdem verarbeitet
            // (Zustand muss weiterlaufen), submit wird nur im Hardcore-
            // Modus tatsaechlich eingereicht.
            if (havePrevRam_) {
                for (auto& lb : leaderboards_) {
                    double val = 0;
                    auto ev = lb.update(ram, &prevRam_, &val);
                    switch (ev) {
                        case rawnes::LeaderboardRuntime::Event::Start:
                            emit leaderboardStarted(lb.id(), QString::fromStdString(lb.title()));
                            emit log(tr("  Leaderboard laeuft: ")+ QString::fromStdString(lb.title()));
                            break;
                        case rawnes::LeaderboardRuntime::Event::Cancel:
                            emit leaderboardCanceled(lb.id(), QString::fromStdString(lb.title()));
                            emit log(tr("  Leaderboard abgebrochen: ")+ QString::fromStdString(lb.title()));
                            break;
                        case rawnes::LeaderboardRuntime::Event::Submit: {
                            QString formatted = QString::number(val, 'f', 0);
                            if (!hardcore_) {
                                emit log(tr("  Leaderboard %1: Wert %2 -- NICHT eingereicht "
                                                 "(nur im Hardcore-Modus gueltig).")
                                         .arg(QString::fromStdString(lb.title()), formatted));
                                break;
                            }
                            auto [ok, info] = client_->ra_submit_lb(lb.id(), user_.toStdString(),
                                                                    token_.toStdString(),
                                                                    static_cast<uint32_t>(val));
                            emit leaderboardSubmitted(lb.id(), QString::fromStdString(lb.title()), formatted);
                            emit log(tr("  *** Leaderboard %1: %2 (%3) ***")
                                     .arg(QString::fromStdString(lb.title()), formatted,
                                         ok ? tr("eingereicht"): (tr("Fehler: ") + QString::fromStdString(info))));
                            break;
                        }
                        case rawnes::LeaderboardRuntime::Event::None:
                            break;
                    }
                }
            }

            prevRam_ = ram;
            havePrevRam_ = true;

            if (pingTimer.elapsed() > kPingIntervalMs) {
                std::string richPresence;
                if (!game_.richPresenceScript.empty()) {
                    auto compiled = rawnes::parseRichPresenceScript(game_.richPresenceScript);
                    if (auto text = rawnes::evaluateRichPresence(compiled)) richPresence = *text;
                }
                auto [ok, info] = client_->ra_ping(game_.gameid, user_.toStdString(),
                                                   token_.toStdString(), richPresence);
                emit log(tr("RA-Aktivitaet: %1").arg(ok ? tr("gemeldet"):
                         (tr("Fehler: ") + QString::fromStdString(info))));
                pingTimer.restart();
            }

            QThread::msleep(50);
        } catch (const std::exception& e) {
            emit connectionLost(QString::fromUtf8(e.what()));
            emit log(tr("Verbindung verloren: ")+ QString::fromUtf8(e.what()));
            QThread::msleep(1000);
        }
    }
    emit log(tr("Monitor gestoppt."));
    emit finished();
}
