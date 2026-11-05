#pragma once
#include "ra_cache.h"
#include "ra_client.h"
#include "RomIndex.h"
#include <QMainWindow>
#include <memory>
#include <optional>

class QLineEdit;
class QPushButton;
class QLabel;
class QThread;
class QVBoxLayout;
class AchievementListWidget;
class HardcorePanel;
class LogPanel;
class LeaderboardPanel;
class LiveValuesWidget;
class QCheckBox;
class FpgaInstallerPanel;
class MonitorWorker;

// WICHTIG (Architekturabgleich gegen rawnes_gui_v3_final.py):
// - Verbindung zum EverDrive passiert EINMAL automatisch kurz nach dem
//   Start (entspricht self.after(200, self._connect)), unabhaengig vom
//   Spielstart. "Erneut verbinden" ist nur ein manueller Retry.
// - Es gibt EINEN Start-Knopf ("Spiel laden & Monitor starten"), keinen
//   separaten "ROM waehlen"-Knopf als Vorbedingung. Der Knopf loest im
//   Worker zuerst automatische Erkennung per Live-Vektor aus; nur wenn
//   die nichts findet, zeigt MainWindow einen Dateidialog (Fallback).
// - Leaderboards: Python hat dafuer keine eigene UI (nur Log-Zeilen),
//   RAW-NES-cpp behaelt das LeaderboardPanel trotzdem als bewusste
//   Zusatzfunktion, nicht als 1:1-Portierung.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onLogin();
    void onIndexRoms();
    void onToggleMonitor();
    void onReconnect();
    void onAboutCat();
    void onFpgaSetupFromMenu();
    void onFpgaRestoreFromMenu();
    void onOpenSupport();

    void onWorkerLog(const QString& msg);
    void onWorkerConnected(const QString& port);
    void onWorkerConnectionFailed(const QString& reason);
    void onWorkerRomNotDetected();
    void onWorkerRomAmbiguous(QString hexKey, QStringList labels, QStringList sources);
    void onWorkerGameLoaded(const QString& name);
    void onWorkerUnlocked(const QString& title, int points, qlonglong achId, bool hardcore);
    void onWorkerProgress(qlonglong achId, int current, int target);
    void onWorkerConnectionLost(const QString& reason);
    void onWorkerHardcoreDowngraded(const QString& liste);
    void onWorkerFinished();
    void onWorkerRememberedChanged(QString hexKey, QString source);

private:
    void buildUi();
    void appendLog(const QString& msg);
    void updateIndexLabel();
    QString romIndexPath() const;
    QString configPath() const;
    void loadConfig();
    void saveConfig();
    void setLanguage(const QString& lang);
    // Merkt fuer jedes Widget seinen deutschen Quelltext als dynamische
    // Eigenschaft; retranslateAll() setzt daraus die Texte neu. Damit ist
    // ein Sprachwechsel ohne Programm-Neustart moeglich.
    void rememberSourceTexts();
    void retranslateAll();
    void toggleLbSidebar(bool open);

    RaCache cache_;
    std::shared_ptr<RaClient> client_;
    QString user_;
    QString token_;
    QString language_ = "de";
    std::shared_ptr<rawnes::RomIndexMap> romIndex_;
    QMap<QString, QString> remembered_; // hexKey -> Quellpfad, fuer Config-Speicherung
    bool coresDirManual_ = false; // cores-Ordner stammt aus der Config/Dialog
    bool connected_ = false;
    bool monitoring_ = false;

    // --- Widgets ---
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passEdit_ = nullptr;
    QPushButton* loginBtn_ = nullptr;
    QLabel* loginStatus_ = nullptr;

    QLabel* gameStatus_ = nullptr;
    QPushButton* indexBtn_ = nullptr;
    QLabel* indexStatusLabel_ = nullptr;

    HardcorePanel* hardcorePanel_ = nullptr;
    AchievementListWidget* achList_ = nullptr;
    LeaderboardPanel* lbPanel_ = nullptr;
    QWidget* lbSidebar_ = nullptr;      // Container rechts am Hauptfenster
    QPushButton* lbHandle_ = nullptr;   // schmaler Griff zum Ein-/Ausklappen
    QLabel* subtitleLbl_ = nullptr;     // enthaelt die Buildnummer -> Sonderfall
    bool lbSidebarOpen_ = false;
    class AchievementPopup* achPopup_ = nullptr;
    QCheckBox* rememberLogin_ = nullptr;  // "Eingeloggt bleiben"
    LiveValuesWidget* liveValues_ = nullptr;
    FpgaInstallerPanel* fpgaPanel_ = nullptr;
    LogPanel* log_ = nullptr;

    QPushButton* monitorBtn_ = nullptr;
    QLabel* connStatus_ = nullptr;
    QPushButton* reconnectBtn_ = nullptr;

    // --- Worker-Thread: EINMAL erzeugt, lebt fuer die App-Laufzeit ---
    QThread* monThread_ = nullptr;
    MonitorWorker* monWorker_ = nullptr;
};
