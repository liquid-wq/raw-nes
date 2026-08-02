#pragma once
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QTranslator>

// Uebersetzungstabelle Deutsch -> Englisch, im Code hinterlegt (analog zu
// MEGA-RAWs i18n.h). Bewusst KEINE Qt-.qm-Dateien: die muessten mit
// lupdate/lrelease erzeugt, mitgeliefert und bei jeder Textaenderung neu
// gebaut werden. Die Tabelle hier wird einkompiliert und kann nicht fehlen.
//
// Der Trick: RawnesTranslator ist ein echter QTranslator, der statt aus
// einer Datei aus dieser Tabelle antwortet. Dadurch funktionieren ALLE
// bestehenden tr()-Aufrufe unveraendert -- es musste keine einzige
// Aufrufstelle angefasst werden.
//
// Header-only und ohne Q_OBJECT (keine Signals/Slots noetig), deshalb
// braucht es weder einen moc-Lauf noch einen Eintrag in CMakeLists.txt.
//
// Nicht erfasst: Texte, die nicht in tr() stehen -- vor allem die
// Log-Meldungen aus monitor_worker.cpp. Die bleiben vorerst deutsch.

inline const QHash<QString, QString>& rawnesTableEn() {
    static const QHash<QString, QString> t = {
        {"    (Ordner existiert nicht)\n",
         "    (folder does not exist)\n"},
        {"  FEHLER: %1\n",
         "  ERROR: %1\n"},
        {"  FPGA-Mapper installiert: %1\n",
         "  FPGA mapper installed: %1\n"},
        {"  Original wiederhergestellt: %1\n",
         "  Original restored: %1\n"},
        {"%1 Archiv(e) uebersprungen -- bitte 7-Zip installieren (https://www.7-zip.org/), dann erneut indizieren.",
         "%1 archive(s) skipped -- please install 7-Zip (https://www.7-zip.org/), then index again."},
        {"%1 FPGA-Mapper erfolgreich installiert.",
         "%1 FPGA mappers installed successfully."},
        {"%1 FPGA-Mapper installiert.\n",
         "%1 FPGA mappers installed.\n"},
        {"%1 Original-Mapper gesichert (ORIG_BACKUP).\n",
         "%1 original mappers backed up (ORIG_BACKUP).\n"},
        {"%1 Original-Mapper wiederhergestellt.",
         "%1 original mappers restored."},
        {"%1 Original-Mapper wiederherstellen?",
         "Restore %1 original mappers?"},
        {"%1 Originale wiederhergestellt.\n",
         "%1 originals restored.\n"},
        {"%1 ROMs indiziert",
         "%1 ROMs indexed"},
        {"(erfordert deaktivierte Savestates/Cheats im EverDrive-Menue)",
         "(requires savestates/cheats disabled in the EverDrive menu)"},
        {"(nicht indiziert — ROM-Auswahl per Dialog)",
         "(not indexed — ROM selection via dialog)"},
        {"Achievements (Klick = Diagnose, Doppelklick = auf RetroAchievements oeffnen)",
         "Achievements (click = diagnose, double-click = open on RetroAchievements)"},
        {"Alle Mapper sind bereits aktuell.",
         "All mappers are already up to date."},
        {"Am EverDrive sind aktiviert: %1\n\nHardcore wurde fuer diese Sitzung beendet, es zaehlt jetzt Softcore. Fuer Hardcore bitte im EverDrive-System-Menue (nicht im Ingame-Menue) deaktivieren, dann Monitor neu starten.",
         "Enabled on the EverDrive: %1\n\nHardcore has been ended for this session; Softcore now counts. For Hardcore, disable them in the EverDrive system menu (not the in-game menu), then restart the monitor."},
        {"Anmelden",
         "Log in"},
        {"Bevor es weitergeht",
         "Before you continue"},
        {"Bitte Benutzername und Passwort eingeben.",
         "Please enter username and password."},
        {"Bitte zuerst anmelden.",
         "Please log in first."},
        {"Cores-Ordner gesetzt: %1\n",
         "Cores folder set: %1\n"},
        {"Cores-Ordner nicht gefunden oder unvollstaendig:\n%1\n\nFehlt:\n%2\nOrdner jetzt manuell waehlen?",
         "Cores folder not found or incomplete:\n%1\n\nMissing:\n%2\nSelect the folder manually now?"},
        {"Cores-Ordner nicht gesetzt -- abgebrochen.\n",
         "Cores folder not set -- cancelled.\n"},
        {"Diese %1 FPGA-Mapper werden installiert:\n",
         "These %1 FPGA mappers will be installed:\n"},
        {"Diese ROMs haben denselben Fingerabdruck, gehoeren aber zu verschiedenen RA-Spielen. Bitte waehlen -- die Wahl gilt fuer diesen Start und wird bei verschiedenen RA-Spielen bewusst NICHT automatisch wiederverwendet:",
         "These ROMs share the same fingerprint but belong to different RA games. Please choose -- the choice applies to this start and is deliberately NOT reused automatically when the candidates are different RA games:"},
        {"Einrichtung übersprungen.\n",
         "Setup skipped.\n"},
        {"Erneut verbinden",
         "Reconnect"},
        {"Es ist noch keine EverDrive-SD-Karte hinterlegt.\n\nBitte im naechsten Fenster das Laufwerk der EverDrive-SD-Karte auswaehlen -- also das Laufwerk, das den Ordner EDN8 enthaelt (z.B. H:\\). Die Auswahl wird gespeichert und beim naechsten Mal nicht mehr abgefragt.",
         "No EverDrive SD card is stored yet.\n\nIn the next window, please select the drive of the EverDrive SD card -- the drive containing the EDN8 folder (e.g. H:\\). The choice is saved and won't be asked again."},
        {"Es ist noch keine EverDrive-SD-Karte hinterlegt.\n\nJetzt das Laufwerk mit dem Ordner EDN8 auswaehlen (z.B. H:\\) und die Mapper einrichten?",
         "No EverDrive SD card is stored yet.\n\nSelect the drive containing the EDN8 folder now (e.g. H:\\) and set up the mappers?"},
        {"EverDrive",
         "EverDrive"},
        {"EverDrive-SD-Karte waehlen (Laufwerk mit dem Ordner EDN8)",
         "Select EverDrive SD card (drive containing the EDN8 folder)"},
        {"FPGA-Mapper",
         "FPGA mappers"},
        {"FPGA-Mapper einrichten",
         "Set up FPGA mappers"},
        {"FPGA-Mapper einrichten...",
         "Set up FPGA mappers..."},
        {"FPGA-Mapper sind aktuell.\n",
         "FPGA mappers are up to date.\n"},
        {"FPGA-Mapper-Installation abgebrochen.\n",
         "FPGA mapper installation cancelled.\n"},
        {"Fehler beim Installieren:\n%1",
         "Error while installing:\n%1"},
        {"Fehler:\n%1",
         "Error:\n%1"},
        {"Hardcore beendet",
         "Hardcore ended"},
        {"Hardcore beendet -- aktiviert: ",
         "Hardcore ended -- enabled: "},
        {"Hardcore-Modus",
         "Hardcore mode"},
        {"Hinweis: %1 sah aus wie der EDN8-Ordner selbst -- automatisch eine Ebene hoeher gesucht.\n",
         "Note: %1 looked like the EDN8 folder itself -- searched one level up automatically.\n"},
        {"In %1 fehlen weiterhin Mapper-Dateien.",
         "Mapper files are still missing in %1."},
        {"In memory of Jason",
         "In memory of Jason"},
        {"Indizierung abgeschlossen.",
         "Indexing complete."},
        {"Kein Backup vorhanden.",
         "No backup available."},
        {"Kein EDN8-Ordner auf der gewählten Karte gefunden.",
         "No EDN8 folder found on the selected card."},
        {"Kein EDN8-Ordner gefunden.",
         "No EDN8 folder found."},
        {"Kein EDN8-Ordner in %1 gefunden.\n",
         "No EDN8 folder found in %1.\n"},
        {"Kein neues Backup nötig (bereits vorhanden oder Dateien fehlten auf der SD).\n",
         "No new backup needed (already present, or the files were missing on the SD card).\n"},
        {"Keine SD-Karte gewählt -- abgebrochen.\n",
         "No SD card selected -- cancelled.\n"},
        {"Ko-fi",
         "Ko-fi"},
        {"Leaderboard abgebrochen: ",
         "Leaderboard cancelled: "},
        {"Leaderboard eingereicht: ",
         "Leaderboard submitted: "},
        {"Leaderboard laeuft: ",
         "Leaderboard running: "},
        {"Leaderboards anzeigen",
         "Show leaderboards"},
        {"Leaderboards ausblenden",
         "Hide leaderboards"},
        {"Live-Werte",
         "Live values"},
        {"Login erfolgreich.",
         "Login successful."},
        {"Login fehlgeschlagen (falsche Zugangsdaten?).",
         "Login failed (wrong credentials?)."},
        {"Login laeuft...",
         "Logging in..."},
        {"Mehrere Treffer",
         "Multiple matches"},
        {"Monitor gestoppt.",
         "Monitor stopped."},
        {"Monitor starten",
         "Start monitor"},
        {"NES-ROMs und Archive (*.nes *.zip *.7z *.rar);;NES-ROMs (*.nes);;Alle Dateien (*)",
         "NES ROMs and archives (*.nes *.zip *.7z *.rar);;NES ROMs (*.nes);;All files (*)"},
        {"Optionen",
         "Options"},
        {"Original-Mapper wiederherstellen",
         "Restore original mappers"},
        {"Originale wiederherstellen",
         "Restore originals"},
        {"RA-Benutzername",
         "RA username"},
        {"RA-Passwort",
         "RA password"},
        {"RAM: -",
         "RAM: -"},
        {"RAW-NES Monitor bereit.",
         "RAW-NES monitor ready."},
        {"ROM oder Archiv waehlen",
         "Select ROM or archive"},
        {"ROM-Sammlung indizieren...",
         "Index ROM collection..."},
        {"RetroAchievements",
         "RetroAchievements"},
        {"SD-Karte erkannt: %1\n",
         "SD card detected: %1\n"},
        {"Schnellstart:  1. Anmelden   2. Spielordner einmal indizieren   3. Spiel am Geraet starten, dann Monitor starten",
         "Quick start:  1. Log in   2. Index your game folder once   3. Start the game on the console, then start the monitor"},
        {"Spiel laden & Monitor starten",
         "Load game & start monitor"},
        {"Spielordner waehlen",
         "Select game folder"},
        {"Sprache / Language",
         "Sprache / Language"},
        {"Verbinde mit EverDrive...",
         "Connecting to EverDrive..."},
        {"Verbindung verloren",
         "Connection lost"},
        {"Verbindung verloren: ",
         "Connection lost: "},
        {"Verbunden. Erneut auf \"Monitor starten\" klicken.",
         "Connected. Click \"Start monitor\" again."},
        {"Vorhandene Originale werden zuerst gesichert (nach ORIG_BACKUP):\n",
         "Existing originals will be backed up first (to ORIG_BACKUP):\n"},
        {"Ziel: %1\n\n",
         "Target: %1\n\n"},
        {"abgebrochen",
         "cancelled"},
        {"about the cat",
         "about the cat"},
        {"bereit — zum Verbinden Monitor starten",
         "ready — start the monitor to connect"},
        {"cores-Ordner waehlen",
         "Select cores folder"},
        {"eingereicht: ",
         "submitted: "},
        {"indiziere... %1 ROMs",
         "indexing... %1 ROMs"},
        {"indiziere... %1 ROMs — %2",
         "indexing... %1 ROMs — %2"},
        {"indiziere... 0 ROMs",
         "indexing... 0 ROMs"},
        {"kein Spiel geladen",
         "no game loaded"},
        {"lade...",
         "loading..."},
        {"laeuft",
         "running"},
        {"nicht angemeldet",
         "not logged in"},
        {"nicht gefunden -- Konsole an? Spiel gestartet?",
         "not found -- console on? game started?"},
        {"verbinde...",
         "connecting..."},
        {"verbinde... (Versuch %1/%2)",
         "connecting... (attempt %1/%2)"},
        {"verbunden (",
         "connected ("},
        {"wartet auf Start",
         "waiting for start"},
        {"Über \"Originale wiederherstellen\" jederzeit rückgängig zu machen.\n",
         "Can be undone at any time via \"Restore originals\".\n"},
        {"■ Monitor stoppen",
         "■ Stop monitor"},
        {"▶ Log (aufklappen)",
         "▶ Log (expand)"},
        {"▼ Log (zuklappen)",
         "▼ Log (collapse)"},
        {"  %1 ohne RA-Eintrag verworfen, %2 verbleiben.",
         "  %1 discarded without RA entry, %2 remaining."},
        {"  (RA-Abfrage uebersprungen -- fehlende md5-Angabe im Index)",
         "  (RA lookup skipped -- no md5 given in the index)"},
        {"  *** Leaderboard %1: %2 (%3) ***",
         "  *** Leaderboard %1: %2 (%3) ***"},
        {"  CHR-Typ grenzt auf %1 ein.",
         "  CHR type narrows it down to %1."},
        {"  Leaderboard %1: Wert %2 -- NICHT eingereicht (nur im Hardcore-Modus gueltig).",
         "  Leaderboard %1: value %2 -- NOT submitted (only valid in Hardcore mode)."},
        {"  Leaderboard abgebrochen: ",
         "  Leaderboard cancelled: "},
        {"  Leaderboard laeuft: ",
         "  Leaderboard running: "},
        {"  Mapper %1 grenzt auf %2 ein.",
         "  Mapper %1 narrows it down to %2."},
        {"  Mapper-Konfiguration nicht lesbar -- Filter uebersprungen.",
         "  Mapper configuration not readable -- filter skipped."},
        {"  PRG-Groesse (Shift %1) grenzt auf %2 ein.",
         "  PRG size (shift %1) narrows it down to %2."},
        {"  PRG-Page noch zu wenig erfasst (%1 Byte) -- kurz spielen und erneut versuchen.",
         "  PRG page not captured enough yet (%1 bytes) -- play briefly and try again."},
        {"  ROM-Code (%1 Byte) grenzt nicht weiter ein -- die Kandidaten sind an den erfassten Stellen gleich.",
         "  ROM code (%1 bytes) does not narrow it down further -- the candidates are identical at the captured locations."},
        {"  ROM-Code (%1 Byte) grenzt von %2 auf %3 ein.",
         "  ROM code (%1 bytes) narrows it down from %2 to %3."},
        {"  laufendes Spiel: Mapper %1, PRG-Shift %2, CHR-Shift %3, CHR-RAM %4",
         "  running game: mapper %1, PRG shift %2, CHR shift %3, CHR RAM %4"},
        {"  pruefe RA-Zuordnung...",
         "  checking RA assignment..."},
        {" -- alle Slots stehen auf $FF",
         " -- all slots read $FF"},
        {" am EverDrive aktiviert -- Hardcore wird fuer den Rest der Session beendet, ab jetzt Softcore.",
         " enabled on the EverDrive -- Hardcore is ended for the rest of this session, Softcore from now on."},
        {" sind am EverDrive aktiviert -- nur Softcore verfuegbar. Fuer Hardcore bitte im EverDrive-System-Menue (nicht im Ingame-Menue) deaktivieren, dann Monitor neu starten.",
         " are enabled on the EverDrive -- only Softcore available. For Hardcore, disable them in the EverDrive system menu (not the in-game menu), then restart the monitor."},
        {"%1 Achievement(s) warten noch auf echte Daten -- werden erst geprueft, wenn ihre Adressen bestaetigt sind. Schutz gegen Fehlbuchungen an RA.",
         "%1 achievement(s) are still waiting for real data -- they are only checked once their addresses are confirmed. Protects against false submissions to RA."},
        {"%1 Achievements geladen (%2 bereits freigeschaltet).",
         "%1 achievements loaded (%2 already unlocked)."},
        {"%1 Adressen werden ueberwacht.",
         "%1 addresses are being monitored."},
        {", ",
         ", "},
        {".",
         "."},
        {"00",
         "00"},
        {": kein Achievement-Set.",
         ": no achievement set."},
        {"ACHTUNG: ",
         "WARNING: "},
        {"ACHTUNG: FPGA-Build %1 erwartet, %2 gefunden -- aktuelle top.rbf deployt?",
         "WARNING: FPGA build %1 expected, %2 found -- is the current top.rbf deployed?"},
        {"ACHTUNG: Nach Spielneustart sind ",
         "WARNING: after the game restart, "},
        {"ACHTUNG: Savestate-Aktivitaet erkannt (Audit-Bit) -- Hardcore-Bedingung moeglicherweise verletzt.",
         "WARNING: savestate activity detected (audit bit) -- Hardcore condition may have been violated."},
        {"Achievement freigeschaltet: %1 (%2)",
         "Achievement unlocked: %1 (%2)"},
        {"Adressen bestaetigt -- wird ausgewertet.",
         "Addresses confirmed -- now being evaluated."},
        {"Alle %1 Varianten gehoeren zu RA-Spiel #%2 -- nehme '%3'.",
         "All %1 variants belong to RA game #%2 -- using '%3'."},
        {"Archiv konnte nicht gelesen werden: ",
         "Archive could not be read: "},
        {"Automatisch erkannt -- kein Dateidialog noetig.",
         "Detected automatically -- no file dialog needed."},
        {"Automatische Erkennung fehlgeschlagen: ",
         "Automatic detection failed: "},
        {"Bedingung wird nicht unterstuetzt -- feuert nie.",
         "Condition is not supported -- will never trigger."},
        {"Eindeutig nach RA-Abgleich: ",
         "Unique after RA comparison: "},
        {"Eindeutig ueber Mapper: ",
         "Unique by mapper: "},
        {"Eindeutig ueber ROM-Code: ",
         "Unique by ROM code: "},
        {"Entpacke Archiv...",
         "Extracting archive..."},
        {"Erkannt: ",
         "Detected: "},
        {"Erkennung abgebrochen. Typische Ursachen: die Konsole steht im EverDrive-Menue statt im Spiel, das Spiel wurde gerade erst gestartet, oder die aktuelle top.rbf ist nicht deployt.",
         "Detection aborted. Typical causes: the console is in the EverDrive menu instead of the game, the game was only just started, or the current top.rbf is not deployed."},
        {"FF",
         "FF"},
        {"FPGA ohne Schreibmarkierungen -- Warmlauf-Heuristik aktiv (Achievements mit konstanten Adressen koennen ausbleiben).",
         "FPGA without write markers -- warm-up heuristic active (achievements with constant addresses may not fire)."},
        {"FPGA-Build %1 bestaetigt.",
         "FPGA build %1 confirmed."},
        {"FPGA-Build konnte nicht gelesen werden (Menue statt Spiel? Bekanntes DMA-Verhalten, kein Fehler).",
         "FPGA build could not be read (menu instead of game? known DMA behaviour, not an error)."},
        {"Fehler: ",
         "Error: "},
        {"Fingerabdruck [",
         "Fingerprint ["},
        {"Fingerabdruck [%1] besteht nur aus %2 -- das ist keine gueltige Antwort aus dem Snoop-RAM, sondern der Leerlaufwert des Busses.",
         "Fingerprint [%1] consists only of %2 -- that is not a valid response from the snoop RAM but the idle value of the bus."},
        {"Fingerabdruck [%1] passt auf %2 ROMs.",
         "Fingerprint [%1] matches %2 ROMs."},
        {"Fruehere Wahl NICHT uebernommen -- die Kandidaten gehoeren zu verschiedenen RA-Spielen. Bitte bewusst auswaehlen.",
         "Earlier choice NOT reused -- the candidates belong to different RA games. Please choose deliberately."},
        {"Fruehere Wahl uebernommen: ",
         "Earlier choice reused: "},
        {"Hardcore-Pruefung fehlgeschlagen -- Ctrl-Byte nicht lesbar. Nur Softcore verfuegbar.",
         "Hardcore check failed -- ctrl byte not readable. Only Softcore available."},
        {"Hardcore-Voraussetzung erfuellt: Savestates und Cheats sind am EverDrive deaktiviert.",
         "Hardcore requirement met: savestates and cheats are disabled on the EverDrive."},
        {"Konnte entpackte ROM nicht zwischenspeichern.",
         "Could not cache the extracted ROM."},
        {"Leaderboards: %1 geladen (%2 nicht auswertbar).",
         "Leaderboards: %1 loaded (%2 not evaluable)."},
        {"MemAddr: ",
         "MemAddr: "},
        {"Nicht verbunden -- erst mit dem EverDrive verbinden.",
         "Not connected -- connect to the EverDrive first."},
        {"RA-Aktivitaet: %1",
         "RA activity: %1"},
        {"Schreibmarkierungen des FPGA verfuegbar -- Achievements werden exakt freigegeben.",
         "FPGA write markers available -- achievements are released exactly."},
        {"Snoop-Region liegt bei 0x%1 (nicht 0x%2).",
         "Snoop region is at 0x%1 (not 0x%2)."},
        {"Snooper liefert Daten: %1/%2 Adressen aendern sich.",
         "Snooper is delivering data: %1/%2 addresses are changing."},
        {"Spiel laeuft (%1 Adressen aktiv) -- alle Achievements scharf.",
         "Game running (%1 addresses active) -- all achievements armed."},
        {"Spiel wurde neu gestartet -- Achievement-Zustand zurueckgesetzt.",
         "Game was restarted -- achievement state reset."},
        {"Verbunden auf ",
         "Connected on "},
        {"WARNUNG: nach 3s keine einzige Wertaenderung%1.\n  Moegliche Gruende:\n  1. Laeuft am Geraet die aktuelle top.rbf mit dem Spiegel-Snooper?\n  2. Laeuft ueberhaupt ein Spiel (nicht das Menue)?\n  3. Beschreibt dieses Spiel die Adressen ueberhaupt?",
         "WARNING: not a single value change after 3s%1.\n  Possible reasons:\n  1. Is the current top.rbf with the mirror snooper running on the device?\n  2. Is a game running at all (not the menu)?\n  3. Does this game write to these addresses at all?"},
        {"\n--- %1 (%2P) ---",
         "\n--- %1 (%2P) ---"},
        {"] nicht in der Sammlung.",
         "] not in the collection."},
        {"aktuelle Werte: ",
         "current values: "},
        {"eingereicht",
         "submitted"},
        {"gebucht",
         "recorded"},
        {"gemeldet",
         "reported"},
        {"ja",
         "yes"},
        {"nein",
         "no"},
        {"nicht snoopbare Adressen: ",
         "non-snoopable addresses: "},
        {"noch keine Messwerte -- Monitor starten.",
         "no measurements yet -- start the monitor."},
        {"wartet noch auf bestaetigte Adressen (Schutz gegen Fehlbuchungen).",
         "still waiting for confirmed addresses (protection against false submissions)."},
        {"%1 Pkt",
         "%1 pts"},
        {"Leaderboard #%1",
         "Leaderboard #%1"},
        {"— v0.1 build %1 — und immer noch nicht perfekt —",
         "— v0.1 build %1 — and still not perfect —"},
        {"Verbinden",
         "Connect"},
        {"Leaderboards ein-/ausklappen",
         "Show/hide leaderboards"},
        {"Eingeloggt bleiben",
         "Stay logged in"},
        {"Benutzername und Passwort in der Konfiguration speichern",
         "Save username and password in the configuration"},
        {"ACHIEVEMENT FREIGESCHALTET!",
         "ACHIEVEMENT UNLOCKED!"},
    };
    return t;
}

