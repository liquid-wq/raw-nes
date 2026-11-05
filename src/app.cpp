#include "app.h"

#include "erzeugt_logo.h"
#include "i18n.h"
#include "plattform.h"
#include "popup.h"
#include "ra_http.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

#ifndef RAWNES_VERSION
#define RAWNES_VERSION "3.0.0"
#endif

namespace {

// Achievement-Liste: Grundhoehe (~3 Zeilen) und Mindesthoehe (~2 Zeilen), in
// Punkten bei 100 % Skalierung.
constexpr float kAchGrundHoehe = 186.0f;
constexpr float kAchMinHoehe = 124.0f;
// Log (aufgeklappt): normale Hoehe und Mindesthoehe (~4 Zeilen).
constexpr float kLogHoehe = 180.0f;
constexpr float kLogMinHoehe = 80.0f;

// Hardcore-Haken: aus, bis RetroAchievements dieses Werkzeug freigibt (bis
// dahin wertet der Server jede Buchung als Softcore). Eine Zeile zum Einschalten.
constexpr bool kHardcoreAktiv = false;

// Sprachen in ihrem eigenen Namen; Reihenfolge wie in MEGA-RAW und MULTI-RAW.
struct SpracheEintrag { const char* kennung; const char* name; };
constexpr SpracheEintrag kSprachen[] = {
    {"de", "Deutsch"},
    {"en", "English"},
    {"es", "Espa\xC3\xB1ol"},
    {"pt", "Portugu\xC3\xAAs (Brasil)"},
    {"fr", "Fran\xC3\xA7" "ais"},
    {"it", "Italiano"},
    {"pl", "Polski"},
};

// Farbpalette aus colors.h der Qt-Fassung (aus einem echten NES-Foto).
constexpr ImU32 Rgb(unsigned rgb, unsigned a = 255) {
    return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, a);
}
constexpr ImU32 kLight = Rgb(0xd7d3cc);        // Gehaeuse oben (Header)
constexpr ImU32 kDark = Rgb(0x534c4d);         // Gehaeuse unten (Hauptflaeche)
constexpr ImU32 kPanel = Rgb(0x3f3a3b);        // abgesetzte Flaechen (Zeilen, Log)
constexpr ImU32 kRed = Rgb(0x790d10);          // Nintendo-Logo, Akzent/Warnung
constexpr ImU32 kTextMain = Rgb(0xd7d3cc);     // Haupttext auf dunklem Grund
constexpr ImU32 kTextMuted = Rgb(0xa9a49c);    // Nebentext
constexpr ImU32 kUnlocked = Rgb(0x6fbf6f);     // freigeschaltet
constexpr ImU32 kInProgress = Rgb(0xdd9999);   // Hit-Count laeuft
constexpr ImU32 kLockedIconBg = Rgb(0x2a2726); // Platzhalter gesperrtes Badge
constexpr ImU32 kJustUnlocked = Rgb(0xffd75e); // goldener Blitz beim Freischalten
constexpr ImU32 kOrange = Rgb(0xe08a2e);       // Statuszeile: Konsole aus / EverDrive-Menue
constexpr ImU32 kWaiting = Rgb(0x6080a0);      // wartet auf bestaetigte Adressen (Python-Wert)

constexpr float kLbSidebarWidth = 300.0f;
constexpr int kLogMaxZeilen = 2000;  // QPlainTextEdit::setMaximumBlockCount(2000)

