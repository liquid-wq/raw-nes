#include "mainwindow.h"
#include "achievement_list_widget.h"
#include "achievement_popup.h"
#include "colors.h"
#include "fpga_installer_panel.h"
#include "hardcore_panel.h"
#include "i18n.h"
#include "leaderboard_panel.h"
#include "live_values_widget.h"
#include "log_panel.h"
#include "monitor_worker.h"
#include "ra_network.h"
#include "version.h"

#include <QDesktopServices>
#include <QDialog>
#include <QGroupBox>
#include <QAction>
#include <QCheckBox>
#include <QMenu>
#include <QScrollArea>
#include <QTextEdit>
#include <QUrl>

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QDir>

namespace {
constexpr int kLbSidebarWidth = 300;

// Der cores-Ordner lag bisher fest auf applicationDirPath()+"/cores".
// Startet die .exe aus dem Build-Ordner (build3\) statt aus RELEASE\,
// existiert er dort nicht -> "FPGA-Mapper einrichten" brach sofort ab.
// Jetzt werden die ueblichen Ablagen der Reihe nach geprueft; findet sich
// keine, bleibt der alte Standardpfad als Anzeigewert (der Nutzer kann den
// Ordner dann im Dialog manuell waehlen, das Ergebnis landet in der Config).
QString defaultCoresDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList kandidaten = {
        appDir + "/cores",
        appDir + "/../cores",
        appDir + "/../../cores",
        appDir + "/../RELEASE/cores",
        appDir + "/../../RELEASE/cores",
        appDir + "/../../../RELEASE/cores",
    };
    for (const QString& k : kandidaten) {
        QDir d(k);
        if (d.exists()) return d.absolutePath();
    }
    return appDir + "/cores";
}
} // namespace
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      cache_((QCoreApplication::applicationDirPath() + "/ra_cache_nes.json").toStdString()),
      client_(std::make_shared<RaClient>(cache_, make_qt_request_fn())) {
    // Worker EINMAL erzeugen und in seinen eigenen Thread verschieben --
    // lebt fuer die gesamte App-Laufzeit (siehe Begruendung in
    // monitor_worker.h: EdSerial/QSerialPort duerfen nur aus dem Thread
    // heraus benutzt werden, in dem sie erzeugt wurden).
    monThread_ = new QThread(this);
    monWorker_ = new MonitorWorker(client_);
    monWorker_->moveToThread(monThread_);
    connect(monThread_, &QThread::finished, monWorker_, &QObject::deleteLater);

    connect(monWorker_, &MonitorWorker::log, this, &MainWindow::onWorkerLog);
    connect(monWorker_, &MonitorWorker::connected, this, &MainWindow::onWorkerConnected);
    connect(monWorker_, &MonitorWorker::connectionFailed, this, &MainWindow::onWorkerConnectionFailed);
    connect(monWorker_, &MonitorWorker::connectAttempt, this, [this](int attempt, int total) {
        connStatus_->setText(tr("verbinde... (Versuch %1/%2)").arg(attempt).arg(total));
    });
    connect(monWorker_, &MonitorWorker::romNotDetected, this, &MainWindow::onWorkerRomNotDetected);
    connect(monWorker_, &MonitorWorker::romAmbiguous, this, &MainWindow::onWorkerRomAmbiguous);
    connect(monWorker_, &MonitorWorker::gameLoaded, this, &MainWindow::onWorkerGameLoaded);
    connect(monWorker_, &MonitorWorker::achievementsReady, this,
           [this](GameData game, std::set<long long> alreadyUnlocked) {
        achList_->setGame(game, alreadyUnlocked);
    });
    connect(monWorker_, &MonitorWorker::unlocked, this, &MainWindow::onWorkerUnlocked);
    connect(monWorker_, &MonitorWorker::progressChanged, this, &MainWindow::onWorkerProgress);
    connect(monWorker_, &MonitorWorker::achievementWaitingChanged, this,
           [this](qlonglong id, bool waiting) { achList_->setWaiting(id, waiting); });
    connect(monWorker_, &MonitorWorker::connectionLost, this, &MainWindow::onWorkerConnectionLost);
    connect(monWorker_, &MonitorWorker::hardcoreDowngraded, this, &MainWindow::onWorkerHardcoreDowngraded);
    connect(monWorker_, &MonitorWorker::rememberedChanged, this, &MainWindow::onWorkerRememberedChanged);
    connect(monWorker_, &MonitorWorker::leaderboardsReady, this,
           [this](const std::vector<std::pair<long long, QString>>& lbs) {
        lbPanel_->setLeaderboards(lbs);
        if (!lbs.empty()) lbPanel_->setVisiblePanel(true);
    });
    // Leaderboard-Events aktualisieren jetzt das echte Panel (setStatus
    // pro Zeile) UND loggen weiter (Diagnose/Audit-Spur).
    connect(monWorker_, &MonitorWorker::leaderboardStarted, this,
           [this](qlonglong id, const QString& title) {
        lbPanel_->setStatus(id, tr("laeuft"));
        appendLog(tr("Leaderboard laeuft: ") + title);
    });
    connect(monWorker_, &MonitorWorker::leaderboardCanceled, this,
           [this](qlonglong id, const QString& title) {
        lbPanel_->setStatus(id, tr("abgebrochen"));
        appendLog(tr("Leaderboard abgebrochen: ") + title);
    });
    connect(monWorker_, &MonitorWorker::leaderboardSubmitted, this,
           [this](qlonglong id, const QString& title, const QString& value) {
        lbPanel_->setStatus(id, tr("eingereicht: ") + value);
        appendLog(tr("Leaderboard eingereicht: ") + title + " = " + value);
    });
    connect(monWorker_, &MonitorWorker::finished, this, &MainWindow::onWorkerFinished);
    connect(monWorker_, &MonitorWorker::liveValuesInit, this,
           [this](const std::vector<uint16_t>& addrs) {
        liveValues_->clearAll();
        for (size_t i = 0; i < addrs.size(); ++i) liveValues_->addAddress(static_cast<int>(i), addrs[i]);
    });
    connect(monWorker_, &MonitorWorker::liveValueUpdate, this,
           [this](int slot, uchar value, int changeCount) {
        liveValues_->updateValue(slot, value, changeCount);
    });

    monThread_->start();
    buildUi();
    // Quelltexte merken, solange alle Widgets noch ihren Aufbauzustand
    // haben -- danach kann jederzeit zwischen den Sprachen gewechselt
    // werden, ohne das Programm neu zu starten.
    rememberSourceTexts();
    loadConfig();

    // KEIN Auto-Connect mehr beim Start (anders als frueher). MEGA-RAW
    // verbindet erst auf Klick ("Monitor starten"/"Erneut verbinden") --
    // beim Programmstart rauscht die Konsole im Menue noch, ein sofortiger
    // Verbindungsversuch scheitert dann ("nicht gefunden") und laeuft in
    // den 3s-Funkstille-Timeout. Der Nutzer verbindet, wenn die Konsole
    // bereit ist. Status bleibt bis dahin neutral.

    // Gemerkte Mehrdeutigkeits-Entscheidungen aus der Config an den Worker
    // uebergeben (entspricht Pythons self._remembered aus _load_config()).
    if (!remembered_.isEmpty()) {
        QMetaObject::invokeMethod(monWorker_, "seedRemembered", Qt::QueuedConnection,
                                  Q_ARG(QStringList, remembered_.keys()),
                                  Q_ARG(QStringList, remembered_.values()));
    }

    // Auto-Login, wenn Zugangsdaten gespeichert sind (Python: silent=True
    // in _load_config(), unterdrueckt nur die Fehler-Messagebox -- die
    // gibt es bei onLogin() ohnehin nicht, daher unveraendert wiederverwendbar).
    if (!userEdit_->text().isEmpty() && !passEdit_->text().isEmpty()) {
        onLogin();
    }
}

