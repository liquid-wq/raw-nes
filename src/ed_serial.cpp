#include "ed_serial.h"

#include "ed_protocol.h"

#include <windows.h>
#include <setupapi.h>
#include <devguid.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {

HANDLE H(void* h) { return static_cast<HANDLE>(h); }

std::string NachUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// "USB\VID_38DF&PID_0017\..." -> vid/pid
bool LiesVidPid(const std::string& hardwareId, uint16_t* vid, uint16_t* pid) {
    std::string s = hardwareId;
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    const size_t v = s.find("vid_");
    const size_t p = s.find("pid_");
    if (v == std::string::npos || p == std::string::npos) return false;
    *vid = (uint16_t)std::strtoul(s.substr(v + 4, 4).c_str(), nullptr, 16);
    *pid = (uint16_t)std::strtoul(s.substr(p + 4, 4).c_str(), nullptr, 16);
    return true;
}

std::string ToHex(const Bytes& b) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (uint8_t c : b) { s.push_back(hex[c >> 4]); s.push_back(hex[c & 0xF]); }
    return s;
}

}  // namespace

EdSerial::EdSerial(std::string port) : portname_(std::move(port)) {}

EdSerial::~EdSerial() { close(); }

// Entspricht readUpTo() der Qt-Fassung (waitForReadyRead + readAll): kehrt
// zurueck, sobald maxLen Bytes da sind oder die Zeit um ist. Gewartet wird
// im Treiber (ReadFile mit Zeitgrenze), nicht mit Sleep -- Sleep hat unter
// Windows eine Aufloesung von rund 15 ms, und memrd laeuft im Poll-Takt
// byteweise ueber alle beobachteten Adressen.
Bytes EdSerial::readUpTo(int maxLen, int timeoutMs) {
    Bytes out;
    if (!handle_) return out;
    const ULONGLONG start = ::GetTickCount64();
    while ((int)out.size() < maxLen) {
        const long long remaining = (long long)timeoutMs - (long long)(::GetTickCount64() - start);
        if (remaining <= 0) break;

        // Liegen Bytes im Puffer, kehrt ReadFile sofort mit ihnen zurueck;
        // sonst wartet es auf das erste Byte, hoechstens `remaining` ms.
        COMMTIMEOUTS t = {};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = (DWORD)remaining;
        t.WriteTotalTimeoutConstant = 500;
        ::SetCommTimeouts(H(handle_), &t);

        uint8_t buf[4096];
        const DWORD want = (DWORD)std::min<int>(maxLen - (int)out.size(), (int)sizeof(buf));
        DWORD got = 0;
        if (!::ReadFile(H(handle_), buf, want, &got, nullptr)) break;
        if (got == 0) break;  // Zeit um
        out.insert(out.end(), buf, buf + got);
    }
    return out;
}

bool EdSerial::write(const Bytes& data) {
    if (!handle_) return false;
    size_t raus = 0;
    while (raus < data.size()) {
        DWORD n = 0;
        if (!::WriteFile(H(handle_), data.data() + raus, (DWORD)(data.size() - raus), &n, nullptr))
            return false;
        if (n == 0) return false;
        raus += n;
    }
    return true;
}

void EdSerial::clearInput() {
    if (handle_) ::PurgeComm(H(handle_), PURGE_RXCLEAR);
}

