// Hauptfenster -- Gegenstueck zu MainWindow und den Panels der Qt-Fassung
// (mainwindow.cpp, achievement_list_widget, achievement_row, live_values_widget,
// leaderboard_panel, hardcore_panel, log_panel, fpga_installer_panel).
//
// Aufbau, Texte, Farben und Ablaeufe folgen der Qt-Fassung. Weil Dear ImGui
// jedes Bild neu zeichnet, gibt es keine Widgets mit eigenem Zustand: der
// Zustand steht hier in Membern, Zeichnen() stellt ihn dar.
#pragma once

#include "bilder.h"
#include "einstellungen.h"
#include "fpga_installer.h"
#include "ra_cache.h"
#include "ra_client.h"
#include "RomIndex.h"
#include "worker.h"

#include "imgui.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct Schriften {
    ImFont* ui = nullptr;         // Segoe UI
    ImFont* fett = nullptr;       // Segoe UI fett
    ImFont* mono = nullptr;       // Consolas (Log, Live-Werte)
    ImFont* monoFett = nullptr;   // Consolas fett (Live-Werte)
    ImFont* courierFett = nullptr;    // Intro
    ImFont* courierKursiv = nullptr;  // Intro
};

class App {
public:
    App(void* fenster, float dpi, const Schriften& schriften);
    ~App();

    // Nach dem Anlegen des OpenGL-Kontexts.
    void Starten();
    // Ein Bild.
    void Zeichnen();
    // Fensterbreite je nach Leaderboard-Seitenleiste (logische Punkte).
    static constexpr float kBreite = 760.0f;
    static constexpr float kHoehe = 900.0f;

    // Aus beliebigem Thread: Auftrag fuer den Oberflaechen-Thread.
    void Post(std::function<void()> f);

    // Fuer die Absturzspur: das Log, wie es im Fenster steht.
    const std::deque<std::string>& LogZeilen() const { return log_; }

private:
    // ---------------- Zustand der Oberflaeche ----------------
    enum class AchState { Locked, Unlocked, JustUnlocked, Unsupported, Waiting };
    struct AchZeile {
        long long id = 0;
        std::string title, desc, badge;
        int points = 0;
        AchState state = AchState::Locked;
        int progCur = 0, progTarget = 0;
        bool showProgress = false;
    };
    struct LiveZeile {
        uint16_t addr = 0;
        int value = -1;
        int changeCount = 0;
        unsigned long long lastChangeMs = 0;
    };
    struct LbZeile {
        long long id = 0;
        std::string title, status;
    };

    // Eigene modale Dialoge (QMessageBox, QInputDialog, Bestaetigung).
    struct Dialog {
        enum class Art { Info, Warnung, Fehler, Frage, Auswahl, Textfeld, Katze, Nachricht, Ersteinrichtung };
        Art art = Art::Info;
        std::string titel, text;
        std::string textEn, link;   // Nachricht: englische Fassung, optionaler Link
        int schritt = 0;            // Ersteinrichtung: 0 Sprache, 1 Spielordner, 2 Mapper
        bool indexJetzt = false;    // Ersteinrichtung: in Schritt 1 gewaehlt
        std::string sprache;        // Ersteinrichtung: gewaehlte Sprache
        std::vector<std::string> optionen;
        int wahl = 0;
        std::function<void()> ja, nein;
        std::function<void(int)> gewaehlt;
        bool geoeffnet = false;
        int nummer = 0;
    };

    // ---------------- Ablaeufe (wie MainWindow-Slots) ----------------
    void onLogin();
    void onIndexRoms();
    void onToggleMonitor();
    void onReconnect();
    void onOpenSupport();
    void ZeigeKatze();
    void setLanguage(const std::string& lang);
    void checkForUpdate();     // version_rawnes.txt auf der Projektseite
    void ZeigeAbschluss();     // Schlussfenster der Ersteinrichtung
    bool mapperSpaeter_ = false;        // Ersteinrichtung: Mapper uebersprungen
    bool abschlussNachIndex_ = false;   // Abschluss erst, wenn der Index fertig ist
    void NachrichtHolen();
    unsigned long long logKopiertMs_ = 0;
    // Hoehenverteilung im Koerper (siehe ZeichneKoerper): Liste, Log, Rest.
    float listH_ = 186.0f, logH_ = 0.0f, fixHoehe_ = 0.0f;
    bool startNachVerbinden_ = false;   // nach dem Verbinden gleich den Monitor starten   // "Log kopieren": Zeitpunkt fuer "Kopiert!"     // nachricht_rawnes.json auf der Projektseite
    void toggleLbSidebar(bool open);
    void appendLog(const std::string& msg);
    void loadConfig();
    void saveConfig();
    void updateIndexLabel();
    std::string configPath() const;
    std::string romIndexPath() const;

    void verdrahteWorker();

    // ---------------- FPGA-Mapper (FpgaInstallerPanel) ----------------
    void setCoresDir(const std::string& dir);
    std::string embeddedCoresDir();
    void ensureCoresDir(bool aktiv, std::function<void()> weiter);
    std::pair<std::string, std::string> resolveMapsDir(const std::string& picked);
    // direkt: ohne Rueckfrage gleich die Laufwerkswahl (Ersteinrichtung).
    void checkCores(bool aktiv, bool direkt = false);
    void doInstall(const std::string& mapsDir, const std::vector<std::string>& files);
    void onRestoreClicked();
    void restoreMitMaps(const std::string& maps);