MainWindow::~MainWindow() {
    // stop() DIREKT aufrufen, nicht per QueuedConnection: es setzt nur das
    // std::atomic<bool> running_, braucht also keine Event-Loop -- die
    // Warteschlange des Workers wird aber gar nicht abgearbeitet, solange er
    // in seiner Poll-Schleife (while(running_) ... msleep(50)) steckt. Der
    // per invokeMethod zugestellte Aufruf kam dort nie an, running_ blieb
    // true, wait() lief ab und der Thread wurde laufend zerstoert
    // ("QThread: Destroyed while thread is still running" im error_log).
    // Folge davon: EdSerial wurde nie geschlossen, der COM-Port blieb bis
    // zum Prozessende belegt.
    if (monWorker_) monWorker_->stop();
    if (monThread_) {
        monThread_->quit();
        // Grosszuegiger als vorher: ein laufendes memrd wartet bis zu 2s auf
        // Daten, danach kommt die Schleife erst zur running_-Pruefung.
        if (!monThread_->wait(5000)) {
            // Letzte Rettung beim Programmende -- immer noch besser, als
            // einen laufenden QThread zu zerstoeren.
            monThread_->terminate();
            monThread_->wait(1000);
        }
    }
}

void MainWindow::buildUi() {
    setWindowTitle("RAW-NES Monitor");
    resize(760, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    lbSidebar_ = new QWidget(central);
    auto* lbSideLayout = new QVBoxLayout(lbSidebar_);
    lbSideLayout->setContentsMargins(6, 6, 6, 6);
    lbSidebar_->hide(); // Startzustand, loadConfig() kann ihn ueberschreiben

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // --- Header --- Selektor "#rawnesHeader" scopt den Stylesheet auf
    // dieses Widget allein (sonst vererbt Qt border-bottom/background an
    // jedes Kind-Label -- Ursache gestapelter roter Linien in einer
    // frueheren Fassung).
    auto* header = new QWidget(central);
    header->setObjectName("rawnesHeader");
    header->setStyleSheet(QString("QWidget#rawnesHeader { background: %1; border-bottom: 3px solid %2; }")
                          .arg(RawnesColors::kLight.name(), RawnesColors::kRed.name()));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(14, 8, 14, 10);
    headerLayout->setSpacing(2);

    auto* optRow = new QHBoxLayout();
    optRow->addStretch(1);
    auto* supportBtn = new QPushButton(tr("Ko-fi"), header);
    supportBtn->setFlat(true);
    supportBtn->setStyleSheet(QString("color: %1; background: transparent; border: none; font-size: 11px;")
                              .arg(RawnesColors::kDark.name()));
    connect(supportBtn, &QPushButton::clicked, this, &MainWindow::onOpenSupport);
    optRow->addWidget(supportBtn);

    auto* optBtn = new QPushButton(tr("Optionen"), header);
    optBtn->setStyleSheet(QString(
        "QPushButton { color: %1; background: %2; border: 1px solid %3; padding: 3px 10px; font-size: 11px; }")
        .arg(RawnesColors::kDark.name(), RawnesColors::kLight.name(), RawnesColors::kDark.name()));
    auto* optMenu = new QMenu(optBtn);
    optMenu->addAction(tr("FPGA-Mapper einrichten"), this, &MainWindow::onFpgaSetupFromMenu);
    // Gegenstueck zum Einrichten. Das FpgaInstallerPanel ist im Hauptfenster
    // ausgeblendet, sein "Originale wiederherstellen"-Knopf war damit
    // unsichtbar -- die Funktion existierte im Code, war aber von der
    // Oberflaeche aus ueberhaupt nicht mehr erreichbar.
    optMenu->addAction(tr("Original-Mapper wiederherstellen"), this,
                       &MainWindow::onFpgaRestoreFromMenu);
    optMenu->addSeparator();
    auto* langMenu = optMenu->addMenu(tr("Sprache / Language"));
    auto* actDe = langMenu->addAction("Deutsch", this, [this]() { setLanguage("de"); });
    auto* actEn = langMenu->addAction("English", this, [this]() { setLanguage("en"); });
    actDe->setCheckable(true);
    actEn->setCheckable(true);
    (language_ == "en" ? actEn : actDe)->setChecked(true);
    connect(langMenu, &QMenu::aboutToShow, this, [this, actDe, actEn]() {
        actDe->setChecked(language_ != "en");
        actEn->setChecked(language_ == "en");
    });
    optMenu->addSeparator();
    optMenu->addAction(tr("about the cat"), this, &MainWindow::onAboutCat);
    optBtn->setMenu(optMenu);
    optRow->addWidget(optBtn);
    headerLayout->addLayout(optRow);

    auto* catLabel = new QLabel(header);
    QPixmap catPix(":/intro/cat_portrait.png");
    if (!catPix.isNull()) catLabel->setPixmap(catPix);
    catLabel->setAlignment(Qt::AlignHCenter);
    headerLayout->addWidget(catLabel);

    auto* titleLbl = new QLabel("RAW-NES", header);
    titleLbl->setAlignment(Qt::AlignHCenter);
    titleLbl->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: bold;")
                            .arg(RawnesColors::kRed.name()));
    headerLayout->addWidget(titleLbl);

    // Der angezeigte Text enthaelt die eingesetzte Buildnummer und findet
    // deshalb keinen Treffer in der Tabelle (dort steht das Muster mit %1).
    // Wird darum in retranslateAll() gesondert neu zusammengesetzt.
    subtitleLbl_ = new QLabel(
        tr("— v0.1 build %1 — und immer noch nicht perfekt —").arg(kGuiBuild), header);
    QLabel* subtitleLbl = subtitleLbl_;
    subtitleLbl->setAlignment(Qt::AlignHCenter);
    subtitleLbl->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;")
                               .arg(RawnesColors::kDark.name()));
    headerLayout->addWidget(subtitleLbl);

    // Gleiche Farbe wie die Subtitle-Zeile darueber (kDark) -- vorher
    // kTextMuted, kaum lesbar auf dem hellen Header-Hintergrund.
    auto* copyrightLbl = new QLabel("© 2026 Liqui", header);
    copyrightLbl->setAlignment(Qt::AlignHCenter);
    copyrightLbl->setStyleSheet(QString("color: %1; font-size: 9px;").arg(RawnesColors::kDark.name()));
    headerLayout->addWidget(copyrightLbl);

    auto* quickLbl = new QLabel(
        tr("Schnellstart:  1. Anmelden   2. Spielordner einmal indizieren   3. Spiel am Geraet "
           "starten, dann Monitor starten"), header);
    quickLbl->setAlignment(Qt::AlignHCenter);
    quickLbl->setStyleSheet(QString("color: %1; font-size: 9px;").arg(RawnesColors::kDark.name()));
    headerLayout->addWidget(quickLbl);

    root->addWidget(header);

    // --- Körper --- in einen Scroll-Bereich gepackt: vorher wurden bei
    // vollem Inhalt (viele Gruppen + Log) alle Widgets auf Minimalgroesse
    // gequetscht (Achievements/Live-Werte zeigten nur 1 Zeile) statt zu
    // scrollen. Jetzt bekommt jeder Bereich seine Mindesthoehe garantiert,
    // der Rest scrollt.
    auto* scrollArea = new QScrollArea(central);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QString("QScrollArea { background: %1; border: none; }")
                              .arg(RawnesColors::kDark.name()));
    auto* body = new QWidget(scrollArea);
    body->setStyleSheet(QString("background: %1;").arg(RawnesColors::kDark.name()));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(14, 12, 14, 12);
    bodyLayout->setSpacing(10);
    scrollArea->setWidget(body);
    // --- Mittelteil: Inhalt links, Leaderboard-Sidebar rechts ---
    // Der Griff bleibt immer sichtbar (schmaler senkrechter Streifen), die
    // Sidebar selbst wird ein- und ausgeblendet. Beim Ausklappen waechst
    // das Fenster nach rechts, statt den Inhalt zusammenzuquetschen.
    auto* mid = new QWidget(central);
    auto* midLayout = new QHBoxLayout(mid);
    midLayout->setContentsMargins(0, 0, 0, 0);
    midLayout->setSpacing(0);
    midLayout->addWidget(scrollArea, 1);

    lbHandle_ = new QPushButton("‹", mid);
    lbHandle_->setFixedWidth(18);
    lbHandle_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    lbHandle_->setToolTip(tr("Leaderboards ein-/ausklappen"));
    lbHandle_->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: none; font-weight: bold; }"
        "QPushButton:hover { background: %3; }")
        .arg(RawnesColors::kPanel.name(), RawnesColors::kTextMain.name(),
             RawnesColors::kDark.name()));
    connect(lbHandle_, &QPushButton::clicked, this,
            [this]() { toggleLbSidebar(!lbSidebarOpen_); });
    midLayout->addWidget(lbHandle_, 0);

    lbSidebar_->setParent(mid);
    lbSidebar_->setFixedWidth(kLbSidebarWidth);
    midLayout->addWidget(lbSidebar_, 0);

    root->addWidget(mid, 1);

    const QString groupStyle = QString(
        "QGroupBox { color: %1; border: 1px solid %2; border-radius: 3px; "
        "margin-top: 10px; font-weight: bold; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; } "
        // Native Checkbox-Indikator bei checkbaren Gruppen (Log) ausblenden --
        // die rendert unter dem dunklen Theme abgeschnitten/glitchy. Zustand
        // wird stattdessen ueber ein Pfeil-Praefix im Titeltext angezeigt.
        "QGroupBox::indicator { width: 0px; height: 0px; }")
        .arg(RawnesColors::kTextMuted.name(), RawnesColors::kTextMuted.name());

    // --- RetroAchievements-Gruppe ---
    auto* raGroup = new QGroupBox(tr("RetroAchievements"), body);
    raGroup->setStyleSheet(groupStyle);
    auto* raLayout = new QHBoxLayout(raGroup);
    userEdit_ = new QLineEdit(raGroup);
    userEdit_->setPlaceholderText(tr("RA-Benutzername"));
    passEdit_ = new QLineEdit(raGroup);
    passEdit_->setPlaceholderText(tr("RA-Passwort"));
    passEdit_->setEchoMode(QLineEdit::Password);
    loginBtn_ = new QPushButton(tr("Anmelden"), raGroup);
    loginStatus_ = new QLabel(tr("nicht angemeldet"), raGroup);
    loginStatus_->setStyleSheet(QString("color: %1;").arg(RawnesColors::kTextMuted.name()));
    raLayout->addWidget(userEdit_);
    raLayout->addWidget(passEdit_);
    // Ohne Haken werden die Zugangsdaten beim Beenden NICHT in
    // rawnes_gui_config.json abgelegt (Standard: an, wie bisher).
    rememberLogin_ = new QCheckBox(tr("Eingeloggt bleiben"), raGroup);
    rememberLogin_->setChecked(true);
    rememberLogin_->setToolTip(tr("Benutzername und Passwort in der Konfiguration speichern"));
    connect(rememberLogin_, &QCheckBox::toggled, this, [this](bool) { saveConfig(); });
    raLayout->addWidget(loginBtn_);
    raLayout->addWidget(rememberLogin_);
    raLayout->addWidget(loginStatus_, 1);
    bodyLayout->addWidget(raGroup);
    connect(loginBtn_, &QPushButton::clicked, this, &MainWindow::onLogin);

    // --- EverDrive-Gruppe --- (zurueck an die urspruengliche Position,
    // direkt nach Login -- das ist der Bereich mit Verbinden/Start-Knopf,
    // muss ohne Scrollen sichtbar sein).
    // Kein separater "ROM waehlen"-Knopf mehr (siehe mainwindow.h-Kommentar):
    // ein Start-Knopf loest Auto-Erkennung aus, Dateidialog nur als Fallback.
    auto* edGroup = new QGroupBox(tr("EverDrive"), body);
    edGroup->setStyleSheet(groupStyle);
    auto* edLayout = new QVBoxLayout(edGroup);

    auto* connRow = new QHBoxLayout();
    connStatus_ = new QLabel(tr("bereit — zum Verbinden Monitor starten"), edGroup);
    connStatus_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(RawnesColors::kTextMuted.name()));
    // Kein Auto-Connect beim Start -> beim ersten Mal ist es keine
    // *erneute* Verbindung. Der Knopf heisst deshalb schlicht "Verbinden".
    reconnectBtn_ = new QPushButton(tr("Verbinden"), edGroup);
    connRow->addWidget(connStatus_);
    connRow->addStretch(1);
    connRow->addWidget(reconnectBtn_);
    edLayout->addLayout(connRow);
    connect(reconnectBtn_, &QPushButton::clicked, this, &MainWindow::onReconnect);

    gameStatus_ = new QLabel(tr("kein Spiel geladen"), edGroup);
    gameStatus_->setStyleSheet(QString("color: %1;").arg(RawnesColors::kTextMuted.name()));
    edLayout->addWidget(gameStatus_);

    auto* indexRow = new QHBoxLayout();
    indexBtn_ = new QPushButton(tr("ROM-Sammlung indizieren..."), edGroup);
    indexStatusLabel_ = new QLabel(tr("(nicht indiziert — ROM-Auswahl per Dialog)"), edGroup);
    indexStatusLabel_->setStyleSheet(QString("color: %1;").arg(RawnesColors::kTextMuted.name()));
    indexRow->addWidget(indexBtn_);
    indexRow->addWidget(indexStatusLabel_, 1);
    edLayout->addLayout(indexRow);
    connect(indexBtn_, &QPushButton::clicked, this, &MainWindow::onIndexRoms);

    // FPGA-Mapper-Installer (RELEASE\cores neben der .exe -- entspricht
    // Pythons CORES_DIR = SCRIPT_DIR/cores). NICHT mehr in die Hauptansicht
    // eingebettet -- ist bereits ueber "Optionen -> FPGA-Mapper einrichten"
    // erreichbar (onFpgaSetupFromMenu()), doppelt im Hauptfenster war
    // unnoetiger Platz/Redundanz. Panel bleibt als Objekt bestehen (fuer
    // checkCores()), nur nicht sichtbar/im Layout.
    fpgaPanel_ = new FpgaInstallerPanel(defaultCoresDir(), edGroup);
    fpgaPanel_->hide();
    connect(fpgaPanel_, &FpgaInstallerPanel::log, this, &MainWindow::appendLog);
    // SD-Pfad und (falls manuell gewaehlt) cores-Ordner ueber Neustarts
    // hinweg merken -- sonst war "Originale wiederherstellen" bei jedem
    // Programmstart wieder ohne bekannte SD-Karte.
    connect(fpgaPanel_, &FpgaInstallerPanel::sdRootChanged, this, [this](const QString&) {
        saveConfig();
    });
    connect(fpgaPanel_, &FpgaInstallerPanel::coresDirChanged, this, [this](const QString&) {
        coresDirManual_ = true;
        saveConfig();
    });

    auto* startRow = new QHBoxLayout();
    monitorBtn_ = new QPushButton(tr("Spiel laden & Monitor starten"), edGroup);
    monitorBtn_->setEnabled(false); // erst nach Login + Verbindung
    startRow->addWidget(monitorBtn_);
    startRow->addStretch(1);
    edLayout->addLayout(startRow);
    connect(monitorBtn_, &QPushButton::clicked, this, &MainWindow::onToggleMonitor);

    hardcorePanel_ = new HardcorePanel(edGroup);
    hardcorePanel_->setHint(tr("(erfordert deaktivierte Savestates/Cheats im EverDrive-Menue)"));
    edLayout->addWidget(hardcorePanel_);

    bodyLayout->addWidget(edGroup);

    // --- Achievements-Gruppe --- Mindesthoehe moderat (nur der Scroll-
    // Bereich war der eigentliche Fix gegen "1 Zeile sichtbar"; eine
    // riesige Mindesthoehe erzeugt im leeren Zustand nur eine grosse
    // leere Flaeche).
    auto* achGroup = new QGroupBox(
        tr("Achievements (Klick = Diagnose, Doppelklick = auf RetroAchievements oeffnen)"), body);
    achGroup->setStyleSheet(groupStyle);
    auto* achGroupLayout = new QVBoxLayout(achGroup);
    achList_ = new AchievementListWidget(achGroup);
    achList_->setFixedHeight(186); // exakt ~3 Achievement-Zeilen; Rest scrollt INNERHALB der Liste
    achGroupLayout->addWidget(achList_);
    connect(achList_, &AchievementListWidget::achievementClicked, this,
           [this](qlonglong id) {
        QMetaObject::invokeMethod(monWorker_, "diagnoseAchievement", Qt::QueuedConnection,
                                  Q_ARG(qlonglong, id));
    });
    connect(achList_, &AchievementListWidget::achievementDoubleClicked, this,
           [](qlonglong id) {
        QDesktopServices::openUrl(QUrl(QString("https://retroachievements.org/achievement/%1").arg(id)));
    });
    bodyLayout->addWidget(achGroup, 2);

    // --- Live-Werte-Gruppe ---
    auto* liveGroup = new QGroupBox(tr("Live-Werte"), body);
    liveGroup->setStyleSheet(groupStyle);
    auto* liveGroupLayout = new QVBoxLayout(liveGroup);
    liveValues_ = new LiveValuesWidget(liveGroup);
    liveValues_->setMinimumHeight(84); // schlankes Hex-Label, ca. 3 Zeilen
    liveGroupLayout->addWidget(liveValues_);
    bodyLayout->addWidget(liveGroup, 0);

    // --- Leaderboard-Panel --- Zusatzfunktion ueber Python hinaus,
    // Python selbst hat dafuer keine eigene UI.
    connect(hardcorePanel_, &HardcorePanel::toggled, this, [this](bool checked) {
        if (checked) toggleLbSidebar(true);
    });

    // Liegt jetzt in der Sidebar rechts (siehe unten), nicht mehr im
    // senkrechten Fluss -- bei 37 Eintraegen war es dort nicht benutzbar.
    lbPanel_ = new LeaderboardPanel(lbSidebar_);
    lbPanel_->setToggleButtonVisible(false); // Griff am Rand uebernimmt das
    lbPanel_->setVisiblePanel(true);
    // Panel soll die volle Sidebar-Hoehe fuellen, nicht oben kleben.
    lbPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lbSidebar_->layout()->addWidget(lbPanel_);

    // --- Log --- einklappbar: checkbare Gruppe, standardmaessig ZU.
    // Der Log-Inhalt frisst sonst dauerhaft Hoehe und zwingt zum Scrollen;
    // eingeklappt ist nur die Titelzeile sichtbar, ein Klick oeffnet ihn.
    auto* logGroup = new QGroupBox(tr("▶ Log (aufklappen)"), body);
    logGroup->setStyleSheet(groupStyle);
    logGroup->setCheckable(true);
    logGroup->setChecked(false);
    auto* logGroupLayout = new QVBoxLayout(logGroup);
    logGroupLayout->setContentsMargins(6, 6, 6, 6);
    log_ = new LogPanel(logGroup);
    log_->setMinimumHeight(120);
    log_->setVisible(false);
    logGroupLayout->addWidget(log_);
    connect(logGroup, &QGroupBox::toggled, this, [this, logGroup](bool on) {
        log_->setVisible(on);
        logGroup->setTitle(on ? tr("▼ Log (zuklappen)") : tr("▶ Log (aufklappen)"));
    });
    bodyLayout->addWidget(logGroup);

    appendLog(tr("RAW-NES Monitor bereit."));

    QTimer::singleShot(600, this, [this]() { fpgaPanel_->checkCores(false); });

    {
        auto idx = rawnes::loadIndex(romIndexPath().toStdString());
        if (!idx.empty()) romIndex_ = std::make_shared<rawnes::RomIndexMap>(std::move(idx));
        updateIndexLabel();
    }
}

