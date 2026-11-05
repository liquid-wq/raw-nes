// ConditionEngine.h -- Portierung von ra_condition_nes.py (rcheevos-
// Semantik: AddSource/SubSource, AndNext/OrNext, ResetNextIf, PauseIf,
// ResetIf, Trigger, Measured, Hit-Counts, Alt-Gruppen, Delta/Prior/BCD,
// Multiplikator/Bitmaske, Bit-Groessen).
//
// 1:1-Portierung, keine Vereinfachung -- diese Engine entscheidet, ob ein
// Achievement triggert, Fehler hier sind nicht tolerierbar. Gegen die
// tatsaechliche Python-Ausfuehrung cross-verifiziert (siehe tests/).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rawnes {

// Ein geparster Operand (Speicherzugriff oder Konstante).
struct Operand {
    bool isConst = false;
    bool isFloatConst = false;   // 'f'-Konstante -- immer unsupported (wie Python)
    double constValue = 0;

    uint32_t ra = 0;             // RA-Byte-Adresse (nur wenn !isConst)
    int nbytes = 1;
    // special: "" | "bit0".."bit7" | "low4" | "up4" | "cnt"
    std::string special;
    bool bigEndian = false;

    char mod = 0;                 // 'd' (delta) / 'p' (prior) / 'b' (BCD) / 0
    char modOp = 0;                // '*' '/' '&' '^' '%' / 0
    double modVal = 1;

    bool unsupported = false;     // Adresse >= 0x10000, Float-Konstante, o.ae.
    bool valid = false;           // false = Parse-Fehler (Token nicht erkannt)
};

// Eine geparste Bedingung (eine Zeile zwischen '_' in MemAddr).
struct Condition {
    char flag = 0;           // 0 = keine, sonst 'A','B','N','O','Z','P','R','T','M','C','D', ...
    Operand left;
    std::string op;           // "" "=" "!=" "<" ">" "<=" ">="
    Operand right;
    bool hasRight = false;
    int target = 0;           // Hit-Ziel (.N.), 0 = kein Ziel
    mutable int hits = 0;      // veraenderlich waehrend der Auswertung
};

using ConditionGroup = std::vector<Condition>;

// Ergebnis von parse_memaddr(): Gruppe 0 = Core, Rest = Alt-Gruppen (S-getrennt).
std::vector<ConditionGroup> parseMemAddr(const std::string& memaddr);

bool isUnsupported(const std::vector<ConditionGroup>& groups);

// RA-Byte-Adresse -> physische NES-RAM-Adresse. Fuer NES 1:1 (kein
// Byteswap, keine Basisverschiebung, im Unterschied zu MEGA-RAW/Genesis).
inline uint32_t raByteToPhys(uint32_t raAddr) { return raAddr & 0xFFFF; }

// Alle physischen Adressen, die die Achievements dieses Spiels brauchen.
std::vector<uint32_t> collectAddresses(
    const std::vector<std::pair<std::string, std::string>>& idAndMem);

using RamMap = std::map<uint32_t, uint8_t>;

// Stateful-Auswertung eines einzelnen Achievements ueber Frames hinweg
// (Hit-Counter, Delta/Prior-Tracking). Entspricht AchievementRuntime.
class AchievementRuntime {
public:
    explicit AchievementRuntime(const std::string& memaddr);

    bool unsupported() const { return unsupported_; }
    void reset();

    // Pro Frame aufrufen. true = Achievement feuert jetzt.
    bool update(const RamMap& ram, const RamMap* prev);

    // Fuer den Fortschrittsbalken in der GUI: aktueller Hit-Stand + Ziel
    // der "interessantesten" Bedingung der Core-Gruppe. Bevorzugt eine
    // Bedingung mit Flag 'M' (Measured, rcheevos-Konvention fuer
    // Fortschrittsanzeige); falls keine vorhanden, faellt es auf die
    // Bedingung mit dem hoechsten Hit-Ziel (>1) zurueck. hasProgress=false
    // wenn keine Bedingung ein Ziel >1 hat (dann macht ein Balken keinen
    // Sinn -- Achievement ist ein reiner Ja/Nein-Trigger).
    // NEU in dieser Portierung, hat kein Python-Gegenstueck in
    // ra_condition_nes.py -- keine Cross-Verifikation moeglich, nur
    // logisch gegengeprueft (siehe tests/).
    struct ProgressInfo { int current = 0; int target = 0; bool hasProgress = false; };
    ProgressInfo progress() const;

private:
    bool walk(ConditionGroup& group, const RamMap& ram, const RamMap* prev,
             bool pausePass, bool* resetOut);
    void trackPrior(const RamMap& ram);

    std::vector<ConditionGroup> groups_;
    bool unsupported_ = false;
    RamMap prior_;   // phys-Adresse -> Wert vor der letzten Aenderung
    RamMap last_;    // phys-Adresse -> letzter gesehener Wert
};

// Fuer Leaderboard-Werte (VAL:-Ausdruck): NUR der einfache, haeufige Fall
// wird unterstuetzt -- ein einzelner Speicherzugriff (mit optionalem
// Groessen-/Modifier-Suffix), keine AddSource-Ketten, keine $-Alternativen.
// RAs volles Value-Format ist eine eigene Mini-Sprache mit eigenen
// Trennzeichen (nicht '_'/'S' wie parseMemAddr) -- dafuer gibt es in
// diesem Projekt keine funktionierende Referenz (ra_leaderboard_nes.py
// ruft eine in ra_condition_nes.py nicht existierende Funktion auf, war
// nie lauffaehig). Komplexere Werte werden erkannt und liefern
// std::nullopt, statt einen falschen Wert an RA zu senden.
std::optional<double> evaluateSimpleValue(const std::string& valueExpr, const RamMap& ram);

// Ein einzelner Leaderboard-Eintrag: Start-/Abbruch-/Einreich-Bedingung
// (wiederverwendet AchievementRuntime -- gleiche Trigger-Syntax) plus
// der (nur im einfachen Fall unterstuetzte) Wert. NEU in dieser
// Portierung -- ra_leaderboard_nes.py war wegen der fehlenden
// racond.parse_condition()-Funktion nie lauffaehig, es gibt also kein
// funktionierendes Original zum 1:1-Portieren, nur die Absicht dahinter.
class LeaderboardRuntime {
public:
    LeaderboardRuntime(long long id, std::string title, std::string format,
                       const std::string& startMem, const std::string& cancelMem,
                       const std::string& submitMem, const std::string& valueMem);

    long long id() const { return id_; }
    const std::string& title() const { return title_; }
    const std::string& format() const { return format_; }
    bool unsupported() const { return unsupported_; }
    bool started() const { return started_; }
    bool submitted() const { return submitted_; }
    void reset();

    enum class Event { None, Start, Cancel, Submit };
    // Pro Frame aufrufen. Bei Event::Submit steht der Wert in outValue.
    Event update(const RamMap& ram, const RamMap* prev, double* outValue);

private:
    long long id_;
    std::string title_;
    std::string format_;
    bool unsupported_ = false;
    bool valueUnsupported_ = false;
    std::string valueExpr_;
    bool started_ = false;
    bool submitted_ = false;
    std::unique_ptr<AchievementRuntime> startRt_;
    std::unique_ptr<AchievementRuntime> cancelRt_;
    std::unique_ptr<AchievementRuntime> submitRt_;
};

} // namespace rawnes
