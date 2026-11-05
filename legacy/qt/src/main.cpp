// main.cpp -- Einstiegspunkt. Fehlerprotokoll nach MEGA-RAW-Muster
// (Qt-Warnungen/-Fehler in error_log.txt, damit bei Abstuerzen ohne
// Konsole trotzdem was nachvollziehbar ist). Startet die Intro-Animation
// vor dem Hauptfenster.
#include "i18n.h"
#include "intro_window.h"
#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <fstream>

int main(int argc, char** argv) {
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
            std::ofstream f("error_log.txt", std::ios::app);
            if (f) {
                f << QTime::currentTime().toString("HH:mm:ss").toStdString()
                  << "  " << msg.toStdString() << "\n";
            }
        }
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    });

    QApplication app(argc, argv);

    // Sprache VOR dem Aufbau der Fenster setzen -- tr() wird beim Erzeugen
    // der Widgets einmal ausgewertet, ein spaeteres Installieren haette
    // keine Wirkung mehr. MainWindow::loadConfig() laeuft erst nach
    // buildUi(), deshalb wird der Schluessel hier direkt gelesen.
    {
        QFile cfg(QCoreApplication::applicationDirPath() + "/rawnes_gui_config.json");
        if (cfg.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(cfg.readAll());
            if (doc.isObject()) {
                rawnesApplyLanguage(&app, doc.object().value("language").toString());
            }
        }
    }
    // Das Intro liefert die Geometrie zurueck, die es benutzt hat; das
    // Hauptfenster uebernimmt sie, damit es an genau derselben Stelle und
    // in derselben Groesse erscheint (buildUi() ruft resize(760,900), die
    // Position bliebe sonst dem Fenstermanager ueberlassen).
    const QRect introGeo = IntroWindow::showBlocking();
    MainWindow w;
    if (introGeo.isValid()) w.setGeometry(introGeo);
    w.show();
    return app.exec();
}
