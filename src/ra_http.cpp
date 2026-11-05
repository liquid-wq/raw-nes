#include "ra_http.h"

#include <windows.h>
#include <winhttp.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

std::wstring Breit(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Wie quote_plus() in der Qt-Fassung (und Pythons urllib.parse.quote_plus).
std::string quote_plus(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') {
            out.push_back((char)c);
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

struct Griff {
    HINTERNET h = nullptr;
    Griff() = default;
    explicit Griff(HINTERNET x) : h(x) {}
    Griff(const Griff&) = delete;
    Griff& operator=(const Griff&) = delete;
    ~Griff() { if (h) ::WinHttpCloseHandle(h); }
};

struct Antwort {
    int status = 0;
    int retryAfter = 0;
    std::string body;
    std::string fehler;  // leer = Verbindung ok (Status kann trotzdem != 200 sein)
};

Antwort Get(HINTERNET sitzung, const std::string& host, const std::string& path, int timeoutMs) {
    Antwort a;
    ::WinHttpSetTimeouts(sitzung, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    Griff verbindung(::WinHttpConnect(sitzung, Breit(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!verbindung.h) { a.fehler = "Verbindung zu " + host + " fehlgeschlagen"; return a; }

    Griff anfrage(::WinHttpOpenRequest(verbindung.h, L"GET", Breit(path).c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE));
    if (!anfrage.h) { a.fehler = "Anfrage konnte nicht angelegt werden"; return a; }

    if (!::WinHttpSendRequest(anfrage.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !::WinHttpReceiveResponse(anfrage.h, nullptr)) {
        const DWORD e = ::GetLastError();
        if (e == ERROR_WINHTTP_TIMEOUT) {
            a.fehler = "Zeitueberschreitung";
        } else {
            char t[96];
            std::snprintf(t, sizeof(t), "Netzwerkfehler %lu", e);
            a.fehler = t;
        }
        return a;
    }

    DWORD status = 0, groesse = sizeof(status);
    ::WinHttpQueryHeaders(anfrage.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &groesse, WINHTTP_NO_HEADER_INDEX);
    a.status = (int)status;
    if (status == 429) {
        wchar_t wert[32] = {};
        DWORD wg = sizeof(wert);
        if (::WinHttpQueryHeaders(anfrage.h, WINHTTP_QUERY_CUSTOM, L"Retry-After", wert, &wg,
                                  WINHTTP_NO_HEADER_INDEX))
            a.retryAfter = ::_wtoi(wert);
    }
    for (;;) {
        DWORD verfuegbar = 0;
        if (!::WinHttpQueryDataAvailable(anfrage.h, &verfuegbar) || verfuegbar == 0) break;
        std::vector<char> puffer(verfuegbar);
        DWORD gelesen = 0;
        if (!::WinHttpReadData(anfrage.h, puffer.data(), verfuegbar, &gelesen)) break;
        a.body.append(puffer.data(), gelesen);
    }
    return a;
}

// Die Qt-Fassung lief ueber QJsonDocument: dort ist jede Zahl ein double, und
// qjson_to_json() machte ganzzahlige Werte wieder zu Int. Der eigene Parser
// unterscheidet nach Schreibweise ("5" vs "5.0"). Damit ra_client.cpp genau
// dieselben Typen sieht wie vorher, hier dieselbe Angleichung.
Json Angleichen(const Json& v) {
    switch (v.type()) {
        case Json::Type::Double: {
            const double d = v.as_double();
            if (std::isfinite(d) && d == (double)(int64_t)d) return Json::integer((int64_t)d);
            return v;
        }
        case Json::Type::Array: {
            Json a = Json::array();
            for (const auto& x : v.items()) a.push_back(Angleichen(x));
            return a;
        }
        case Json::Type::Object: {
            Json o = Json::object();
            for (const auto& kv : v.entries()) o.set(kv.first, Angleichen(kv.second));
            return o;
        }
        default:
            return v;
    }
}

// Eine Sitzung je Thread, wie der thread_local QNetworkAccessManager der
// Qt-Fassung: haelt die Verbindung offen statt sie je Anfrage neu aufzubauen.
HINTERNET Sitzung(const char* agent) {
    static thread_local Griff sitzung;
    if (!sitzung.h) {
        sitzung.h = ::WinHttpOpen(Breit(agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    return sitzung.h;
}

}  // namespace

std::string ra_urlencode(const std::map<std::string, std::string>& params) {
    std::string out;
    for (const auto& kv : params) {
        if (!out.empty()) out += '&';
        out += quote_plus(kv.first) + "=" + quote_plus(kv.second);
    }
    return out;
}

RequestFn make_winhttp_request_fn() {
    return [](const std::map<std::string, std::string>& params) -> Json {
        HINTERNET s = Sitzung(RA_USER_AGENT);
        if (!s) throw std::runtime_error("WinHttpOpen fehlgeschlagen");
        const Antwort a = Get(s, RA_HOST, "/dorequest.php?" + ra_urlencode(params), 12000);
        if (!a.fehler.empty()) {
            if (a.fehler == "Zeitueberschreitung")
                throw std::runtime_error("Zeitueberschreitung (12s) bei RA-Anfrage");
            throw std::runtime_error(a.fehler);
        }
        if (a.status == 429) throw RateLimited(a.retryAfter);
        if (a.status < 200 || a.status >= 300) {
            throw std::runtime_error("HTTP " + std::to_string(a.status));
        }
        Json doc;
        try {
            doc = Json::parse(a.body);
        } catch (...) {
            throw std::runtime_error("ungueltige JSON-Antwort von RA");
        }
        if (doc.type() != Json::Type::Object) throw std::runtime_error("ungueltige JSON-Antwort von RA");
        return Angleichen(doc);
    };
}

bool http_get(const std::string& host, const std::string& path, int timeoutMs,
              const char* userAgent, std::string* body) {
    // Eigene Sitzung je Aufruf: laeuft in wechselnden Hilfsthreads.
    Griff sitzung(::WinHttpOpen(Breit(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!sitzung.h) return false;
    Antwort a = Get(sitzung.h, host, path, timeoutMs);
    if (!a.fehler.empty() || a.status < 200 || a.status >= 300) return false;
    if (body) *body = std::move(a.body);
    return true;
}