QString MainWindow::romIndexPath() const {
    return QCoreApplication::applicationDirPath() + "/rawnes_rom_index.json";
}

QString MainWindow::configPath() const {
    return QCoreApplication::applicationDirPath() + "/rawnes_gui_config.json";
}

void MainWindow::loadConfig() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return;
    QJsonObject obj = doc.object();

    userEdit_->setText(obj.value("ra_user").toString());
    passEdit_->setText(obj.value("ra_pass").toString());
    if (rememberLogin_) rememberLogin_->setChecked(obj.value("remember_login").toBool(true));
    language_ = obj.value("language").toString("de");

    // Mapper-Installer: gemerkte SD-Karte und ggf. manuell gesetzter
    // cores-Ordner (buildUi() lief bereits, fpgaPanel_ existiert hier).
    if (fpgaPanel_) {
        QString sd = obj.value("sd_root").toString();
        if (!sd.isEmpty()) fpgaPanel_->setSdRoot(sd);
        // Sidebar-Zustand bewusst NICHT wiederherstellen: main.cpp setzt nach
        // dem Intro w.setGeometry(introGeo) mit 760px Breite. Eine beim Start
        // ausgeklappte Sidebar wurde dadurch in ein zu schmales Fenster
        // gequetscht. Sie beginnt darum immer eingeklappt und geht auf,
        // sobald Hardcore gewaehlt wird.
        QString cd = obj.value("cores_dir").toString();
        if (!cd.isEmpty()) {
            fpgaPanel_->setCoresDir(cd);
            coresDirManual_ = true;
        }
    }

    QJsonObject rem = obj.value("remembered").toObject();
    remembered_.clear();
    for (auto it = rem.begin(); it != rem.end(); ++it) {
        remembered_.insert(it.key(), it.value().toString());
    }
}

