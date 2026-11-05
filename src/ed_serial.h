// Serieller Anschluss zum EverDrive N8 PRO.
//
// Ablauf, Zeiten und Fehlertexte 1:1 aus der Qt-Fassung (ed_serial_qt.h/.cpp);
// dort ist auch begruendet, warum die Funkstille-Schleife bei 3000 ms gedeckelt
// ist, warum der Handshake auf demselben Port wiederholt wird und wozu es
// memrdNoRecover() gibt. Ersetzt ist nur der Unterbau: Win32-API statt
// QSerialPort, std::vector statt QByteArray.
//
// Blockiert den aufrufenden Thread -- nur aus dem Worker-Thread aufrufen.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using Bytes = std::vector<uint8_t>;

class EdSerial {
public:
    explicit EdSerial(std::string port = "COM10");
    ~EdSerial();

    // Wirft std::runtime_error bei Fehlschlag.
    bool open();
    void close();
    bool isOpen() const { return handle_ != nullptr; }

    Bytes memrd(uint32_t addr, uint32_t length);
    void memwr(uint32_t addr, const Bytes& data);

    // Wie memrd(), aber OHNE recover()-Kaskade. Fuer Leseversuche, deren
    // Fehlschlag ein erwarteter Normalfall ist (FPGA-Build-Check, waehrend
    // die Konsole im EverDrive-Menue steht).
    Bytes memrdNoRecover(uint32_t addr, uint32_t length);

    // Liest bis zu `length` Byte einer Datei von der SD-Karte des EverDrive
    // (oeffnen, lesen, schliessen; Befehle wie in Krikzz' edio/everdrive.c).
    // Ohne recover(): bei jedem Fehler wird ein leeres Ergebnis geliefert.
    Bytes fileReadNoRecover(const std::string& path, uint32_t length);

    // Groesse einer Datei auf der SD-Karte (CMD_F_FINFO), -1 bei Fehler.
    long long fileSizeNoRecover(const std::string& path);
    // CMD_SYS_INF (26): 64 Byte SysInfoIO aus Krikzz' everdrive.h. Leer bei Fehler.
    Bytes sysInfoNoRecover();
    // Anschluss schliessen und neu oeffnen (nach dem Ausschalten der Konsole ist
    // die alte Verbindung tot). Wirft nicht; false = kein EverDrive erreichbar.
    bool neuOeffnen();
    // Ganze Datei lesen (in 512-Byte-Bloecken wie in Krikzz' Beispiel).
    // Leer bei jedem Fehler.
    Bytes fileReadAllNoRecover(const std::string& path, uint32_t size);

    const std::string& portName() const { return portname_; }

private:
    Bytes readUpTo(int maxLen, int timeoutMs);
    bool write(const Bytes& data);
    void clearInput();
    bool recover();
    Bytes memrdRaw(uint32_t addr, uint32_t length);
    void memwrRaw(uint32_t addr, const Bytes& data);

    std::string portname_;
    void* handle_ = nullptr;
    std::mutex lock_;
};

// Alle COM-Anschluesse des Systems, mit USB-Kennung, falls vorhanden.
struct SerialPortInfo {
    std::string name;
    uint16_t vid = 0, pid = 0;
    bool hatKennung = false;
};
std::vector<SerialPortInfo> availableSerialPorts();

// Probiert `preferred` (falls gesetzt), dann die Anschluesse mit der
// EverDrive-Kennung, nur ohne solchen Treffer alle uebrigen.
// {nullptr, ""}, wenn keiner funktioniert hat.
std::pair<std::unique_ptr<EdSerial>, std::string> find_everdrive(const std::string& preferred = std::string());

// Diagnosemodus: bekommt jeden Schritt des Wiederverbindens als Text (leer = aus).
extern std::function<void(const std::string&)> g_serialProtokoll;
