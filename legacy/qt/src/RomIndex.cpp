#include "RomIndex.h"
#include "Md5.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>

#include <nlohmann/json.hpp>
#include "../third_party/miniz/miniz.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rawnes {
namespace {

std::string toHex(const uint8_t* data, size_t len) {
    static const char* hexChars = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hexChars[data[i] >> 4]);
        out.push_back(hexChars[data[i] & 0xF]);
    }
    return out;
}

std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

// Liest alle .nes-Eintraege aus einer .zip-Datei (Name, Rohdaten) --
// via miniz (dieselbe Bibliothek, die auch MEGA-RAWs archive_extract.cpp
// nutzt), statt eines selbstgebauten Central-Directory-Parsers +zlib.
// Deutlich weniger Code, gleiche Bibliothek wie im Schwesterprojekt.
std::vector<std::pair<std::string, std::vector<uint8_t>>> readNesFromZip(
    const std::string& zipPath) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;

    mz_zip_archive za;
    std::memset(&za, 0, sizeof(za));
    if (!mz_zip_reader_init_file(&za, zipPath.c_str(), 0)) return out;

    int n = static_cast<int>(mz_zip_reader_get_num_files(&za));
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st)) continue;
        if (mz_zip_reader_is_file_a_directory(&za, i)) continue;

        std::string name(st.m_filename);
        std::string lower = name;
        for (auto& c : lower) c = static_cast<char>(tolower(c));
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".nes") continue;

        size_t uncompSize = static_cast<size_t>(st.m_uncomp_size);
        std::vector<uint8_t> buf(uncompSize);
        if (uncompSize > 0) {
            if (!mz_zip_reader_extract_to_mem(&za, i, buf.data(), uncompSize, 0)) continue;
        }
        out.emplace_back(name, std::move(buf));
    }
    mz_zip_reader_end(&za);
    return out;
}

// Sucht eine installierte 7-Zip-EXE: erst die ueblichen Installationspfade,
// dann PATH via 'where'. Leerer String, wenn nichts gefunden wurde.
std::string findSevenZipExe() {
    static const char* kCommonPaths[] = {
        "C:\\Program Files\\7-Zip\\7z.exe",
        "C:\\Program Files (x86)\\7-Zip\\7z.exe",
    };
    for (auto* p : kCommonPaths) {
        if (fs::exists(p)) return p;
    }
    FILE* pipe = _popen("where 7z.exe 2>NUL", "r");
    if (!pipe) return "";
    char buf[512] = {0};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) result = buf;
    _pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    if (!result.empty() && fs::exists(result)) return result;
    return "";
}

// Entpackt alle .nes-Eintraege aus einem Archiv (.7z/.rar/.tar/.gz) via
// externer 7z.exe in einen Temp-Ordner, liest sie, raeumt danach auf.
// 7-Zip kann RAR mit-entpacken (nicht erstellen) -- ein Codepfad deckt
// damit alle gaengigen Packformate ab, kein Extra-Tool pro Format noetig.
// outLog (optional) bekommt die rohe 7z-Konsolenausgabe -- wichtig fuer
// die Diagnose, wenn 0 Dateien gefunden werden (falscher Grund sonst
// nicht von echtem 7z-Fehler unterscheidbar).
std::vector<std::pair<std::string, std::vector<uint8_t>>> readNesFromArchive(
    const std::string& sevenZipExe, const std::string& archivePath,
    std::string* outLog = nullptr) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;
    std::error_code ec;

    fs::path tempDir = fs::temp_directory_path() /
        ("rawnes_arc_" + std::to_string(std::hash<std::string>{}(archivePath)));
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    // -y = alle Rueckfragen bestaetigen, -r = auch in Unterordnern des
    // Archivs nach *.nes suchen.
    //
    // Aeusseres Anfuehrungszeichen-Paar ("" ... "") ist KEIN Tippfehler:
    // bekannter cmd.exe-Quoting-Bug bei _popen()/system() unter Windows --
    // wenn der Befehl mehrere gequotete Pfade mit Leerzeichen enthaelt
    // (z.B. "C:\Program Files\7-Zip\7z.exe" UND "Metroid (USA).7z"),
    // verschluckt sich cmd.exe am ersten Pfad ("Der Befehl "C:\Program"
    // ist ... nicht gefunden"). Der ganze String muss dafuer nochmal in
    // ein Quote-Paar gewickelt werden.
    std::string innerCmd = "\"" + sevenZipExe + "\" x \"" + archivePath +
                           "\" -o\"" + tempDir.string() + "\" -y \"*.nes\" -r 2>&1";
    std::string cmd = "\"" + innerCmd + "\"";
    if (FILE* pipe = _popen(cmd.c_str(), "r")) {
        char buf[256];
        std::string output;
        while (fgets(buf, sizeof(buf), pipe)) output += buf;
        _pclose(pipe);
        if (outLog) *outLog = output;
    }

    for (auto it = fs::recursive_directory_iterator(tempDir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        for (auto& c : ext) c = static_cast<char>(tolower(c));
        if (ext != ".nes") continue;
        out.emplace_back(it->path().filename().string(), readFile(it->path().string()));
    }
    fs::remove_all(tempDir, ec);
    return out;
}

} // namespace