void MainWindow::saveConfig() {
    QJsonObject obj;
    // Klartext-Speicherung von Benutzername/Passwort entspricht 1:1
    // Pythons _save_config() (json.dump ra_user/ra_pass) -- bestehendes
    // Verhalten des Originals, kein neues Risiko durch den C++-Port.
    // Ohne "Eingeloggt bleiben" werden Benutzername und Passwort NICHT
    // abgelegt; der Haken selbst wird immer gespeichert.
    const bool merken = !rememberLogin_ || rememberLogin_->isChecked();
    obj["remember_login"] = merken;
    obj["ra_user"] = merken ? userEdit_->text() : QString();
    obj["ra_pass"] = merken ? passEdit_->text() : QString();
    obj["language"] = language_;
    if (fpgaPanel_) {
        if (!fpgaPanel_->sdRoot().isEmpty()) obj["sd_root"] = fpgaPanel_->sdRoot();
        // Nur einen bewusst gewaehlten Ordner festschreiben -- sonst wuerde
        // ein automatisch gefundener Pfad einzementiert und die Suche nie
        // wieder greifen, falls die .exe umzieht.
        if (coresDirManual_) obj["cores_dir"] = fpgaPanel_->coresDir();
    }
    QJsonObject rem;
    for (auto it = remembered_.constBegin(); it != remembered_.constEnd(); ++it) {
        rem[it.key()] = it.value();
    }
    obj["remembered"] = rem;

    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}

