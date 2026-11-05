#include "einstellungen.h"

#include "json.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string Text(const Json& o, const char* schluessel, const std::string& standard = std::string()) {
    const Json* v = o.find(schluessel);
    return (v && v->type() == Json::Type::String) ? v->as_string() : standard;
}

}  // namespace

void Einstellungen::Laden(const std::string& pfad) {
    std::ifstream f(std::filesystem::u8path(pfad), std::ios::binary);
    if (!f) return;
    dateiDa = true;
    std::stringstream ss;
    ss << f.rdbuf();
    Json obj;
    try {
        obj = Json::parse(ss.str());
    } catch (...) {
        return;
    }
    if (obj.type() != Json::Type::Object) return;

    raUser = Text(obj, "ra_user");
    raToken = Text(obj, "ra_token");
    if (const Json* v = obj.find("setup_done"); v && v->type() == Json::Type::Bool) einrichtungFertig = v->as_bool();
    raPassAlt = Text(obj, "ra_pass");
    if (const Json* v = obj.find("message_read"); v && v->type() == Json::Type::Int) messageRead = v->as_int();
    const Json* merken = obj.find("remember_login");
    rememberLogin = (merken && merken->type() == Json::Type::Bool) ? merken->as_bool() : true;
    language = Text(obj, "language", "de");
    sdRoot = Text(obj, "sd_root");
    coresDir = Text(obj, "cores_dir");

    remembered.clear();
    if (const Json* rem = obj.find("remembered")) {
        if (rem->type() == Json::Type::Object) {
            for (const auto& kv : rem->entries())
                if (kv.second.type() == Json::Type::String) remembered[kv.first] = kv.second.as_string();
        }
    }
}

void Einstellungen::Speichern(const std::string& pfad) const {
    Json obj = Json::object();
    // Ohne "Eingeloggt bleiben" werden Benutzername und Sitzungsschluessel
    // NICHT abgelegt; der Haken selbst wird immer gespeichert. Ein Passwort
    // wird nie geschrieben ("ra_pass" aus aelteren Fassungen faellt damit weg).
    obj.set("remember_login", Json::boolean(rememberLogin));
    obj.set("ra_user", Json::string(rememberLogin ? raUser : std::string()));
    obj.set("ra_token", Json::string(rememberLogin ? raToken : std::string()));
    obj.set("message_read", Json::integer(messageRead));
    obj.set("setup_done", Json::boolean(einrichtungFertig));
    obj.set("language", Json::string(language));
    if (!sdRoot.empty()) obj.set("sd_root", Json::string(sdRoot));
    if (!coresDir.empty()) obj.set("cores_dir", Json::string(coresDir));
    Json rem = Json::object();
    for (const auto& kv : remembered) rem.set(kv.first, Json::string(kv.second));
    obj.set("remembered", rem);

    std::ofstream f(std::filesystem::u8path(pfad), std::ios::binary | std::ios::trunc);
    if (f) f << obj.dump();
}