std::optional<std::array<uint8_t, 6>> vectorsFromInes(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A)
        return std::nullopt;
    size_t prgSize = static_cast<size_t>(data[4]) * 16384;
    if (prgSize == 0) return std::nullopt;
    size_t prgEnd = 16 + prgSize;
    if (prgEnd > data.size()) return std::nullopt;
    std::array<uint8_t, 6> v{};
    std::copy(data.begin() + (prgEnd - 6), data.begin() + prgEnd, v.begin());
    return v;
}

std::optional<std::array<uint8_t, 256>> lastPageFromInes(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A)
        return std::nullopt;
    size_t prgSize = static_cast<size_t>(data[4]) * 16384;
    if (prgSize < 256) return std::nullopt;
    size_t end = 16 + prgSize;
    if (end > data.size()) return std::nullopt;
    std::array<uint8_t, 256> page{};
    std::copy(data.begin() + (end - 256), data.begin() + end, page.begin());
    return page;
}

std::optional<InesHeaderInfo> inesHeaderInfo(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A)
        return std::nullopt;
    uint8_t b6 = data[6], b7 = data[7];
    int mapper = (b6 >> 4) | (b7 & 0xF0);
    if ((b7 & 0x0C) == 0x08 && data.size() > 8) mapper |= (data[8] & 0x0F) << 8;
    InesHeaderInfo info;
    info.mapper = mapper;
    info.prg = data[4];
    info.chr = data[5];
    info.mirror = b6 & 0x01;
    info.four = (b6 >> 3) & 0x01;
    return info;
}

std::string inesMd5(const std::vector<uint8_t>& dataIn) {
    std::vector<uint8_t> data = dataIn;
    if (data.size() >= 4 && data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 0x1A) {
        data.erase(data.begin(), data.begin() + 16);
    }
    return md5Hex(data);
}

std::string indexKey(const std::array<uint8_t, 6>& vec6) {
    return toHex(vec6.data(), 4); // nur NMI+Reset, wie im Original
}