// Kerne in app.rc.in: Kennung 100 + Index in FpgaInstaller::coreFiles().
constexpr int kKernRessource = 100;

ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 MitDeckkraft(ImU32 c, float a) {
    const unsigned alpha = (unsigned)std::lround(((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

unsigned long long JetztMs() { return ::GetTickCount64(); }

// Text in Schrift/Groesse/Farbe, ohne Umbruch.
void Txt(ImFont* f, float px, ImU32 farbe, const std::string& s) {
    ImGui::PushFont(f, px);
    ImGui::PushStyleColor(ImGuiCol_Text, V(farbe));
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void Zentriert(ImFont* f, float px, ImU32 farbe, const std::string& s) {
    ImGui::PushFont(f, px);
    const float b = ImGui::CalcTextSize(s.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - b) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, V(farbe));
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

std::string LeseDatei(const std::string& pfad) {
    std::ifstream f(fs::u8path(pfad), std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Farbe eines frisch geaenderten Live-Werts: gold, glimmt ueber 1 s zum
// Ruhe-Gruen zurueck (live_values_widget.cpp, colorForAge).
ImU32 FarbeNachAlter(unsigned long long lastMs, unsigned long long now) {
    constexpr int kGoldR = 0xff, kGoldG = 0xc8, kGoldB = 0x3d;
    constexpr int kRestR = 0x3d, kRestG = 0xd6, kRestB = 0x8c;
    constexpr unsigned long long kFadeMs = 1000;
    if (lastMs == 0) return IM_COL32(kRestR, kRestG, kRestB, 255);
    const unsigned long long age = now - lastMs;
    if (age >= kFadeMs) return IM_COL32(kRestR, kRestG, kRestB, 255);
    const double t = (double)age / (double)kFadeMs;
    return IM_COL32((int)(kGoldR + t * (kRestR - kGoldR)), (int)(kGoldG + t * (kRestG - kGoldG)),
                    (int)(kGoldB + t * (kRestB - kGoldB)), 255);
}

// Checkbox wie der Windows-Stil der Qt-Fassung: Rahmen ohne Fuellung, heller Haken.
bool Haken(const char* text, bool* wert) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, V(0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, V(Rgb(0xd7d3cc, 24)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, V(Rgb(0xd7d3cc, 40)));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, V(kTextMain));
    ImGui::PushStyleColor(ImGuiCol_Border, V(kTextMuted));
    const bool geaendert = ImGui::Checkbox(text, wert);
    ImGui::PopStyleColor(5);
    return geaendert;
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

// ============================================================================
// Aufbau und Abbau
// ============================================================================

App::App(void* fenster, float dpi, const Schriften& schriften)
    : fenster_(fenster), dpi_(dpi), f_(schriften),
      cache_(plattform::ExeOrdner() + "/ra_cache_nes.json"),
      client_(std::make_shared<RaClient>(cache_, make_winhttp_request_fn())) {
    // Worker EINMAL erzeugen -- lebt fuer die gesamte Laufzeit in seinem
    // eigenen Thread (siehe worker.h).
    worker_ = std::make_unique<MonitorWorker>(client_);
    verdrahteWorker();
    badgeThread_ = std::thread([this]() { BadgeThread(); });
}

App::~App() {
    // stop() direkt: setzt nur das Flag (siehe Destruktor der Qt-Fassung).
    if (worker_) worker_->stop();
    worker_.reset();
    {
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeEnde_ = true;
    }
    badgeCv_.notify_all();
    if (badgeThread_.joinable()) badgeThread_.join();
    for (auto& t : hilfsThreads_) if (t.joinable()) t.join();
    bilder::Freigeben(logo_);
    for (auto& kv : badges_) bilder::Freigeben(kv.second.tex);
}

void App::Post(std::function<void()> f) {
    std::lock_guard<std::mutex> l(postMutex_);
    postQueue_.push_back(std::move(f));
}

std::string App::configPath() const { return plattform::ExeOrdner() + "/rawnes_gui_config.json"; }
std::string App::romIndexPath() const { return plattform::ExeOrdner() + "/rawnes_rom_index.json"; }

void App::Starten() {
    // Katzen-Portrait aus raw-turn (dieselbe Katze), RGBA 128x128, belegt 69x106.
    logo_ = bilder::Hochladen(kLogoPixel, kLogoTexturKante, kLogoTexturKante, false);
    logo_.w = kLogoBreite;
    logo_.h = kLogoHoehe;
    logo_.u = (float)kLogoBreite / kLogoTexturKante;
    logo_.v = (float)kLogoHoehe / kLogoTexturKante;

    setCoresDir(embeddedCoresDir());
    loadConfig();

    appendLog(T("RAW-NES Monitor bereit."));

    {
        auto idx = rawnes::loadIndex(romIndexPath());
        if (!idx.empty()) romIndex_ = std::make_shared<rawnes::RomIndexMap>(std::move(idx));
        updateIndexLabel();
    }

    // Gemerkte Mehrdeutigkeits-Entscheidungen an den Worker.
    if (!cfg_.remembered.empty()) worker_->seedRemembered(cfg_.remembered);

    // KEIN Auto-Connect beim Start (siehe mainwindow.cpp): verbunden wird erst
    // auf Klick, wenn die Konsole bereit ist.

    // Auto-Login mit gespeichertem Sitzungsschluessel -- oder einmalig mit
    // einem Passwort aus einer aelteren Konfiguration, das danach nicht mehr
    // gespeichert wird.
    if (userBuf_[0] && (passBuf_[0] || !cfg_.raToken.empty())) onLogin();

    checkForUpdate();

    // Erster Start (keine Konfiguration): Sprache waehlen, dann FPGA-Mapper.
    // Vorbelegt mit der Sprache von Windows.
    if (!cfg_.dateiDa || !cfg_.einrichtungFertig) {
        std::string vorschlag = "en";
        switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
            case LANG_GERMAN: vorschlag = "de"; break;
            case LANG_SPANISH: vorschlag = "es"; break;
            case LANG_PORTUGUESE: vorschlag = "pt"; break;
            case LANG_FRENCH: vorschlag = "fr"; break;
            case LANG_ITALIAN: vorschlag = "it"; break;
            case LANG_POLISH: vorschlag = "pl"; break;
            default: break;
        }
        i18n::SpracheSetzen(vorschlag);
        Dialog d;
        d.art = Dialog::Art::Ersteinrichtung;
        d.sprache = vorschlag;
        d.nummer = ++dialogNummer_;
        dialoge_.push_front(std::move(d));
        startCheckErledigt_ = true;   // die stille Mapper-Pruefung uebernimmt die Ersteinrichtung
    }

    startMs_ = JetztMs();
}

// ============================================================================
// Worker-Verdrahtung (Gegenstueck zu den connect()-Aufrufen)
// ============================================================================

void App::verdrahteWorker() {
    MonitorWorker& w = *worker_;
    w.log = [this](const std::string& m) { Post([this, m]() { appendLog(m); }); };
    w.connected = [this](const std::string& port) {
        Post([this, port]() {
            connected_ = true;
            connPort_ = port;
            letzterPort_ = port;
            connAttempt_.clear();
            connFarbe_ = kUnlocked;
            monitorBtnEnabled_ = !token_.empty();
            monitorBtnKey_ = "Spiel erkennen & Monitor starten";
            autoVerbinden_ = false;
            if (startNachVerbinden_) {
                startNachVerbinden_ = false;
                onToggleMonitor();   // ueber "Spiel erkennen & Monitor starten" verbunden: gleich weiter
            }
            // Sonst startet der Monitor von selbst, sobald die Leerlauf-Abfrage
            // ein laufendes Spiel meldet.
        });
    };
    w.connectionFailed = [this](const std::string& reason) {
        Post([this, reason]() {
            if (autoVerbinden_) {
                // Versuch kam von selbst: still bleiben, spaeter erneut.
                autoVerbinden_ = false;
                connected_ = false;
                connAttempt_.clear();
                naechsteVerbindungMs_ = JetztMs() + 5000;
                return;
            }
            connected_ = false;
            leerlauf_ = 0;
            connPort_.clear();
            connAttempt_.clear();
            connStatusKey_ = "nicht gefunden -- Konsole an? Spiel gestartet?";
            connFarbe_ = kRed;
            appendLog(reason);
            startNachVerbinden_ = false;
            monitorBtnKey_ = "Spiel erkennen & Monitor starten";
            monitorBtnEnabled_ = !token_.empty();  // Nutzer darf erneut versuchen
            hardcoreEnabled_ = true;
        });
    };
    w.connectAttempt = [this](int attempt, int total) {
        Post([this, attempt, total]() {
            connAttempt_ = Fmt(T("verbinde... (Versuch %1/%2)"), {i18n::Zahl(attempt), i18n::Zahl(total)});
        });
    };
    w.romNotDetected = [this]() {
        Post([this]() {
            // Kein automatischer Treffer -> Dateidialog (Pythons Fallback in _start()).
            const std::string path = plattform::DateiWaehlen(
                fenster_, T("ROM oder Archiv wählen"),
                {{i18n::Englisch() ? "NES ROMs and archives" : "NES-ROMs und Archive", "*.nes;*.zip;*.7z;*.rar"},
                 {i18n::Englisch() ? "NES ROMs" : "NES-ROMs", "*.nes"},
                 {i18n::Englisch() ? "All files" : "Alle Dateien", "*.*"}});
            if (path.empty()) {
                monitoring_ = false;
                monitorBtnEnabled_ = true;
                hardcoreEnabled_ = true;
                monitorBtnKey_ = "Spiel erkennen & Monitor starten";
                return;
            }
            worker_->continueWithPath(path);
        });
    };
    w.romAmbiguous = [this](const std::string& hexKey, const std::vector<std::string>& labels,
                            const std::vector<std::string>& sources) {
        Post([this, hexKey, labels, sources]() {
            Dialog d;
            d.art = Dialog::Art::Auswahl;
            d.titel = T("Mehrere Treffer");
            d.text = T("Diese ROMs haben denselben Fingerabdruck, gehören aber zu "
                       "verschiedenen RA-Spielen. Bitte wählen -- die Wahl gilt für diesen "
                       "Start und wird bei verschiedenen RA-Spielen bewusst NICHT "
                       "automatisch wiederverwendet:");
            d.optionen = labels;
            d.gewaehlt = [this, hexKey, sources](int idx) {
                if (idx < 0 || idx >= (int)sources.size()) idx = 0;
                worker_->continueWithChosenRom(hexKey, sources[(size_t)idx]);
            };
            d.nein = [this]() {
                monitoring_ = false;
                monitorBtnEnabled_ = true;
                hardcoreEnabled_ = true;
                monitorBtnKey_ = "Spiel erkennen & Monitor starten";
            };
            d.nummer = ++dialogNummer_;
            dialoge_.push_back(std::move(d));
        });
    };
    w.gameLoaded = [this](const std::string& name) {
        Post([this, name]() {
            gameStatus_ = name;
            // Polling laeuft -- Knopf wird zum Stopp-Knopf.
            monitorBtnEnabled_ = true;
            monitorBtnKey_ = "■ Monitor stoppen";
            monitorBtnRot_ = true;
        });
    };
    w.achievementsReady = [this](const GameData& game, const std::set<long long>& already) {
        Post([this, game, already]() {
            ach_.clear();
            selectedId_ = -1;
            for (const auto& ac : game.achievements) {
                AchZeile z;
                z.id = ac.id;
                z.title = ac.title;
                z.desc = ac.desc;
                z.points = ac.points;
                z.badge = ac.badge;
                z.state = already.count(ac.id) ? AchState::Unlocked : AchState::Locked;
                ach_.push_back(std::move(z));
            }
        });
    };
    w.unlocked = [this](const std::string& title, int points, long long achId, bool hardcore) {
        (void)hardcore;
        Post([this, title, points, achId]() {
            std::string desc, badge;
            for (auto& z : ach_) {
                if (z.id != achId) continue;
                z.state = AchState::JustUnlocked;
                desc = z.desc;
                badge = z.badge;
            }
            // Badge fuer die Einblendung: immer die ungesperrte Variante.
            std::string png;
            if (!badge.empty()) {
                if (Badge* b = BadgeHolen(badge)) png = b->png;
                if (png.empty()) png = LeseDatei(plattform::ExeOrdner() + "/badge_cache/" + badge + ".png");
            }
            popup::Zeigen(title, desc, points, png, T("ACHIEVEMENT FREIGESCHALTET!"), std::string(T("%1 Pkt")).substr(3), dpi_);
        });
    };
    w.progressChanged = [this](long long achId, int current, int target) {
        Post([this, achId, current, target]() {
            for (auto& z : ach_) {
                if (z.id != achId) continue;
                if (target <= 1) { z.showProgress = false; continue; }
                z.showProgress = true;
                z.progCur = current;
                z.progTarget = target;
            }
        });
    };
    w.achievementWaitingChanged = [this](long long id, bool waiting) {
        Post([this, id, waiting]() {
            for (auto& z : ach_) {
                if (z.id != id) continue;
                if (z.state == AchState::Unlocked || z.state == AchState::JustUnlocked) continue;
                z.state = waiting ? AchState::Waiting : AchState::Locked;
            }
        });
    };
    w.connectionLost = [this](const std::string& reason) {
        Post([this, reason]() {
            connPort_.clear();
            connStatusKey_ = "Verbindung verloren";
            connFarbe_ = kRed;
            connected_ = false;   // naechster Start verbindet neu
            appendLog(std::string(T("Verbindung verloren: ")) + reason);
        });
    };
    // Konsole aus oder Spiel verlassen: alles wie nach dem Programmstart.
    // Der naechste Klick auf "Spiel erkennen & Monitor starten" verbindet neu.
    w.spielVerlassen = [this]() {
        Post([this]() {
            // Die Verbindung im Worker bleibt bestehen (die Leerlauf-Abfrage oeffnet
            // sie bei Bedarf neu). Vorlaeufig "Konsole aus"; ist es nur das Menue,
            // korrigiert die naechste Abfrage binnen 3 s.
            connected_ = !letzterPort_.empty();
            connPort_ = letzterPort_;
            leerlauf_ = 3;
            spielWeg_ = true;          // kein "Monitor angehalten", keine Pause
            connAttempt_.clear();
            connStatusKey_ = "nicht verbunden";
            connFarbe_ = 0;
            gameStatus_.clear();
            ach_.clear();
            selectedId_ = -1;
            lbs_.clear();
            live_.clear();
        });
    };
    w.leerlaufStatus = [this](int zustand) {
        Post([this, zustand]() {
            leerlauf_ = zustand;
            if (zustand == 1 || zustand == 3) autoPause_ = false;   // Spiel verlassen: das naechste startet von selbst
            if (monitoring_) return;
            if (zustand == 2 && !autoPause_ && !token_.empty()) {
                onToggleMonitor();                    // Spiel laeuft: Monitor von selbst starten
                return;
            }
            monitorBtnKey_ = (zustand == 2 && autoPause_) ? "Monitor fortsetzen" : "Spiel erkennen & Monitor starten";
        });
    };
    w.hardcoreDowngraded = [this](const std::string& liste) {
        Post([this, liste]() {
            hardcore_ = false;
            appendLog(std::string(T("Hardcore beendet -- aktiviert: ")) + liste);
            Warnung(T("Hardcore beendet"),
                    Fmt(T("Am EverDrive sind aktiviert: %1\n\n"
                          "Hardcore wurde für diese Sitzung beendet, es zählt jetzt "
                          "Softcore. Für Hardcore bitte im EverDrive-System-Menü "
                          "(nicht im Ingame-Menü) deaktivieren, dann Monitor neu starten."),
                        {liste}));
        });
    };
    w.rememberedChanged = [this](const std::string& hexKey, const std::string& source) {
        Post([this, hexKey, source]() {
            cfg_.remembered[hexKey] = source;
            saveConfig();
        });
    };
    w.leaderboardsReady = [this](const LeaderboardList& lbs) {
        Post([this, lbs]() {
            lbs_.clear();
            for (const auto& [id, title] : lbs) {
                LbZeile z;
                z.id = id;
                // Ohne Titel die ID zeigen, damit der Eintrag unterscheidbar bleibt.
                z.title = Trim(title).empty() ? Fmt(T("Leaderboard #%1"), {i18n::Zahl(id)}) : title;
                z.status = "wartet auf Start";
                lbs_.push_back(std::move(z));
            }
        });
    };
    auto lbStatus = [this](long long id, const std::string& status) {
        for (auto& z : lbs_) if (z.id == id) z.status = status;
    };
    w.leaderboardStarted = [this, lbStatus](long long id, const std::string& title) {
        Post([this, lbStatus, id, title]() {
            lbStatus(id, "läuft");
            appendLog(std::string(T("Leaderboard läuft: ")) + title);
        });
    };
    w.leaderboardCanceled = [this, lbStatus](long long id, const std::string& title) {
        Post([this, lbStatus, id, title]() {
            lbStatus(id, "abgebrochen");
            appendLog(std::string(T("Leaderboard abgebrochen: ")) + title);
        });
    };
    w.leaderboardSubmitted = [this, lbStatus](long long id, const std::string& title, const std::string& value) {
        Post([this, lbStatus, id, title, value]() {
            lbStatus(id, std::string("eingereicht: \x01") + value);
            appendLog(std::string(T("Leaderboard eingereicht: ")) + title + " = " + value);
        });
    };
    w.finished = [this]() {
        Post([this]() {
            monitoring_ = false;
            if (spielWeg_) {
                // Konsole aus / Spiel verlassen: keine Pause, das naechste Spiel
                // startet den Monitor wieder von selbst.
                autoPause_ = false;
                monitorBtnKey_ = "Spiel erkennen & Monitor starten";
            } else {
                // Stopp von Hand, Spiel ohne Set, Fehler: fuer dieses Spiel nicht
                // von selbst neu starten.
                autoPause_ = true;
                leerlauf_ = 0;   // die naechste Abfrage meldet den Stand neu
                monitorBtnKey_ = connected_ ? "Monitor fortsetzen" : "Spiel erkennen & Monitor starten";
            }
            spielWeg_ = false;
            monitorBtnEnabled_ = true;
            hardcoreEnabled_ = true;
            monitorBtnRot_ = false;
            // "Monitor gestoppt." schreibt die Abfrageschleife selbst ins Log.
        });
    };
    w.liveValuesInit = [this](const std::vector<uint16_t>& addrs) {
        Post([this, addrs]() {
            live_.clear();
            for (size_t i = 0; i < addrs.size(); ++i) {
                LiveZeile z;
                z.addr = addrs[i];
                live_[(int)i] = z;
            }
        });
    };
    w.liveValueUpdate = [this](int slot, uint8_t value, int changeCount) {
        Post([this, slot, value, changeCount]() {
            auto it = live_.find(slot);
            if (it == live_.end()) return;
            LiveZeile& z = it->second;
            if (z.value != value && z.value >= 0) z.lastChangeMs = JetztMs();
            z.value = value;
            z.changeCount = changeCount;
        });
    };
}

// ============================================================================
// Konfiguration
// ============================================================================

void App::loadConfig() {
    cfg_.Laden(configPath());
    std::snprintf(userBuf_, sizeof(userBuf_), "%s", cfg_.raUser.c_str());
    // Ein Passwort steht hier nur noch aus einer aelteren Konfiguration.
    std::snprintf(passBuf_, sizeof(passBuf_), "%s", cfg_.raPassAlt.c_str());
    rememberLogin_ = cfg_.rememberLogin;
    i18n::SpracheSetzen(cfg_.language);
    // Sidebar beginnt immer eingeklappt (siehe mainwindow.cpp loadConfig).
    //
    // "cores_dir" aus der Konfiguration wird hier bewusst NICHT uebernommen:
    // Die Kerne stecken in der Exe und gelten immer. Ein Ordner, den die
    // Qt-Fassung einmal gespeichert hat, kann veraltete Kerne enthalten (so
    // war es mit 004.RBF im Repo). Ein eigener Ordner wird nur noch gefragt,
    // wenn das Auspacken scheitert (ensureCoresDir).
}

void App::saveConfig() {
    cfg_.raUser = userBuf_;
    // Nur der Sitzungsschluessel, nie das Passwort.
    cfg_.raToken = token_;
    cfg_.rememberLogin = rememberLogin_;
    cfg_.language = i18n::Sprache();
    // Nur einen in DIESER Sitzung bewusst gewaehlten cores-Ordner festschreiben;
    // sonst bleibt der Eintrag, wie er war (die Qt-Fassung im selben Ordner
    // benutzt ihn weiter).
    if (coresDirManual_) cfg_.coresDir = coresDir_;
    cfg_.Speichern(configPath());
}

void App::setLanguage(const std::string& lang) {
    if (i18n::Sprache() == lang) return;
    i18n::SpracheSetzen(lang);
    saveConfig();
}

void App::updateIndexLabel() {
    indexStatus_.clear();
}

void App::appendLog(const std::string& msg) {
    // appendPlainText: ein Absatz je Aufruf, Zeilenumbrueche bleiben erhalten.
    size_t start = 0;
    for (;;) {
        const size_t nl = msg.find('\n', start);
        log_.push_back(msg.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    while ((int)log_.size() > kLogMaxZeilen) log_.pop_front();
}

// ============================================================================
// Ablaeufe
// ============================================================================

void App::onLogin() {
    const std::string user = userBuf_;
    const std::string pw = passBuf_;
    // Ohne Passwort: mit dem gespeicherten Sitzungsschluessel, wenn er zum
    // Benutzernamen gehoert.
    const std::string gemerkt = (pw.empty() && user == cfg_.raUser) ? cfg_.raToken : std::string();
    if (user.empty() || (pw.empty() && gemerkt.empty())) {
        loginStatusText_.clear();
        loginStatusKey_ = "Bitte Benutzername und Passwort eingeben.";
        return;
    }
    loginLaeuft_ = true;
    loginStatusText_.clear();
    loginStatusKey_ = "Login läuft...";

    hilfsThreads_.emplace_back([this, user, pw, gemerkt]() {
        std::string statusText, statusKey, newToken;
        bool ok = false;
        try {
            auto tok = pw.empty() ? client_->ra_login_token(user, gemerkt) : client_->ra_login(user, pw);
            if (tok) {
                newToken = *tok;
                statusText = "\xE2\x9C\x93 " + user;  // ✓
                ok = true;
            } else if (pw.empty()) {
                statusKey = "Sitzung abgelaufen -- bitte Passwort eingeben.";
            } else {
                statusKey = "Login fehlgeschlagen (falsche Zugangsdaten?).";
            }
        } catch (const RateLimited& e) {
            statusText = "RA drosselt, bitte " + std::to_string(e.retry_after) + "s warten.";
        } catch (const std::exception& e) {
            statusText = std::string("Netzwerkfehler: ") + e.what();
        }
        Post([this, statusText, statusKey, newToken, ok, user, automatisch = pw.empty()]() {
            loginLaeuft_ = false;
            loginStatusText_ = statusText;
            if (!statusKey.empty()) loginStatusKey_ = statusKey;
            if (ok) {
                loginOk_ = true;
                token_ = newToken;
                passBuf_[0] = '\0';   // Passwort nicht im Speicher halten
                anmeldUser_ = user;
                anmeldAuto_ = automatisch;
                anmeldStart_ = ImGui::GetTime();
                // Laeuft schon ein Spiel, gleich ueberwachen (sonst kaeme kein neuer Anstoss).
                if (leerlauf_ == 2 && !monitoring_ && !autoPause_) onToggleMonitor();
                cfg_.raPassAlt.clear();
                appendLog(T("Login erfolgreich."));
                monitorBtnEnabled_ = !token_.empty();
                saveConfig();
            }
        });
    });
}

void App::onIndexRoms() {
    const std::string dir = plattform::OrdnerWaehlen(fenster_, T("Spielordner wählen"));
    if (dir.empty()) return;

    indexLaeuft_ = true;
    indexStatus_ = T("indexiere... 0 ROMs");

    hilfsThreads_.emplace_back([this, dir]() {
        int skipped = 0;
        // Fortschritt: zeigt auch den aktuellen Dateinamen -- bleibt die
        // Anzeige bei einer Datei stehen, blockiert genau die.
        auto progress = [this](int count, const std::string& file) {
            Post([this, count, file]() {
                if (!file.empty())
                    indexStatus_ = Fmt(T("indexiere... %1 ROMs — %2"), {i18n::Zahl(count), file});
                else
                    indexStatus_ = Fmt(T("indexiere... %1 ROMs"), {i18n::Zahl(count)});
            });
        };
        auto index = rawnes::buildIndex(dir, &skipped, progress);
        auto indexPtr = std::make_shared<rawnes::RomIndexMap>(std::move(index));
        rawnes::saveIndex(*indexPtr, romIndexPath());

        Post([this, indexPtr, skipped]() {
            romIndex_ = indexPtr;
            indexLaeuft_ = false;
            updateIndexLabel();
            appendLog(T("Indexierung abgeschlossen."));
            if (skipped > 0) {
                appendLog(Fmt(T("%1 Archiv(e) übersprungen -- bitte 7-Zip installieren "
                                "(https://www.7-zip.org/), dann erneut indexieren."),
                              {i18n::Zahl(skipped)}));
            }
            if (abschlussNachIndex_) {
                abschlussNachIndex_ = false;
                ZeigeAbschluss();
            }
        });
    });
}

void App::onReconnect() {
    connPort_.clear();
    connAttempt_.clear();
    connStatusKey_ = "verbinde...";
    connFarbe_ = kTextMuted;
    worker_->connectToDevice();
}

// Ohne Verbindung alle 2 s nachsehen, ob ein EverDrive N8 PRO (USB 38DF:0017)
// steckt, und dann still verbinden. Nur dieser Anschluss wird angefasst --
// fremde (z. B. Bluetooth) koennen beim Oeffnen lange haengen.
void App::autoVerbindenPruefen() {
    if (connected_ || monitoring_ || autoVerbinden_ || startNachVerbinden_ || !worker_) return;
    const long long jetzt = JetztMs();
    if (jetzt < naechsteVerbindungMs_ || jetzt - startMs_ < 1500) return;
    naechsteVerbindungMs_ = jetzt + 2000;
    bool everdrive = false;
    for (const auto& p : availableSerialPorts())
        if (p.hatKennung && p.vid == 0x38DF && p.pid == 0x0017) { everdrive = true; break; }
    if (!everdrive) return;
    autoVerbinden_ = true;
    worker_->connectToDevice();
}

void App::onToggleMonitor() {
    if (monitoring_) {
        // stop() direkt: setzt nur das Flag, die Poll-Schleife sieht es sofort.
        worker_->stop();
        return;  // finished() raeumt den Knopf auf
    }
    if (token_.empty()) {
        appendLog(T("Bitte zuerst anmelden."));
        return;
    }
    // Noch nicht verbunden? Dann jetzt verbinden; der Monitor startet danach
    // von selbst (frueher musste man ein zweites Mal klicken).
    if (!connected_) {
        appendLog(T("Verbinde mit EverDrive..."));
        startNachVerbinden_ = true;
        monitorBtnEnabled_ = false;
        monitorBtnKey_ = "verbinde...";
        worker_->connectToDevice();
        return;
    }
    monitorBtnEnabled_ = false;
    monitorBtnKey_ = "lade...";
    // Alles vom vorigen Spiel wegraeumen, bevor das neue erkannt wird. Sonst
    // bliebe z. B. die alte Achievement-Liste stehen, wenn das neue Spiel kein
    // Set hat (F-15 Strike Eagle nach EarthBound, 05.11.2026).
    gameStatus_.clear();
    ach_.clear();
    selectedId_ = -1;
    lbs_.clear();
    live_.clear();
    // Hardcore wird genau hier einmal gelesen -- waehrend der Ueberwachung
    // gesperrt, sonst behauptete das Haekchen etwas Falsches.
    hardcoreEnabled_ = false;
    monitoring_ = true;
    worker_->startMonitoring(romIndex_, userBuf_, token_, hardcore_);
}

// Fenster "über die Katze"; unten der Hinweis auf Tierheime und Ko-fi.
void App::ZeigeKatze() {
        Dialog d;
        d.art = Dialog::Art::Katze;
        d.titel = T("über die Katze");
        // Wortgleich aus mainwindow.cpp (onAboutCat). Die Qt-Fassung zeigt
        // ihn in beiden Sprachen auf Deutsch.
        d.text =
            "Ich habe mich fuer meine Katze als Programmlogo entschieden. "
            "Die folgenden Zeilen sind ihr gewidmet \xE2\x80\x94 eine kleine Hommage "
            "und ein Gedenken.\n\n"
            "Jede dritte Katze ab einem Alter von 10\xE2\x80\x93" "12 Jahren leidet an CNI "
            "(chronischer Niereninsuffizienz). Leider wird die Krankheit oft "
            "erst sichtbar, wenn sie sich bereits dem Endstadium zuneigt. "
            "Viele Aerzte weisen nicht darauf hin.\n\n"
            "Fuehlen Sie sich also nicht sicher, nur weil Sie regelmaessig und "
            "verantwortungsvoll zum Tierarzt gehen und dieser sagt, es sei "
            "alles in Ordnung. Bitte verlangen Sie regelmaessig einen Bluttest. "
            "Nur dieser kann die Katze retten und ihr Leben nachweislich "
            "wertvoll verlaengern. Frueherkennung macht den Unterschied.\n\n"
            "Mein Kater bekam Anfang 2025 die Diagnose, nachdem es "
            "Auffaelligkeiten in seinem Verhalten gab: mehr Durst, der Drang, "
            "ungewoehnliche Wasserquellen aufzusuchen. Wir entschieden uns "
            "fuer eine Behandlung in einer Tierklinik, und waehrend dieser "
            "Zeit haben ihn sich gleich mehrere Aerzte angeschaut \xE2\x80\x94 und jeder "
            "war anderer Meinung. Es ist wichtig, auch Ihren eigenen Sinnen "
            "zu trauen, da Sie Ihre Katze am besten kennen.\n\n"
            "Am 11. Juni 2025 ist er gestorben.\n\n"
            "Die Katze im Intro ist ein Pixel-Portrait zur Erinnerung an "
            "meinen Kater, abgeleitet von einem echten Foto aus dem Jahr "
            "2010 \xE2\x80\x94 als wir ihn aus dem Tierheim holten.\n\n"
            "Auch wenn dieses Projekt einen Spenden-Button hat: Die groesste "
            "Freude, die Sie mir machen koennen, ist, Ihre Katze auf diese "
            "Krankheit untersuchen zu lassen.\n\n"
            "* CNI betrifft nicht nur Katzen, sondern auch andere Tiere.\n\n"
            "Und wenn das jetzt auch nur einer macht, hat dieser Text seinen "
            "Sinn und Zweck in einer Software, die damit eigentlich gar nichts "
            "zu tun hat, schon vollends erfuellt.\n\n"
            "Ich danke euch fuers Lesen.\n\n"
            "In memory of Jason \xE2\x80\x94 2010\xE2\x80\x93" "2025.";
        d.nummer = ++dialogNummer_;
        dialoge_.push_back(std::move(d));
}

void App::onOpenSupport() {
    ZeigeKatze();
}

void App::toggleLbSidebar(bool open) {
    if (lbSidebarOpen_ == open) return;
    lbSidebarOpen_ = open;
    // Fensterbreite mitziehen, damit der Inhalt links gleich breit bleibt.
    HWND h = static_cast<HWND>(fenster_);
    RECT r;
    if (h && !::IsZoomed(h) && ::GetWindowRect(h, &r)) {
        const int delta = (int)std::lround((kLbSidebarWidth + 12.0f) * dpi_);
        ::SetWindowPos(h, nullptr, r.left, r.top, (r.right - r.left) + (open ? delta : -delta),
                       r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    saveConfig();
}

// ============================================================================
// FPGA-Mapper (FpgaInstallerPanel der Qt-Fassung)
// ============================================================================

void App::setCoresDir(const std::string& dir) {
    if (dir.empty()) return;
    coresDir_ = dir;
    installer_ = std::make_unique<rawnes::FpgaInstaller>(coresDir_);
}

// Die Kerne stecken in der Exe. FpgaInstaller arbeitet mit einem Ordner,
// deshalb werden sie einmal je Start in einen eigenen Ordner unter %TEMP%
// geschrieben -- nur, wenn sie dort fehlen oder abweichen.
std::string App::embeddedCoresDir() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "rawnes_cores";
    fs::create_directories(dir, ec);
    const auto& files = rawnes::FpgaInstaller::coreFiles();
    for (size_t i = 0; i < files.size(); ++i) {
        const void* d = nullptr;
        size_t n = 0;
        if (!bilder::Ressource(kKernRessource + (int)i, &d, &n)) {
            appendLog(Fmt(T("FPGA-Kerne konnten nicht bereitgestellt werden: %1"), {files[i]}));
            continue;
        }
        const fs::path ziel = dir / files[i];
        const std::string vorhanden = LeseDatei(ziel.u8string());
        if (vorhanden.size() == n && std::memcmp(vorhanden.data(), d, n) == 0) continue;
        std::ofstream f(ziel, std::ios::binary | std::ios::trunc);
        if (!f.write(static_cast<const char*>(d), (std::streamsize)n))
            appendLog(Fmt(T("FPGA-Kerne konnten nicht bereitgestellt werden: %1"), {ziel.u8string()}));
    }
    std::string s = dir.u8string();
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

void App::ensureCoresDir(bool aktiv, std::function<void()> weiter) {
    if (installer_ && installer_->coresAvailable()) { weiter(); return; }
    if (!aktiv) return;

    std::string detail;
    if (!fs::is_directory(fs::u8path(coresDir_))) {
        detail = T("    (Ordner existiert nicht)\n");
    } else {
        for (const auto& f : rawnes::FpgaInstaller::coreFiles())
            if (!fs::exists(fs::u8path(coresDir_) / f)) detail += "    " + f + "\n";
    }
    Frage(T("FPGA-Mapper"),
          Fmt(T("Cores-Ordner nicht gefunden oder unvollständig:\n%1\n\n"
                "Fehlt:\n%2\nOrdner jetzt manuell wählen?"), {coresDir_, detail}),
          [this, weiter]() {
              const std::string dir = plattform::OrdnerWaehlen(fenster_, T("cores-Ordner wählen"), coresDir_);
              if (dir.empty()) return;
              setCoresDir(dir);
              if (!installer_->coresAvailable()) {
                  Warnung(T("FPGA-Mapper"), Fmt(T("In %1 fehlen weiterhin Mapper-Dateien."), {dir}));
                  return;
              }
              coresDirManual_ = true;
              saveConfig();
              appendLog(Fmt(T("Cores-Ordner gesetzt: %1\n"), {coresDir_}));
              weiter();
          },
          [this]() { appendLog(T("Cores-Ordner nicht gesetzt -- abgebrochen.\n")); });
}

// Waehlt der Nutzer den EDN8-Ordner statt der Kartenwurzel, eine Ebene hoeher
// suchen (fpga_installer_panel.cpp, resolveMapsDir).
std::pair<std::string, std::string> App::resolveMapsDir(const std::string& picked) {
    std::string maps = rawnes::FpgaInstaller::mapsDirFromSd(picked);
    if (!maps.empty()) return {maps, picked};
    const fs::path eltern = fs::u8path(picked).parent_path();
    if (!eltern.empty() && eltern != fs::u8path(picked)) {
        std::string parent = eltern.u8string();
        for (char& c : parent) if (c == '\\') c = '/';
        const std::string mapsFromParent = rawnes::FpgaInstaller::mapsDirFromSd(parent);
        if (!mapsFromParent.empty()) {
            appendLog(Fmt(T("Hinweis: %1 sah aus wie der EDN8-Ordner selbst -- "
                            "automatisch eine Ebene höher gesucht.\n"), {picked}));
            return {mapsFromParent, parent};
        }
    }
    return {"", picked};
}

void App::checkCores(bool aktiv, bool direkt) {
    ensureCoresDir(aktiv, [this, aktiv, direkt]() {
        std::string maps;
        if (!cfg_.sdRoot.empty()) maps = rawnes::FpgaInstaller::mapsDirFromSd(cfg_.sdRoot);

        auto mitMaps = [this, aktiv](const std::string& m) {
            auto noetig = installer_->coresNeedInstall(m);
            if (noetig.empty()) {
                appendLog(T("FPGA-Mapper sind aktuell.\n"));
                if (aktiv) Info(T("FPGA-Mapper"), T("Alle Mapper sind bereits aktuell."));
                return;
            }
            doInstall(m, noetig);
        };

        if (!maps.empty()) { mitMaps(maps); return; }
        if (!aktiv) return;  // stiller Start-Check: SD nicht da, nichts tun

        auto waehleSd = [this, mitMaps]() {
                  const std::string sd = plattform::OrdnerWaehlen(
                      fenster_, T("EverDrive-SD-Karte wählen (Laufwerk mit dem Ordner EDN8)"));
                  if (sd.empty()) {
                      appendLog(T("Keine SD-Karte gewählt -- abgebrochen.\n"));
                      return;
                  }
                  auto [m, echteWurzel] = resolveMapsDir(sd);
                  if (m.empty()) {
                      appendLog(Fmt(T("Kein EDN8-Ordner in %1 gefunden.\n"), {sd}));
                      Warnung(T("FPGA-Mapper"), T("Kein EDN8-Ordner auf der gewählten Karte gefunden."));
                      return;
                  }
                  cfg_.sdRoot = echteWurzel;
                  saveConfig();
                  appendLog(Fmt(T("SD-Karte erkannt: %1\n"), {echteWurzel}));
                  mitMaps(m);
        };
        // Aus der Ersteinrichtung: dort wurde "jetzt einrichten" schon
        // gewaehlt, also gleich die Laufwerkswahl ohne Rueckfrage.
        if (direkt) { waehleSd(); return; }
        Frage(T("FPGA-Mapper"),
              T("Es ist noch keine EverDrive-SD-Karte hinterlegt.\n\n"
                "Jetzt das Laufwerk mit dem Ordner EDN8 auswählen "
                "(z.B. H:\\) und die Mapper einrichten?"),
              waehleSd,
              [this]() { appendLog(T("Einrichtung übersprungen.\n")); });
    });
}

void App::doInstall(const std::string& mapsDir, const std::vector<std::string>& files) {
    auto wirdGesichert = installer_->filesToBackup(mapsDir, files);

    // Bestaetigungsdialog, entspricht _confirm_install() in Python.
    std::string text = Fmt(T("Ziel: %1\n\n"), {mapsDir});
    text += Fmt(T("Diese %1 FPGA-Mapper werden installiert:\n"), {i18n::Zahl((long long)files.size())});
    for (const auto& f : files) text += "    " + f + "\n";
    text += "\n";
    if (!wirdGesichert.empty()) {
        text += T("Vorhandene Originale werden zuerst gesichert (nach ORIG_BACKUP):\n");
        for (const auto& f : wirdGesichert) text += "    " + f + "\n";
        text += std::string("\n") + T("Über \"Originale wiederherstellen\" jederzeit rückgängig zu machen.\n");
    } else {
        text += T("Kein neues Backup nötig (bereits vorhanden oder Dateien fehlten auf der SD).\n");
    }

    Dialog d;
    d.art = Dialog::Art::Textfeld;
    d.titel = T("FPGA-Mapper");
    d.text = text;
    d.ja = [this, mapsDir, files]() {
        auto result = installer_->installCores(mapsDir, files);
        if (result.backedUp > 0)
            appendLog(Fmt(T("%1 Original-Mapper gesichert (ORIG_BACKUP).\n"), {i18n::Zahl(result.backedUp)}));
        // Nur melden, was wirklich durchging.
        if (result.errors.empty()) {
            for (const auto& f : files) appendLog(Fmt(T("  FPGA-Mapper installiert: %1\n"), {f}));
        } else {
            for (const auto& e : result.errors) appendLog(Fmt(T("  FEHLER: %1\n"), {e}));
        }
        if (!result.errors.empty()) {
            std::string errText;
            for (const auto& e : result.errors) errText += e + "\n";
            Fehler(T("FPGA-Mapper"), Fmt(T("Fehler beim Installieren:\n%1"), {errText}));
        } else {
            Info(T("FPGA-Mapper"), Fmt(T("%1 FPGA-Mapper erfolgreich installiert."), {i18n::Zahl(result.installed)}));
        }
        appendLog(Fmt(T("%1 FPGA-Mapper installiert.\n"), {i18n::Zahl(result.installed)}));
    };
    d.nein = [this]() { appendLog(T("FPGA-Mapper-Installation abgebrochen.\n")); };
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::restoreMitMaps(const std::string& maps) {
    auto vorhanden = installer_->backedUpFilesAvailable(maps);
    if (vorhanden.empty()) {
        Info(T("FPGA-Mapper"), T("Kein Backup vorhanden."));
        return;
    }
    Frage(T("FPGA-Mapper"),
          Fmt(T("%1 Original-Mapper wiederherstellen?"), {i18n::Zahl((long long)vorhanden.size())}),
          [this, maps, vorhanden]() {
              auto result = installer_->restoreOriginals(maps, vorhanden);
              if (result.errors.empty()) {
                  for (const auto& f : vorhanden) appendLog(Fmt(T("  Original wiederhergestellt: %1\n"), {f}));
              } else {
                  for (const auto& e : result.errors) appendLog(Fmt(T("  FEHLER: %1\n"), {e}));
              }
              if (!result.errors.empty()) {
                  std::string errText;
                  for (const auto& e : result.errors) errText += e + "\n";
                  Fehler(T("FPGA-Mapper"), Fmt(T("Fehler:\n%1"), {errText}));
              } else {
                  Info(T("FPGA-Mapper"),
                       Fmt(T("%1 Original-Mapper wiederhergestellt."), {i18n::Zahl(result.restored)}));
              }
              appendLog(Fmt(T("%1 Originale wiederhergestellt.\n"), {i18n::Zahl(result.restored)}));
          });
}

void App::onRestoreClicked() {
    std::string maps;
    if (!cfg_.sdRoot.empty()) maps = rawnes::FpgaInstaller::mapsDirFromSd(cfg_.sdRoot);
    if (!maps.empty()) { restoreMitMaps(maps); return; }

    Info(T("Original-Mapper wiederherstellen"),
         T("Es ist noch keine EverDrive-SD-Karte hinterlegt.\n\n"
           "Bitte im nächsten Fenster das Laufwerk der EverDrive-SD-Karte "
           "auswählen -- also das Laufwerk, das den Ordner EDN8 enthält "
           "(z.B. H:\\). Die Auswahl wird gespeichert und beim nächsten "
           "Mal nicht mehr abgefragt."),
         [this]() {
             const std::string sd = plattform::OrdnerWaehlen(
                 fenster_, T("EverDrive-SD-Karte wählen (Laufwerk mit dem Ordner EDN8)"));
             if (sd.empty()) return;
             auto [m, echteWurzel] = resolveMapsDir(sd);
             if (m.empty()) {
                 Warnung(T("FPGA-Mapper"), T("Kein EDN8-Ordner gefunden."));
                 return;
             }
             cfg_.sdRoot = echteWurzel;
             saveConfig();
             restoreMitMaps(m);
         });
}

// ============================================================================
// Update-Hinweis und Nachricht von der Projektseite
// ============================================================================

// Schlussfenster der Ersteinrichtung: erst mit OK gilt sie als erledigt.
// Wird vorher beendet, kommt sie beim naechsten Start wieder.
void App::ZeigeAbschluss() {
    Info(T("Ersteinrichtung abgeschlossen"),
         mapperSpaeter_ ? T("Ohne die FPGA-Mapper auf der SD-Karte funktioniert die Überwachung nicht. Du kannst sie jederzeit über Optionen > FPGA-Mapper einrichten nachholen. Verbinde danach das EverDrive per USB mit dem PC, melde dich oben an und starte ein Spiel im EverDrive-Menü.")
                        : T("Steck die SD-Karte wieder ins EverDrive, verbinde es per USB mit dem PC und schalte die Konsole ein. Melde dich oben mit deinem RetroAchievements-Konto an und starte ein Spiel im EverDrive-Menü. RAW-NES erkennt es und startet die Überwachung von selbst."),
         [this]() {
             cfg_.einrichtungFertig = true;
             saveConfig();
         });
}

// data/version_rawnes.txt enthaelt nur die Build-Nummer. Ist sie groesser als
// die eigene, gibt es einen Hinweis. Ohne Netz oder Datei: still.
void App::checkForUpdate() {
    hilfsThreads_.emplace_back([this]() {
        ::Sleep(2000);
        std::string body;
        if (!http_get("liquid-wq.github.io", "/data/version_rawnes.txt", 8000, "RAW-NES-UpdateCheck", &body)) return;
        size_t a = 0;
        while (a < body.size() && (unsigned char)body[a] <= ' ') ++a;
        size_t e = a;
        while (e < body.size() && body[e] >= '0' && body[e] <= '9') ++e;
        size_t r = e;
        while (r < body.size() && (unsigned char)body[r] <= ' ') ++r;
        if (e == a || r != body.size()) return;     // nur eine Zahl
        const int remote = std::atoi(body.substr(a, e - a).c_str());
        if (remote <= RAWNES_BUILD) return;
        Post([this, remote]() {
            Info(T("Update verfügbar"),
                 Fmt(T("Eine neue Version ist verfügbar (Build %1, du hast Build %2).\n\nBesuche die Support-Seite zum Download."),
                     {i18n::Zahl(remote), i18n::Zahl(RAWNES_BUILD)}),
                 []() { plattform::UrlOeffnen("https://liquid-wq.github.io/raw-nes/"); });
        });
    });
    NachrichtHolen();
}

// Datei auf der Seite, z. B.:
//   { "id": 1, "de": "...", "en": "...", "link": "https://...",
//     "ab_build": 53, "bis_build": 60 }
// id: fuer jede neue Nachricht hochzaehlen (0 oder fehlend = keine).
// link, ab_build, bis_build: optional. Angezeigt wird reiner Text, jede id
// genau einmal (gemerkt in der Konfiguration, "message_read").
void App::NachrichtHolen() {
    const long long gelesen = cfg_.messageRead;
    hilfsThreads_.emplace_back([this, gelesen]() {
        ::Sleep(2500);
        std::string body;
        if (!http_get("liquid-wq.github.io", "/data/nachricht_rawnes.json", 8000, "RAW-NES-Nachricht", &body))
            return;
        if (body.size() >= 3 && (unsigned char)body[0] == 0xEF && (unsigned char)body[1] == 0xBB &&
            (unsigned char)body[2] == 0xBF)
            body.erase(0, 3);   // mit Stueckliste gespeichert
        long long id = 0;
        std::string de, en, link;
        try {
            Json j = Json::parse(body);
            if (j.type() != Json::Type::Object) return;
            auto zahl = [&](const char* k, long long vorgabe) {
                const Json* v = j.find(k);
                return (v && v->type() == Json::Type::Int) ? v->as_int() : vorgabe;
            };
            auto text = [&](const char* k) {
                const Json* v = j.find(k);
                return (v && v->type() == Json::Type::String) ? v->as_string() : std::string();
            };
            id = zahl("id", 0);
            if (id <= 0 || id == gelesen) return;
            if (RAWNES_BUILD < zahl("ab_build", 0) || RAWNES_BUILD > zahl("bis_build", 1000000)) return;
            de = text("de");
            en = text("en");
            link = text("link");
            if (link.rfind("https://", 0) != 0) link.clear();   // nur sichere Adressen
            if (de.empty() && en.empty()) return;
            if (de.size() > 4000) de.resize(4000);
            if (en.size() > 4000) en.resize(4000);
        } catch (...) {
            return;
        }
        Post([this, id, de, en, link]() {
            Dialog d;
            d.art = Dialog::Art::Nachricht;
            d.titel = T("Nachricht");
            d.text = de.empty() ? en : de;
            d.textEn = en.empty() ? de : en;
            d.link = link;
            d.nummer = ++dialogNummer_;
            d.ja = [this, id]() {
                cfg_.messageRead = id;
                saveConfig();
            };
            dialoge_.push_back(std::move(d));
        });
    });
}

// ============================================================================
// Dialoge
// ============================================================================

void App::Info(const std::string& titel, const std::string& text, std::function<void()> ok) {
    Dialog d;
    d.art = Dialog::Art::Info;
    d.titel = titel;
    d.text = text;
    d.ja = std::move(ok);
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Warnung(const std::string& titel, const std::string& text) {
    Dialog d;
    d.art = Dialog::Art::Warnung;
    d.titel = titel;
    d.text = text;
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Fehler(const std::string& titel, const std::string& text) {
    Dialog d;
    d.art = Dialog::Art::Fehler;
    d.titel = titel;
    d.text = text;
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::Frage(const std::string& titel, const std::string& text,
                std::function<void()> ja, std::function<void()> nein) {
    Dialog d;
    d.art = Dialog::Art::Frage;
    d.titel = titel;
    d.text = text;
    d.ja = std::move(ja);
    d.nein = std::move(nein);
    d.nummer = ++dialogNummer_;
    dialoge_.push_back(std::move(d));
}

void App::ZeichneDialog() {
    if (dialoge_.empty()) return;
    Dialog& d = dialoge_.front();
    const std::string id = d.titel + "###dialog" + std::to_string(d.nummer);
    if (!d.geoeffnet) {
        ImGui::OpenPopup(id.c_str());
        d.geoeffnet = true;
    }
    const float s = dpi_;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (d.art == Dialog::Art::Textfeld) {
        ImGui::SetNextWindowSize(ImVec2(500 * s, 400 * s), ImGuiCond_Appearing);
    } else if (d.art == Dialog::Art::Katze) {
        ImGui::SetNextWindowSize(ImVec2(500 * s, 620 * s), ImGuiCond_Appearing);
    } else {
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, V(kDark));
    // Was nach dem Schliessen ausgefuehrt wird. Erst nach EndPopup und nach
    // dem Entfernen aus der Warteschlange -- die Rueckrufe duerfen selbst
    // neue Dialoge anlegen.
    std::function<void()> danach;
    bool schliessen = false;
    // Ersteinrichtung: Dialoge, die eine Aufgabe dort neu anlegt (etwa die
    // Bestaetigung der Mapper), kommen VOR den naechsten Schritt.
    bool vorziehen = false;
    size_t vorherAnzahl = 0;

    // Die Katze bekommt ein X oben rechts (wie RAW-SNES).
    bool offen = true;
    const bool mitX = d.art == Dialog::Art::Katze;
    if (ImGui::BeginPopupModal(id.c_str(), mitX ? &offen : nullptr, flags)) {
        const float knopf = 90 * s;
        auto Knoepfe = [&](const char* a, const char* b) -> int {
            const float gesamt = b ? knopf * 2 + 8 * s : knopf;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - gesamt));
            int r = 0;
            if (ImGui::Button(a, ImVec2(knopf, 0))) r = 1;
            if (b) {
                ImGui::SameLine(0, 8 * s);
                if (ImGui::Button(b, ImVec2(knopf, 0))) r = 2;
            }
            return r;
        };

        switch (d.art) {
            case Dialog::Art::Info:
            case Dialog::Art::Warnung:
            case Dialog::Art::Fehler:
            case Dialog::Art::Frage: {
                ImU32 marke = d.art == Dialog::Art::Warnung ? kJustUnlocked
                            : d.art == Dialog::Art::Fehler ? Rgb(0xe06060) : 0;
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460 * s);
                if (marke) ImGui::PushStyleColor(ImGuiCol_Text, V(marke));
                ImGui::TextUnformatted(d.text.c_str());
                if (marke) ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8 * s));
                if (d.art == Dialog::Art::Frage) {
                    const int r = Knoepfe(T("Ja"), T("Nein"));
                    if (r == 1) { danach = d.ja; schliessen = true; }
                    if (r == 2) { danach = d.nein; schliessen = true; }
                } else {
                    if (Knoepfe("OK", nullptr) == 1 || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        danach = d.ja;
                        schliessen = true;
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !schliessen) {
                    danach = d.art == Dialog::Art::Frage ? d.nein : d.ja;
                    schliessen = true;
                }
                break;
            }
            case Dialog::Art::Auswahl: {
                // QInputDialog::getItem: Text, Auswahlliste, OK/Abbrechen.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460 * s);
                ImGui::TextUnformatted(d.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 4 * s));
                ImGui::SetNextItemWidth(460 * s);
                const char* vorschau = d.optionen.empty() ? "" : d.optionen[(size_t)d.wahl].c_str();
                if (ImGui::BeginCombo("##wahl", vorschau)) {
                    for (int i = 0; i < (int)d.optionen.size(); ++i) {
                        if (ImGui::Selectable(d.optionen[(size_t)i].c_str(), i == d.wahl)) d.wahl = i;
                    }
                    ImGui::EndCombo();
                }
                ImGui::Dummy(ImVec2(0, 8 * s));
                const int r = Knoepfe("OK", T("Abbrechen"));
                if (r == 1) {
                    const int w = d.wahl;
                    auto cb = d.gewaehlt;
                    danach = [cb, w]() { if (cb) cb(w); };
                    schliessen = true;
                }
                if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) { danach = d.nein; schliessen = true; }
                break;
            }
            case Dialog::Art::Textfeld: {
                // Bestaetigung mit Textfeld (FPGA-Mapper), Ja/Nein.
                const float unten = ImGui::GetFrameHeightWithSpacing() + 8 * s;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kPanel));
                ImGui::BeginChild("text", ImVec2(0, ImGui::GetContentRegionAvail().y - unten),
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(d.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 4 * s));
                const int r = Knoepfe(T("Ja"), T("Nein"));
                if (r == 1) { danach = d.ja; schliessen = true; }
                if (r == 2 || ImGui::IsKeyPressed(ImGuiKey_Escape)) { danach = d.nein; schliessen = true; }
                break;
            }
            case Dialog::Art::Nachricht: {
                // Reiner Text; ein Link oeffnet sich erst auf Klick.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 440 * s);
                ImGui::TextUnformatted((i18n::Englisch() ? d.textEn : d.text).c_str());
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8 * s));
                if (!d.link.empty()) {
                    if (ImGui::Button(T("Mehr erfahren"))) plattform::UrlOeffnen(d.link);
                    ImGui::SameLine();
                }
                if (ImGui::Button("OK", ImVec2(80 * s, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    danach = d.ja;      // erst jetzt gilt sie als gelesen
                    schliessen = true;
                }
                break;
            }
            case Dialog::Art::Ersteinrichtung: {
                const float breit = 380 * s;
                if (d.schritt == 0) {
                    ImGui::TextUnformatted("Sprache / Language / Idioma / Langue / Lingua / J\xC4\x99zyk");
                    ImGui::Dummy(ImVec2(0, 4 * s));
                    for (const auto& sp : kSprachen)
                        if (ImGui::Selectable(sp.name, d.sprache == sp.kennung, 0, ImVec2(breit, 0))) {
                            d.sprache = sp.kennung;
                            i18n::SpracheSetzen(d.sprache);   // sofort sichtbar
                        }
                    ImGui::Dummy(ImVec2(0, 8 * s));
                    if (ImGui::Button(T("Weiter"), ImVec2(breit, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        i18n::SpracheSetzen(d.sprache);
                        cfg_.einrichtungFertig = false;   // erst am Ende erledigt
                        saveConfig();
                        d.schritt = 1;
                    }
                } else if (d.schritt == 1) {
                    // Schritt 1: FPGA-Mapper. "jetzt" oeffnet sofort die
                    // Laufwerkswahl; die Bestaetigung der Mapper kommt vor dem
                    // naechsten Schritt (siehe "vorziehen" unten).
                    Txt(f_.fett, 14, kRed, T("Willkommen bei RAW-NES"));
                    ImGui::Dummy(ImVec2(0, 4 * s));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + breit);
                    ImGui::TextUnformatted(T("Damit Achievements funktionieren, müssen die FPGA-Mapper einmal auf die "
                                             "SD-Karte des EverDrive N8 PRO.\n\n1. SD-Karte aus der Konsole nehmen\n"
                                             "2. In den Kartenleser am PC stecken\n3. Im nächsten Fenster das Laufwerk "
                                             "mit dem Ordner EDN8 wählen\n\nDen Rest erledigt das Programm."));
                    ImGui::PopTextWrapPos();
                    ImGui::Dummy(ImVec2(0, 8 * s));
                    if (ImGui::Button(T("FPGA-Mapper jetzt einrichten"), ImVec2(breit, 0))) {
                        vorherAnzahl = dialoge_.size();
                        checkCores(true, true);   // Laufwerkswahl sofort
                        d.schritt = 2;
                        vorziehen = dialoge_.size() > vorherAnzahl;
                        if (vorziehen) ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Button((std::string(T("Später")) + "##mapper").c_str(), ImVec2(breit, 0))) {
                        appendLog(T("Das geht jederzeit über Optionen > FPGA-Mapper einrichten."));
                        mapperSpaeter_ = true;
                        d.schritt = 2;
                    }
                } else if (d.schritt == 2) {
                    // Kein Index mehr noetig: das Spiel wird ueber den Hash der
                    // gestarteten Datei erkannt. Direkt zum Abschluss.
                    danach = [this]() { ZeigeAbschluss(); };
                    schliessen = true;
                }
                break;
            }
            case Dialog::Art::Katze: {
                // about the cat: Portrait doppelt gross, Name, Jahre, Text.
                if (logo_.ok()) {
                    const ImVec2 g(logo_.w * 2.0f * s, logo_.h * 2.0f * s);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - g.x) * 0.5f);
                    ImGui::Image((ImTextureID)(intptr_t)logo_.id, g, ImVec2(0, 0), ImVec2(logo_.u, logo_.v));
                }
                Zentriert(f_.fett, 14, kUnlocked, T("In memory of Jason"));
                Zentriert(f_.ui, 11, kTextMuted, "2010 \xE2\x80\x93 2025");
                const float unten = ImGui::GetFrameHeightWithSpacing() + 8 * s;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kPanel));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 12 * s));
                ImGui::BeginChild("katzentext", ImVec2(0, ImGui::GetContentRegionAvail().y - unten),
                                  ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::PushStyleColor(ImGuiCol_Text, V(kTextMain));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(d.text.c_str());
                ImGui::Dummy(ImVec2(0, 6 * s));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 4 * s));
                ImGui::PushStyleColor(ImGuiCol_Text, V(kTextMuted));
                ImGui::TextUnformatted(T("Falls Sie kein Tier in Ihrer Familie haben: Auch die Tiere in den Tierheimen freuen sich, denn sie sind ebenfalls auf Hilfe angewiesen."));
                {
                    // "zusaetzlich" unterstreichen: erst die Tiere, das Projekt nur obendrein.
                    const char* satz = T("Und wer zusätzlich dieses Projekt unterstützen oder aus Wertschätzung etwas dalassen möchte, kann das hier tun:");
                    const char* wort = T("zusätzlich");
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::TextUnformatted(satz);
                    const std::string st(satz);
                    const size_t pos = st.find(wort);
                    if (pos != std::string::npos) {
                        const float x0 = ImGui::CalcTextSize(satz, satz + pos).x;
                        const float x1 = x0 + ImGui::CalcTextSize(wort).x;
                        if (x1 <= ImGui::GetContentRegionAvail().x) {   // Wort steht in der ersten Zeile
                            const float y = p.y + ImGui::GetFontSize();
                            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + x0, y), ImVec2(p.x + x1, y),
                                                                ImGui::GetColorU32(ImGuiCol_Text), 1.0f * s);
                        }
                    }
                }
                ImGui::PopStyleColor();
                if (ImGui::TextLink("Ko-fi")) plattform::UrlOeffnen("https://ko-fi.com/liqui69747");
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 4 * s));
                if (Knoepfe("OK", nullptr) == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape)) schliessen = true;
                break;
            }
        }
        if (schliessen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (mitX && !offen) schliessen = true;

    if (vorziehen && !schliessen) {
        // Ersteinrichtung kurz zuruecklegen: neue Dialoge nach vorn, die
        // Einrichtung geht danach beim gemerkten Schritt wieder auf.
        dialoge_.front().geoeffnet = false;
        std::vector<Dialog> neu(std::make_move_iterator(dialoge_.begin() + (long)vorherAnzahl),
                                std::make_move_iterator(dialoge_.end()));
        dialoge_.erase(dialoge_.begin() + (long)vorherAnzahl, dialoge_.end());
        dialoge_.insert(dialoge_.begin(), std::make_move_iterator(neu.begin()), std::make_move_iterator(neu.end()));
        return;
    }

    if (schliessen) {
        dialoge_.pop_front();
        if (danach) danach();
    }
}