namespace {
// Liest/schreibt den sichtbaren Text eines Widgets bzw. einer Aktion.
// QGroupBox und QMenu benutzen title(), alles andere text().
QString leseText(QObject* o) {
    if (auto* gb = qobject_cast<QGroupBox*>(o))   return gb->title();
    if (auto* m  = qobject_cast<QMenu*>(o))       return m->title();
    if (auto* a  = qobject_cast<QAction*>(o))     return a->text();
    if (auto* b  = qobject_cast<QAbstractButton*>(o)) return b->text();
    if (auto* l  = qobject_cast<QLabel*>(o))      return l->text();
    return QString();
}
void schreibeText(QObject* o, const QString& t) {
    if (auto* gb = qobject_cast<QGroupBox*>(o))   { gb->setTitle(t); return; }
    if (auto* m  = qobject_cast<QMenu*>(o))       { m->setTitle(t);  return; }
    if (auto* a  = qobject_cast<QAction*>(o))     { a->setText(t);   return; }
    if (auto* b  = qobject_cast<QAbstractButton*>(o)) { b->setText(t); return; }
    if (auto* l  = qobject_cast<QLabel*>(o))      { l->setText(t);   return; }
}
const char* kSrcProp = "rawnesSrc";
} // namespace

void MainWindow::rememberSourceTexts() {
    QList<QObject*> alle;
    for (auto* w : findChildren<QWidget*>()) alle << w;
    for (auto* a : findChildren<QAction*>()) alle << a;
    alle << this;
    for (QObject* o : alle) {
        const QString t = leseText(o);
        if (t.isEmpty()) continue;
        // rawnesQuelle() faengt den Fall ab, dass das Programm bereits auf
        // Englisch gestartet ist -- gemerkt wird immer der deutsche Text.
        o->setProperty(kSrcProp, rawnesQuelle(t));
    }
    setProperty("rawnesTitleSrc", rawnesQuelle(windowTitle()));
}

void MainWindow::toggleLbSidebar(bool open) {
    if (!lbSidebar_ || !lbHandle_) return;
    if (lbSidebarOpen_ == open && lbSidebar_->isVisible() == open) return;
    lbSidebarOpen_ = open;
    lbSidebar_->setVisible(open);
    lbHandle_->setText(open ? "›" : "‹");
    // Fensterbreite mitziehen, damit der Inhalt links gleich breit bleibt.
    const int delta = kLbSidebarWidth + 12;
    resize(width() + (open ? delta : -delta), height());
    saveConfig();
}

