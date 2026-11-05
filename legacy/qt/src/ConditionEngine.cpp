#include "ConditionEngine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <regex>
#include <set>

namespace rawnes {
namespace {

// Groessen-Tabelle, exakt wie Python _SIZE: size_key(lowercase) -> (nbytes, special, big_endian)
struct SizeInfo { int nbytes; std::string special; bool be; };

const std::map<std::string, SizeInfo>& sizeTable() {
    static const std::map<std::string, SizeInfo> t = {
        {"",  {2, "", false}}, {" ", {2, "", false}},
        {"h", {1, "", false}},
        {"x", {4, "", false}},
        {"w", {3, "", false}},
        {"i", {2, "", true}},
        {"j", {3, "", true}},
        {"g", {4, "", true}},
        {"m", {1, "bit0", false}}, {"n", {1, "bit1", false}},
        {"o", {1, "bit2", false}}, {"p", {1, "bit3", false}},
        {"q", {1, "bit4", false}}, {"r", {1, "bit5", false}},
        {"s", {1, "bit6", false}}, {"t", {1, "bit7", false}},
        {"l", {1, "low4", false}}, {"u", {1, "up4", false}},
        {"k", {1, "cnt", false}},
    };
    return t;
}

std::string toLowerStr(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Entspricht _OP_RE aus dem Python-Original, gruppenidentisch:
//  1=mod([dpb]?) 2=size_c 3=addr_hex 4=const_hex(nach 'h') 5=const_dec
//  6=const_f(nach 'f') 7=mod_op 8=mod_hex 9=mod_dec
const std::regex& opRegex() {
    static const std::regex re(
        R"(^([dpb]?)(?:0x([hHxXwWiIjJgGmMnNoOpPqQrRsStTlLuUkK ]?)([0-9a-fA-F]+)|h([0-9a-fA-F]+)|(\d+(?:\.\d+)?)|f([0-9.]+))(?:([*/&^%])(?:0x([0-9a-fA-F]+)|([0-9.]+)))?$)");
    return re;
}

// Entspricht _CMP_RE -- Reihenfolge der Alternative ist wichtig (!=/<=/>=
// vor </>/=, sonst wuerde z.B. ">=" faelschlich als "=" nach ">" erkannt).
const std::regex& cmpRegex() {
    static const std::regex re(R"(!=|<=|>=|<|>|=)");
    return re;
}

Operand parseOperand(const std::string& tokenIn) {
    Operand out;
    std::string token = trim(tokenIn);
    if (token.empty()) return out; // valid=false -> "kein Operand" (Python: None)

    std::smatch m;
    if (!std::regex_match(token, m, opRegex())) return out; // valid=false

    std::string mod = m[1].str();
    std::string sizeC = m[2].str();
    std::string addrHex = m[3].str();
    std::string constHex = m[4].str();
    std::string constDec = m[5].str();
    std::string constF = m[6].str();
    std::string modOpS = m[7].str();
    std::string modHex = m[8].str();
    std::string modDec = m[9].str();

    char modOp = 0;
    double modVal = 1;
    if (!modHex.empty()) {
        modVal = static_cast<double>(std::stoll(modHex, nullptr, 16));
        modOp = modOpS[0];
    } else if (!modDec.empty()) {
        modVal = std::stod(modDec);
        modOp = modOpS[0];
    } else {
        modOp = 0;
        modVal = 1;
    }

    out.valid = true;
    out.mod = mod.empty() ? 0 : mod[0];
    out.modOp = modOp;
    out.modVal = modVal;

    if (!constHex.empty()) {
        out.isConst = true;
        out.constValue = static_cast<double>(std::stoll(constHex, nullptr, 16));
        return out;
    }
    if (!constDec.empty()) {
        out.isConst = true;
        out.constValue = std::stod(constDec);
        return out;
    }
    if (!constF.empty()) {
        out.isConst = true;
        out.isFloatConst = true;
        out.constValue = 0;
        out.unsupported = true;
        return out;
    }

    // Speicherzugriff
    uint32_t addr = static_cast<uint32_t>(std::stoul(addrHex, nullptr, 16));
    std::string sizeKey = (!sizeC.empty() && sizeC != " ") ? toLowerStr(sizeC) : sizeC;
    auto it = sizeTable().find(sizeKey);
    SizeInfo si = (it != sizeTable().end()) ? it->second : SizeInfo{1, "", false};

    out.ra = addr;
    out.nbytes = si.nbytes;
    out.special = si.special;
    out.bigEndian = si.be;
    if (addr >= 0x10000) out.unsupported = true;
    return out;
}

Condition parseConditionLine(const std::string& condIn) {
    Condition c;
    std::string cond = condIn;

    // Flag: ^([A-Za-z]):(.*)$  -- Gross-/Kleinschreibung wird NICHT
    // normalisiert (bewusst, wie im Original -- ein kleines "a:" ist
    // NICHT dasselbe wie "A:" und macht die Bedingung unsupported).
    if (cond.size() >= 2 && std::isalpha(static_cast<unsigned char>(cond[0])) &&
        cond[1] == ':') {
        char f = cond[0];
        cond = cond.substr(2);
        c.flag = (f == 'Q') ? 0 : f; // Q = MeasuredIf, im Trigger-Pfad wie normale Bedingung
    }

    // Hit-Ziel: \.(\d+)\.$  -- strikt mit Schlusspunkt
    static const std::regex targetRe(R"(\.(\d+)\.$)");
    std::smatch tm;
    if (std::regex_search(cond, tm, targetRe)) {
        c.target = std::stoi(tm[1].str());
        cond = cond.substr(0, tm.position(0));
    }

    std::smatch cm;
    if (std::regex_search(cond, cm, cmpRegex())) {
        c.op = cm.str(0);
        c.left = parseOperand(cond.substr(0, cm.position(0)));
        c.right = parseOperand(cond.substr(cm.position(0) + cm.length(0)));
        c.hasRight = true;
    } else {
        c.op.clear();
        c.left = parseOperand(cond);
        c.hasRight = false;
    }
    return c;
}

// Entspricht re.split(r'(?<!0x)S', memaddr) -- C++ std::regex kennt kein
// Lookbehind, daher manueller Scan: an jedem 'S' trennen, das NICHT die
// zwei Zeichen "0x" direkt davor hat.
std::vector<std::string> splitAltGroups(const std::string& memaddr) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < memaddr.size(); ++i) {
        char ch = memaddr[i];
        if (ch == 'S') {
            bool precededBy0x = (i >= 2 && memaddr[i - 2] == '0' && memaddr[i - 1] == 'x');
            if (!precededBy0x) {
                parts.push_back(cur);
                cur.clear();
                continue;
            }
        }
        cur.push_back(ch);
    }
    parts.push_back(cur);
    return parts;
}

std::vector<std::string> splitChar(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

const std::set<char>& supportedFlags() {
    // Python: {None,'A','B','N','O','Z','P','R','T','M','C','D'} --
    // 0 hier steht fuer "kein Flag" (Python None).
    static const std::set<char> s = {0, 'A', 'B', 'N', 'O', 'Z', 'P', 'R', 'T', 'M', 'C', 'D'};
    return s;
}

double applyMod(double v, const Operand& op) {
    if (op.modOp == 0) return v; // mult ist im Original immer 1, wenn kein modOp gesetzt ist
    switch (op.modOp) {
        case '*': return v * op.modVal;
        case '/': return op.modVal != 0 ? v / op.modVal : 0;
        case '&': return static_cast<double>(static_cast<int64_t>(v) & static_cast<int64_t>(op.modVal));
        case '^': return static_cast<double>(static_cast<int64_t>(v) ^ static_cast<int64_t>(op.modVal));
        case '%': return op.modVal != 0
                      ? static_cast<double>(static_cast<int64_t>(v) % static_cast<int64_t>(op.modVal))
                      : 0;
        default: return v;
    }
}

double bcd(int64_t v, int nbytes) {
    int64_t total = 0;
    for (int shift = (nbytes * 2 - 1) * 4; shift >= 0; shift -= 4) {
        total = total * 10 + ((v >> shift) & 0xF);
    }
    return static_cast<double>(total);
}

double readOperand(const Operand& op, const RamMap& ram, const RamMap* prev,
                   const RamMap& prior) {
    if (op.isConst) return applyMod(op.constValue, op);

    const RamMap* src = &ram;
    if (op.mod == 'd') src = prev;
    else if (op.mod == 'p') src = &prior; // prior faellt in C++ nicht auf prev zurueck wie
                                          // im Python-Default (prior wird immer mitgefuehrt,
                                          // entspricht dem "prior if prior is not None" Fall)

    int64_t v = 0;
    if (op.bigEndian) {
        for (int i = 0; i < op.nbytes; ++i) {
            auto it = src->find(raByteToPhys(op.ra + i));
            uint8_t b = (it != src->end()) ? it->second : 0;
            v = (v << 8) | b;
        }
    } else {
        for (int i = 0; i < op.nbytes; ++i) {
            auto it = src->find(raByteToPhys(op.ra + i));
            uint8_t b = (it != src->end()) ? it->second : 0;
            v |= static_cast<int64_t>(b) << (8 * i);
        }
    }

    if (!op.special.empty()) {
        if (op.special.rfind("bit", 0) == 0) {
            int bitNo = op.special[3] - '0';
            v = (v >> bitNo) & 1;
        } else if (op.special == "low4") {
            v = v & 0xF;
        } else if (op.special == "up4") {
            v = (v >> 4) & 0xF;
        } else if (op.special == "cnt") {
            v = static_cast<int64_t>(__builtin_popcount(static_cast<unsigned>(v & 0xFF)));
        }
    }
    double vd = static_cast<double>(v);
    if (op.mod == 'b') vd = bcd(v, op.nbytes);
    return applyMod(vd, op);
}

bool compareVals(double a, const std::string& op, double b) {
    if (op == "=")  return a == b;
    if (op == "!=") return a != b;
    if (op == ">")  return a > b;
    if (op == ">=") return a >= b;
    if (op == "<")  return a < b;
    if (op == "<=") return a <= b;
    return false;
}

} // namespace

std::vector<ConditionGroup> parseMemAddr(const std::string& memaddr) {
    std::vector<ConditionGroup> groups;
    for (const auto& grp : splitAltGroups(memaddr)) {
        ConditionGroup conds;
        for (const auto& piece : splitChar(grp, '_')) {
            std::string c = trim(piece);
            if (!c.empty()) conds.push_back(parseConditionLine(c));
        }
        if (!conds.empty()) groups.push_back(std::move(conds));
    }
    return groups;
}

bool isUnsupported(const std::vector<ConditionGroup>& groups) {
    if (groups.empty()) return true;
    for (auto& g : groups) {
        for (auto& c : g) {
            if (supportedFlags().find(c.flag) == supportedFlags().end()) return true;
            if (!c.left.valid) return true;
            if (!c.op.empty() && !c.right.valid) return true;
            if (c.left.valid && c.left.unsupported) return true;
            if (c.right.valid && c.right.unsupported) return true;
        }
    }
    return false;
}

std::vector<uint32_t> collectAddresses(
    const std::vector<std::pair<std::string, std::string>>& idAndMem) {
    std::set<uint32_t> addrs;
    for (auto& [id, mem] : idAndMem) {
        (void)id;
        for (auto& grp : parseMemAddr(mem)) {
            for (auto& c : grp) {
                for (const Operand* side : {&c.left, &c.right}) {
                    if (side->valid && !side->isConst && !side->unsupported) {
                        for (int i = 0; i < side->nbytes; ++i) {
                            addrs.insert(raByteToPhys(side->ra + i));
                        }
                    }
                }
            }
        }
    }
    return {addrs.begin(), addrs.end()};
}

AchievementRuntime::AchievementRuntime(const std::string& memaddr) {
    groups_ = parseMemAddr(memaddr);
    unsupported_ = isUnsupported(groups_);
}

void AchievementRuntime::reset() {
    for (auto& g : groups_) for (auto& c : g) c.hits = 0;
}

void AchievementRuntime::trackPrior(const RamMap& ram) {
    for (auto& [addr, val] : ram) {
        auto it = last_.find(addr);
        if (it != last_.end() && it->second != val) prior_[addr] = it->second;
        last_[addr] = val;
    }
}

bool AchievementRuntime::walk(ConditionGroup& group, const RamMap& ram, const RamMap* prev,
                              bool pausePass, bool* resetOut) {
    double accum = 0;
    double hitPool = 0;
    std::optional<bool> chainVal;
    std::string chainOp; // "and" / "or" / ""
    bool resetNext = false;
    bool paused = false;
    bool satisfied = true;
    bool anyCountable = false;
    bool reset = false;

    for (auto& c : group) {
        char flag = c.flag;
        double rawVal = readOperand(c.left, ram, prev, prior_);

        if (flag == 'A') { accum += rawVal; continue; }
        if (flag == 'B') { accum -= rawVal; continue; }

        double lval = rawVal + accum;
        bool raw;
        if (!c.op.empty()) {
            raw = compareVals(lval, c.op, readOperand(c.right, ram, prev, prior_));
        } else {
            raw = true;
        }

        bool combined;
        if (!chainVal.has_value()) combined = raw;
        else if (chainOp == "or") combined = *chainVal || raw;
        else combined = *chainVal && raw;

        if (flag == 'N') { chainVal = combined; chainOp = "and"; accum = 0; continue; }
        if (flag == 'O') { chainVal = combined; chainOp = "or"; accum = 0; continue; }
        if (flag == 'Z') {
            if (combined) resetNext = true;
            chainVal.reset(); chainOp.clear(); accum = 0;
            continue;
        }
        if (flag == 'C' || flag == 'D') {
            if (!pausePass) {
                if (resetNext) c.hits = 0;
                if (combined && (c.target == 0 || c.hits < c.target)) c.hits += 1;
            }
            hitPool += (flag == 'C') ? c.hits : -c.hits;
            resetNext = false;
            chainVal.reset(); chainOp.clear(); accum = 0;
            continue;
        }

        // reale Bedingung (kein Flag / P / R / T / M)
        accum = 0;
        chainVal.reset(); chainOp.clear();

        bool isPause = (flag == 'P');
        bool doHits = (pausePass && isPause) || (!pausePass && !isPause);

        bool sat;
        if (doHits) {
            if (resetNext) c.hits = 0;
            if (c.target > 0) {
                if (combined && c.hits < c.target) c.hits += 1;
                sat = (c.hits + hitPool) >= c.target;
            } else {
                sat = combined;
            }
        } else {
            if (c.target > 0) sat = (c.hits + hitPool) >= c.target;
            else sat = combined;
        }
        hitPool = 0;
        resetNext = false;

        if (isPause) {
            if (pausePass && sat) paused = true;
            continue;
        }
        if (pausePass) continue;

        if (flag == 'R') {
            if (sat) reset = true;
            continue;
        }

        anyCountable = true;
        if (!sat) satisfied = false;
    }

    if (pausePass) return paused;
    if (resetOut) *resetOut = reset;
    return anyCountable ? satisfied : true;
}

bool AchievementRuntime::update(const RamMap& ram, const RamMap* prev) {
    if (unsupported_ || prev == nullptr) {
        trackPrior(ram);
        return false;
    }
    trackPrior(ram);

    std::vector<bool> paused;
    paused.reserve(groups_.size());
    for (auto& g : groups_) paused.push_back(walk(g, ram, prev, true, nullptr));

    std::vector<bool> results;
    results.reserve(groups_.size());
    bool resetAll = false;
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (paused[i]) { results.push_back(false); continue; }
        bool rst = false;
        bool sat = walk(groups_[i], ram, prev, false, &rst);
        results.push_back(sat);
        if (rst) resetAll = true;
    }