bool EdSerial::open() {
    close();

    // Ab COM10 ist die \\.\-Schreibweise Pflicht.
    const std::string pfad = "\\\\.\\" + portname_;
    HANDLE h = ::CreateFileA(pfad.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        char t[64];
        std::snprintf(t, sizeof(t), "Fehler %lu", ::GetLastError());
        throw std::runtime_error("Port " + portname_ + " konnte nicht geoeffnet werden: " + t);
    }

    // 921600 Baud, 8N1, keine Flusssteuerung -- wie in der Qt-Fassung.
    // DTR und RTS gesetzt, wie pyserial es beim Oeffnen tut (Python-Original).
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    ::GetCommState(h, &dcb);
    dcb.BaudRate = 921600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!::SetCommState(h, &dcb)) {
        ::CloseHandle(h);
        throw std::runtime_error("Port " + portname_ + " konnte nicht geoeffnet werden: "
                                 "Baudrate/Rahmenformat nicht setzbar");
    }
    ::SetupComm(h, 1 << 16, 1 << 12);
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handle_ = h;

    // Wie edlink: 66 Null-Bytes senden, danach Reststrom abwarten (0.3s
    // Funkstille) und leeren -- entspricht ed_serial_nes.py open().
    write(Bytes(66, 0));

    // Eingangspuffer aktiv leersaugen bis 0.3s Funkstille. Deckel 3000 ms,
    // Begruendung in ed_serial_qt.cpp: der Deckel greift nur, wenn die
    // Konsole durchgehend sendet; zu frueh abgebrochen liefert memrd danach
    // Leerlaufwerte (0xFF).
    double quietMs = 0.0;
    const ULONGLONG t0 = ::GetTickCount64();
    while (quietMs < 300.0 && ::GetTickCount64() - t0 < 3000) {
        Bytes chunk = readUpTo(4096, 50);
        if (!chunk.empty()) quietMs = 0.0;
        else quietMs += 50.0;
    }
    clearInput();

    // Status-Handshake: CMD_STATUS senden, Antwort muss 0x5A oder 0xA5
    // enthalten. Bis zu 3 Versuche auf DEMSELBEN offenen Port.
    const auto cmdVec = ed_cmd(CMD_STATUS);
    Bytes resp;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        clearInput();
        write(Bytes(cmdVec.begin(), cmdVec.end()));
        ::Sleep(50);
        resp = readUpTo(4, 300);
        if (std::find(resp.begin(), resp.end(), (uint8_t)0x5A) != resp.end() ||
            std::find(resp.begin(), resp.end(), (uint8_t)0xA5) != resp.end()) {
            return true;
        }
        if (attempt < 3) ::Sleep(100);
    }

    const std::string hex = resp.empty() ? "leer" : ToHex(resp);
    close();
    throw std::runtime_error("EverDrive antwortet nicht auf Status-Anfrage (" + portname_ +
                             ", Antwort: " + hex + ")");
}

void EdSerial::close() {
    if (handle_) {
        ::CloseHandle(H(handle_));
        handle_ = nullptr;
    }
}

// Diagnosemodus: Schritte des Wiederverbindens mitschreiben (leer = aus).
std::function<void(const std::string&)> g_serialProtokoll;

bool EdSerial::recover() {
    auto P = [](const std::string& s) { if (g_serialProtokoll) g_serialProtokoll(s); };
    P("recover: " + portname_ + " schliessen");
    close();
    ::Sleep(800);
    try {
        P("recover: " + portname_ + " öffnen");
        open();
        P("recover: " + portname_ + " wieder offen");
        return true;
    } catch (const std::exception& e) {
        P(std::string("recover: ") + portname_ + " -- " + e.what());
    } catch (...) {}

    // Nur Anschluesse des EverDrive N8 PRO (VID 38DF / PID 0017). Frueher
    // wurden hier ALLE Anschluesse probiert, auch Bluetooth -- deren Oeffnen
    // kann ewig dauern; Folge (Test 04.11.2026): nach dem Ausschalten der
    // Konsole hing die Abfrage, und das Programm liess sich nicht beenden.
    for (const auto& info : availableSerialPorts()) {
        if (!(info.hatKennung && info.vid == 0x38DF && info.pid == 0x0017)) continue;
        if (info.name == portname_) continue;   // schon oben versucht
        try {
            portname_ = info.name;
            P("recover: " + portname_ + " öffnen (anderer Anschluss)");
            open();
            P("recover: " + portname_ + " offen");
            return true;
        } catch (...) {
            close();
        }
    }
    P("recover: kein EverDrive erreichbar");
    return false;
}

Bytes EdSerial::memrd(uint32_t addr, uint32_t length) {
    try {
        return memrdRaw(addr, length);
    } catch (const std::exception& e) {
        if (g_serialProtokoll) g_serialProtokoll(std::string("memrd fehlgeschlagen: ") + e.what());
        if (recover()) return memrdRaw(addr, length);
        throw;
    }
}

Bytes EdSerial::memrdNoRecover(uint32_t addr, uint32_t length) {
    return memrdRaw(addr, length);
}

void EdSerial::memwr(uint32_t addr, const Bytes& data) {
    try {
        memwrRaw(addr, data);
    } catch (...) {
        if (recover()) { memwrRaw(addr, data); return; }
        throw;
    }
}