void MainWindow::retranslateAll() {
    const bool en = (language_ == "en");
    QList<QObject*> alle;
    for (auto* w : findChildren<QWidget*>()) alle << w;
    for (auto* a : findChildren<QAction*>()) alle << a;
    for (QObject* o : alle) {
        const QVariant v = o->property(kSrcProp);
        if (!v.isValid()) continue;
        const QString quelle = v.toString();
        // Nur ersetzen, was seit dem Aufbau unveraendert ist -- sonst wuerde
        // eine Laufzeitmeldung ("verbunden (COM10)", "%1 ROMs indiziert")
        // auf ihren Startwert zurueckgesetzt.
        if (!rawnesUnveraendert(leseText(o), quelle)) continue;
        schreibeText(o, rawnesText(quelle, en));
    }
    // Sonderfall: die Verbindungszeile wird zur Laufzeit aus Text + Port
    // zusammengesetzt und faellt daher durch die Unveraendert-Pruefung.
    if (connStatus_) {
        const QVariant pv = connStatus_->property("rawnesPort");
        if (pv.isValid() && !pv.toString().isEmpty()) {
            connStatus_->setText(rawnesText("verbunden (", en) + pv.toString() + ")");
        }
    }
    if (subtitleLbl_) {
        subtitleLbl_->setText(
            rawnesText("— v0.1 build %1 — und immer noch nicht perfekt —", en)
                .arg(kGuiBuild));
    }
    if (lbHandle_) lbHandle_->setToolTip(rawnesText("Leaderboards ein-/ausklappen", en));
    const QVariant tv = property("rawnesTitleSrc");
    if (tv.isValid()) setWindowTitle(rawnesText(tv.toString(), en));
}

void MainWindow::setLanguage(const QString& lang) {
    if (language_ == lang) return;
    language_ = lang;
    saveConfig();
    // Translator umhaengen und alle bereits gebauten Texte neu setzen --
    // kein Neustart noetig. Dialoge, die spaeter erst erzeugt werden, sind
    // ohnehin korrekt, weil ihr tr() dann in der neuen Sprache auswertet.
    rawnesApplyLanguage(QCoreApplication::instance(), language_);
    retranslateAll();
    QMessageBox::information(this,
        lang == "en" ? "Language" : "Sprache",
        lang == "en"
            ? "Language set to English."
            : "Sprache auf Deutsch gesetzt.");
}

void MainWindow::onWorkerRememberedChanged(QString hexKey, QString source) {
    remembered_.insert(hexKey, source);
    saveConfig();
}

void MainWindow::updateIndexLabel() {
    if (!romIndex_ || romIndex_->empty()) {
        indexStatusLabel_->setText(tr("(nicht indiziert — ROM-Auswahl per Dialog)"));
        return;
    }
    int n = 0;
    for (auto& [key, entries] : *romIndex_) n += static_cast<int>(entries.size());
    indexStatusLabel_->setText(tr("%1 ROMs indiziert").arg(n));
}

void MainWindow::onIndexRoms() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Spielordner waehlen"));
    if (dir.isEmpty()) return;

    indexBtn_->setEnabled(false);
    indexStatusLabel_->setText(tr("indiziere... 0 ROMs"));

    auto* thread = new QThread(this);
    connect(thread, &QThread::started, thread, [this, dir, thread]() {
        int skipped = 0;
        // WICHTIG: Qt::DirectConnection unten am connect() erzwingen --
        // ohne das laeuft diese Lambda (inkl. buildIndex/7z-Aufruf) im
        // GUI-Thread statt hier im Worker-Thread (Grund fuer "Keine
        // Rueckmeldung" waehrend der Indizierung).
        // Fortschritts-Callback: laeuft im Worker-Thread, deshalb jede
        // UI-Aktualisierung per invokeMethod in den GUI-Thread schieben.
        // Zeigt jetzt auch den aktuellen Dateinamen -- bleibt die Anzeige
        // bei einer Datei stehen, blockiert genau die (grosses/kaputtes
        // Archiv). Vorher lief buildIndex stumm -> Eindruck 'haengt'.
        auto progress = [this](int count, const std::string& file) {
            QString f = QString::fromStdString(file);
            QMetaObject::invokeMethod(this, [this, count, f]() {
                if (!f.isEmpty()) {
                    indexStatusLabel_->setText(tr("indiziere... %1 ROMs — %2").arg(count).arg(f));
                } else {
                    indexStatusLabel_->setText(tr("indiziere... %1 ROMs").arg(count));
                }
            }, Qt::QueuedConnection);
        };
        auto index = rawnes::buildIndex(dir.toStdString(), &skipped, progress);
        auto indexPtr = std::make_shared<rawnes::RomIndexMap>(std::move(index));
        rawnes::saveIndex(*indexPtr, romIndexPath().toStdString());

        QMetaObject::invokeMethod(this, [this, indexPtr, skipped]() {
            romIndex_ = indexPtr;
            indexBtn_->setEnabled(true);
            updateIndexLabel();
            appendLog(tr("Indizierung abgeschlossen."));
            if (skipped > 0) {
                appendLog(tr("%1 Archiv(e) uebersprungen -- bitte 7-Zip installieren "
                             "(https://www.7-zip.org/), dann erneut indizieren.").arg(skipped));
            }
        }, Qt::QueuedConnection);
        thread->quit();
    }, Qt::DirectConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::appendLog(const QString& msg) {
    if (log_) log_->append(msg);
}

void MainWindow::onLogin() {
    QString user = userEdit_->text();
    QString pw = passEdit_->text();
    if (user.isEmpty() || pw.isEmpty()) {
        loginStatus_->setText(tr("Bitte Benutzername und Passwort eingeben."));
        return;
    }
    loginBtn_->setEnabled(false);
    loginStatus_->setText(tr("Login laeuft..."));

    auto* thread = new QThread(this);
    // Qt::DirectConnection s.u. -- gleicher Grund wie bei onIndexRoms().
    connect(thread, &QThread::started, thread, [this, user, pw, thread]() {
        QString statusText, styleSheet, newToken;
        bool ok = false;
        try {
            auto tok = client_->ra_login(user.toStdString(), pw.toStdString());
            if (tok) {
                newToken = QString::fromStdString(*tok);
                statusText = QString::fromUtf8("\u2713 ") + user;
                styleSheet = QString("color: %1;").arg(RawnesColors::kUnlocked.name());
                ok = true;
            } else {
                statusText = tr("Login fehlgeschlagen (falsche Zugangsdaten?).");
            }
        } catch (const RateLimited& e) {
            statusText = QString("RA drosselt, bitte %1s warten.").arg(e.retry_after);
        } catch (const std::exception& e) {
            statusText = QString("Netzwerkfehler: %1").arg(e.what());
        }
        QMetaObject::invokeMethod(this, [this, statusText, styleSheet, newToken, ok]() {
            loginBtn_->setEnabled(true);
            loginStatus_->setText(statusText);
            if (ok) {
                loginStatus_->setStyleSheet(styleSheet);
                token_ = newToken;
                appendLog(tr("Login erfolgreich."));
                monitorBtn_->setEnabled(!token_.isEmpty());
                saveConfig();
            }
        }, Qt::QueuedConnection);
        thread->quit();
    }, Qt::DirectConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::onReconnect() {
    connStatus_->setText(tr("verbinde..."));
    connStatus_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(RawnesColors::kTextMuted.name()));
    QMetaObject::invokeMethod(monWorker_, "connectToDevice", Qt::QueuedConnection,
                              Q_ARG(QString, QString()));
}

void MainWindow::onToggleMonitor() {
    if (monitoring_) {
        // stop() DIREKT aufrufen: es setzt nur das std::atomic<bool>
        // running_ und braucht keine Event-Loop. Per QueuedConnection kam
        // der Aufruf nie an, solange der Worker in seiner Poll-Schleife
        // steckte -- der Knopf wirkte dadurch tot. Gleiche Ursache wie im
        // Destruktor.
        monWorker_->stop();
        return; // onWorkerFinished raeumt den Button-Zustand auf
    }
    if (token_.isEmpty()) {
        appendLog(tr("Bitte zuerst anmelden."));
        return;
    }
    // Noch nicht verbunden? Dann JETZT verbinden (Konsole ist inzwischen
    // aus dem Menue raus/ruhig) -- danach nochmal auf "Monitor starten"
    // klicken. Kein Auto-Connect beim Programmstart mehr.
    if (!connected_) {
        appendLog(tr("Verbinde mit EverDrive..."));
        QMetaObject::invokeMethod(monWorker_, "connectToDevice", Qt::QueuedConnection,
                                  Q_ARG(QString, QString()));
        return;
    }
    monitorBtn_->setEnabled(false);
    monitorBtn_->setText(tr("lade..."));
    // Hardcore wird genau hier einmal gelesen und lebt danach als hardcore_
    // im Worker. Ein Umschalten waehrend der Sitzung hatte deshalb keinerlei
    // Wirkung -- das Haekchen zeigte aber trotzdem den neuen Zustand an und
    // behauptete damit etwas Falsches. Waehrend der Ueberwachung gesperrt.
    hardcorePanel_->setEnabled(false);
    monitoring_ = true;
    QMetaObject::invokeMethod(
        monWorker_, "startMonitoring", Qt::QueuedConnection,
        Q_ARG(std::shared_ptr<rawnes::RomIndexMap>, romIndex_),
        Q_ARG(QString, userEdit_->text()), Q_ARG(QString, token_),
        Q_ARG(bool, hardcorePanel_->isChecked()));
}

void MainWindow::onWorkerConnected(const QString& port) {
    connected_ = true;
    // Port getrennt hinterlegen, damit retranslateAll() die Zeile beim
    // Sprachwechsel neu zusammensetzen kann. Vorher war sie ein zur
    // Laufzeit gesetzter Text und blieb deshalb in der alten Sprache
    // stehen ("connected (COM10)" auch nach Umschalten auf Deutsch).
    connStatus_->setProperty("rawnesPort", port);
    connStatus_->setText(tr("verbunden (") + port + ")");
    connStatus_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(RawnesColors::kUnlocked.name()));
    monitorBtn_->setEnabled(!token_.isEmpty());
    monitorBtn_->setText(tr("Monitor starten"));
    appendLog(tr("Verbunden. Erneut auf \"Monitor starten\" klicken."));
}

void MainWindow::onWorkerConnectionFailed(const QString& reason) {
    connected_ = false;
    connStatus_->setText(tr("nicht gefunden -- Konsole an? Spiel gestartet?"));
    connStatus_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(RawnesColors::kRed.name()));
    appendLog(reason);
    monitorBtn_->setEnabled(!token_.isEmpty()); // Nutzer darf erneut versuchen
    hardcorePanel_->setEnabled(true);
}

