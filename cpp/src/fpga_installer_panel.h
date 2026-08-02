#pragma once
#include "fpga_installer.h"
#include <QWidget>
#include <memory>
#include <optional>
#include <utility>

class QPushButton;
class QLabel;

// Qt-Dialoge (SD-Ordner wählen, Bestätigung) um die reine FpgaInstaller-
// Logik herum. Entspricht _check_cores_on_sd/_confirm_install/
// _install_cores/_restore_originals/_update_restore_btn in der Python-GUI.
class FpgaInstallerPanel : public QWidget {
    Q_OBJECT
public:
    explicit FpgaInstallerPanel(QString coresDir, QWidget* parent = nullptr);

    // Prüft beim Start still (aktiv=false: nur melden, wenn wirklich was
    // fehlt) oder mit vollem Feedback, wenn der Nutzer den Knopf klickt
    // (aktiv=true). Entspricht _check_cores_on_sd(aktiv=...).
    void checkCores(bool aktiv);

    // --- Persistenz-Anbindung (MainWindow speichert beides in der Config) ---
    // Ohne diese wurde der SD-Pfad nur im laufenden Programm gemerkt: nach
    // einem Neustart war rememberedSd_ leer, updateRestoreButtonState()
    // liess den Wiederherstellen-Knopf deaktiviert, und der eingebaute
    // SD-Auswahl-Fallback in onRestoreClicked() war dadurch unerreichbar.
    void setCoresDir(const QString& dir);
    QString coresDir() const { return coresDir_; }
    void setSdRoot(const QString& sdRoot);
    QString sdRoot() const { return rememberedSd_; }

public slots:
    // Oeffentlich, damit "Optionen -> Original-Mapper wiederherstellen"
    // die Funktion direkt aufrufen kann. Das Panel selbst ist im
    // Hauptfenster ausgeblendet (fpgaPanel_->hide()), seine Knoepfe sind
    // also unsichtbar -- vorher gab es damit ueberhaupt keinen Weg hierher.
    void onRestoreClicked();

signals:
    void log(const QString& msg);
    void sdRootChanged(const QString& sdRoot);
    void coresDirChanged(const QString& dir);

private slots:
    void onInstallClicked();

private:
    std::optional<QString> pickSdCard();
    // Siehe .cpp-Kommentar: Fallback auf den uebergeordneten Ordner, falls
    // der Nutzer den EDN8-Ordner statt der SD-Kartenwurzel waehlt.
    std::pair<std::string, QString> resolveMapsDir(const QString& picked);
    void updateRestoreButtonState();
    // Prueft den Cores-Ordner; bei aktiv=true darf der Nutzer ihn manuell
    // waehlen, statt nur eine Fehlermeldung zu bekommen.
    bool ensureCoresDir(bool aktiv);
    void applySdRoot(const QString& sdRoot); // setzt + meldet per Signal
    void doInstall(const QString& mapsDir, const std::vector<std::string>& files);

    QString coresDir_;
    std::unique_ptr<rawnes::FpgaInstaller> installer_;
    QString rememberedSd_; // entspricht self._sd_root in Python

    QPushButton* installBtn_;
    QPushButton* restoreBtn_;
    QLabel* statusLabel_;
};
