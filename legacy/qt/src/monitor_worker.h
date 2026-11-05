#pragma once
#include "ConditionEngine.h"
#include "ed_serial_qt.h"
#include "ra_client.h"
#include "RichPresence.h"
#include "RomIndex.h"
#include "rawnes_addr.h"

#include <QMutex>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include <utility>

// Noetig fuer Cross-Thread-Signale (QueuedConnection).
Q_DECLARE_METATYPE(std::vector<uint16_t>)
Q_DECLARE_METATYPE(std::shared_ptr<rawnes::RomIndexMap>)
Q_DECLARE_METATYPE(GameData)
Q_DECLARE_METATYPE(std::set<long long>)
// WICHTIG: std::pair<long long, QString> enthaelt ein Komma. In einem
// Makro-Argument wird das vom Praeprozessor als Argument-Trenner gelesen
// (Q_DECLARE_METATYPE bekaeme scheinbar 2 Argumente) -> Makro expandiert
// nicht -> MonitorWorker wird nie sauber definiert -> "incomplete type".
// Deshalb den Typ vorher als kommafreien Alias kapseln.
using LeaderboardList = std::vector<std::pair<long long, QString>>;
Q_DECLARE_METATYPE(LeaderboardList)

// WICHTIG (Architekturwechsel ggue. der ersten Fassung): Dieser Worker
// wird jetzt EINMAL in MainWindow's Konstruktor erzeugt und lebt fuer
// die gesamte App-Laufzeit in seinem eigenen QThread -- NICHT mehr bei
// jedem Klick auf "Start" neu gebaut. Grund: QSerialPort (in EdSerial)
// darf laut Qt nur aus dem Thread heraus benutzt werden, in dem es
// erzeugt wurde. Python teilt `self.es` einfach ueber Threads hinweg
// (GIL macht das gutmuetig) -- das ist in Qt so nicht sicher. Die
// EverDrive-Verbindung entsteht daher ausschliesslich innerhalb dieses
// Worker-Threads (in connectToDevice()) und bleibt dort fuer alle
// weiteren Aufrufe (Auto-Erkennung, Polling) exklusiv.
//
// Ablauf, jetzt 1:1 zu rawnes_gui_v3_final.py:
//  1. MainWindow ruft kurz nach dem Start connectToDevice() auf
//     (entspricht self.after(200, self._connect)) -- laeuft EINMAL,
//     unabhaengig vom Spielstart.
//  2. Klick auf "Spiel laden & Monitor starten" -> startMonitoring():
//     versucht zuerst automatische Erkennung per Live-Vektor-Fingerabdruck
//     (RomIndex-Abgleich, wie _detect_rom() in Python). Bei Treffer geht's
//     direkt weiter zu continueWithPath(). Bei keinem Treffer sendet der
//     Worker romNotDetected() -- MainWindow zeigt dann (wie in Python)
//     einen Dateidialog und ruft continueWithPath() mit der Wahl auf.
//  3. continueWithPath(): Archiv ggf. entpacken, identify_and_load()
//     (hasht die tatsaechliche Datei -- WICHTIG: auch nach Auto-Erkennung
//     wird die Datei neu gehasht, nicht der im Index gespeicherte md5
//     uebernommen, exakt wie in Python _start()), Achievements/Watches
//     aufbauen, Poll-Loop starten.
class MonitorWorker : public QObject {
    Q_OBJECT
public:
    explicit MonitorWorker(std::shared_ptr<RaClient> client);

public slots:
    // Einmal beim App-Start aufrufen (und erneut bei "Erneut verbinden").
    void connectToDevice(QString preferred = QString());

    // Gespeicherte Mehrdeutigkeits-Entscheidungen aus der Config-Datei
    // uebernehmen (parallel keys/sources statt std::map, damit das
    // problemlos per QueuedConnection uebergeben werden kann).
    void seedRemembered(QStringList keys, QStringList sources);

    // Entspricht Klick auf "Spiel laden & Monitor starten".
    void startMonitoring(std::shared_ptr<rawnes::RomIndexMap> romIndex,
                         QString user, QString token, bool hardcore);

    // MainWindow ruft das nach Dateidialog-Auswahl auf (nur wenn
    // romNotDetected() gefeuert hat), ODER intern direkt nach Auto-Treffer.
    void continueWithPath(QString path);

    // MainWindow ruft das nach Nutzerwahl im Mehrdeutigkeits-Dialog auf
    // (romAmbiguous()). hexKey leer = nichts merken (kommt vom manuellen
    // Dateidialog-Fallback, nicht von einem Fingerabdruck-Treffer).
    void continueWithChosenRom(QString hexKey, QString source);

