#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RAW-NES Deinstallation
Entfernt die von RAW-NES genutzten Python-Pakete und optional die
gespeicherten Einstellungen. Fragt vor jedem Schritt nach.

WICHTIG: Die Pakete (pyserial usw.) koennten auch von anderen Programmen
genutzt werden. Das Script fragt daher einzeln und entfernt nichts
automatisch.

Start: python uninstall.py    (oder uninstall.bat doppelklicken)
"""

import sys
import os
import subprocess

# Diese Pakete hat der Installer ggf. installiert.
PAKETE = [
    ("pyserial", "USB-Kommunikation -- evtl. von anderen Tools genutzt"),
    ("pywebview", "Intro-Fenster"),
    ("py7zr",    "7z-Entpacken (optional)"),
]

CONFIG_DATEIEN = [
    "rawnes_gui_config.json",
    "rawnes_rom_index.json",
]


def linie():
    print("-" * 60)


def frage_ja(text, default_nein=True):
    hinweis = "[j/N]" if default_nein else "[J/n]"
    while True:
        a = input(text + f" {hinweis}: ").strip().lower()
        if a == "":
            return not default_nein
        if a in ("j", "ja", "y", "yes"):
            return True
        if a in ("n", "nein", "no"):
            return False
        print("  Bitte j oder n eingeben.")


def pip_uninstall(paket):
    try:
        subprocess.check_call(
            [sys.executable, "-m", "pip", "uninstall", "-y", paket])
        return True
    except subprocess.CalledProcessError:
        return False


def main():
    print()
    linie()
    print("  RAW-NES  -  Deinstallation")
    linie()
    print()
    print("Dieses Script entfernt die Python-Pakete und Einstellungen von")
    print("RAW-NES. Die Programmdateien selbst (die .py, das Intro, die")
    print("cores) loeschst du einfach von Hand -- es ist nur ein Ordner.")
    print()
    print("ACHTUNG: Die Pakete koennten auch von anderen Programmen genutzt")
    print("werden. Entferne sie nur, wenn du sicher bist.")
    print()

    if not frage_ja("Fortfahren?"):
        print("  Abgebrochen. Nichts wurde geaendert.")
        input("\n  Enter zum Beenden...")
        return
    print()

    # Schritt 1: Pakete einzeln
    print("Schritt 1/2:  Python-Pakete")
    entfernt = []
    for paket, zweck in PAKETE:
        print(f"\n-- {paket} ({zweck})")
        if frage_ja(f"  {paket} entfernen?"):
            if pip_uninstall(paket):
                entfernt.append(paket)
                print(f"  OK -- {paket} entfernt.")
            else:
                print(f"  {paket} war nicht installiert oder Fehler.")
        else:
            print("  Behalten.")
    print()

    # Schritt 2: Einstellungen
    print("Schritt 2/2:  Gespeicherte Einstellungen")
    hier = os.path.dirname(os.path.abspath(__file__))
    vorhanden = [f for f in CONFIG_DATEIEN
                 if os.path.isfile(os.path.join(hier, f))]
    if not vorhanden:
        print("  Keine Einstellungsdateien gefunden.")
    else:
        print("  Gefunden:", ", ".join(vorhanden))
        if frage_ja("  Einstellungen (Login, ROM-Index) loeschen?"):
            for f in vorhanden:
                try:
                    os.remove(os.path.join(hier, f))
                    print(f"  Geloescht: {f}")
                except Exception as e:
                    print(f"  Fehler bei {f}: {e}")
        else:
            print("  Behalten.")

    print()
    linie()
    if entfernt:
        print("  Entfernte Pakete:", ", ".join(entfernt))
    print("  Fertig. Den RAW-NES-Ordner kannst du jetzt loeschen.")
    linie()
    input("\n  Enter zum Beenden...")


if __name__ == "__main__":
    main()
