#include "Plausibel.h"

#include <cstdio>

namespace rawnes {

std::string unplausibelGrund(int buildGelesen, int buildErwartet, const RamMap& ram) {
    // a) Laeuft unser Kern noch? Die Build-Kennung muss lesbar sein und
    //    der beim Start gelesenen entsprechen.
    if (buildGelesen < 0) return "build ?";
    if (buildErwartet >= 0 && buildGelesen != buildErwartet) {
        char t[16];
        std::snprintf(t, sizeof(t), "build %02x", (unsigned)buildGelesen & 0xFF);
        return t;
    }
    // b) Alle Werte gleichzeitig FF oder 00: Leerlauf des Busses, kein
    //    Spielzustand (erst ab 4 Adressen aussagekraeftig).
    if (ram.size() >= 4) {
        bool alleFF = true, alle00 = true;
        for (const auto& kv : ram) {
            if (kv.second != 0xFF) alleFF = false;
            if (kv.second != 0x00) alle00 = false;
        }
        if (alleFF) return "FF";
        if (alle00) return "00";
    }
    return std::string();
}

std::optional<RamMap> Abfragefilter::weiter(const RamMap& ram) {
    warte_.push_back(ram);
    if ((int)warte_.size() <= k_) return std::nullopt;
    RamMap frei = std::move(warte_.front());
    warte_.pop_front();
    return frei;
}

}  // namespace rawnes
