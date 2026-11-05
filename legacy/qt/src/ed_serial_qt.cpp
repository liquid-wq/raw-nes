#include "ed_serial_qt.h"
#include "ed_protocol.h"
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QSerialPortInfo>
#include <QThread>
#include <stdexcept>

EdSerial::EdSerial(QString port) : portname_(std::move(port)) {}

QByteArray EdSerial::readUpTo(int maxLen, int timeoutMs) {
    QByteArray out;
    QElapsedTimer t; t.start();
    while (out.size() < maxLen) {
        int remaining = timeoutMs - static_cast<int>(t.elapsed());
        if (remaining <= 0) break;
        if (!ser_->waitForReadyRead(remaining)) break;
        out += ser_->readAll();
    }
    return out;
}

bool EdSerial::open() {
    ser_ = std::make_unique<QSerialPort>();
    ser_->setPortName(portname_);
    ser_->setBaudRate(921600);
    // Explizit statt auf Qt-Defaults verlassen (8N1, kein Flow Control).
    ser_->setDataBits(QSerialPort::Data8);
    ser_->setParity(QSerialPort::NoParity);
    ser_->setStopBits(QSerialPort::OneStop);
    ser_->setFlowControl(QSerialPort::NoFlowControl);
    if (!ser_->open(QIODevice::ReadWrite)) {
        throw std::runtime_error(("Port " + portname_ + " konnte nicht geoeffnet werden: "
                                  + ser_->errorString()).toStdString());
    }

    // Wie edlink: 66 Null-Bytes senden, danach Reststrom abwarten (0.3s
    // Funkstille) und leeren -- entspricht ed_serial_nes.py open().
    ser_->write(QByteArray(66, '\0'));
    ser_->waitForBytesWritten(500);
    ser_->flush();

    // Eingangspuffer aktiv leersaugen bis 0.3s Funkstille -- 1:1 aus
    // MEGA-RAW uebernommen (dort zuverlaessig UND schnell). Die vorherige
    // RAW-NES-Variante (400ms hart + 3-fach-Retry aussenrum) summierte
    // sich zu ~10s; dieser eine saubere Durchlauf ist schneller.
    // Deckel bleibt bei 3000ms. Er war zwischenzeitlich auf 1200ms gesenkt,
    // um Wartezeit zu sparen -- das war die falsche Stellschraube: der Deckel
    // greift NUR, wenn die Konsole durchgehend sendet; im Normalfall endet
    // die Schleife ohnehin nach 300ms Stille. Wird zu frueh abgebrochen,
    // ist der Puffer nicht sauber, der Handshake gelingt trotzdem (er hat
    // drei Versuche) und memrd liefert danach Leerlaufwerte (0xFF).
    // Die eigentliche Zeitersparnis kommt vom VID/PID-Filter in
    // find_everdrive(), dem Handshake-Retry und memrdNoRecover().
    double quietMs = 0.0;
    QElapsedTimer t0; t0.start();
    while (quietMs < 300.0 && t0.elapsed() < 3000) {
        QByteArray chunk = readUpTo(4096, 50);
        if (!chunk.isEmpty()) quietMs = 0.0;
        else quietMs += 50.0;
    }
    ser_->clear(QSerialPort::Input);

    // Status-Handshake: CMD_STATUS senden, Antwort muss 0x5A oder 0xA5
    // enthalten (Key-Byte des EverDrive).
    // Bis zu 3 Versuche, aber auf DEMSELBEN bereits offenen Port -- kostet
    // ~450ms pro Fehlversuch. Frueher wurde bei Fehlschlag stattdessen der
    // komplette open()-Zyklus inkl. Funkstille-Loop wiederholt (~3,5s pro
    // Versuch, mal Anzahl COM-Ports) -- das war die gemeldete Wartezeit.
    auto cmdVec = ed_cmd(CMD_STATUS);
    QByteArray resp;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        ser_->clear(QSerialPort::Input);
        ser_->write(QByteArray::fromRawData(
            reinterpret_cast<const char*>(cmdVec.data()), static_cast<int>(cmdVec.size())));
        ser_->waitForBytesWritten(500);
        ser_->flush();
        QThread::msleep(50);
        resp = readUpTo(4, 300);
        if (resp.contains(static_cast<char>(0x5A)) ||
            resp.contains(static_cast<char>(0xA5))) {
            return true;
        }
        if (attempt < 3) QThread::msleep(100);
    }

    QString hex = resp.isEmpty() ? "leer" : QString(resp.toHex());
    close();
    throw std::runtime_error(
        ("EverDrive antwortet nicht auf Status-Anfrage (" + portname_ +
         ", Antwort: " + hex + ")").toStdString());
}