RomIndexMap buildIndex(const std::string& base, int* skippedArchives,
                       const std::function<void(int, const std::string&)>& progress) {
    RomIndexMap index;
    int n = 0;
    if (skippedArchives) *skippedArchives = 0;
    if (!fs::exists(base)) return index;

    auto addEntry = [&](const std::string& name, const std::string& source,
                        const std::vector<uint8_t>& data) {
        auto vec = vectorsFromInes(data);
        if (!vec) return;
        RomIndexEntry e;
        e.name = name;
        e.source = source;
        e.vec6Hex = toHex(vec->data(), 6);
        e.md5 = inesMd5(data);
        auto page = lastPageFromInes(data);
        if (page) e.pageHex = toHex(page->data(), 256);
        auto hdr = inesHeaderInfo(data);
        if (hdr) { e.mapper = hdr->mapper; e.prg = hdr->prg; e.chr = hdr->chr;
                   e.mirror = hdr->mirror; e.four = hdr->four; }
        index[indexKey(*vec)].push_back(std::move(e));
        ++n;
    };

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             base, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) continue;
        if (!it->is_regular_file(ec)) continue;
        std::string path = it->path().string();
        std::string ext = it->path().extension().string();
        for (auto& c : ext) c = static_cast<char>(tolower(c));

        // Nur relevante Dateitypen melden (nicht jede Textdatei im Ordner).
        bool relevant = (ext == ".nes" || ext == ".zip" || ext == ".7z" ||
                         ext == ".rar" || ext == ".tar" || ext == ".gz");
        if (relevant && progress) {
            // VOR der Verarbeitung: zeigt live, welche Datei gerade dran
            // ist. Bleibt die Anzeige bei einer Datei stehen, blockiert
            // genau die (meist ein grosses/kaputtes Archiv im 7z-Aufruf).
            progress(n, it->path().filename().string());
        }

        try {
            if (ext == ".nes") {
                auto data = readFile(path);
                addEntry(it->path().filename().string(), path, data);
            } else if (ext == ".zip") {
                for (auto& [name, data] : readNesFromZip(path)) {
                    // Python nimmt bei ZIP-Eintraegen nur den Basisnamen
                    // (os.path.basename), da ZIP-interne Pfade Ordner
                    // enthalten koennen.
                    fs::path np(name);
                    addEntry(np.filename().string(), path, data);
                }
            } else if (ext == ".7z" || ext == ".rar" || ext == ".tar" || ext == ".gz") {
                static const std::string sevenZip = findSevenZipExe();
                if (sevenZip.empty()) {
                    if (skippedArchives) ++*skippedArchives;
                    continue; // keine 7-Zip-Installation gefunden
                }
                for (auto& [name, data] : readNesFromArchive(sevenZip, path)) {
                    addEntry(name, path, data);
                }
            }
            // NACH erfolgreicher Verarbeitung: aktualisierte Gesamtzahl.
            if (relevant && progress) progress(n, std::string());
        } catch (const std::exception&) {
            // Eine einzelne kaputte/unlesbare Datei darf die gesamte
            // Indizierung nicht abbrechen -- ueberspringen und weiter.
            if (skippedArchives) ++*skippedArchives;
        } catch (...) {
            if (skippedArchives) ++*skippedArchives;
        }
    }
    return index;
}

void saveIndex(const RomIndexMap& index, const std::string& path) {
    json j = json::object();
    for (auto& [key, entries] : index) {
        json arr = json::array();
        for (auto& e : entries) {
            json je = {{"name", e.name}, {"source", e.source},
                      {"vec6", e.vec6Hex}, {"md5", e.md5},
                      {"page", e.pageHex.empty() ? json(nullptr) : json(e.pageHex)}};
            if (e.mapper >= 0) je["mapper"] = e.mapper;
            if (e.prg >= 0) je["prg"] = e.prg;
            if (e.chr >= 0) je["chr"] = e.chr;
            if (e.mirror >= 0) je["mirror"] = e.mirror;
            if (e.four >= 0) je["four"] = e.four;
            arr.push_back(je);
        }
        j[key] = arr;
    }
    std::ofstream f(path);
    f << j.dump();
}

RomIndexMap loadIndex(const std::string& path) {
    RomIndexMap index;
    std::ifstream f(path);
    if (!f) return index;
    json j;
    try { f >> j; } catch (const std::exception&) { return index; }
    for (auto& [key, arr] : j.items()) {
        std::vector<RomIndexEntry> entries;
        for (auto& je : arr) {
            RomIndexEntry e;
            e.name = je.value("name", "");
            e.source = je.value("source", "");
            e.vec6Hex = je.value("vec6", "");
            e.md5 = je.value("md5", "");
            if (je.contains("page") && !je["page"].is_null()) e.pageHex = je["page"].get<std::string>();
            e.mapper = je.value("mapper", -1);
            e.prg = je.value("prg", -1);
            e.chr = je.value("chr", -1);
            e.mirror = je.value("mirror", -1);
            e.four = je.value("four", -1);
            entries.push_back(std::move(e));
        }
        index[key] = std::move(entries);
    }
    return index;
}

