# RAW-NES (C++ / Qt6)

[English](README.md) | Deutsch

RetroAchievements auf einer echten NES, per USB über den EverDrive N8 PRO.
Kein Emulator im Spiel.

![RAW-NES in Aktion](../docs/rawnes_uebersicht.gif)

Dies ist der C++-Port von RAW-NES und die Fassung, in der die
Weiterentwicklung stattfindet. Gleicher Ansatz und dieselben FPGA-Kerne wie
die Python-Fassung, native Programmdatei, keine Python-Installation nötig.

Eine Übersicht über beide Fassungen steht in der
[Haupt-README](../README.de.md).

---

## Was es macht

Ein eigener FPGA-Kern auf dem EverDrive belauscht den CPU-Bus der NES und
spiegelt den Arbeitsspeicher der Konsole in FPGA-Block-RAM. RAW-NES liest
diese Werte live über USB, wertet die RetroAchievements-Bedingungen auf dem
PC aus und schaltet Achievements auf den RetroAchievements-Servern frei,
während du auf Originalhardware spielst.

* Live-Verfolgung der Achievements, einschließlich Hit-Count-Bedingungen
* Leaderboards
* Hardcore-Modus mit Prüfung, ob Savestates und Cheats abgeschaltet sind
* Automatische Spielerkennung über den Vektor-Fingerabdruck des ROMs
* Deutsch und Englisch, ohne Neustart umschaltbar

---

## Voraussetzungen

* NES oder kompatible Konsole
* EverDrive N8 PRO mit USB-Kabel zum PC
* Windows 64-Bit
* Ein RetroAchievements-Konto
* Deine NES-ROM-Sammlung auf dem PC (`.nes`, `.zip`, `.7z`)

---

## Einrichtung

**1. Entpacken** in einen Ordner deiner Wahl. Der Ordner `cores` bleibt neben
`rawnes_gui.exe` liegen.

**2. FPGA-Kerne einrichten.** SD-Karte des EverDrive in den PC stecken,
RAW-NES starten, dann *Optionen -> FPGA-Mapper einrichten*. Das ersetzt die
Mapper-Kerne in `EDN8\MAPS\` durch die RAW-NES-Fassungen und sichert die
Originale nach `EDN8\MAPS\ORIG_BACKUP\`. *Optionen -> Original-Mapper
wiederherstellen* macht das jederzeit rückgängig.

**3. Anmelden** mit deinem RetroAchievements-Benutzernamen und Passwort.

**4. ROM-Sammlung indizieren.** *ROM-Sammlung indizieren...*, dann den Ordner
auswählen. Dabei werden die Interrupt-Vektoren jedes ROMs einmal gelesen,
damit das laufende Spiel ohne Dateidialog erkannt wird. Nur nötig, wenn sich
die Sammlung ändert.

**5. Spiel starten** auf der Konsole, dann *Spiel laden & Monitor starten*.

---

## Hardcore-Modus

Leaderboards und Hardcore-Freischaltungen zählen nur, wenn Savestates und
Cheats am EverDrive selbst abgeschaltet sind. Das geschieht im
EverDrive-System-Menü: *Filebrowser -> \[SELECT] -> Options -> "In Game Menu"
aus, "Cheats" aus*. Das Ingame-Menü ist etwas anderes.

RAW-NES liest das Kontrollbyte des EverDrive und fällt auf Softcore zurück,
wenn eines von beiden aktiv ist: beim Start der Sitzung, vor dem Polling und
nach jedem Konsolen-Reset. Abschalten kann es die Optionen nicht für dich.

---

## Wenn etwas nicht funktioniert

**Nicht gefunden -- Konsole an? Spiel gestartet?**
Der EverDrive antwortet über USB nur, während ein Spiel läuft. Erst das Spiel
starten, dann verbinden.

**Abweichende FPGA-Version**
Auf der SD-Karte liegen nicht die RAW-NES-Mapper. *Optionen -> FPGA-Mapper
einrichten* erneut ausführen.

**Alle Werte stehen auf FF**
Gleiche Ursache: Es liegen die Krikzz-Originale auf der Karte.

**Unsupported Game Version**
Für das laufende ROM gibt es bei RetroAchievements kein Achievement-Set, oder
es wurde eine andere Regionalfassung gewählt. Monitor stoppen, neu starten
und bei der Rückfrage die richtige auswählen.

---

## Fehler melden

Ausschließlich über GitHub Issues:
https://github.com/liquid-wq/raw-nes/issues

Bitte das Log aus dem Programmfenster und die Buildnummer angeben, die unter
dem Titel steht.

---

## Lizenz

Quelloffen einsehbar, aber nicht frei verwendbar. Siehe `LICENSE`.
Fremdkomponenten und deren Lizenzen stehen in `THIRD-PARTY-NOTICES.txt`.

Erstellt von Liqui -- https://github.com/liquid-wq