// Dateizugriff ueber die MCU des EverDrive. Rahmen wie bei allen Befehlen
// (2B D4 <Befehl> <Befehl xor FF>), Ablauf aus Krikzz' edio/everdrive.c:
//   C9 Modus(1) Laenge(2) Pfad  -> Status (10): A5 xx, xx = 0 ok
//   CA Laenge(4)                -> 1 Byte Ergebnis (0 ok), dann die Daten
//   CE                          -> Status
// Werte little-endian (wie memrd beim N8 PRO).
Bytes EdSerial::fileReadNoRecover(const std::string& path, uint32_t length) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_ || path.empty() || path.size() > 255 || length == 0 || length > 512) return {};
    auto befehl = [](uint8_t c) { return Bytes{0x2B, 0xD4, c, (uint8_t)(c ^ 0xFF)}; };
    auto status = [&]() -> int {
        write(befehl(0x10));
        Bytes r = readUpTo(2, 1000);
        if (r.size() != 2) return -1;
        if (r[1] == 0xA5) return r[0];
        if (r[0] == 0xA5) return r[1];
        return -2;
    };
    clearInput();
    Bytes oeffnen = befehl(0xC9);
    oeffnen.push_back(0x01);   // FA_READ
    oeffnen.push_back((uint8_t)(path.size() & 0xFF));
    oeffnen.push_back((uint8_t)(path.size() >> 8));
    oeffnen.insert(oeffnen.end(), path.begin(), path.end());
    write(oeffnen);
    if (status() != 0) return {};
    Bytes lesen = befehl(0xCA);
    for (int i = 0; i < 4; ++i) lesen.push_back((uint8_t)(length >> (8 * i)));
    write(lesen);
    Bytes antwort = readUpTo(1, 1000);
    Bytes daten;
    if (antwort.size() == 1 && antwort[0] == 0) daten = readUpTo((int)length, 2000);
    write(befehl(0xCE));
    status();
    if (daten.size() != length) return {};
    return daten;
}

// CMD_F_FINFO (D0): Laenge(2) Pfad -> 1 Byte Ergebnis (0 ok), dann 9 Byte
// (Groesse u32, Datum u16, Zeit u16, Attribut u8), dann Name als Laenge(2)+Text.
bool EdSerial::neuOeffnen() {
    close();
    ::Sleep(300);
    try {
        return open();
    } catch (...) {
        close();
        return false;
    }
}

Bytes EdSerial::sysInfoNoRecover() {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_) return {};
    clearInput();
    write(Bytes{0x2B, 0xD4, 0x26, (uint8_t)(0x26 ^ 0xFF)});
    Bytes r = readUpTo(64, 1000);
    if (r.size() != 64) return {};
    return r;
}

long long EdSerial::fileSizeNoRecover(const std::string& path) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_ || path.empty() || path.size() > 255) return -1;
    clearInput();
    Bytes info{0x2B, 0xD4, 0xD0, (uint8_t)(0xD0 ^ 0xFF)};
    info.push_back((uint8_t)(path.size() & 0xFF));
    info.push_back((uint8_t)(path.size() >> 8));
    info.insert(info.end(), path.begin(), path.end());
    write(info);
    Bytes r = readUpTo(1, 1000);
    if (r.size() != 1 || r[0] != 0) return -1;
    Bytes kopf = readUpTo(9, 1000);
    if (kopf.size() != 9) return -1;
    Bytes nl = readUpTo(2, 1000);
    if (nl.size() == 2) readUpTo(nl[0] | (nl[1] << 8), 1000);   // Name verwerfen
    if (kopf[8] & 0x10) return -1;                                // Verzeichnis
    return (long long)kopf[0] | ((long long)kopf[1] << 8) | ((long long)kopf[2] << 16) | ((long long)kopf[3] << 24);
}

Bytes EdSerial::fileReadAllNoRecover(const std::string& path, uint32_t size) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_ || path.empty() || path.size() > 255 || size == 0) return {};
    auto befehl = [](uint8_t c) { return Bytes{0x2B, 0xD4, c, (uint8_t)(c ^ 0xFF)}; };
    auto status = [&]() -> int {
        write(befehl(0x10));
        Bytes r = readUpTo(2, 1000);
        if (r.size() != 2) return -1;
        if (r[1] == 0xA5) return r[0];
        if (r[0] == 0xA5) return r[1];
        return -2;
    };
    clearInput();
    Bytes oeffnen = befehl(0xC9);
    oeffnen.push_back(0x01);   // FA_READ
    oeffnen.push_back((uint8_t)(path.size() & 0xFF));
    oeffnen.push_back((uint8_t)(path.size() >> 8));
    oeffnen.insert(oeffnen.end(), path.begin(), path.end());
    write(oeffnen);
    if (status() != 0) return {};
    Bytes daten;
    daten.reserve(size);
    bool ok = true;
    while (daten.size() < size) {
        const uint32_t block = std::min<uint32_t>(512, size - (uint32_t)daten.size());
        Bytes lesen = befehl(0xCA);
        for (int i = 0; i < 4; ++i) lesen.push_back((uint8_t)(block >> (8 * i)));
        write(lesen);
        Bytes antwort = readUpTo(1, 1000);
        if (antwort.size() != 1 || antwort[0] != 0) { ok = false; break; }
        Bytes teil = readUpTo((int)block, 2000);
        if (teil.size() != block) { ok = false; break; }
        daten.insert(daten.end(), teil.begin(), teil.end());
    }
    write(befehl(0xCE));
    status();
    if (!ok) return {};
    return daten;
}