    if (resetAll) { reset(); return false; }

    bool core = results.empty() ? true : results[0];
    bool anyAlt = false;
    for (size_t i = 1; i < results.size(); ++i) if (results[i]) { anyAlt = true; break; }
    bool hasAlts = results.size() > 1;
    return core && (!hasAlts || anyAlt);
}

AchievementRuntime::ProgressInfo AchievementRuntime::progress() const {
    ProgressInfo info;
    if (groups_.empty() || unsupported_) return info;
    const ConditionGroup& core = groups_[0];

    // Erst nach einer 'M'-Bedingung (Measured) mit Ziel > 1 suchen.
    for (const auto& c : core) {
        if (c.flag == 'M' && c.target > 1) {
            info.current = std::min(c.hits, c.target);
            info.target = c.target;
            info.hasProgress = true;
            return info;
        }
    }
    // Fallback: Bedingung mit dem hoechsten Hit-Ziel > 1 (irgendein
    // zaehlbarer Fortschritt ist besser als gar keiner, auch wenn sie
    // nicht explizit als Measured markiert ist).
    for (const auto& c : core) {
        if (c.target > info.target && c.target > 1) {
            info.current = std::min(c.hits, c.target);
            info.target = c.target;
            info.hasProgress = true;
        }
    }
    return info;
}