void EdSerial::close() {
    if (ser_) {
        if (ser_->isOpen()) ser_->close();
        ser_.reset();
    }
}

bool EdSerial::recover() {
    close();
    QThread::msleep(800);
    try {
        open();
        return true;
    } catch (...) {}

    for (const auto& info : QSerialPortInfo::availablePorts()) {
        try {
            portname_ = info.portName();
            open();
            return true;
        } catch (...) {
            close();
        }
    }
    return false;
}

QByteArray EdSerial::memrd(uint32_t addr, uint32_t length) {
    try {
        return memrdRaw(addr, length);
    } catch (...) {
        if (recover()) return memrdRaw(addr, length);
        throw;
    }
}

QByteArray EdSerial::memrdNoRecover(uint32_t addr, uint32_t length) {
    return memrdRaw(addr, length);
}

void EdSerial::memwr(uint32_t addr, const QByteArray& data) {
    try {
        memwrRaw(addr, data);
    } catch (...) {
        if (recover()) { memwrRaw(addr, data); return; }
        throw;
    }
}

QByteArray EdSerial::memrdRaw(uint32_t addr, uint32_t length) {
    QMutexLocker lock(&lock_);
    if (!ser_) throw std::runtime_error("memrd: nicht verbunden");
    ser_->clear(QSerialPort::Input);
    auto pkt = ed_build_memrd_packet(addr, length);
    ser_->write(QByteArray::fromRawData(reinterpret_cast<const char*>(pkt.data()),
                                        static_cast<int>(pkt.size())));
    ser_->flush();
    QByteArray data = readUpTo(static_cast<int>(length), 2000);
    if (static_cast<uint32_t>(data.size()) != length) {
        throw std::runtime_error("memrd: " + std::to_string(data.size()) + "/" +
                                 std::to_string(length) + " Byte erhalten");
    }
    return data;
}

void EdSerial::memwrRaw(uint32_t addr, const QByteArray& data) {
    QMutexLocker lock(&lock_);
    if (!ser_) throw std::runtime_error("memwr: nicht verbunden");
    std::vector<uint8_t> dvec(data.begin(), data.end());
    auto pkt = ed_build_memwr_packet(addr, dvec);
    ser_->write(QByteArray::fromRawData(reinterpret_cast<const char*>(pkt.data()),
                                        static_cast<int>(pkt.size())));
    ser_->flush();
}

std::pair<std::unique_ptr<EdSerial>, QString> find_everdrive(const QString& preferred) {
    // EverDrive N8 PRO meldet sich als VID 0x38DF / PID 0x0017 (Microsoft
    // usbser). Ports mit dieser Kennung zuerst probieren, danach der Rest
    // als Fallback (falls eine andere Firmware/Revision abweichende IDs
    // hat). So wird nicht jeder fremde COM-Port (Bluetooth etc.) mit dem
    // teuren open()-Handshake angetestet.
    constexpr quint16 kEverdriveVid = 0x38DF;
    constexpr quint16 kEverdrivePid = 0x0017;

    QStringList preferredMatch;  // passende VID/PID
    QStringList rest;            // alles andere (Fallback)
    if (!preferred.isEmpty()) preferredMatch << preferred;

    for (const auto& info : QSerialPortInfo::availablePorts()) {
        const QString name = info.portName();
        if (preferredMatch.contains(name) || rest.contains(name)) continue;
        bool idMatch = info.hasVendorIdentifier() && info.hasProductIdentifier() &&
                       info.vendorIdentifier() == kEverdriveVid &&
                       info.productIdentifier() == kEverdrivePid;
        if (idMatch) preferredMatch << name;
        else rest << name;
    }

    // Fremde Ports (Bluetooth, USB-Seriell-Adapter, ...) NUR noch antesten,
    // wenn gar kein VID/PID-Treffer existiert. Jeder Fehlversuch kostet einen
    // vollen open()-Zyklus; sie alle durchzuprobieren, obwohl der EverDrive
    // eindeutig identifiziert ist, war reine Wartezeit.
    QStringList candidates = preferredMatch.isEmpty() ? rest : preferredMatch;
    for (const auto& port : candidates) {
        auto ed = std::make_unique<EdSerial>(port);
        try {
            ed->open();
            return {std::move(ed), port};
        } catch (...) {
            ed->close();
        }
    }
    return {nullptr, QString()};
}
