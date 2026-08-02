#include "fpga_installer_panel.h"
#include "colors.h"

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>

using rawnes::FpgaInstaller;

FpgaInstallerPanel::FpgaInstallerPanel(QString coresDir, QWidget* parent)
    : QWidget(parent), coresDir_(std::move(coresDir)),
      installer_(std::make_unique<FpgaInstaller>(coresDir_.toStdString())) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    installBtn_ = new QPushButton(tr("FPGA-Mapper einrichten..."), this);
    restoreBtn_ = new QPushButton(tr("Originale wiederherstellen"), this);
    restoreBtn_->setEnabled(false);
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QString("color: %1; font-size: 10px;").arg(RawnesColors::kTextMuted.name()));

    layout->addWidget(installBtn_);
    layout->addWidget(restoreBtn_);
    layout->addWidget(statusLabel_, 1);

    connect(installBtn_, &QPushButton::clicked, this, &FpgaInstallerPanel::onInstallClicked);
    connect(restoreBtn_, &QPushButton::clicked, this, &FpgaInstallerPanel::onRestoreClicked);
}

std::optional<QString> FpgaInstallerPanel::pickSdCard() {
    // Titel nennt ausdruecklich das Erkennungsmerkmal (Ordner EDN8), damit
    // klar ist, dass die Laufwerkswurzel der EverDrive-Karte gemeint ist --
    // "SD-Karte waehlen" allein liess offen, welcher Ordner erwartet wird.
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("EverDrive-SD-Karte waehlen (Laufwerk mit dem Ordner EDN8)"));
    if (dir.isEmpty()) return std::nullopt;
    return dir;
}

void FpgaInstallerPanel::setCoresDir(const QString& dir) {
    if (dir.isEmpty()) return;
    coresDir_ = dir;
    installer_ = std::make_unique<FpgaInstaller>(coresDir_.toStdString());
}

void FpgaInstallerPanel::setSdRoot(const QString& sdRoot) {
    rememberedSd_ = sdRoot;
    updateRestoreButtonState();
}

void FpgaInstallerPanel::applySdRoot(const QString& sdRoot) {
    rememberedSd_ = sdRoot;
    updateRestoreButtonState();
    emit sdRootChanged(sdRoot);
}

// Frueher brach checkCores() hier einfach mit "Cores-Ordner unvollstaendig"
// ab. Der Pfad kommt aus applicationDirPath()+"/cores"; laeuft die .exe aus
// dem Build-Ordner statt aus RELEASE\, existiert er dort nicht -- und es gab
// keinerlei Moeglichkeit, den richtigen Ordner anzugeben. Jetzt: konkret
// benennen, was fehlt, und manuelle Auswahl anbieten (wird von MainWindow
// in der Config gespeichert).
bool FpgaInstallerPanel::ensureCoresDir(bool aktiv) {
    if (installer_->coresAvailable()) return true;
    if (!aktiv) return false;

    QString detail;
    if (!QDir(coresDir_).exists()) {
        detail = tr("    (Ordner existiert nicht)\n");
    } else {
        for (const auto& f : FpgaInstaller::coreFiles()) {
            QString name = QString::fromStdString(f);
            if (!QFileInfo::exists(QDir(coresDir_).filePath(name))) {
                detail += "    " + name + "\n";
            }
        }
    }

    if (QMessageBox::question(this, tr("FPGA-Mapper"),
            tr("Cores-Ordner nicht gefunden oder unvollstaendig:\n%1\n\n"
               "Fehlt:\n%2\nOrdner jetzt manuell waehlen?").arg(coresDir_, detail))
        != QMessageBox::Yes) {
        emit log(tr("Cores-Ordner nicht gesetzt -- abgebrochen.\n"));
        return false;
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("cores-Ordner waehlen"), coresDir_);
    if (dir.isEmpty()) return false;
    setCoresDir(dir);
    if (!installer_->coresAvailable()) {
        QMessageBox::warning(this, tr("FPGA-Mapper"),
            tr("In %1 fehlen weiterhin Mapper-Dateien.").arg(dir));
        return false;
    }
    emit coresDirChanged(coresDir_);
    emit log(tr("Cores-Ordner gesetzt: %1\n").arg(coresDir_));
    return true;
}