// ================= Leaderboard-Wert (nur einfacher Fall) =================

std::optional<double> evaluateSimpleValue(const std::string& valueExpr, const RamMap& ram) {
    std::string trimmed = trim(valueExpr);
    // Erkennt jedes ':' (AddSource-Kette) oder '$' (Max-Alternativen) --
    // beides nicht unterstuetzt, siehe Header-Kommentar.
    if (trimmed.empty() || trimmed.find(':') != std::string::npos ||
        trimmed.find('$') != std::string::npos) {
        return std::nullopt;
    }
    Operand op = parseOperand(trimmed);
    if (!op.valid || op.unsupported) return std::nullopt;
    RamMap emptyPrior;
    double v = readOperand(op, ram, nullptr, emptyPrior);
    return v;
}

// ================= LeaderboardRuntime =================

LeaderboardRuntime::LeaderboardRuntime(long long id, std::string title, std::string format,
                                       const std::string& startMem, const std::string& cancelMem,
                                       const std::string& submitMem, const std::string& valueMem)
    : id_(id), title_(std::move(title)), format_(std::move(format)), valueExpr_(valueMem) {
    startRt_ = std::make_unique<AchievementRuntime>(startMem);
    cancelRt_ = std::make_unique<AchievementRuntime>(cancelMem);
    submitRt_ = std::make_unique<AchievementRuntime>(submitMem);
    unsupported_ = startRt_->unsupported() || cancelRt_->unsupported() || submitRt_->unsupported();
    // Wert-Unterstuetzung wird erst beim ersten update() geprueft (braucht
    // RamMap fuer den Test) -- hier nur grob auf ':'/'$' pruefen.
    std::string t = trim(valueMem);
    valueUnsupported_ = t.empty() || t.find(':') != std::string::npos ||
                       t.find('$') != std::string::npos;
}

void LeaderboardRuntime::reset() {
    started_ = false;
    submitted_ = false;
    startRt_->reset();
    cancelRt_->reset();
    submitRt_->reset();
}

LeaderboardRuntime::Event LeaderboardRuntime::update(const RamMap& ram, const RamMap* prev,
                                                      double* outValue) {
    if (unsupported_ || submitted_ || prev == nullptr) return Event::None;

    if (!started_) {
        if (startRt_->update(ram, prev)) {
            started_ = true;
            return Event::Start;
        }
        return Event::None;
    }

    if (cancelRt_->update(ram, prev)) {
        started_ = false;
        return Event::Cancel;
    }

    if (submitRt_->update(ram, prev)) {
        if (valueUnsupported_) {
            // Trigger hat ausgeloest, aber der Wert kann nicht sicher
            // berechnet werden -- lieber nichts einreichen als raten.
            submitted_ = true;
            return Event::None;
        }
        auto v = evaluateSimpleValue(valueExpr_, ram);
        if (!v) { submitted_ = true; return Event::None; }
        submitted_ = true;
        if (outValue) *outValue = *v;
        return Event::Submit;
    }
    return Event::None;
}

} // namespace rawnes