void MainWindow::onWorkerRomNotDetected() {
    // Entspricht Pythons Fallback in _start(): kein automatischer Treffer
    // -> Dateidialog anbieten, direkt .nes/.zip/.7z/.rar.
    QString path = QFileDialog::getOpenFileName(
        this, tr("ROM oder Archiv waehlen"), QString(),
        tr("NES-ROMs und Archive (*.nes *.zip *.7z *.rar);;NES-ROMs (*.nes);;Alle Dateien (*)"));
    if (path.isEmpty()) {
        monitoring_ = false;
        monitorBtn_->setEnabled(true);
        hardcorePanel_->setEnabled(true);
        monitorBtn_->setText(tr("Spiel laden & Monitor starten"));
        return;
    }
    QMetaObject::invokeMethod(monWorker_, "continueWithPath", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void MainWindow::onWorkerRomAmbiguous(QString hexKey, QStringList labels, QStringList sources) {
    // Entspricht Pythons simpledialog.askinteger()-Rueckfrage in
    // _detect_rom(): mehrere ROMs teilen sich den Vektor-Fingerabdruck
    // und liessen sich auch ueber Mapper/PRG/CHR/ROM-Code/RA-GameID nicht
    // weiter eingrenzen -- der Nutzer muss einmalig entscheiden.
    bool ok = false;
    QString choice = QInputDialog::getItem(
        this, tr("Mehrere Treffer"),
        tr("Diese ROMs haben denselben Fingerabdruck, gehoeren aber zu "
           "verschiedenen RA-Spielen. Bitte waehlen -- die Wahl gilt fuer diesen "
           "Start und wird bei verschiedenen RA-Spielen bewusst NICHT "
           "automatisch wiederverwendet:"),
        labels, 0, false, &ok);
    if (!ok) {
        monitoring_ = false;
        monitorBtn_->setEnabled(true);
        hardcorePanel_->setEnabled(true);
        monitorBtn_->setText(tr("Spiel laden & Monitor starten"));
        return;
    }
    int idx = labels.indexOf(choice);
    if (idx < 0) idx = 0;
    QMetaObject::invokeMethod(monWorker_, "continueWithChosenRom", Qt::QueuedConnection,
                              Q_ARG(QString, hexKey), Q_ARG(QString, sources[idx]));
}

void MainWindow::onWorkerGameLoaded(const QString& name) {
    gameStatus_->setText(name);
    // Polling laeuft jetzt -- Button wird zum Stopp-Knopf. Vorher zeigte
    // er dauerhaft "Spiel laden & Monitor starten", man konnte nicht
    // erkennen dass ein zweiter Klick stoppt.
    monitorBtn_->setEnabled(true);
    monitorBtn_->setText(tr("■ Monitor stoppen"));
    monitorBtn_->setStyleSheet(QString(
        "QPushButton { color: %1; font-weight: bold; }")
        .arg(RawnesColors::kRed.name()));
}

void MainWindow::onWorkerLog(const QString& msg) {
    appendLog(msg);
}

void MainWindow::onWorkerUnlocked(const QString& title, int points, qlonglong achId, bool hardcore) {
    Q_UNUSED(hardcore);
    achList_->setUnlocked(achId, true);
    // Popup unten rechts einblenden. Wird beim ersten Mal erzeugt und dann
    // wiederverwendet -- ein zweites Achievement waehrend der Anzeige setzt
    // die Animation einfach neu auf.
    if (!achPopup_) achPopup_ = new AchievementPopup();
    // Das unlocked-Signal liefert nur Titel, Punkte und ID. Beschreibung und
    // Badge holt sich das Popup ueber die Liste, die beides aus GameData
    // kennt. Liefert badgeFor() nichts (kein Badge hinterlegt, Datei nicht
    // ladbar), zeichnet das Popup weiterhin einen Pokal.
    achPopup_->zeige(title, achList_->descriptionFor(achId), points,
                     achList_->badgeFor(achId), language_ == "en");
}

void MainWindow::onWorkerProgress(qlonglong achId, int current, int target) {
    achList_->setProgress(achId, current, target);
}

void MainWindow::onWorkerConnectionLost(const QString& reason) {
    connStatus_->setText(tr("Verbindung verloren"));
    appendLog(tr("Verbindung verloren: ") + reason);
}

void MainWindow::onWorkerHardcoreDowngraded(const QString& liste) {
    hardcorePanel_->setChecked(false);
    appendLog(tr("Hardcore beendet -- aktiviert: ") + liste);
    // Popup, nicht nur Log -- entspricht Pythons messagebox.showwarning()
    // an allen drei Pruefpunkten (Session-Start, vor Poll-Loop, bei Reset).
    QMessageBox::warning(this, tr("Hardcore beendet"),
        tr("Am EverDrive sind aktiviert: %1\n\n"
           "Hardcore wurde fuer diese Sitzung beendet, es zaehlt jetzt "
           "Softcore. Fuer Hardcore bitte im EverDrive-System-Menue "
           "(nicht im Ingame-Menue) deaktivieren, dann Monitor neu starten.")
        .arg(liste));
}

void MainWindow::onWorkerFinished() {
    monitoring_ = false;
    monitorBtn_->setEnabled(true);
    hardcorePanel_->setEnabled(true);
    monitorBtn_->setText(tr("Spiel laden & Monitor starten"));
    monitorBtn_->setStyleSheet(QString()); // Stopp-Rot zuruecksetzen
    appendLog(tr("Monitor gestoppt."));
}

void MainWindow::onFpgaSetupFromMenu() {
    fpgaPanel_->checkCores(true);
}

void MainWindow::onFpgaRestoreFromMenu() {
    fpgaPanel_->onRestoreClicked();
}

void MainWindow::onOpenSupport() {
    QMessageBox::information(this, tr("Bevor es weitergeht"),
        "Bitte lesen Sie zuerst \"about the cat\" in den Optionen.\n\n"
        "Please read \"about the cat\" in the options first.");
    // Direkt zu Ko-fi. Frueher zeigte das auf liquid-wq.github.io/data/ --
    // das war eine Weiterleitung dorthin, ist inzwischen aber die
    // Downloadseite.
    QDesktopServices::openUrl(QUrl("https://ko-fi.com/liqui69747"));
}

void MainWindow::onAboutCat() {
    static const QString kAboutText =
        "Ich habe mich fuer meine Katze als Programmlogo entschieden. "
        "Die folgenden Zeilen sind ihr gewidmet — eine kleine Hommage "
        "und ein Gedenken.\n\n"
        "Jede dritte Katze ab einem Alter von 10–12 Jahren leidet an CNI "
        "(chronischer Niereninsuffizienz). Leider wird die Krankheit oft "
        "erst sichtbar, wenn sie sich bereits dem Endstadium zuneigt. "
        "Viele Aerzte weisen nicht darauf hin.\n\n"
        "Fuehlen Sie sich also nicht sicher, nur weil Sie regelmaessig und "
        "verantwortungsvoll zum Tierarzt gehen und dieser sagt, es sei "
        "alles in Ordnung. Bitte verlangen Sie regelmaessig einen Bluttest. "
        "Nur dieser kann die Katze retten und ihr Leben nachweislich "
        "wertvoll verlaengern. Frueherkennung macht den Unterschied.\n\n"
        "Mein Kater bekam Anfang 2025 die Diagnose, nachdem es "
        "Auffaelligkeiten in seinem Verhalten gab: mehr Durst, der Drang, "
        "ungewoehnliche Wasserquellen aufzusuchen. Wir entschieden uns "
        "fuer eine Behandlung in einer Tierklinik, und waehrend dieser "
        "Zeit haben ihn sich gleich mehrere Aerzte angeschaut — und jeder "
        "war anderer Meinung. Es ist wichtig, auch Ihren eigenen Sinnen "
        "zu trauen, da Sie Ihre Katze am besten kennen.\n\n"
        "Am 11. Juni 2025 ist er gestorben.\n\n"
        "Die Katze im Intro ist ein Pixel-Portrait zur Erinnerung an "
        "meinen Kater, abgeleitet von einem echten Foto aus dem Jahr "
        "2010 — als wir ihn aus dem Tierheim holten.\n\n"
        "Auch wenn dieses Projekt einen Spenden-Button hat: Die groesste "
        "Freude, die Sie mir machen koennen, ist, Ihre Katze auf diese "
        "Krankheit untersuchen zu lassen.\n\n"
        "* CNI betrifft nicht nur Katzen, sondern auch andere Tiere.\n\n"
        "Und wenn das jetzt auch nur einer macht, hat dieser Text seinen "
        "Sinn und Zweck in einer Software, die damit eigentlich gar nichts "
        "zu tun hat, schon vollends erfuellt.\n\n"
        "Ich danke euch fuers Lesen.\n\n"
        "In memory of Jason — 2010–2025.";

    QDialog dlg(this);
    dlg.setWindowTitle(tr("about the cat"));
    dlg.resize(500, 620);
    dlg.setStyleSheet(QString("background: %1;").arg(RawnesColors::kDark.name()));
    auto* layout = new QVBoxLayout(&dlg);

    auto* catLabel = new QLabel(&dlg);
    QPixmap catPix(":/intro/cat_portrait.png");
    if (!catPix.isNull()) catLabel->setPixmap(catPix.scaled(catPix.size() * 2));
    catLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(catLabel);

    auto* nameLbl = new QLabel(tr("In memory of Jason"), &dlg);
    nameLbl->setAlignment(Qt::AlignHCenter);
    nameLbl->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;")
                           .arg(RawnesColors::kUnlocked.name()));
    layout->addWidget(nameLbl);

    auto* yearsLbl = new QLabel("2010 – 2025", &dlg);
    yearsLbl->setAlignment(Qt::AlignHCenter);
    yearsLbl->setStyleSheet(QString("color: %1; font-size: 11px;").arg(RawnesColors::kTextMuted.name()));
    layout->addWidget(yearsLbl);

    auto* textEdit = new QTextEdit(&dlg);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(kAboutText);
    textEdit->setStyleSheet(QString("background: %1; color: %2; border: none; padding: 12px;")
                            .arg(RawnesColors::kPanel.name(), RawnesColors::kTextMain.name()));
    layout->addWidget(textEdit, 1);

    dlg.exec();
}