// Robuster als ein einzelner mapsDirFromSd()-Aufruf: falls der Nutzer
// direkt den EDN8-Ordner waehlt statt der SD-Kartenwurzel (leicht zu
// verwechseln -- der Dialog heisst "SD-Karte waehlen", aber der sichtbare
// Ordner beim Durchklicken ist meist EDN8), haengt mapsDirFromSd() das
// EDN8-Muster nochmal an ("...\EDN8\EDN8\MAPS") und findet nichts.
// Hier wird bei leerem Ergebnis automatisch der uebergeordnete Ordner
// probiert, bevor aufgegeben wird. Gibt bei Erfolg (mapsPath, echteWurzel)
// zurueck -- echteWurzel wird gemerkt, nicht der EDN8-Unterordner, damit
// kuenftige Aufrufe wieder korrekt funktionieren.
std::pair<std::string, QString> FpgaInstallerPanel::resolveMapsDir(const QString& picked) {
    std::string maps = FpgaInstaller::mapsDirFromSd(picked.toStdString());
    if (!maps.empty()) return {maps, picked};

    QDir d(picked);
    if (d.cdUp()) {
        QString parent = d.absolutePath();
        std::string mapsFromParent = FpgaInstaller::mapsDirFromSd(parent.toStdString());
        if (!mapsFromParent.empty()) {
            emit log(tr("Hinweis: %1 sah aus wie der EDN8-Ordner selbst -- "
                       "automatisch eine Ebene hoeher gesucht.\n").arg(picked));
            return {mapsFromParent, parent};
        }
    }
    return {"", picked};
}

void FpgaInstallerPanel::updateRestoreButtonState() {
    if (rememberedSd_.isEmpty()) {
        restoreBtn_->setEnabled(false);
        return;
    }
    std::string maps = FpgaInstaller::mapsDirFromSd(rememberedSd_.toStdString());
    restoreBtn_->setEnabled(!maps.empty() && installer_->hasBackup(maps));
}

void FpgaInstallerPanel::checkCores(bool aktiv) {
    if (!ensureCoresDir(aktiv)) return;

    std::string maps;
    if (!rememberedSd_.isEmpty()) maps = FpgaInstaller::mapsDirFromSd(rememberedSd_.toStdString());

    if (maps.empty()) {
        if (!aktiv) return; // beim stillen Start-Check: SD nicht da, einfach nichts tun
        if (QMessageBox::question(this, tr("FPGA-Mapper"),
                tr("Es ist noch keine EverDrive-SD-Karte hinterlegt.\n\n"
                   "Jetzt das Laufwerk mit dem Ordner EDN8 auswaehlen "
                   "(z.B. H:\\) und die Mapper einrichten?")) !=
            QMessageBox::Yes) {
            emit log(tr("Einrichtung übersprungen.\n"));
            return;
        }
        auto sd = pickSdCard();
        if (!sd) {
            emit log(tr("Keine SD-Karte gewählt -- abgebrochen.\n"));
            return;
        }
        QString echteWurzel;
        std::tie(maps, echteWurzel) = resolveMapsDir(*sd);
        if (maps.empty()) {
            emit log(tr("Kein EDN8-Ordner in %1 gefunden.\n").arg(*sd));
            QMessageBox::warning(this, tr("FPGA-Mapper"),
                tr("Kein EDN8-Ordner auf der gewählten Karte gefunden."));
            return;
        }
        applySdRoot(echteWurzel);
        emit log(tr("SD-Karte erkannt: %1\n").arg(echteWurzel));
    }

    auto noetig = installer_->coresNeedInstall(maps);
    if (noetig.empty()) {
        emit log(tr("FPGA-Mapper sind aktuell.\n"));
        if (aktiv) {
            QMessageBox::information(this, tr("FPGA-Mapper"), tr("Alle Mapper sind bereits aktuell."));
        }
        updateRestoreButtonState();
        return;
    }

    doInstall(QString::fromStdString(maps), noetig);
}