// ============================================================================
// Badges -- Download in eigenem Thread, wie badge_loader.cpp (Cache-Ordner
// badge_cache neben der Exe, 8 s Zeitgrenze). Die Qt-Fassung lud synchron im
// Oberflaechen-Thread; hier blockiert ein langsames Netz das Fenster nicht.
// ============================================================================

App::Badge* App::BadgeHolen(const std::string& name) {
    if (name.empty()) return nullptr;
    auto it = badges_.find(name);
    if (it != badges_.end()) return &it->second;
    Badge& b = badges_[name];
    {
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeWarteschlange_.push_back(name);
    }
    badgeCv_.notify_one();
    return &b;
}

void App::BadgeThread() {
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const std::string ordner = plattform::ExeOrdner() + "/badge_cache";
    std::error_code ec;
    fs::create_directories(fs::u8path(ordner), ec);
    for (;;) {
        std::string name;
        {
            std::unique_lock<std::mutex> l(badgeMutex_);
            badgeCv_.wait(l, [this]() { return badgeEnde_ || !badgeWarteschlange_.empty(); });
            if (badgeEnde_) break;
            name = badgeWarteschlange_.front();
            badgeWarteschlange_.pop_front();
        }
        const std::string lokal = ordner + "/" + name + ".png";
        std::string png = LeseDatei(lokal);
        if (png.empty()) {
            std::string body;
            if (http_get("media.retroachievements.org", "/Badge/" + name + ".png", 8000,
                         "Mozilla/5.0 (MEGA-RAW Achievement Badge Loader)", &body)) {
                std::ofstream f(fs::u8path(lokal), std::ios::binary);
                f.write(body.data(), (std::streamsize)body.size());
                png = std::move(body);
            }
        }
        BadgeErgebnis e;
        e.name = name;
        e.ok = !png.empty() && bilder::PngDekodieren(png.data(), png.size(), &e.w, &e.h, &e.rgba);
        e.png = std::move(png);
        std::lock_guard<std::mutex> l(badgeMutex_);
        badgeFertig_.push_back(std::move(e));
    }
    ::CoUninitialize();
}