Bytes EdSerial::memrdRaw(uint32_t addr, uint32_t length) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_) throw std::runtime_error("memrd: nicht verbunden");
    clearInput();
    const auto pkt = ed_build_memrd_packet(addr, length);
    write(Bytes(pkt.begin(), pkt.end()));
    Bytes data = readUpTo((int)length, 2000);
    if ((uint32_t)data.size() != length) {
        throw std::runtime_error("memrd: " + std::to_string(data.size()) + "/" +
                                 std::to_string(length) + " Byte erhalten");
    }
    return data;
}

void EdSerial::memwrRaw(uint32_t addr, const Bytes& data) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_) throw std::runtime_error("memwr: nicht verbunden");
    const auto pkt = ed_build_memwr_packet(addr, data);
    write(Bytes(pkt.begin(), pkt.end()));
}

std::vector<SerialPortInfo> availableSerialPorts() {
    std::vector<SerialPortInfo> aus;
    HDEVINFO info = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE) return aus;

    SP_DEVINFO_DATA daten = {};
    daten.cbSize = sizeof(daten);
    for (DWORD i = 0; ::SetupDiEnumDeviceInfo(info, i, &daten); ++i) {
        HKEY key = ::SetupDiOpenDevRegKey(info, &daten, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t name[64] = {};
        DWORD typ = 0, groesse = sizeof(name);
        const LONG r = ::RegQueryValueExW(key, L"PortName", nullptr, &typ, (LPBYTE)name, &groesse);
        ::RegCloseKey(key);
        if (r != ERROR_SUCCESS || name[0] == 0) continue;

        SerialPortInfo p;
        p.name = NachUtf8(name);
        wchar_t puffer[512] = {};
        if (::SetupDiGetDeviceRegistryPropertyW(info, &daten, SPDRP_HARDWAREID, nullptr,
                                                (PBYTE)puffer, sizeof(puffer), nullptr))
            p.hatKennung = LiesVidPid(NachUtf8(puffer), &p.vid, &p.pid);
        aus.push_back(std::move(p));
    }
    ::SetupDiDestroyDeviceInfoList(info);
    std::sort(aus.begin(), aus.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) { return a.name < b.name; });
    return aus;
}

std::pair<std::unique_ptr<EdSerial>, std::string> find_everdrive(const std::string& preferred) {
    // EverDrive N8 PRO meldet sich als VID 0x38DF / PID 0x0017. Fremde Ports
    // (Bluetooth, USB-Seriell-Adapter, ...) werden NUR angetestet, wenn es
    // gar keinen Treffer mit dieser Kennung gibt -- jeder Fehlversuch kostet
    // einen vollen open()-Zyklus.
    constexpr uint16_t kEverdriveVid = 0x38DF;
    constexpr uint16_t kEverdrivePid = 0x0017;

    std::vector<std::string> preferredMatch;
    std::vector<std::string> rest;
    if (!preferred.empty()) preferredMatch.push_back(preferred);

    auto enthalten = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    for (const auto& info : availableSerialPorts()) {
        if (enthalten(preferredMatch, info.name) || enthalten(rest, info.name)) continue;
        const bool idMatch = info.hatKennung && info.vid == kEverdriveVid && info.pid == kEverdrivePid;
        if (idMatch) preferredMatch.push_back(info.name);
        else rest.push_back(info.name);
    }

    // Bei Erfolg steht im zweiten Wert der Anschluss, sonst je versuchtem
    // Anschluss der Grund (fuers Log).
    const std::vector<std::string>& candidates = preferredMatch.empty() ? rest : preferredMatch;
    std::string gruende;
    for (const auto& port : candidates) {
        auto ed = std::make_unique<EdSerial>(port);
        try {
            ed->open();
            return {std::move(ed), port};
        } catch (const std::exception& e) {
            std::string grund = e.what();
            // Windows-Fehler 5: Zugriff verweigert -- ein anderes Programm haelt den Anschluss.
            if (grund.size() >= 8 && grund.compare(grund.size() - 8, 8, "Fehler 5") == 0)
                grund += " -- belegt, ist noch ein anderes Programm (RAW-NES, MULTI-RAW, ...) offen?";
            gruende += "\n  " + port + ": " + grund;
            ed->close();
        } catch (...) {
            gruende += "\n  " + port + ": unbekannter Fehler";
            ed->close();
        }
    }
    if (candidates.empty()) gruende = "\n  kein serieller Anschluss gefunden -- USB-Kabel?";
    return {nullptr, gruende};
}
