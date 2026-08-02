#pragma once
#include <QByteArray>
#include <QMutex>
#include <QSerialPort>
#include <QString>
#include <memory>
#include <utility>

// Struktur/Timing bewusst identisch zur Python-Vorlage (ed_serial_nes.py)
// und zu MEGA-RAWs ed_serial_qt.h gehalten. NUR Weg B (memrd/memwr/open/
// close/recover/find) -- der alte Weg-A-Code aus dem Python-Original
// (FIFO-Streaming, Stub-Injection) wird bewusst nicht mitportiert.
// WICHTIG: blockiert den aufrufenden Thread (synchron) -- in der GUI
// daher aus einem Worker-Thread aufrufen, nie aus dem UI-Thread.
class EdSerial {
public:
    explicit EdSerial(QString port = "COM10");

    // Wirft std::runtime_error bei Fehlschlag.
    bool open();
    void close();
    bool isOpen() const { return ser_ && ser_->isOpen(); }

    QByteArray memrd(uint32_t addr, uint32_t length);
    void memwr(uint32_t addr, const QByteArray& data);

    // Wie memrd(), aber OHNE recover()-Kaskade. Fuer Leseversuche, deren
    // Fehlschlag ein erwarteter Normalfall ist (z.B. FPGA-Build-Check,
    // waehrend die Konsole im EverDrive-Menue steht -> MCU-DMA blockiert
    // den Bus). Dort loeste recover() bisher einen kompletten
    // Port-Durchlauf von ~20s aus, obwohl gar kein Fehler vorlag.
    QByteArray memrdNoRecover(uint32_t addr, uint32_t length);

    const QString& portName() const { return portname_; }

private:
    QByteArray readUpTo(int maxLen, int timeoutMs);
    bool recover();
    QByteArray memrdRaw(uint32_t addr, uint32_t length);
    void memwrRaw(uint32_t addr, const QByteArray& data);

    QString portname_;
    std::unique_ptr<QSerialPort> ser_;
    QMutex lock_;
};

// Probiert `preferred` (falls gesetzt) und alle vorhandenen COM-Ports
// durch, bis eines erfolgreich oeffnet. {nullptr, ""} wenn keiner
// funktioniert hat. Bewusst kein gezielter VID/PID-Filter, wie im
// Python-Original.
std::pair<std::unique_ptr<EdSerial>, QString> find_everdrive(const QString& preferred = QString());