class RawnesTranslator : public QTranslator {
public:
    // Leerer Rueckgabewert bedeutet fuer Qt "keine Uebersetzung vorhanden",
    // dann wird der Originaltext benutzt -- genau das gewuenschte Verhalten
    // fuer alles, was (noch) nicht in der Tabelle steht.
    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr,
                      int n = -1) const override {
        Q_UNUSED(context); Q_UNUSED(disambiguation); Q_UNUSED(n);
        if (!sourceText) return QString();
        const auto& t = rawnesTableEn();
        auto it = t.constFind(QString::fromUtf8(sourceText));
        return (it == t.constEnd()) ? QString() : it.value();
    }

    // QTranslator::isEmpty() prueft auf geladene Dateidaten und waere hier
    // immer true -- installTranslator wuerde den Translator dann ignorieren.
    bool isEmpty() const override { return false; }
};

// ---- Rueckwaerts-Tabelle und Helfer fuer das Umschalten im laufenden Betrieb ----

// Englisch -> Deutsch, aus der Vorwaertstabelle erzeugt. Wird gebraucht, um
// beim Merken der Originaltexte den deutschen Quelltext zu bestimmen, auch
// wenn das Programm bereits auf Englisch gestartet wurde.
inline const QHash<QString, QString>& rawnesTableDe() {
    static const QHash<QString, QString> t = []{
        QHash<QString, QString> r;
        const auto& fwd = rawnesTableEn();
        for (auto it = fwd.constBegin(); it != fwd.constEnd(); ++it) {
            r.insert(it.value(), it.key());
        }
        return r;
    }();
    return t;
}