// ============================================================================
// Zeichnen
// ============================================================================

bool App::GruppeBeginn(const char* id, const std::string& titel, bool klickbar) {
    // QGroupBox: Rahmen 1 px kTextMuted, Ecken 3 px, Titel fett links oben im
    // Rahmen. Gezeichnet wird der Rahmen in GruppeEnde, wenn die Hoehe feststeht.
    const float s = dpi_;
    ImGui::PushID(id);
    Gruppe g;
    g.start = ImGui::GetCursorScreenPos();
    g.breite = ImGui::GetContentRegionAvail().x;
    g.innen = g.breite - 20 * s;
    g.titel = titel;

    ImGui::PushFont(f_.fett, 12);
    const ImVec2 ts = ImGui::CalcTextSize(titel.c_str());
    ImGui::PopFont();
    g.titelBreite = ts.x;
    g.titelHoehe = ts.y;
    bool geklickt = false;
    if (klickbar) {
        ImGui::SetCursorScreenPos(ImVec2(g.start.x + 8 * s, g.start.y));
        geklickt = ImGui::InvisibleButton("##titel", ImVec2(ts.x + 8 * s, ts.y));
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    g.inhaltX = g.start.x + 10 * s;
    ImGui::SetCursorScreenPos(ImVec2(g.inhaltX, g.start.y + ts.y + 6 * s));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 6 * s));
    gruppen_.push_back(std::move(g));
    return geklickt;
}

