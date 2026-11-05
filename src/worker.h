// Monitor-Worker: Verbindung, Spielerkennung, Poll-Schleife.
//
// Logik 1:1 aus der Qt-Fassung (monitor_worker.h/.cpp), dort auch die
// ausfuehrlichen Begruendungen. Geaendert ist nur die Verdrahtung:
//  - Statt QThread + QueuedConnection ein eigener std::thread mit
//    Auftragswarteschlange. Auftraege laufen der Reihe nach in diesem
//    Thread, die EverDrive-Verbindung lebt ausschliesslich dort.
//  - Statt Qt-Signalen Rueckrufe (std::function). Sie werden im
//    Worker-Thread aufgerufen; die Oberflaeche reicht sie in ihre eigene
//    Warteschlange weiter (siehe App::Post).
//  - diagnoseAchievement() wird auch WAEHREND der Poll-Schleife ausgefuehrt.
//    In der Qt-Fassung blieb der Klick bis zum Stoppen liegen, weil die
//    Ereignisschleife des Worker-Threads in der Poll-Schleife nicht lief.
#pragma once

#include "ConditionEngine.h"
#include "RichPresence.h"
#include "RomIndex.h"
#include "Plausibel.h"
#include "ed_serial.h"
#include "ra_client.h"
#include "rawnes_addr.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using LeaderboardList = std::vector<std::pair<long long, std::string>>;

class MonitorWorker {
public:
    explicit MonitorWorker(std::shared_ptr<RaClient> client);
    ~MonitorWorker();

    // --- Auftraege (aus der Oberflaeche; laufen im Worker-Thread) ---------
    void connectToDevice(const std::string& preferred = std::string());
    void seedRemembered(const std::map<std::string, std::string>& remembered);
    void startMonitoring(std::shared_ptr<rawnes::RomIndexMap> romIndex,
                         const std::string& user, const std::string& token, bool hardcore);
    void continueWithPath(const std::string& path);
    void continueWithChosenRom(const std::string& hexKey, const std::string& source);
    void diagnoseAchievement(long long achievementId);

    // Direkt, nicht ueber die Warteschlange: setzt nur das atomare Flag.
    void stop();

    // --- Rueckrufe (werden im Worker-Thread aufgerufen) -------------------
    std::function<void(const std::string&)> log;
    std::function<void(const std::string&)> connected;
    std::function<void(const std::string&)> connectionFailed;
    std::function<void(int, int)> connectAttempt;
    std::function<void(const std::string&, int, long long, bool)> unlocked;
    std::function<void(long long, int, int)> progressChanged;
    std::function<void(long long, bool)> achievementWaitingChanged;
    std::function<void(const std::string&)> connectionLost;
    // Spiel ist weg (Konsole aus, Spiel verlassen): Oberflaeche in den Grundzustand.
    std::function<void()> spielVerlassen;
    // Ohne laufenden Monitor: 1 = kein Spiel aktiv (Menue oder Konsole aus), 2 = Spiel laeuft.
    std::function<void(int)> leerlaufStatus;
    std::function<void(const std::string&)> hardcoreDowngraded;
    std::function<void()> finished;
    std::function<void(long long, const std::string&)> leaderboardStarted;
    std::function<void(long long, const std::string&)> leaderboardCanceled;
    std::function<void(long long, const std::string&, const std::string&)> leaderboardSubmitted;
    std::function<void(const LeaderboardList&)> leaderboardsReady;
    std::function<void()> romNotDetected;
    // hexKey, Anzeigetexte, Quellpfade
    std::function<void(const std::string&, const std::vector<std::string>&,
                       const std::vector<std::string>&)> romAmbiguous;
    std::function<void(const std::string&)> gameLoaded;
    std::function<void(const GameData&, const std::set<long long>&)> achievementsReady;
    std::function<void(const std::string&, const std::string&)> rememberedChanged;
    std::function<void(const std::vector<uint16_t>&)> liveValuesInit;
    std::function<void(int, uint8_t, int)> liveValueUpdate;

private:
    using Clock = std::chrono::steady_clock;

    void post(std::function<void()> auftrag);
    void run();
    void runPendingDiagnose();
    void pruefeLeerlauf();
    int leerlaufZustand_ = 0;   // 0 unbekannt, 1 EverDrive-Menue, 2 Spiel laeuft, 3 Konsole aus