// Der Translator lebt als Singleton, damit er beim Umschalten sauber
// installiert und wieder entfernt werden kann.
inline RawnesTranslator& rawnesTranslator() {
    static RawnesTranslator t;
    return t;
}

inline void rawnesApplyLanguage(QCoreApplication* app, const QString& lang) {
    if (!app) return;
    if (lang == "en") app->installTranslator(&rawnesTranslator());
    else app->removeTranslator(&rawnesTranslator());
}

// Deutscher Quelltext -> Anzeigetext in der gewuenschten Sprache.
inline QString rawnesText(const QString& quelle, bool englisch) {
    if (!englisch) return quelle;
    const auto& t = rawnesTableEn();
    auto it = t.constFind(quelle);
    return (it == t.constEnd()) ? quelle : it.value();
}

// Aktuell angezeigter Text -> deutscher Quelltext (fuer das Merken).
inline QString rawnesQuelle(const QString& angezeigt) {
    const auto& r = rawnesTableDe();
    auto it = r.constFind(angezeigt);
    return (it == r.constEnd()) ? angezeigt : it.value();
}

// Wurde der Text seit dem Aufbau NICHT veraendert? Nur dann darf er beim
// Sprachwechsel ersetzt werden -- sonst wuerde eine Laufzeitmeldung wie
// "verbunden (COM10)" auf ihren Startwert zurueckspringen.
inline bool rawnesUnveraendert(const QString& aktuell, const QString& quelle) {
    if (aktuell == quelle) return true;
    const auto& t = rawnesTableEn();
    auto it = t.constFind(quelle);
    return it != t.constEnd() && aktuell == it.value();
}
