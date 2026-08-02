# RAW-NES C++/Qt6-Port — Übergabe (Stand Build 35)

## Umgebung
- Quellen: `C:\raw-nes\cpp\src\`, Build-Ordner: `C:\raw-nes\cpp\build3`
- `CMakeLists.txt` listet Quellen **explizit** (kein GLOB). `AUTOMOC ON`, `AUTORCC ON`,
  `src/resources.qrc` ist bereits eingetragen (Zeilen 38 und 53) — für Header-only-Dateien
  wie `i18n.h` ist **kein** Eintrag nötig.
- Standard-Bauablauf (Timestamp-Refresh ist Pflicht, sonst überspringt make den Rebuild):

```powershell
cd C:\raw-nes\cpp\build3
Get-ChildItem ..\src\<dateien> | ForEach-Object { $_.LastWriteTime = Get-Date }
cmake --build . --target rawnes_gui
```
- Betrifft die Änderung `monitor_worker.cpp`, `ed_serial_qt.*`, `fpga_installer.cpp` oder
  andere Core-Dateien, vorher:
  `Remove-Item -Recurse -Force CMakeFiles\rawnes_core.dir` → `cmake ..` →
  `cmake --build . --target rawnes_core`
- Header geändert und kein "Building CXX object" sichtbar:
  `Remove-Item -Recurse -Force CMakeFiles\rawnes_gui.dir\rawnes_gui_autogen`

## In dieser Session erledigt (Build 19–35)

| Thema | Ergebnis |
|---|---|
| Verbindungsdauer | VID/PID-Filter in `find_everdrive`, Handshake-Retry auf demselben Port, `memrdNoRecover` für den Build-Check. Funkstille-Deckel bewusst **wieder bei 3000 ms** — Senkung war die falsche Stellschraube. |
| Thread-Shutdown | `stop()` wird direkt aufgerufen statt per `QueuedConnection` (setzt nur ein `std::atomic<bool>`). Behebt "Destroyed while thread is still running" **und** den toten Stop-Knopf. |
| RAM-Anzeige | Dreispaltig, scrollbar (112 px = 4 Zeilen), Änderung leuchtet gold und glimmt über 1 s zu `#3dd68c` zurück. Fade-Timer, weil die Farbe sonst einfror. |
| FPGA-Mapper | `copyOverwrite()` (Schreibschutz weg → löschen → kopieren) behebt MinGWs "File exists" bei Install **und** Restore; Zeitstempel wird übernommen. `defaultCoresDir()` sucht 6 Kandidatenpfade. Menüeintrag "Original-Mapper wiederherstellen" ergänzt (war vorher von der Oberfläche aus unerreichbar). SD-Pfad und cores-Ordner in der Config. |
| Leerlauf-Erkennung | Fingerabdruck aus lauter `FF`/`00` bricht die Erkennung ab, statt ein fremdes Spiel zu raten. Build-Register `0xFF` gilt nicht mehr als Build-Nummer. |
| Intro | Echter 1:1-Port aus `rawnes_intro.html` (620 Zeitpunkte gegen das JS geprüft, 0 Abweichungen). 12 Frames aus der HTML extrahiert, Maße stimmen exakt mit `FMETA`. Fenstergröße = Hauptfenstergröße, Position wird an `MainWindow` weitergereicht. |
| i18n | `i18n.h` header-only: `RawnesTranslator` beantwortet `tr()` aus einer Tabelle (188 Einträge), **keine `.qm`-Dateien**. Umschaltung ohne Neustart über `rememberSourceTexts()`/`retranslateAll()`. |
| Leaderboards | Panel als **Sidebar** rechts, Griff am Rand, Fenster wächst 760 → 1072 px, Zustand in Config (`lb_sidebar`). |

## Offene Punkte
1. **"Eingeloggt bleiben"-Checkbox** — Zugangsdaten liegen bereits in
   `rawnes_gui_config.json` (`ra_user`, `ra_pass`); es fehlt die Wahlmöglichkeit,
   das zu unterlassen.
2. **RAM-Werte anklickbar** — waren im C++-Port **nie** vorhanden (im Original ohne
   `mousePressEvent`). Die Python-Version soll das können; ohne die Python-Quelle ist
   unklar, was der Klick auslösen soll. Klickbar ist bisher nur die Achievement-Liste.
3. **`QFont::setPointSize: Point size <= 0`** — jede Sitzung einmal im `error_log.txt`.
   Unter Linux/Qt 6.4 nicht reproduzierbar, im Code kein `setPointSize`. Rein kosmetisch.
4. **Sidebar-Feinschliff** — Breite ist fix (300 px); ziehbar via `QSplitter` wäre denkbar.
5. **Hardware-Gegenprobe Hardcore** — der Softcore-Fall (eines der beiden EverDrive-Bits
   an) ist noch nicht geprüft.

## Wichtige Erkenntnisse (nicht verlieren)
- **Base64 in HTML niemals einlesen.** `rawnes_intro.html` sind 855 751 Zeichen, davon
  846 000 Bilddaten (~210 000 Token). Filterskript vorschalten:
  `re.sub(r'"([A-Za-z0-9+/=]{200,})"', ...)`. Die PNG-Frames lassen sich mit
  `base64.b64decode` direkt daraus extrahieren, ohne sie anzusehen.
- **Die GUI lässt sich im Container real bauen und testen** (offscreen). Nötig dafür:
  `apt-get install qt6-base-dev qt6-serialport-dev`, `nlohmann/json.hpp` von GitHub,
  ein miniz-Stub, `-D_popen=popen -D_pclose=pclose`, `main.cpp` beim Linken auslassen,
  modale Dialoge per Timer schließen. Damit sind Widget-Tests statt bloßer Syntaxchecks
  möglich — hat mehrere Fehldiagnosen verhindert.
- **Testzustand säubern:** `rawnes_gui_config.json` im Testverzeichnis vor jedem Lauf
  löschen, sonst verfälschen gespeicherte Werte das Ergebnis (hat einmal einen
  Fehlalarm "Umschaltung funktioniert nicht" erzeugt).
- **Suchmuster prüfen, bevor Zahlen genannt werden.** "92 untranslatierte Texte" und
  später "28" stammten aus einem zu groben Regex, der Folgeliterale statt der
  Argumente fand. Tatsächlich waren es drei.
- **Laufzeittexte beim Sprachwechsel:** `retranslateAll()` ersetzt nur, was seit dem
  Aufbau unverändert ist. Texte, die zur Laufzeit zusammengesetzt werden
  (Verbindungszeile mit Port, Untertitel mit Buildnummer), brauchen dort einen
  **expliziten Sonderfall** — sonst bleiben sie in der alten Sprache stehen.