    // Rumpf der oeffentlichen Auftraege, im Worker-Thread.
    void doConnect(const std::string& preferred);
    void doStartMonitoring(std::shared_ptr<rawnes::RomIndexMap> romIndex,
                           const std::string& user, const std::string& token, bool hardcore);
    // md5: bekannter RA-Hash (aus der gelesenen Datei) -- dann wird path nicht geoeffnet.
    void doContinueWithPath(const std::string& path, const std::string& md5 = std::string());
    void doContinueWithChosenRom(const std::string& hexKey, const std::string& source);
    void doDiagnose(long long achievementId);

    struct TrackedAchievement {
        Achievement info;
        std::unique_ptr<rawnes::AchievementRuntime> rt;
        bool unlocked = false;
        std::set<uint32_t> needed;
    };

    std::optional<std::vector<std::string>> hardcoreViolations();
    void checkHardcoreAtStart();
    void checkHardcoreOnReset();
    void reportViolations(const std::vector<std::string>& v, bool midSession);

    struct DetectResult {
        std::optional<std::string> resolvedSource;
        std::string exactMd5;     // RA-Hash der vom EverDrive gestarteten Datei
        std::vector<std::pair<std::string, std::string>> ambiguous;  // label, source
        std::string hexKey;
    };
    DetectResult tryAutoDetect();

    struct MapCfg { int mapper = -1; int prgShift = -1; int chrShift = -1; bool chrRam = false; int mirror = 0; };
    std::optional<MapCfg> readMapCfg();
    static int shiftFor(int units16k);
    std::vector<rawnes::RomIndexEntry> filterByMapper(std::vector<rawnes::RomIndexEntry> treffer);

    void runPollLoop();

    // Bildpuffer (FPGA-Build >= 12), siehe worker.cpp.
    bool richteRingEin();
    void ringAus();
    bool leseRingBild(uint8_t f, rawnes::RamMap& ram);
    static constexpr int kRingBuild = 12;
    static constexpr uint32_t kRingGenOffset  = 0x0820;   // lesen: Bildnummer; schreiben: Steuerung
    static constexpr uint32_t kRingCtrlOffset = 0x0820;
    static constexpr uint32_t kRingCntOffset  = 0x0821;
    static constexpr uint32_t kRingStatOffset = 0x0822;   // Bit0 an, Bit1 Kopie laeuft
    static constexpr uint32_t kRingLoOffset   = 0x0C00;
    static constexpr uint32_t kRingHiOffset   = 0x0D00;
    static constexpr uint32_t kRingBufOffset  = 0x6000;
    static constexpr uint8_t  kRingAbschnitte = 16;
    static constexpr size_t   kRingMaxAdressen = 252;
    bool ringAktiv_ = false;
    uint8_t ringGen_ = 0;
    std::vector<uint16_t> ringAddrs_;
    long long ringBilder_ = 0;
    long long ringLuecken_ = 0;

    void emitLog(const std::string& s) { if (log) log(s); }

    // --- Thread und Warteschlange ---
    std::thread thread_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::function<void()>> queue_;
    std::deque<long long> diagnoseQueue_;
    bool quit_ = false;

    // --- Zustand (nur im Worker-Thread benutzt) ---
    std::unique_ptr<EdSerial> ed_;
    std::string port_;
    uint32_t snoopBase_ = RawnesAddr::kSnoopBase;
    std::map<std::string, std::string> remembered_;

    std::shared_ptr<RaClient> client_;
    std::shared_ptr<rawnes::RomIndexMap> romIndex_;
    std::string user_;
    std::string token_;
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

    // SICHERUNG GEGEN FEHLBUCHUNGEN (1:1 Python), siehe monitor_worker.h.
    std::set<uint32_t> confirmedAddrs_;
    bool wrBitsAvailable_ = false;  // FPGA-Build >= 9
    int fpgaBuild_ = -1;
    int unplausibelFolge_ = 0;
    // Abfragen warten auf Bestaetigung durch die naechsten (core/Plausibel).
    rawnes::Abfragefilter filter_;   // Abfragen in Folge ohne laufenden Kern / mit Leerlaufwerten
    bool warmedUp_ = false;
    bool haveWarmSince_ = false;
    Clock::time_point warmSince_;
    Clock::time_point sessionStart_;
    bool warned3s_ = false;
    static constexpr int kWarmupMinActive = 3;
    static constexpr double kWarmupSeconds = 5.0;
    std::set<long long> waitingIds_;
};