void App::GruppeEnde() {
    const float s = dpi_;
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    const Gruppe g = gruppen_.back();
    gruppen_.pop_back();
    const float unten = ImGui::GetItemRectMax().y + 8 * s;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = g.start.x, y = g.start.y;
    dl->AddRect(ImVec2(x + 0.5f, y + g.titelHoehe * 0.5f), ImVec2(x + g.breite - 0.5f, unten),
                kTextMuted, 3 * s, 0, 1.0f);
    // Titel unterbricht die Rahmenlinie (subcontrol-origin: margin; left: 8px; padding: 0 4px).
    dl->AddRectFilled(ImVec2(x + 8 * s, y), ImVec2(x + 8 * s + g.titelBreite + 8 * s, y + g.titelHoehe), kDark);
    dl->AddText(f_.fett, 12 * s, ImVec2(x + 12 * s, y), kTextMuted, g.titel.c_str());

    ImGui::SetCursorScreenPos(ImVec2(x, unten));
    ImGui::Dummy(ImVec2(g.breite, 1));
    ImGui::PopID();
}

void App::Zeichnen() {
    // Auftraege aus Worker und Hilfsthreads.
    {
        std::vector<std::function<void()>> q;
        {
            std::lock_guard<std::mutex> l(postMutex_);
            q.swap(postQueue_);
        }
        for (auto& f : q) f();
    }
    // Fertig geladene Badges als Textur hochladen (nur hier: OpenGL-Thread).
    {
        std::vector<BadgeErgebnis> fertig;
        {
            std::lock_guard<std::mutex> l(badgeMutex_);
            fertig.swap(badgeFertig_);
        }
        for (auto& e : fertig) {
            Badge& b = badges_[e.name];
            b.png = std::move(e.png);
            if (e.ok) {
                b.tex = bilder::Hochladen(e.rgba.data(), e.w, e.h, false);
                b.zustand = Badge::Zustand::Fertig;
            } else {
                b.zustand = Badge::Zustand::Fehlt;
            }
        }
    }
    // Stiller Mapper-Check kurz nach dem Start (QTimer::singleShot(600, ...)).
    if (!startCheckErledigt_ && JetztMs() - startMs_ >= 600) {
        startCheckErledigt_ = true;
        checkCores(false);
    }
    autoVerbindenPruefen();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##haupt", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);

    ZeichneKopf();
    ZeichneKoerper();

    ImGui::End();
    ZeichneDialog();
}

