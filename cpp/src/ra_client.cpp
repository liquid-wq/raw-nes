#include "ra_client.h"
#include "Md5.h"

#include <algorithm>
#include <fstream>

RaClient::RaClient(RaCache& cache, RequestFn request_fn)
    : cache_(cache), request_(std::move(request_fn)) {}

namespace {
std::optional<long long> falsy_or_none(const Json* v) {
    if (!v || v->is_null()) return std::nullopt;
    if (v->type() == Json::Type::Int && v->as_int() == 0) return std::nullopt;
    if (v->type() == Json::Type::Int) return v->as_int();
    return std::nullopt;
}
std::string to_lower_str(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Zerlegt den kombinierten Leaderboard-Mem-String ("STA:...::CAN:...::
// SUB:...::VAL:...") in die vier Teile. ANNAHME, nicht gegen eine echte
// RA-Antwort dieses Projekts verifiziert (siehe ausfuehrlicher Kommentar
// in build_game()) -- basiert auf dem oeffentlich dokumentierten, seit
// Jahren stabilen RA-Leaderboard-Format. Fehlt ein Teil, bleibt das
// jeweilige Feld leer -> die zugehoerige AchievementRuntime wird dann
// unsupported (parseMemAddr("") -> leere Gruppe -> isUnsupported=true).
struct LbMemParts { std::string sta, can, sub, val; };
LbMemParts splitLeaderboardMem(const std::string& raw) {
    LbMemParts out;
    std::string s = raw;
    auto extract = [&](const std::string& tag) -> std::string {
        size_t pos = s.find(tag);
        if (pos == std::string::npos) return "";
        size_t start = pos + tag.size();
        size_t end = s.find("::", start);
        std::string part = (end == std::string::npos) ? s.substr(start) : s.substr(start, end - start);
        return part;
    };
    out.sta = extract("STA:");
    out.can = extract("CAN:");
    out.sub = extract("SUB:");
    out.val = extract("VAL:");
    return out;
}
} // namespace

std::optional<std::string> RaClient::ra_login(const std::string& user, const std::string& pw) {
    try {
        Json d = request_({{"r", "login2"}, {"u", user}, {"p", pw}});
        const Json* success = d.find("Success");
        bool ok = success && success->type() == Json::Type::Bool && success->as_bool();
        if (!ok) return std::nullopt;
        const Json* token = d.find("Token");
        if (!token || token->type() != Json::Type::String) return std::nullopt;
        return token->as_string();
    } catch (...) {
        return std::nullopt;
    }
}

std::string RaClient::nes_hash(const std::string& rom_path) {
    std::ifstream f(rom_path, std::ios::binary);
    if (!f) throw std::runtime_error("ROM nicht lesbar: " + rom_path);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() >= 4 && data[0] == 'N' && data[1] == 'E' && data[2] == 'S' &&
        data[3] == 0x1A) {
        data.erase(data.begin(), data.begin() + 16);
    }
    return rawnes::md5Hex(data);
}

std::optional<long long> RaClient::ra_gameid(const std::string& md5, bool raise_limit) {
    const Json* cached = cache_.gameid_get(md5);
    if (cached) return falsy_or_none(cached);
    try {
        Json d = request_({{"r", "gameid"}, {"m", md5}});
        const Json* gid_raw = d.find("GameID");
        std::optional<long long> gid = falsy_or_none(gid_raw);
        cache_.gameid_set(md5, gid ? Json::integer(*gid) : Json::null());
        cache_.save(false);
        return gid;
    } catch (const RateLimited&) {
        if (raise_limit) throw;
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<GameData> RaClient::build_game(long long gid, const std::string& user,
                                             const std::string& token) {
    std::string key = std::to_string(gid);
    Json patch;
    const Json* cached = cache_.patch_get(key);
    if (cached) {
        patch = *cached;
    } else {
        try {
            Json d = request_({{"r", "patch"}, {"g", key}, {"u", user}, {"t", token}});
            const Json* pd = d.find("PatchData");
            if (!pd || pd->is_null()) return std::nullopt;
            patch = *pd;
            bool truthy = !(patch.type() == Json::Type::Object && patch.entries().empty());
            if (truthy) {
                cache_.patch_set(key, patch);
                cache_.save(false);
            }
        } catch (...) {
            return std::nullopt;
        }
    }

    GameData game;
    game.gameid = gid;
    const Json* title = patch.find("Title");
    game.name = (title && title->type() == Json::Type::String) ? title->as_string()
                                                                : ("#" + key);

    if (to_lower_str(game.name).find("unsupported game version") != std::string::npos) {
        game.no_set = true;
        return game;
    }

    const Json* achs = patch.find("Achievements");
    if (achs && achs->type() == Json::Type::Array) {
        for (const auto& ac : achs->items()) {
            const Json* flags = ac.find("Flags");
            const Json* id = ac.find("ID");
            int flagsVal = (flags && flags->type() == Json::Type::Int) ? int(flags->as_int()) : 0;
            int64_t idVal = (id && id->type() == Json::Type::Int) ? id->as_int() : 0;
            if (flagsVal != 3 || idVal >= 101000000) continue;

            Achievement a;
            a.id = idVal;
            auto getStr = [&](const char* key) -> std::string {
                const Json* v = ac.find(key);
                return (v && v->type() == Json::Type::String) ? v->as_string() : "";
            };
            a.title = getStr("Title");
            a.desc = getStr("Description");
            a.mem = getStr("MemAddr");
            a.badge = getStr("BadgeName");
            const Json* pts = ac.find("Points");
            a.points = (pts && pts->type() == Json::Type::Int) ? int(pts->as_int()) : 0;
            game.achievements.push_back(std::move(a));
        }
    }

    // NEU ggue. ra_client_nes.py -- das liest "Leaderboards" und
    // "RichPresencePatch" NIE aus der Patch-Antwort (verifiziert: beide
    // Schluessel kommen im Python-Original nirgends vor). Feldnamen hier
    // sind auf oeffentlich dokumentierte RA-API-Konventionen gestuetzt
    // ("Leaderboards": Array mit ID/Title/Description/Format/Mem,
    // "RichPresencePatch": rohes Script als String in der Connect-API),
    // aber NICHT gegen eine echte Antwort dieses Projekts verifiziert --
    // beim ersten echten Leaderboard-Test gegenpruefen.
    const Json* lbs = patch.find("Leaderboards");
    if (lbs && lbs->type() == Json::Type::Array) {
        for (const auto& lb : lbs->items()) {
            auto getStr = [&](const char* key) -> std::string {
                const Json* v = lb.find(key);
                return (v && v->type() == Json::Type::String) ? v->as_string() : "";
            };
            const Json* id = lb.find("ID");
            if (!id || id->type() != Json::Type::Int) continue;
            Leaderboard l;
            l.id = id->as_int();
            l.title = getStr("Title");
            l.desc = getStr("Description");
            l.format = getStr("Format");
            if (l.format.empty()) l.format = "SCORE";
            auto parts = splitLeaderboardMem(getStr("Mem"));
            l.startMem = parts.sta;
            l.cancelMem = parts.can;
            l.submitMem = parts.sub;
            l.valueMem = parts.val;
            game.leaderboards.push_back(std::move(l));
        }
    }
    const Json* rp = patch.find("RichPresencePatch");
    if (rp && rp->type() == Json::Type::String) {
        game.richPresenceScript = rp->as_string();
    }
    game.no_set = false;
    return game;
}

std::pair<std::optional<GameData>, std::string> RaClient::identify_and_load(
    const std::string& rom_path, const std::string& user, const std::string& token) {
    std::string md5;
    try {
        md5 = nes_hash(rom_path);
    } catch (const std::exception& e) {
        return {std::nullopt, std::string("ROM konnte nicht gelesen werden: ") + e.what()};
    }
    auto gid = ra_gameid(md5);
    if (!gid) {
        return {std::nullopt, "Kein RA-Eintrag fuer diese ROM gefunden (Hash: " + md5 + ")."};
    }
    auto game = build_game(*gid, user, token);
    if (!game) {
        return {std::nullopt, "RA-Patch fuer Spiel #" + std::to_string(*gid) +
                             " konnte nicht geladen werden."};
    }
    game->md5 = md5;
    if (game->no_set) {
        return {game, "Spiel erkannt, aber kein Achievement-Set vorhanden."};
    }
    return {game, ""};
}

std::pair<std::set<long long>, std::string> RaClient::ra_unlocks(
    long long gid, const std::string& user, const std::string& token,
    const std::set<long long>& gueltige_ids, bool hardcore) {
    std::set<long long> ids;
    try {
        std::map<std::string, std::string> params = {
            {"r", "startsession"}, {"g", std::to_string(gid)}, {"u", user}, {"t", token}};
        if (hardcore) params["h"] = "1";
        Json d = request_(params);
        for (const auto& kv : d.entries()) {
            if (to_lower_str(kv.first).find("unlock") == std::string::npos) continue;
            if (kv.second.type() != Json::Type::Array) continue;
            for (const auto& e : kv.second.items()) {
                if (e.type() == Json::Type::Object) {
                    const Json* id = e.find("ID");
                    if (!id) id = e.find("AchievementID");
                    if (!id) id = e.find("id");
                    if (id && id->type() == Json::Type::Int) ids.insert(id->as_int());
                } else if (e.type() == Json::Type::Int) {
                    ids.insert(e.as_int());
                }
            }
        }
        if (!gueltige_ids.empty()) {
            std::set<long long> filtered;
            for (auto id : ids) if (gueltige_ids.count(id)) filtered.insert(id);
            ids = filtered;
        }
        return {ids, "OK"};
    } catch (const std::exception& e) {
        return {{}, std::string("Fehler: ") + e.what()};
    }
}

std::pair<bool, std::string> RaClient::ra_ping(long long gid, const std::string& user,
                                               const std::string& token,
                                               const std::string& rich_presence) {
    try {
        std::map<std::string, std::string> params = {
            {"r", "ping"}, {"g", std::to_string(gid)}, {"u", user}, {"t", token}};
        if (!rich_presence.empty()) {
            params["m"] = rich_presence.substr(0, std::min<size_t>(120, rich_presence.size()));
        }
        Json d = request_(params);
        const Json* success = d.find("Success");
        if (success && success->type() == Json::Type::Bool && success->as_bool()) return {true, "OK"};
        return {false, "RA lehnte Ping ab: " + d.dump()};
    } catch (const std::exception& e) {
        return {false, std::string("Netzwerk-/API-Fehler: ") + e.what()};
    }
}

std::pair<bool, std::string> RaClient::ra_award(long long achid, const std::string& user,
                                                const std::string& token, bool hardcore) {
    try {
        Json d = request_({{"r", "awardachievement"}, {"a", std::to_string(achid)},
                           {"h", hardcore ? "1" : "0"}, {"u", user}, {"t", token}});
        const Json* success = d.find("Success");
        if (success && success->type() == Json::Type::Bool && success->as_bool()) return {true, "OK"};
        return {false, "RA lehnte ab: " + d.dump()};
    } catch (const std::exception& e) {
        return {false, std::string("Netzwerk-/API-Fehler: ") + e.what()};
    }
}

std::pair<bool, std::string> RaClient::ra_submit_lb(long long lb_id, const std::string& user,
                                                    const std::string& token, uint32_t value) {
    // Hash-Formel bestaetigt aus echtem RetroArch-Quellcode (siehe
    // Header-Kommentar). Parameternamen unverifiziert.
    try {
        std::string sig = std::to_string(lb_id) + user + std::to_string(lb_id);
        std::string v = rawnes::md5Hex(sig);
        Json d = request_({{"r", "submitlbentry"}, {"i", std::to_string(lb_id)},
                           {"s", std::to_string(value)}, {"v", v},
                           {"u", user}, {"t", token}});
        const Json* success = d.find("Success");
        if (success && success->type() == Json::Type::Bool && success->as_bool()) return {true, "OK"};
        return {false, "RA lehnte ab: " + d.dump()};
    } catch (const std::exception& e) {
        return {false, std::string("Netzwerk-/API-Fehler: ") + e.what()};
    }
}