std::pair<std::vector<RomIndexEntry>, int> matchPage(
    const std::vector<RomIndexEntry>& treffer,
    const std::array<uint8_t, 256>& pageBytes,
    const std::array<uint8_t, 256>& seenBits) {
    std::vector<int> positions;
    for (int i = 0; i < 256; ++i) if (seenBits[i]) positions.push_back(i);
    if (positions.size() < 8) return {treffer, 0}; // zu wenig Daten fuer eine Aussage

    std::vector<RomIndexEntry> passend;
    for (auto& t : treffer) {
        if (t.pageHex.empty()) continue;
        auto roh = fromHex(t.pageHex);
        if (roh.size() != 256) continue;
        bool ok = true;
        for (int i : positions) if (roh[i] != pageBytes[i]) { ok = false; break; }
        if (ok) passend.push_back(t);
    }
    if (!passend.empty()) return {passend, static_cast<int>(positions.size())};
    return {treffer, static_cast<int>(positions.size())};
}

std::vector<RomIndexEntry> lookup(const RomIndexMap& index, const std::array<uint8_t, 6>& vec6) {
    auto it = index.find(indexKey(vec6));
    if (it == index.end()) return {};
    std::vector<RomIndexEntry> treffer = it->second;
    // Wurde auch der IRQ-Vektor gelesen (Bytes 4-5 nicht 0x00/0xFF), damit
    // weiter eingrenzen -- wie im Original.
    bool irqRead = !((vec6[4] == 0x00 && vec6[5] == 0x00) ||
                     (vec6[4] == 0xFF && vec6[5] == 0xFF));
    if (treffer.size() > 1 && irqRead) {
        std::string fullHex = toHex(vec6.data(), 6);
        std::vector<RomIndexEntry> genau;
        for (auto& t : treffer) if (t.vec6Hex == fullHex) genau.push_back(t);
        if (!genau.empty()) return genau;
    }
    return treffer;
}

std::vector<uint8_t> extractSingleNesFromArchive(const std::string& archivePath,
                                                  std::string* outError) {
    auto fail = [&](const std::string& msg) {
        if (outError) *outError = msg;
        return std::vector<uint8_t>{};
    };

    std::string ext = fs::path(archivePath).extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(c));

    std::vector<std::pair<std::string, std::vector<uint8_t>>> found;
    if (ext == ".zip") {
        found = readNesFromZip(archivePath);
    } else if (ext == ".7z" || ext == ".rar" || ext == ".tar" || ext == ".gz") {
        std::string sevenZip = findSevenZipExe();
        if (sevenZip.empty()) {
            return fail("keine 7-Zip-Installation gefunden (https://www.7-zip.org/)");
        }
        std::string sevenZipLog;
        found = readNesFromArchive(sevenZip, archivePath, &sevenZipLog);
        if (found.empty()) {
            // Echte 7z-Ausgabe mitliefern statt nur "nichts gefunden" --
            // zeigt z.B. Passwortschutz, beschaedigtes Archiv, falsche
            // 7z-Version etc., statt das zu verschweigen.
            std::string trimmed = sevenZipLog;
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'))
                trimmed.pop_back();
            return fail(trimmed.empty() ? "keine .nes-Datei im Archiv gefunden"
                                        : "7-Zip meldet: " + trimmed);
        }
    } else {
        return fail("kein unterstuetztes Archivformat");
    }

    if (found.empty()) return fail("keine .nes-Datei im Archiv gefunden");
    if (found.size() > 1) return fail("mehrere .nes-Dateien im Archiv -- bitte per "
                                      "ROM-Sammlung-Indizierung verwenden");
    return found.front().second;
}

} // namespace rawnes