void App::ZeichneKopf() {
    const float s = dpi_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float breite = ImGui::GetContentRegionAvail().x;

    // Hintergrund vorab: Hoehe steht erst am Ende fest, deshalb in einen
    // eigenen Kanal hinter den Inhalt.
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    // --- Menueleiste oben links: Optionen, Ko-fi -- wie in mega-raw und
    // raw-turn (QMenuBar, Standardschrift, Eintraege ohne Rahmen).
    {
        ImGui::SetCursorScreenPos(ImVec2(start.x + 2 * s, start.y + 2 * s));
        ImGui::PushFont(f_.ui, 12);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * s, 4 * s));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, V(0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(Rgb(0xc8c4bc)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(Rgb(0xb9b4ab)));
        ImGui::PushStyleColor(ImGuiCol_Text, V(kDark));
        const bool menueOffen = ImGui::IsPopupOpen("optionen_menue");
        if (menueOffen) ImGui::PushStyleColor(ImGuiCol_Button, V(Rgb(0xc8c4bc)));
        if (ImGui::Button((std::string(T("Optionen")) + "###optionen").c_str())) ImGui::OpenPopup("optionen_menue");
        if (menueOffen) ImGui::PopStyleColor();
        const ImVec2 unterKnopf(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
        ImGui::SameLine(0, 0);
        if (ImGui::Button((std::string(T("Ko-fi")) + "###kofi").c_str())) onOpenSupport();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
        ImGui::PopFont();
        if (ImGui::IsPopupOpen("optionen_menue")) ImGui::SetNextWindowPos(unterKnopf);
        ZeichneOptionenMenue();
    }
    const float menueUnten = ImGui::GetCursorScreenPos().y;

    ImGui::SetCursorScreenPos(ImVec2(start.x + 14 * s, menueUnten + 4 * s));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * s));
    const float innen = breite - 28 * s;

    // --- Katzen-Portrait
    if (logo_.ok()) {
        const float h = 53.0f * s;
        const float w = h * logo_.w / logo_.h;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (innen - w) * 0.5f);
        ImGui::Image((ImTextureID)(intptr_t)logo_.id, ImVec2(w, h), ImVec2(0, 0), ImVec2(logo_.u, logo_.v));
    }

    auto ZentriertIn = [&](ImFont* f, float px, ImU32 farbe, const std::string& t) {
        ImGui::PushFont(f, px);
        const float b = ImGui::CalcTextSize(t.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (innen - b) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, V(farbe));
        ImGui::TextUnformatted(t.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    };
    ZentriertIn(f_.fett, 20, kRed, "RAW-NES");
    ZentriertIn(f_.fett, 11, kDark, Fmt(T("— v%1 build %2 — und immer noch nicht perfekt —"), {RAWNES_VERSION, i18n::Zahl(RAWNES_BUILD)}));
    ZentriertIn(f_.ui, 9, kDark, "\xC2\xA9 2026 Liqui");
    ZentriertIn(f_.ui, 9, kDark,
                T("Schnellstart:  1. Anmelden   2. Spielordner einmal indexieren   3. Spiel am Gerät "
                  "starten, dann Monitor starten"));

    ImGui::PopStyleVar();
    ImGui::EndGroup();
    const float ende = ImGui::GetItemRectMax().y + 10 * s;

    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(start, ImVec2(start.x + breite, ende), kLight);
    dl->AddRectFilled(ImVec2(start.x, ende), ImVec2(start.x + breite, ende + 3 * s), kRed);
    dl->ChannelsMerge();

    ImGui::SetCursorScreenPos(ImVec2(start.x, ende + 3 * s));
}