    // ---------------- Dialoge ----------------
    void Info(const std::string& titel, const std::string& text, std::function<void()> ok = nullptr);
    void Warnung(const std::string& titel, const std::string& text);
    void Fehler(const std::string& titel, const std::string& text);
    void Frage(const std::string& titel, const std::string& text,
               std::function<void()> ja, std::function<void()> nein = nullptr);
    void ZeichneDialog();

    // ---------------- Badges ----------------
    struct Badge {
        enum class Zustand { Laedt, Fertig, Fehlt } zustand = Zustand::Laedt;
        bilder::Textur tex;
        std::string png;
    };
    Badge* BadgeHolen(const std::string& name);
    void BadgeThread();

    // ---------------- Zeichnen ----------------
    void ZeichneKopf();
    void ZeichneKoerper();
    void ZeichneRa();
    void ZeichneLoginStatus(const std::string& status);
    void ZeichneEverDrive();
    void ZeichneAchievements();
    void ZeichneLiveWerte();
    void ZeichneLog();
    void ZeichneSeitenleiste();
    void ZeichneOptionenMenue();
    struct Gruppe {
        ImVec2 start;
        float breite = 0, innen = 0, inhaltX = 0;
        float titelBreite = 0, titelHoehe = 0;
        std::string titel;
    };
    std::vector<Gruppe> gruppen_;
    // id: feste Kennung (der Titel wechselt mit der Sprache). Liefert true,
    // wenn der Titel angeklickt wurde (nur mit klickbar).
    bool GruppeBeginn(const char* id, const std::string& titel, bool klickbar = false);
    void GruppeEnde();
    void ZeichneAchZeile(AchZeile& z, float breite);
    void AuswahlZeile(long long id);

    // ---------------- Felder ----------------
    void* fenster_ = nullptr;
    float dpi_ = 1.0f;
    Schriften f_;

    RaCache cache_;
    std::shared_ptr<RaClient> client_;
    std::unique_ptr<MonitorWorker> worker_;

    std::mutex postMutex_;
    std::vector<std::function<void()>> postQueue_;

    std::string token_;
    std::shared_ptr<rawnes::RomIndexMap> romIndex_;
    Einstellungen cfg_;
    bool coresDirManual_ = false;
    std::string coresDir_;
    std::unique_ptr<rawnes::FpgaInstaller> installer_;
    bool connected_ = false;
    // Automatisch verbinden und starten (05.11.2026):
    bool autoVerbinden_ = false;      // laufender Verbindungsversuch kam von selbst
    long long naechsteVerbindungMs_ = 0;
    bool autoPause_ = false;
    bool spielWeg_ = false;           // Monitor endete, weil das Spiel weg ist (nicht von Hand)
    std::string letzterPort_;          // Monitor hat aufgehoert: fuer dieses Spiel nicht von selbst neu starten
    void autoVerbindenPruefen();
    int leerlauf_ = 0;                // ohne Monitor: 1 EverDrive-Menue, 2 Spiel laeuft, 3 Konsole aus
    bool monitoring_ = false;

    // Eingabefelder
    char userBuf_[128] = {};
    char passBuf_[128] = {};
    bool rememberLogin_ = true;
    bool hardcore_ = false;

    // Anzeigen. *Key = deutscher Quelltext, wird je Bild uebersetzt.
    std::string loginStatusKey_ = "nicht angemeldet";
    std::string loginStatusText_;     // gesetzt: wortwoertlich statt Key
    bool loginOk_ = false;
    double anmeldStart_ = -1.0;       // Einblendung nach der Anmeldung (ImGui::GetTime)
    bool anmeldAuto_ = false;         // per gespeichertem Sitzungsschluessel
    std::string anmeldUser_;
    bool loginLaeuft_ = false;
    std::string connStatusKey_ = "nicht verbunden";
    std::string connPort_;            // gesetzt: "verbunden (PORT)"
    std::string connAttempt_;         // "verbinde... (Versuch 1/1)"
    ImU32 connFarbe_ = 0;
    std::string gameStatus_;          // leer: "kein Spiel geladen"
    std::string indexStatus_;         // leer: aus romIndex_ abgeleitet
    bool indexLaeuft_ = false;
    std::string monitorBtnKey_ = "Spiel erkennen & Monitor starten";
    bool monitorBtnEnabled_ = false;
    bool monitorBtnRot_ = false;
    bool hardcoreEnabled_ = true;

    std::vector<AchZeile> ach_;
    float achExtra_ = 0.0f;   // freier Platz, um den die Achievement-Gruppe waechst
    long long selectedId_ = -1;
    std::map<int, LiveZeile> live_;
    std::vector<LbZeile> lbs_;
    bool lbSidebarOpen_ = false;

    std::deque<std::string> log_;
    bool logOffen_ = false;
    bool logAmEnde_ = true;

    std::deque<Dialog> dialoge_;
    int dialogNummer_ = 0;
    bool optionenOffen_ = false;

    bilder::Textur logo_;

    std::mutex badgeMutex_;
    std::map<std::string, Badge> badges_;
    std::deque<std::string> badgeWarteschlange_;
    std::condition_variable badgeCv_;
    std::thread badgeThread_;
    bool badgeEnde_ = false;
    struct BadgeErgebnis { std::string name; bool ok; std::string png; int w, h; std::vector<uint8_t> rgba; };
    std::vector<BadgeErgebnis> badgeFertig_;

    std::vector<std::thread> hilfsThreads_;
    unsigned long long startMs_ = 0;
    bool startCheckErledigt_ = false;
};
