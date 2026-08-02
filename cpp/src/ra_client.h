#pragma once
#include "json.h"
#include "ra_cache.h"
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Der eigentliche HTTP-Call ist injizierbar (RequestFn) -- Muster 1:1 aus
// MEGA-RAWs ra_client.h uebernommen, damit diese Logik ohne echtes
// Netzwerk testbar bleibt. Fuer die echte GUI wird RequestFn durch
// make_qt_request_fn() (ra_network.h) ersetzt.

struct RateLimited : std::runtime_error {
    int retry_after;
    explicit RateLimited(int ra) : std::runtime_error("rate limited"), retry_after(ra) {}
};

using RequestFn = std::function<Json(const std::map<std::string, std::string>& params)>;

struct Achievement {
    int64_t id = 0;
    std::string title;
    std::string desc;
    std::string mem;   // MemAddr-Bedingungsstring, wird von ra_engine geparst
    int points = 0;
    std::string badge;
};

// NEU (nicht in ra_client_nes.py -- das liest "Leaderboards" nie aus der
// Patch-Antwort, siehe ausfuehrlicher Kommentar in build_game()).
// mem enthaelt den kompletten "STA:...::CAN:...::SUB:...::VAL:..."-String,
// wird beim Laden in die vier Teile zerlegt (siehe splitLeaderboardMem()).
struct Leaderboard {
    int64_t id = 0;
    std::string title;
    std::string desc;
    std::string format; // "SCORE" | "TIME_SECONDS" | "TIME_FRAMES" | ...
    std::string startMem, cancelMem, submitMem, valueMem;
};

struct GameData {
    std::string name;
    int64_t gameid = 0;
    std::string md5;
    bool no_set = false;
    std::vector<Achievement> achievements;
    // NEU, siehe Leaderboard-Struct-Kommentar.
    std::vector<Leaderboard> leaderboards;
    // NEU -- rohes Rich-Presence-Script aus der Patch-Antwort, falls
    // vorhanden. RichPresence.h wertet es (bewusst minimal, kein voller
    // Interpreter) zu einem Aktivitaetstext fuer ra_ping() aus.
    std::string richPresenceScript;
};

class RaClient {
public:
    RaClient(RaCache& cache, RequestFn request_fn);

    // ra_login2: Token bei Erfolg, sonst leer.
    std::optional<std::string> ra_login(const std::string& user, const std::string& pw);

    // RA-Hash fuer NES: iNES-Header (16 Byte) abschneiden, Rest hashen.
    static std::string nes_hash(const std::string& rom_path);

    // ra_gameid: Rueckgabe leer bei "kein GameID". Wirft RateLimited weiter,
    // wenn raise_limit=true und die Anfrage drosselt.
    std::optional<long long> ra_gameid(const std::string& md5, bool raise_limit = false);

    // ra_patch + Achievement-Set-Aufbereitung (Flags==3, ID<101000000).
    std::optional<GameData> build_game(long long gameid, const std::string& user,
                                       const std::string& token);

    // Kompletter Ablauf: Hash -> GameID -> Achievement-Set.
    std::pair<std::optional<GameData>, std::string> identify_and_load(
        const std::string& rom_path, const std::string& user, const std::string& token);

    // ra_unlocks: bereits freigeschaltete IDs (optional gefiltert auf
    // gueltige_ids), Statustext.
    std::pair<std::set<long long>, std::string> ra_unlocks(
        long long gameid, const std::string& user, const std::string& token,
        const std::set<long long>& gueltige_ids, bool hardcore);

    // r=ping -- Aktivitaets-Ping, alle 2 Minuten faellig (Spielzeit-
    // Tracking laeuft serverseitig ueber die Ping-Frequenz).
    std::pair<bool, std::string> ra_ping(long long gameid, const std::string& user,
                                         const std::string& token,
                                         const std::string& rich_presence = "");

    // r=awardachievement
    std::pair<bool, std::string> ra_award(long long achid, const std::string& user,
                                          const std::string& token, bool hardcore);

    // r=submitlbentry -- Leaderboard-Wert uebermitteln.
    // ACHTUNG: Hash-Formel bestaetigt (echter RetroArch-Quellcode), aber
    // Parameternamen (i/s/v) sind aus dem Muster verwandter Endpunkte
    // abgeleitet, nicht 1:1 quellverifiziert.
    std::pair<bool, std::string> ra_submit_lb(long long lb_id, const std::string& user,
                                              const std::string& token, uint32_t value);

    RaCache* cachePtr() { return &cache_; }

private:
    RaCache& cache_;
    RequestFn request_;
};