void App::ZeichneOptionenMenue() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, V(kLight));
    ImGui::PushStyleColor(ImGuiCol_Text, V(kDark));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, V(Rgb(0xc8c4bc)));
    ImGui::PushStyleColor(ImGuiCol_Border, V(kDark));
    ImGui::PushStyleColor(ImGuiCol_Separator, V(Rgb(0xa9a49c)));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, V(kRed));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * dpi_, 6 * dpi_));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * dpi_, 4 * dpi_));
    if (ImGui::BeginPopup("optionen_menue")) {
        if (ImGui::MenuItem(T("FPGA-Mapper einrichten"))) checkCores(true);
        // Gegenstueck zum Einrichten -- in der Qt-Fassung zeitweise nicht
        // erreichbar, deshalb ausdruecklich im Menue.
        if (ImGui::MenuItem(T("Original-Mapper wiederherstellen"))) onRestoreClicked();
        ImGui::Separator();
        if (ImGui::BeginMenu(T("Sprache / Language"))) {
            for (const auto& sp : kSprachen)
                if (ImGui::MenuItem(sp.name, nullptr, i18n::Sprache() == sp.kennung)) setLanguage(sp.kennung);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(T("über die Katze"))) {
            ZeigeKatze();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6);
}

void App::ZeichneKoerper() {
    const float s = dpi_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float griff = 18 * s;
    const float seite = lbSidebarOpen_ ? kLbSidebarWidth * s : 0.0f;
    const float links = std::max(100.0f, avail.x - griff - seite);

    // --- Koerper: Scrollbereich mit den Gruppen
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kDark));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * s, 12 * s));
    ImGui::BeginChild("koerper", ImVec2(links, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * s, 10 * s));
    // Platz verteilen: alles ausser Achievement-Liste und Log ist fest
    // (gemessen im vorigen Bild). Den Rest bekommt zuerst das Log in normaler
    // Hoehe, dann die Liste; reicht er nicht, schrumpft erst die Liste (bis
    // ~2 Zeilen), dann das Log (bis ~4 Zeilen).
    {
        const float platz = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
        const float rest = platz - fixHoehe_;
        const float logNorm = logOffen_ ? kLogHoehe * s : 0.0f;
        listH_ = std::max(kAchMinHoehe * s, rest - logNorm);
        logH_ = logOffen_ ? std::max(kLogMinHoehe * s, std::min(logNorm, rest - listH_)) : 0.0f;
    }
    ZeichneRa();
    ZeichneEverDrive();
    ZeichneAchievements();
    ZeichneLiveWerte();
    ZeichneLog();
    // Die Achievement-Gruppe nimmt den freien Platz bis zum unteren Rand
    // (bodyLayout->addWidget(achGroup, 2) in der Qt-Fassung); das Log sitzt
    // dann unten. Gemessen wird hier, wirksam im naechsten Bild.
    {
        // Feste Hoehe = alles, was nicht Liste oder Log ist.
        const float ende = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y;
        fixHoehe_ = ende - listH_ - logH_;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // --- Griff: schmaler Streifen, klappt die Leaderboard-Seitenleiste
    ImGui::SameLine(0, 0);
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const bool k = ImGui::InvisibleButton("##lbgriff", ImVec2(griff, avail.y));
        const bool ueber = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + griff, p0.y + avail.y), ueber ? kDark : kPanel);
        ImGui::PushFont(f_.fett, 14);
        const char* pfeil = lbSidebarOpen_ ? "\xE2\x80\xBA" : "\xE2\x80\xB9";  // › ‹
        const ImVec2 ts = ImGui::CalcTextSize(pfeil);
        dl->AddText(ImVec2(p0.x + (griff - ts.x) * 0.5f, p0.y + (avail.y - ts.y) * 0.5f), kTextMain, pfeil);
        ImGui::PopFont();
        if (ueber) ImGui::SetTooltip("%s", T("Leaderboards ein-/ausklappen"));
        if (k) toggleLbSidebar(!lbSidebarOpen_);
    }

    if (lbSidebarOpen_) {
        ImGui::SameLine(0, 0);
        ZeichneSeitenleiste();
    }
}

// Statuszeile neben dem Login. Nach einer Anmeldung kurz eine Einblendung wie
// in MEGA-RAW: mittig, fett, acht ruhige Pulse (9,5 s), 0,6 s stehen, dann in
// 0,7 s nach links gleiten; danach bleibt der normale Status ("✓ Name").
void App::ZeichneLoginStatus(const std::string& status) {
    const double kBlink = 9.5, kPause = 0.6, kGleiten = 0.7;
    const double t = anmeldStart_ < 0 ? -1.0 : ImGui::GetTime() - anmeldStart_;
    if (t < 0 || t >= kBlink + kPause + kGleiten || token_.empty()) {
        anmeldStart_ = -1.0;
        ImGui::PushStyleColor(ImGuiCol_Text, V(loginOk_ ? kUnlocked : kTextMuted));
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopStyleColor();
        return;
    }
    // Deckkraft: Stuetzwerte 0 -> 1 -> 0,25 -> 1 ... (letzter Puls bleibt 1),
    // ueber den ganzen Ablauf mit InOutSine verteilt.
    float alpha = 1.0f;
    if (t < kBlink) {
        const double p = t / kBlink;
        const double e = -(std::cos(3.14159265358979 * p) - 1.0) / 2.0;
        const int kPulse = 8;
        const double pos = e * 2 * kPulse;
        const int k = std::min((int)pos, 2 * kPulse - 1);
        const double f = pos - k;
        auto stuetz = [&](int i) -> double {
            if (i == 0) return 0.0;
            if (i % 2 == 1) return 1.0;
            return i == 2 * kPulse ? 1.0 : 0.25;
        };
        alpha = (float)(stuetz(k) + (stuetz(k + 1) - stuetz(k)) * f);
    }
    // Text wird je Bild gebildet, damit er einem Sprachwechsel folgt.
    const std::string text = "\xE2\x9C\x93 " + (anmeldAuto_
        ? Fmt(T("Automatisch angemeldet als %1 (per Sitzungstoken)"), {anmeldUser_})
        : Fmt(T("Angemeldet als %1"), {anmeldUser_}));
    ImGui::PushFont(f_.fett, ImGui::GetFontSize());
    const ImVec2 groesse = ImGui::CalcTextSize(text.c_str());
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float breite = ImGui::GetContentRegionAvail().x;
    const float xMitte = std::max(0.0f, (breite - groesse.x) / 2);
    float x = xMitte;
    if (t > kBlink + kPause) {
        double p = (t - kBlink - kPause) / kGleiten;
        p = p < 0.5 ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3) / 2;   // InOutCubic
        x = (float)(xMitte * (1.0 - p));
    }
    ImU32 farbe = kUnlocked;
    farbe = (farbe & ~IM_COL32_A_MASK) | ((ImU32)(alpha * 255.0f) << IM_COL32_A_SHIFT);
    ImGui::GetWindowDrawList()->AddText(ImVec2(start.x + x, start.y), farbe, text.c_str());
    ImGui::Dummy(ImVec2(std::max(1.0f, breite), groesse.y));
    ImGui::PopFont();
}

