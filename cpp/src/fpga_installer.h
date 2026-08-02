#pragma once
#include <string>
#include <vector>

namespace rawnes {

// Reine Logik (kein Qt), 1:1 aus rawnes_gui_v3_final.py portiert
// (_cores_available, _cores_need_install, _maps_dir_from_sd,
// _install_cores, _restore_originals, _file_hash). UI-Dialoge (SD-
// Ordner wählen, Bestätigung) leben separat in einer Qt-Schicht
// (fpga_installer_panel.h/cpp), damit diese Klasse ohne Qt-SDK
// testbar bleibt und gegen Python cross-verifiziert werden kann.
class FpgaInstaller {
public:
    // {"000.RBF", "001.RBF", "004.RBF", "005.RBF", "021.RBF"}
    static const std::vector<std::string>& coreFiles();
    static constexpr const char* kBackupSubdir = "ORIG_BACKUP";

    explicit FpgaInstaller(std::string coresDir);

    // Sind alle CORE_FILES im coresDir (RELEASE\cores\) vorhanden?
    bool coresAvailable() const;

    // EDN8/MAPS-Pfad aus einem SD-Wurzelpfad. Leerer String, wenn kein
    // EDN8-Ordner bzw. kein MAPS-Unterordner existiert.
    static std::string mapsDirFromSd(const std::string& sdRoot);

    // Dateien, die installiert werden müssen (fehlen auf der SD oder
    // MD5-Hash weicht vom coresDir ab).
    std::vector<std::string> coresNeedInstall(const std::string& mapsDir) const;

    // Welche der zu installierenden Dateien würden beim Install
    // gesichert (existieren auf der SD, aber noch kein Backup vorhanden)?
    std::vector<std::string> filesToBackup(const std::string& mapsDir,
                                           const std::vector<std::string>& files) const;

    struct InstallResult {
        int backedUp = 0;
        int installed = 0;
        std::vector<std::string> errors; // "Dateiname: Fehlertext"
    };
    // Sichert zuerst (nur falls noch kein Backup existiert -- das erste
    // Backup ist das echte Krikzz-Original, wird nie überschrieben),
    // kopiert dann die neuen Dateien aus coresDir auf die SD.
    InstallResult installCores(const std::string& mapsDir, const std::vector<std::string>& files);

    // Welche Backup-Dateien sind tatsächlich vorhanden (wiederherstellbar)?
    std::vector<std::string> backedUpFilesAvailable(const std::string& mapsDir) const;

    struct RestoreResult {
        int restored = 0;
        std::vector<std::string> errors;
    };
    RestoreResult restoreOriginals(const std::string& mapsDir,
                                   const std::vector<std::string>& files);

    bool hasBackup(const std::string& mapsDir) const;

private:
    std::string coresDir_;
};

} // namespace rawnes