    // Einfachklick auf ein Achievement -- Diagnose in den Log schreiben.
    // Entspricht Pythons _erklaere_achievement() TEILWEISE: MemAddr,
    // benoetigte Adressen, nicht-snoopbare darunter, unsupported-Flag und
    // aktuelle Werte werden gezeigt. Die feine Pro-Bedingung-Erklaerung
    // (Python: ra_condition_nes.erklaere()) fehlt -- ConditionEngine.h
    // exponiert dafuer keine Funktion, ohne die will ich nicht raten.
    void diagnoseAchievement(qlonglong achievementId);

    void stop();

signals:
    void log(const QString& msg);
    void connected(const QString& port);
    void connectionFailed(const QString& reason);
    void connectAttempt(int attempt, int totalAttempts); // fuer sichtbaren Fortschritt statt starrem "verbinde..."
    void unlocked(const QString& title, int points, qlonglong achId, bool hardcore);
    void progressChanged(qlonglong achId, int current, int target);
    // "wartet auf bestaetigte Adressen" -- nur bei Zustandswechsel gesendet
    // (kein Signal-Spam jeden Poll-Zyklus).
    void achievementWaitingChanged(qlonglong achId, bool waiting);
    void connectionLost(const QString& reason);
    void hardcoreDowngraded(const QString& liste);
    void finished(); // Session (Polling) beendet -- Worker selbst laeuft weiter

    // Leaderboard-Ereignisse (NEU, kein Python-Vorbild -- siehe
    // ConditionEngine.h/LeaderboardRuntime-Kommentar). Laut RA-Regel
    // zaehlen Leaderboard-Eintraege nur im Hardcore-Modus -- start/cancel
    // werden trotzdem geloggt (Diagnose), submitted nur bei hardcore_ true.
    void leaderboardStarted(qlonglong lbId, const QString& title);
    void leaderboardCanceled(qlonglong lbId, const QString& title);
    void leaderboardSubmitted(qlonglong lbId, const QString& title, const QString& formattedValue);
    // Beim Spielstart: alle geladenen Leaderboards (ID + Titel), damit das
    // Panel die Liste aufbauen kann. Ohne dieses Signal blieb das Panel leer.
    void leaderboardsReady(const LeaderboardList& idsAndTitles);

    // Auto-Erkennung ohne Treffer -- MainWindow soll Dateidialog zeigen.
    void romNotDetected();
    // Auto-Erkennung mehrdeutig (mehrere ROMs teilen sich den Vektor-
    // Fingerabdruck, auch nach Mapper-/Page-/RA-Eingrenzung nicht mehr
    // reduzierbar) -- MainWindow soll den Nutzer waehlen lassen.
    // hexKey wird bei continueWithChosenRom() mitgegeben, damit die Wahl
    // fuer die restliche Sitzung gemerkt werden kann (wie Python
    // self._remembered).
    void romAmbiguous(QString hexKey, QStringList labels, QStringList sources);
    void gameLoaded(const QString& name);
    // NEU: komplette Spieldaten fuer die Achievement-Liste. gameLoaded()
    // oben schickt nur den Namen (fuer den Status-Text), dieses Signal
    // separat mit den vollen Daten -- war der Grund, warum die Liste nach
    // dem Architekturwechsel auf Auto-Erkennung leer blieb (niemand rief
    // mehr achList_->setGame() auf).
    void achievementsReady(GameData game, std::set<long long> alreadyUnlocked);
    // Neue Mehrdeutigkeits-Entscheidung getroffen -- MainWindow soll das
    // dauerhaft in die Config-Datei schreiben (Python: self._remembered[hexs]
    // = wahl["source"]; self._save_config()).
    void rememberedChanged(QString hexKey, QString source);

    void liveValuesInit(const std::vector<uint16_t>& addresses);
    void liveValueUpdate(int slot, uchar value, int changeCount);

private:
    struct TrackedAchievement {
        Achievement info;
        std::unique_ptr<rawnes::AchievementRuntime> rt;
        bool unlocked = false;
        // Adressen, die diese eine Bedingung braucht -- fuer die
        // "bestaetigt/wartet noch"-Pruefung (siehe SICHERUNG GEGEN
        // FEHLBUCHUNGEN in continueWithPath()/runPollLoop()).
        std::set<uint32_t> needed;
    };

    std::optional<std::vector<std::string>> hardcoreViolations();
    void checkHardcoreAtStart();
    void checkHardcoreOnReset();
    void reportViolations(const std::vector<std::string>& v, bool midSession);

