# RAW-NES

**RetroAchievements on original NES hardware** — via EverDrive N8 PRO, no emulator.

RAW-NES tracks and unlocks [RetroAchievements](https://retroachievements.org)
while you play on a **real NES** using an **EverDrive N8 PRO**. No emulator, no
ROM patching. Log in, start a game on the console, and RAW-NES identifies it,
reads the console's RAM live over USB, and submits achievements automatically.

It is the NES counterpart to MEGA-RAW (Sega Mega Drive) by the same author.

---

## English

### How it works

RAW-NES uses an FPGA **bus snooper**: a custom FPGA mapper on the EverDrive N8
PRO mirrors the console's internal 2 KB RAM into FPGA block RAM by watching the
CPU bus directly. The PC reads those values over USB — without modifying the ROM
or injecting code. The game runs exactly as on a stock cartridge.

- **Automatic game identification** — no file dialog: RAW-NES recognizes the
  running game by fingerprint and loads the matching achievement set.
- **No ROM patching** — the snooper reads RAM passively; the game is untouched.
- **Live value display** — watched RAM addresses update in real time; changed
  values flash gold and fade back to green.
- **Achievement links** — click an achievement to open it on RetroAchievements.
- **Automatic FPGA-mapper installer** — installs the snooper mappers onto the SD
  card and backs up the originals first (restore any time).
- **Bilingual** — German / English, switchable at runtime (no restart).
- **Update check** — notifies you when a newer version is available.

### Requirements

- **EverDrive N8 PRO** with SD card
- **Windows** PC with a USB connection to the cartridge
- **Python 3.8+**
- A **RetroAchievements** account

### Installation

1. Unpack the RAW-NES folder anywhere.
2. Run **`install.bat`** (or `python install.py`). It checks every dependency
   step by step and installs only what is missing, after asking. Packages:
   `pyserial` (required), `pywebview` (intro), `py7zr` (optional, for `.7z`
   game archives).
3. Start RAW-NES: `python rawnes_gui_v3_final.py`

An uninstall script that reverses these steps ships alongside the
installer: **`uninstall.bat`** (or `python uninstall.py`).

### First run — installing the FPGA mappers

On first start, RAW-NES offers to install its FPGA mappers onto the EverDrive
SD card. Insert the SD into your PC, pick the drive (the one with the `EDN8`
folder), and confirm. RAW-NES backs up the original Krikzz mappers to
`EDN8/MAPS/ORIG_BACKUP/` before replacing them — you can restore them any time
with the **Restore originals** button. After installing, safely eject the SD,
insert it into the EverDrive, and cold-start the console.

Skipped this or need to redo it later? Open the options menu and choose
**"🎮 Set up FPGA mappers"** — it runs the same setup on demand.

### Usage

1. **Log in** to RetroAchievements (credentials are remembered).
2. **Index your game folder** once, so RAW-NES can identify games without a
   file dialog.
3. **Start a game** on the console (the reset clears the snooper's watch flags,
   so the game must be running before monitoring starts).
4. Click **Start monitor**. RAW-NES identifies the running game by fingerprint,
   loads the matching achievement set, and begins polling. Unlocks are submitted
   automatically.

### Modes

RAW-NES runs in **Softcore** mode. Hardcore is not yet available: the EverDrive
N8 PRO offers savestates, which conflict with hardcore rules, and reliable
savestate detection is still in development.

### Known limitations

- Game identification uses RAM/vector fingerprinting, since the N8 firmware has
  no "which game is running" command.

### License

© 2026 Liqui. Use and distribution permitted for private, non-commercial
purposes. No warranty. Not an official RetroAchievements product.

RetroAchievements is a trademark of its respective owners. EverDrive and
EverDrive N8 PRO are products of Krikzz.

---

## Deutsch

**RetroAchievements auf original NES-Hardware** — über EverDrive N8 PRO, ohne Emulator.

RAW-NES erfasst und schaltet [RetroAchievements](https://retroachievements.org)
frei, während du auf einer **echten NES-Konsole** mit **EverDrive N8 PRO**
spielst. Kein Emulator, kein ROM-Patch. Anmelden, Spiel auf der Konsole
starten — RAW-NES erkennt es, liest das RAM der Konsole live über USB aus
und übermittelt Achievements automatisch.

Es ist das NES-Gegenstück zu MEGA-RAW (Sega Mega Drive) vom selben Autor.

### Funktionsweise

RAW-NES verwendet einen FPGA-**Bus-Snooper**: Ein eigener FPGA-Mapper auf dem
EverDrive N8 PRO spiegelt die internen 2 KB RAM der Konsole in FPGA-Block-RAM,
indem er den CPU-Bus direkt mitliest. Der PC liest diese Werte über USB — ohne
das ROM zu verändern. Das Spiel läuft exakt wie mit einem Original-Modul.

- **Automatische Spielerkennung** — kein Dateidialog: RAW-NES erkennt das
  laufende Spiel per Vektorabgleich (Reset-/NMI-Vektoren) und lädt das
  passende Achievement-Set.
- **Kein ROM-Patch** — der Snooper liest das RAM passiv; das Spiel bleibt
  unangetastet.
- **Live-Werte-Anzeige** — geänderte Werte blitzen golden auf und verblassen
  zurück zu Grün.
- **Achievement-Links** — Klick auf ein Achievement öffnet es bei RetroAchievements.
- **Automatischer FPGA-Mapper-Installer** — installiert die Snooper-Mapper auf
  die SD und sichert vorher die Originale (jederzeit wiederherstellbar).
- **Zweisprachig** — Deutsch / Englisch, zur Laufzeit umschaltbar (ohne
  Neustart).
- **Update-Prüfung** — benachrichtigt bei neuerer Version.

### Voraussetzungen

- **EverDrive N8 PRO** mit SD-Karte
- **Windows**-PC mit USB-Verbindung zum Modul
- **Python 3.8+**
- Ein **RetroAchievements**-Konto

### Installation

1. Den RAW-NES-Ordner beliebig entpacken.
2. **`install.bat`** starten (oder `python install.py`). Es prüft jede
   Abhängigkeit Schritt für Schritt und installiert nur, was fehlt — nach
   Rückfrage. Pakete: `pyserial` (nötig), `pywebview` (Intro), `py7zr`
   (optional, für `.7z`-Archive).
3. RAW-NES starten: `python rawnes_gui_v3_final.py`

Dem Installationsskript liegt ein Deinstallations-Skript bei, das diese
Schritte bei Bedarf rückgängig macht: **`uninstall.bat`** (oder
`python uninstall.py`).

### Erster Start — FPGA-Mapper installieren

Beim ersten Start bietet RAW-NES an, seine FPGA-Mapper auf die EverDrive-SD zu
installieren. SD in den PC stecken, das Laufwerk wählen (das mit dem
`EDN8`-Ordner) und bestätigen. RAW-NES sichert die Original-Krikzz-Mapper vor
dem Ersetzen nach `EDN8/MAPS/ORIG_BACKUP/` — mit **Originale wiederherstellen**
jederzeit zurücksetzbar. Nach der Installation SD sicher auswerfen, in den
EverDrive stecken, Konsole kaltstarten.

Übersprungen oder später nochmal nötig? Im Options-Menü **"🎮 FPGA-Mapper
einrichten"** wählen — startet dieselbe Einrichtung erneut.

### Bedienung

1. **Anmelden** bei RetroAchievements (Zugangsdaten werden gemerkt).
2. **Spielordner einmal indizieren**, damit RAW-NES Spiele ohne Dateidialog
   erkennt.
3. **Spiel am Gerät starten** (der Reset löscht die Watch-Flags des Snoopers,
   das Spiel muss also laufen, bevor die Überwachung startet).
4. **Monitor starten** klicken. RAW-NES erkennt das Spiel per Vektorabgleich,
   lädt das passende Achievement-Set und beginnt mit dem Polling. Freischaltungen
   werden automatisch übermittelt.

### Modi

RAW-NES läuft im **Softcore**-Modus. Hardcore ist noch nicht verfügbar: Der
EverDrive N8 PRO bietet Savestates, die den Hardcore-Regeln widersprechen, und
eine zuverlässige Savestate-Erkennung ist noch in Entwicklung.

### Bekannte Einschränkungen

- Die Spielerkennung erfolgt per Vektorabgleich (Reset-/NMI-Vektoren), da die
  N8-Firmware keinen Befehl für das laufende Spiel hat.

### Lizenz

© 2026 Liqui. Nutzung und Weitergabe nur für private, nicht-kommerzielle
Zwecke gestattet. Keine Gewährleistung. Kein offizielles
RetroAchievements-Produkt.

RetroAchievements ist eine Marke der jeweiligen Rechteinhaber. EverDrive und
EverDrive N8 PRO sind Produkte von Krikzz.
