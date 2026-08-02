#include "fpga_installer.h"
#include "Md5.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace rawnes {
namespace {

std::string fileHash(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    return md5Hex(data);
}

// std::filesystem::copy_file(..., overwrite_existing) schlaegt unter MinGW
// reproduzierbar mit "File exists" fehl, wenn die Zieldatei bereits da ist
// -- betraf sowohl das Installieren als auch das Wiederherstellen (dort
// scheiterten alle fuenf .RBF gleichzeitig). Deshalb hier nicht auf das
// overwrite-Flag verlassen: Schreibschutz entfernen, Ziel loeschen, dann
// frisch kopieren. Gibt einen leeren String bei Erfolg zurueck, sonst den
// Fehlertext inklusive der beteiligten Pfade.
std::string copyOverwrite(const fs::path& von, const fs::path& nach) {
    std::error_code ec;

    if (!fs::is_regular_file(von, ec)) {
        return "Quelle fehlt: " + von.string();
    }

    if (fs::exists(nach, ec)) {
        // Schreibgeschuetzte Zieldateien (auf SD-Karten nicht unueblich)
        // wuerden sonst schon am remove() scheitern.
        std::error_code pec;
        fs::permissions(nach, fs::perms::owner_write, fs::perm_options::add, pec);
        std::error_code rec;
        fs::remove(nach, rec);
        if (rec) {
            return "Ziel nicht ersetzbar (" + nach.string() + "): " + rec.message();
        }
    }

    std::error_code cec;
    fs::copy_file(von, nach, cec);
    if (cec) {
        return cec.message() + " (" + von.string() + " -> " + nach.string() + ")";
    }

    // Zeitstempel der Quelle uebernehmen -- entspricht Pythons shutil.copy2()
    // im Original. fs::copy_file() garantiert das nicht, wodurch eine korrekt
    // wiederhergestellte Datei im Explorer das heutige Datum zeigte und wie
    // ein fehlgeschlagener Restore aussah. Schlaegt das fehl, ist die Datei
    // trotzdem korrekt kopiert -- daher kein Fehler, nur ignoriert.
    std::error_code tec;
    auto zeit = fs::last_write_time(von, tec);
    if (!tec) {
        std::error_code sec;
        fs::last_write_time(nach, zeit, sec);
    }
    return {};
}

} // namespace

const std::vector<std::string>& FpgaInstaller::coreFiles() {
    static const std::vector<std::string> files = {
        "000.RBF", "001.RBF", "004.RBF", "005.RBF", "021.RBF"};
    return files;
}

FpgaInstaller::FpgaInstaller(std::string coresDir) : coresDir_(std::move(coresDir)) {}

bool FpgaInstaller::coresAvailable() const {
    if (!fs::is_directory(coresDir_)) return false;
    for (const auto& f : coreFiles()) {
        if (!fs::is_regular_file(fs::path(coresDir_) / f)) return false;
    }
    return true;
}

std::string FpgaInstaller::mapsDirFromSd(const std::string& sdRoot) {
    fs::path edn8 = fs::path(sdRoot) / "EDN8";
    if (!fs::is_directory(edn8)) return {};
    fs::path maps = edn8 / "MAPS";
    if (!fs::is_directory(maps)) return {};
    return maps.string();
}

std::vector<std::string> FpgaInstaller::coresNeedInstall(const std::string& mapsDir) const {
    std::vector<std::string> noetig;
    for (const auto& f : coreFiles()) {
        fs::path quelle = fs::path(coresDir_) / f;
        fs::path ziel = fs::path(mapsDir) / f;
        if (!fs::is_regular_file(ziel)) {
            noetig.push_back(f);
        } else if (fileHash(quelle.string()) != fileHash(ziel.string())) {
            noetig.push_back(f);
        }
    }
    return noetig;
}

std::vector<std::string> FpgaInstaller::filesToBackup(
    const std::string& mapsDir, const std::vector<std::string>& files) const {
    fs::path backup = fs::path(mapsDir) / kBackupSubdir;
    std::vector<std::string> result;
    for (const auto& f : files) {
        bool existsOnSd = fs::is_regular_file(fs::path(mapsDir) / f);
        bool alreadyBackedUp = fs::is_regular_file(backup / f);
        if (existsOnSd && !alreadyBackedUp) result.push_back(f);
    }
    return result;
}

FpgaInstaller::InstallResult FpgaInstaller::installCores(
    const std::string& mapsDir, const std::vector<std::string>& files) {
    InstallResult res;
    fs::path backup = fs::path(mapsDir) / kBackupSubdir;
    std::error_code ec;
    fs::create_directories(backup, ec);

    // Sichern -- NUR wenn eine Datei da ist und noch kein Backup existiert
    // (das erste Backup ist das echte Krikzz-Original, wird nie mit einer
    // bereits installierten Snooper-Version überschrieben).
    for (const auto& f : files) {
        fs::path zielOrig = fs::path(mapsDir) / f;
        fs::path backupZiel = backup / f;
        if (fs::is_regular_file(zielOrig) && !fs::is_regular_file(backupZiel)) {
            std::string err = copyOverwrite(zielOrig, backupZiel);
            if (err.empty()) {
                ++res.backedUp;
            } else {
                res.errors.push_back("Backup " + f + ": " + err);
            }
        }
    }

    for (const auto& f : files) {
        std::string err = copyOverwrite(fs::path(coresDir_) / f, fs::path(mapsDir) / f);
        if (err.empty()) {
            ++res.installed;
        } else {
            res.errors.push_back(f + ": " + err);
        }
    }
    return res;
}

std::vector<std::string> FpgaInstaller::backedUpFilesAvailable(const std::string& mapsDir) const {
    fs::path backup = fs::path(mapsDir) / kBackupSubdir;
    std::vector<std::string> vorhanden;
    for (const auto& f : coreFiles()) {
        if (fs::is_regular_file(backup / f)) vorhanden.push_back(f);
    }
    return vorhanden;
}

FpgaInstaller::RestoreResult FpgaInstaller::restoreOriginals(
    const std::string& mapsDir, const std::vector<std::string>& files) {
    RestoreResult res;
    fs::path backup = fs::path(mapsDir) / kBackupSubdir;
    for (const auto& f : files) {
        std::string err = copyOverwrite(backup / f, fs::path(mapsDir) / f);
        if (err.empty()) {
            ++res.restored;
        } else {
            res.errors.push_back(f + ": " + err);
        }
    }
    return res;
}

bool FpgaInstaller::hasBackup(const std::string& mapsDir) const {
    return !backedUpFilesAvailable(mapsDir).empty();
}

} // namespace rawnes