    // Ergebnis der Auto-Erkennung: entweder eindeutig aufgeloest
    // (resolvedSource gesetzt), oder mehrdeutig (ambiguous gefuellt,
    // Paare aus Anzeigetext + Quellpfad), oder gar kein Treffer (beides leer).
    struct DetectResult {
        std::optional<QString> resolvedSource;
        std::vector<std::pair<QString, QString>> ambiguous; // label, source
        QString hexKey;
    };
    // Live-Vektor-Fingerabdruck gegen romIndex_ pruefen, dann -- bei
    // Mehrdeutigkeit -- ueber Mapper-Konfiguration, PRG/CHR-Groesse,
    // gelesenen ROM-Code (Page) und RA-GameID eingrenzen (1:1 zu Python
    // _detect_rom()/_filter_by_mapper()). Laedt NICHTS -- das eigentliche
    // Laden/Hashen passiert danach immer ueber continueWithPath(),
    // exakt wie in Python (identify_and_load() hasht die Datei neu,
    // unabhaengig vom im Index gespeicherten md5).
    DetectResult tryAutoDetect();

    struct MapCfg { int mapper = -1; int prgShift = -1; int chrShift = -1; bool chrRam = false; int mirror = 0; };
    std::optional<MapCfg> readMapCfg();
    static int shiftFor(int units16k);
    std::vector<rawnes::RomIndexEntry> filterByMapper(std::vector<rawnes::RomIndexEntry> treffer);

    void runPollLoop();

    std::unique_ptr<EdSerial> ed_;
    QString port_;
    // Zur Laufzeit ermittelte Snoop-Basisadresse (siehe rawnes_addr.h-
    // Kommentar): wird in connectToDevice() durch Ausprobieren beider
    // Kandidaten gesetzt, entspricht Pythons ADDR_SNOOP-Auto-Erkennung.
    uint32_t snoopBase_ = RawnesAddr::kSnoopBase;
    // hexKey (6-Byte-Vektor als Hex) -> gewaehlter Quellpfad, fuer die
    // restliche Sitzung gemerkt (entspricht Pythons self._remembered,
    // dort zusaetzlich auf Platte persistiert -- hier bewusst nur
    // In-Memory fuer diese Sitzung, kein Config-Speichermechanismus
    // in dieser Klasse vorhanden).
    std::map<std::string, std::string> remembered_;

    std::shared_ptr<RaClient> client_;
    std::shared_ptr<rawnes::RomIndexMap> romIndex_;
    QString user_;
    QString token_;
    GameData game_;
    std::set<long long> alreadyUnlocked_;

    std::atomic<bool> running_{false};
    std::atomic<bool> hardcore_{false};

    std::vector<TrackedAchievement> tracked_;
    std::vector<rawnes::LeaderboardRuntime> leaderboards_;
    rawnes::RamMap prevRam_;
    bool havePrevRam_ = false;
    uint8_t lastResetCtr_ = 0;
    bool haveResetCtr_ = false;
    bool sstSeenReported_ = false;

    std::vector<uint16_t> watchedAddrs_;
    std::vector<int> changeCounts_;

    // ================= SICHERUNG GEGEN FEHLBUCHUNGEN (1:1 Python) =================
    // Der FPGA-Spiegel steht auf $00 bis das Spiel eine Adresse zum ersten
    // Mal beschreibt -- ohne diese Absicherung wuerde z.B. "$6877==0" sofort
    // beim ersten Poll faelschlich ausloesen. Eine Adresse gilt erst als
    // "bestaetigt", wenn (a) ihr Wert sich mindestens einmal geaendert hat,
    // oder (b) das FPGA ein Schreibmarkierungs-Bit dafuer liefert (ab Build
    // 9, hart) bzw. (c) als Fallback: genug andere Adressen haben sich eine
    // Weile bewegt (Warmlauf-Heuristik, WARMUP_MIN_ACTIVE/WARMUP_SECONDS).
    std::set<uint32_t> confirmedAddrs_;
    bool wrBitsAvailable_ = false; // FPGA-Build >= 9
    int fpgaBuild_ = -1;
    bool warmedUp_ = false;
    bool haveWarmSince_ = false;
    QElapsedTimer warmSinceClock_;
    QElapsedTimer sessionClock_;
    bool warned3s_ = false;
    static constexpr int kWarmupMinActive = 3;
    static constexpr double kWarmupSeconds = 5.0;
    std::set<long long> waitingIds_; // aktuell als "wartet" gemeldete Achievement-IDs
};