void App::ZeichneRa() {
    const float s = dpi_;
    GruppeBeginn("ra", T("RetroAchievements"));
    ImGui::PushFont(f_.ui, 12);
    ImGui::AlignTextToFramePadding();

    ImGui::SetNextItemWidth(150 * s);
    ImGui::InputTextWithHint("##user", T("RA-Benutzername"), userBuf_, sizeof(userBuf_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150 * s);
    ImGui::InputTextWithHint("##pass", T("RA-Passwort"), passBuf_, sizeof(passBuf_), ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::BeginDisabled(loginLaeuft_);
    if (ImGui::Button(T("Anmelden"))) onLogin();
    ImGui::EndDisabled();
    ImGui::SameLine();
    // Ohne Haken werden die Zugangsdaten NICHT abgelegt (Standard: an).
    if (Haken(T("Eingeloggt bleiben"), &rememberLogin_)) saveConfig();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Benutzername und Sitzungsschlüssel speichern (das Passwort wird nicht gespeichert)"));
    // Status in eigener Zeile unter den Feldern: der lange Text der
    // Einblendung passt nicht hinter den Haken.
    const std::string status = loginStatusText_.empty() ? T(loginStatusKey_.c_str()) : loginStatusText_;
    ZeichneLoginStatus(status);
    ImGui::PopFont();
    GruppeEnde();
}

void App::ZeichneEverDrive() {
    const float s = dpi_;
    GruppeBeginn("everdrive", T("EverDrive"));
    ImGui::PushFont(f_.ui, 12);
    const float innen = gruppen_.back().innen;
    const float inhaltX = gruppen_.back().inhaltX;

    // Zeile: Verbindungsstatus ... [Verbinden]
    {
        // Die Zeile sagt, was der MONITOR tut -- die Verbindung besteht fuer die
        // Automatik ohnehin dauerhaft und wird nicht mehr eigens genannt.
        std::string status;
        ImU32 farbe = kTextMuted;
        if (!connAttempt_.empty()) status = connAttempt_;
        else if (connPort_.empty()) {
            status = T(connStatusKey_.c_str());
            if (connFarbe_ == kRed) farbe = kRed;
        } else if (monitoring_) {
            status = Fmt(T("✓ Monitor läuft (%1)"), {connPort_});
            farbe = kUnlocked;
        } else if (leerlauf_ == 3) {
            status = T("Konsole aus");
            farbe = kOrange;
        } else if (leerlauf_ == 1) {
            status = T("EverDrive-Menü");
            farbe = kOrange;
        } else if (autoPause_) {
            status = T("Monitor angehalten");
            farbe = kJustUnlocked;
        } else if (leerlauf_ == 2) {
            status = T("Spiel läuft");
        } else {
            status = T("verbinde...");   // verbunden, aber der Zustand ist noch nicht abgefragt
        }
        ImGui::AlignTextToFramePadding();
        Txt(f_.fett, 12, farbe, status);
        // Kein eigener Verbinden-Knopf mehr: "Spiel erkennen & Monitor starten"
        // sucht das EverDrive selbst (wie MEGA-RAW).
        (void)innen;
        (void)inhaltX;
    }
    (void)s;

    Txt(f_.ui, 12, kTextMuted, gameStatus_.empty() ? T("kein Spiel geladen") : gameStatus_);

    // Zeile: [ROM-Sammlung indexieren...] Status
    {
        ImGui::BeginDisabled(indexLaeuft_);
        if (ImGui::Button(T("ROM-Sammlung indexieren..."))) onIndexRoms();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        std::string label = indexStatus_;
        if (label.empty()) {
            if (!romIndex_ || romIndex_->empty()) {
                label = T("(nicht indexiert — ROM-Auswahl per Dialog)");
            } else {
                long long n = 0;
                for (auto& kv : *romIndex_) n += (long long)kv.second.size();
                label = Fmt(T("%1 ROMs indexiert"), {i18n::Zahl(n)});
            }
        }
        Txt(f_.ui, 12, kTextMuted, label);
    }

    // Start-/Stopp-Knopf
    {
        ImGui::BeginDisabled(!monitorBtnEnabled_);
        if (monitorBtnRot_) ImGui::PushStyleColor(ImGuiCol_Text, V(Rgb(0xe05a5a)));
        ImGui::PushFont(monitorBtnRot_ ? f_.fett : f_.ui, 12);
        if (ImGui::Button((std::string(T(monitorBtnKey_.c_str())) + "###monitor").c_str())) onToggleMonitor();
        ImGui::PopFont();
        if (monitorBtnRot_) ImGui::PopStyleColor();
        ImGui::EndDisabled();
    }

    // Hardcore-Modus + Hinweis. Ausgeblendet, bis RetroAchievements das
    // Werkzeug freigibt -- bis dahin wertet der Server alles als Softcore.
    if (kHardcoreAktiv) {
        ImGui::BeginDisabled(!hardcoreEnabled_);
        bool hc = hardcore_;
        if (Haken(T("Hardcore-Modus"), &hc)) {
            hardcore_ = hc;
            // Leaderboards zaehlen nur im Hardcore-Modus: Seitenleiste oeffnen.
            if (hc) toggleLbSidebar(true);
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8 * s);
        ImGui::AlignTextToFramePadding();
        Txt(f_.ui, 10, kTextMuted, T("(erfordert deaktivierte Savestates/Cheats im EverDrive-Menü)"));
    }
    ImGui::PopFont();
    GruppeEnde();
}

void App::ZeichneAchZeile(AchZeile& z, float breite) {
    const float s = dpi_;
    const float hoehe = 52 * s;
    ImGui::PushID((int)(z.id & 0x7fffffff));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##zeile", ImVec2(breite, hoehe));
    const bool doppel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool klick = ImGui::IsItemActivated();
    ImGui::PopID();

    // Statusfarben und Deckkraft wie achievement_row.cpp (applyStateStyle).
    ImU32 accent = kTextMuted, pointsColor = kTextMuted;
    float deck = 1.0f;
    switch (z.state) {
        case AchState::Unlocked:     accent = pointsColor = kUnlocked; break;
        case AchState::JustUnlocked: accent = pointsColor = kJustUnlocked; break;
        case AchState::Locked:       accent = pointsColor = kTextMuted; deck = 0.7f; break;
        case AchState::Unsupported:  accent = pointsColor = kInProgress; deck = 0.5f; break;
        case AchState::Waiting:      accent = pointsColor = kWaiting; deck = 0.85f; break;
    }
    const bool selected = (selectedId_ == z.id);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + breite, p0.y + hoehe);
    // 3 px farbiger Streifen links
    dl->AddRectFilled(p0, ImVec2(p0.x + 3 * s, p1.y), accent);
    const ImVec2 c0(p0.x + 3 * s, p0.y);
    if (selected) {
        dl->AddRectFilled(c0, p1, MitDeckkraft(kLockedIconBg, deck), 4 * s);
        dl->AddRect(c0, p1, MitDeckkraft(kUnlocked, deck), 4 * s, 0, 1.0f);
    } else {
        dl->AddRectFilled(c0, p1, MitDeckkraft(kPanel, deck), 4 * s);
    }

    // Badge 36x36
    const ImVec2 b0(c0.x + 10 * s, p0.y + 8 * s), b1(b0.x + 36 * s, b0.y + 36 * s);
    dl->AddRectFilled(b0, b1, MitDeckkraft(kLockedIconBg, deck), 4 * s);
    if (!z.badge.empty()) {
        const bool frei = z.state == AchState::Unlocked || z.state == AchState::JustUnlocked;
        Badge* b = BadgeHolen(frei ? z.badge : z.badge + "_lock");
        if (b && b->tex.ok())
            dl->AddImage((ImTextureID)(intptr_t)b->tex.id, b0, b1, ImVec2(0, 0), ImVec2(b->tex.u, b->tex.v),
                         MitDeckkraft(IM_COL32_WHITE, deck));
    }

    // Rechte Spalte: Punkte bzw. Fortschritt, darunter Balken 70x6
    const float rechtsBreite = 70 * s;
    const float rx1 = p1.x - 10 * s;
    std::string punkte = z.showProgress
        ? std::to_string(z.progCur) + "/" + std::to_string(z.progTarget)
        : Fmt(T("%1 Pkt"), {i18n::Zahl(z.points)});
    ImFont* fu = f_.ui;
    const float pw = fu->CalcTextSizeA(11 * s, FLT_MAX, 0.0f, punkte.c_str()).x;
    const float py = z.showProgress ? p0.y + 14 * s : p0.y + (hoehe - 11 * s) * 0.5f - 2 * s;
    dl->AddText(fu, 11 * s, ImVec2(rx1 - pw, py), MitDeckkraft(pointsColor, deck), punkte.c_str());
    if (z.showProgress && z.progTarget > 1) {
        const ImVec2 q0(rx1 - rechtsBreite, py + 15 * s), q1(rx1, py + 21 * s);
        dl->AddRectFilled(q0, q1, MitDeckkraft(kLockedIconBg, deck), 3 * s);
        const float anteil = std::max(0.0f, std::min(1.0f, (float)z.progCur / (float)z.progTarget));
        if (anteil > 0) dl->AddRectFilled(q0, ImVec2(q0.x + (q1.x - q0.x) * anteil, q1.y),
                                          MitDeckkraft(kInProgress, deck), 3 * s);
    }

    // Titel und Beschreibung
    const float tx = b1.x + 10 * s;
    const float tw = std::max(10.0f, rx1 - rechtsBreite - 10 * s - tx);
    dl->PushClipRect(ImVec2(tx, p0.y), ImVec2(tx + tw, p1.y), true);
    dl->AddText(fu, 12 * s, ImVec2(tx, p0.y + 7 * s), MitDeckkraft(kTextMain, deck), z.title.c_str());
    dl->AddText(fu, 10 * s, ImVec2(tx, p0.y + 24 * s), MitDeckkraft(kTextMuted, deck), z.desc.c_str(), nullptr, tw);
    dl->PopClipRect();

    if (klick) {
        // Einfachklick: Zeile hervorheben + Diagnose in den Log.
        selectedId_ = z.id;
        worker_->diagnoseAchievement(z.id);
    }
    if (doppel) plattform::UrlOeffnen("https://retroachievements.org/achievement/" + std::to_string(z.id));
    ImGui::Dummy(ImVec2(0, 4 * s));
}

void App::ZeichneAchievements() {
    const float s = dpi_;
    GruppeBeginn("achievements", T("Achievements (Klick = Diagnose, Doppelklick = auf RetroAchievements öffnen)"));
    const float innen = gruppen_.back().innen;
    // Die Liste selbst nimmt den freien Platz (frueher: feste ~3 Zeilen plus
    // leere Flaeche darunter). Gescrollt wird nur INNERHALB der Liste.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kDark));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("achliste", ImVec2(innen, listH_));
    const float breite = ImGui::GetContentRegionAvail().x;
    for (auto& z : ach_) ZeichneAchZeile(z, breite);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    GruppeEnde();
}

void App::ZeichneLiveWerte() {
    const float s = dpi_;
    GruppeBeginn("live", T("Live-Werte"));
    const float innen = gruppen_.back().innen;
    // Vier Zeilen bei 11 pt und 165 % Zeilenabstand; der Rest scrollt.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(Rgb(0x0a0e14)));
    ImGui::PushStyleColor(ImGuiCol_Border, V(Rgb(0x2a3a4a)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, V(Rgb(0x0a0e14)));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, V(Rgb(0x2a3a4a)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 4 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10 * s);
    ImGui::BeginChild("live", ImVec2(innen, 112 * s), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    const float px = 11.0f * 96.0f / 72.0f;  // 11pt
    if (live_.empty()) {
        Txt(f_.mono, px, Rgb(0x3dd68c), T("RAM: -"));
    } else {
        const unsigned long long now = JetztMs();
        const float zeile = px * s * 1.65f;
        const float spalte = ImGui::GetContentRegionAvail().x / 3.0f;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int i = 0;
        for (const auto& [slot, z] : live_) {
            (void)slot;
            const float x = o.x + (i % 3) * spalte;
            const float y = o.y + (i / 3) * zeile + (zeile - px * s) * 0.5f;
            char adr[8];
            std::snprintf(adr, sizeof(adr), "$%04X", z.addr);
            char wert[4];
            if (z.value < 0) std::snprintf(wert, sizeof(wert), "--");
            else std::snprintf(wert, sizeof(wert), "%02X", z.value);
            dl->AddText(f_.mono, px * s, ImVec2(x, y), Rgb(0x5a7a6a), adr);
            const float aw = f_.mono->CalcTextSizeA(px * s, FLT_MAX, 0.0f, adr).x +
                             f_.mono->CalcTextSizeA(px * s, FLT_MAX, 0.0f, " ").x;
            dl->AddText(f_.monoFett ? f_.monoFett : f_.mono, px * s, ImVec2(x + aw, y),
                        FarbeNachAlter(z.lastChangeMs, now), wert);
            ++i;
        }
        ImGui::Dummy(ImVec2(1, ((i + 2) / 3) * zeile));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    GruppeEnde();
}

void App::ZeichneLog() {
    const float s = dpi_;
    // Einklappbar, standardmaessig ZU -- eingeklappt nur die Titelzeile.
    const char* titel = logOffen_ ? T("▼ Log (zuklappen)") : T("▶ Log (aufklappen)");
    if (GruppeBeginn("log", titel, true)) logOffen_ = !logOffen_;
    const float innen = gruppen_.back().innen;
    if (logOffen_) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kPanel));
        ImGui::PushStyleColor(ImGuiCol_Text, V(kTextMain));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * s, 4 * s));
        ImGui::BeginChild("log", ImVec2(innen, logH_), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(f_.mono, 11);
        ImGuiListClipper clip;
        clip.Begin((int)log_.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(log_[(size_t)i].c_str());
        ImGui::PopFont();
        // Am Ende bleiben, solange der Nutzer nicht hochgescrollt hat.
        if (logAmEnde_) ImGui::SetScrollHereY(1.0f);
        logAmEnde_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;

        // Kopieren: In der Qt-Fassung liess sich der Text markieren; fuer
        // Fehlermeldungen wird das Log gebraucht (README: "Log aus dem
        // Programmfenster"). Rechtsklick -> kopieren, oder Strg+C ueber dem Log.
        auto kopieren = [this]() {
            std::string alles;
            for (const auto& z : log_) { alles += z; alles += "\r\n"; }
            ImGui::SetClipboardText(alles.c_str());
        };
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_C, false))
            kopieren();
        if (ImGui::BeginPopupContextWindow("logmenue")) {
            if (ImGui::MenuItem(T("Log kopieren"))) kopieren();
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        // Sichtbarer Knopf: fuer Fehlermeldungen das ganze Log in die
        // Zwischenablage. Zeigt kurz "Kopiert!" als Bestaetigung.
        const bool frisch = logKopiertMs_ && JetztMs() - logKopiertMs_ < 2000;
        const std::string knopf = std::string(frisch ? T("Kopiert!") : T("Log kopieren")) + "###logkopieren";
        if (ImGui::Button(knopf.c_str())) {
            kopieren();
            logKopiertMs_ = JetztMs();
        }
    } else {
        ImGui::Dummy(ImVec2(innen, 0));
    }
    GruppeEnde();
}

void App::ZeichneSeitenleiste() {
    const float s = dpi_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kDark));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s, 6 * s));
    ImGui::BeginChild("seitenleiste", ImVec2(avail.x, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(kPanel));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * s, 4 * s));
    ImGui::BeginChild("lbliste", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(f_.ui, 11);
    ImGui::PushStyleColor(ImGuiCol_Text, V(kTextMain));
    for (const auto& z : lbs_) {
        // Status ist ein deutscher Schluessel; "eingereicht: " traegt den Wert
        // hinter einem Trennzeichen.
        std::string status;
        const size_t trenn = z.status.find('\x01');
        if (trenn != std::string::npos) status = std::string(T(z.status.substr(0, trenn).c_str())) + z.status.substr(trenn + 1);
        else status = T(z.status.c_str());
        const std::string zeile = z.title + " -- " + status;
        ImGui::TextUnformatted(zeile.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