void FpgaInstallerPanel::doInstall(const QString& mapsDir, const std::vector<std::string>& files) {
    std::string mapsStd = mapsDir.toStdString();
    auto wirdGesichert = installer_->filesToBackup(mapsStd, files);

    // Bestaetigungsdialog, entspricht _confirm_install() in Python.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("FPGA-Mapper"));
    auto* dlgLayout = new QVBoxLayout(&dlg);

    QString text = tr("Ziel: %1\n\n").arg(mapsDir);
    text += tr("Diese %1 FPGA-Mapper werden installiert:\n").arg(files.size());
    for (const auto& f : files) text += "    " + QString::fromStdString(f) + "\n";
    text += "\n";
    if (!wirdGesichert.empty()) {
        text += tr("Vorhandene Originale werden zuerst gesichert (nach ORIG_BACKUP):\n");
        for (const auto& f : wirdGesichert) text += "    " + QString::fromStdString(f) + "\n";
        text += "\n" + tr("Über \"Originale wiederherstellen\" jederzeit rückgängig zu machen.\n");
    } else {
        text += tr("Kein neues Backup nötig (bereits vorhanden oder Dateien fehlten auf der SD).\n");
    }

    auto* textEdit = new QTextEdit(&dlg);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    dlgLayout->addWidget(textEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dlg);
    dlgLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.resize(500, 400);

    if (dlg.exec() != QDialog::Accepted) {
        emit log(tr("FPGA-Mapper-Installation abgebrochen.\n"));
        return;
    }

    auto result = installer_->installCores(mapsStd, files);
    if (result.backedUp > 0) {
        emit log(tr("%1 Original-Mapper gesichert (ORIG_BACKUP).\n").arg(result.backedUp));
    }
    // Nur melden, was wirklich durchging -- vorher wurde jede Datei als
    // installiert geloggt, auch wenn das Kopieren fehlschlug.
    if (result.errors.empty()) {
        for (const auto& f : files) {
            emit log(tr("  FPGA-Mapper installiert: %1\n").arg(QString::fromStdString(f)));
        }
    } else {
        for (const auto& e : result.errors) {
            emit log(tr("  FEHLER: %1\n").arg(QString::fromStdString(e)));
        }
    }
    if (!result.errors.empty()) {
        QString errText;
        for (const auto& e : result.errors) errText += QString::fromStdString(e) + "\n";
        QMessageBox::critical(this, tr("FPGA-Mapper"), tr("Fehler beim Installieren:\n%1").arg(errText));
    } else {
        QMessageBox::information(this, tr("FPGA-Mapper"),
            tr("%1 FPGA-Mapper erfolgreich installiert.").arg(result.installed));
    }
    emit log(tr("%1 FPGA-Mapper installiert.\n").arg(result.installed));
    updateRestoreButtonState();
}

void FpgaInstallerPanel::onInstallClicked() {
    checkCores(true);
}

void FpgaInstallerPanel::onRestoreClicked() {
    std::string maps;
    if (!rememberedSd_.isEmpty()) maps = FpgaInstaller::mapsDirFromSd(rememberedSd_.toStdString());

    if (maps.empty()) {
        QMessageBox::information(this, tr("Original-Mapper wiederherstellen"),
            tr("Es ist noch keine EverDrive-SD-Karte hinterlegt.\n\n"
               "Bitte im naechsten Fenster das Laufwerk der EverDrive-SD-Karte "
               "auswaehlen -- also das Laufwerk, das den Ordner EDN8 enthaelt "
               "(z.B. H:\\). Die Auswahl wird gespeichert und beim naechsten "
               "Mal nicht mehr abgefragt."));
        auto sd = pickSdCard();
        if (!sd) return;
        QString echteWurzel;
        std::tie(maps, echteWurzel) = resolveMapsDir(*sd);
        if (maps.empty()) {
            QMessageBox::warning(this, tr("FPGA-Mapper"), tr("Kein EDN8-Ordner gefunden."));
            return;
        }
        applySdRoot(echteWurzel);
    }

    auto vorhanden = installer_->backedUpFilesAvailable(maps);
    if (vorhanden.empty()) {
        QMessageBox::information(this, tr("FPGA-Mapper"), tr("Kein Backup vorhanden."));
        return;
    }
    if (QMessageBox::question(this, tr("FPGA-Mapper"),
            tr("%1 Original-Mapper wiederherstellen?").arg(vorhanden.size())) != QMessageBox::Yes) {
        return;
    }

    auto result = installer_->restoreOriginals(maps, vorhanden);
    if (result.errors.empty()) {
        for (const auto& f : vorhanden) {
            emit log(tr("  Original wiederhergestellt: %1\n").arg(QString::fromStdString(f)));
        }
    } else {
        for (const auto& e : result.errors) {
            emit log(tr("  FEHLER: %1\n").arg(QString::fromStdString(e)));
        }
    }
    if (!result.errors.empty()) {
        QString errText;
        for (const auto& e : result.errors) errText += QString::fromStdString(e) + "\n";
        QMessageBox::critical(this, tr("FPGA-Mapper"), tr("Fehler:\n%1").arg(errText));
    } else {
        QMessageBox::information(this, tr("FPGA-Mapper"),
            tr("%1 Original-Mapper wiederhergestellt.").arg(result.restored));
    }
    emit log(tr("%1 Originale wiederhergestellt.\n").arg(result.restored));
}
