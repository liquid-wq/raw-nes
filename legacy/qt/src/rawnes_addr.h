#pragma once
#include <cstdint>

// Adressen im PiBus-Adressraum des EverDrive N8 PRO, RAW-NES-spezifisch
// (ce_snoop-Fenster von rawnes_snoop.sv, ce_cfg-Register aus sys_cfg.sv).
namespace RawnesAddr {
    // KORREKTUR: Der vorherige Kommentar hier ("0x1804000 ist die auf
    // Hardware bestaetigte Variante") war falsch/veraltet -- verifiziert
    // gegen die echte, aktuelle rawnes_gui_v3_final.py: dort ist
    // ADDR_SNOOP = 0x1808000 der Standardwert (Zeile 164), UND die GUI
    // probiert bei jedem Verbinden zusaetzlich beide Adressen durch
    // (0x1808000 zuerst, dann 0x1804000 als Fallback), liest jeweils das
    // Build-Register und bleibt bei der Adresse, die den erwarteten Build
    // liefert. Nur wenn keine der beiden passt (z.B. weil eine aeltere
    // FPGA-Firmware ohne den erwarteten Build laeuft), bleibt es beim
    // Standardwert 0x1808000 -- genau der hier aktuell relevante Fall.
    // kSnoopBase hier ist daher nur der FALLBACK/Standardwert; die
    // eigentliche Auswahl passiert zur Laufzeit in
    // MonitorWorker::connectToDevice() (siehe dort, snoopBase_-Member).
    constexpr uint32_t kSnoopBase = 0x1808000;
    constexpr uint32_t kSnoopBaseAlt = 0x1804000; // zweiter Kandidat beim Probing
    constexpr uint32_t kRamMirrorSize = 0x0800;   // 2 KB, NES-RAM $0000-$07FF, 1:1 gespiegelt
    constexpr uint32_t kBuildOffset = 0x0810;
    constexpr uint32_t kResetCtrOffset = 0x0811;
    constexpr uint32_t kSstStatusOffset = 0x0812; // Bit0=live sst.act, Bit1=sst_seen seit Reset
    constexpr int kFpgaBuildExpected = 11;

    // ce_cfg (Live-Register-Bank, sys_cfg.sv scfg[16]). ACHTUNG: nicht mit
    // der MapConfig-Adresse 0x1800000 verwechseln -- das hier ist die
    // richtige.
    constexpr uint32_t kCfgBase = 0x1800020;
    constexpr uint32_t kCtrlByteOffset = 7;       // scfg[7]
    constexpr uint8_t kCtSsOnBit = 0x02;          // Bit1: Savestates/"In Game Menu"
    constexpr uint8_t kCtGgOnBit = 0x04;          // Bit2: Cheats
}
