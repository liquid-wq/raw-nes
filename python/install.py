#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RAW-NES Installer
Prueft alle Abhaengigkeiten Schritt fuer Schritt und installiert nur, was
fehlt -- nach Rueckfrage. Aendert nichts ohne Zustimmung.

Start: python install.py    (oder install.bat doppelklicken)
"""

import sys
import subprocess
import importlib.util

# (Modulname zum Importieren, pip-Paketname, wofuer)
ABHAENGIGKEITEN = [
    ("serial",   "pyserial", "USB-Kommunikation mit dem EverDrive N8 PRO"),
    ("webview",  "pywebview", "Intro-Fenster (HTML)"),
    ("py7zr",    "py7zr",    "Spiele aus .7z-Archiven laden (optional)"),
]

MIN_PYTHON = (3, 8)


def linie():
    print("-" * 60)


def frage_ja(text):
    while True:
        a = input(text + " [j/n]: ").strip().lower()
        if a in ("j", "ja", "y", "yes"):
            return True
        if a in ("n", "nein", "no"):
            return False
        print("  Bitte j oder n eingeben.")


def modul_vorhanden(modulname):
    return importlib.util.find_spec(modulname) is not None


def pip_install(paket):
    print(f"  Installiere {paket} ...")
    try:
        subprocess.check_call(
            [sys.executable, "-m", "pip", "install", paket])
        return True
    except subprocess.CalledProcessError:
        return False


def main():
    print()
    linie()
    print("  RAW-NES  -  Installation der Abhaengigkeiten")
    linie()
    print()

    # Schritt 1: Python-Version
    print("Schritt 1/2:  Python-Version pruefen")
    v = sys.version_info
    print(f"  Gefunden: Python {v.major}.{v.minor}.{v.micro}")
    if (v.major, v.minor) < MIN_PYTHON:
        print(f"  FEHLER: RAW-NES braucht mindestens Python "
              f"{MIN_PYTHON[0]}.{MIN_PYTHON[1]}.")
        print("  Bitte eine neuere Python-Version installieren: "
              "https://www.python.org/downloads/")
        input("\n  Enter zum Beenden...")
        return
    print("  OK -- Python-Version passt.")
    print()

    # Schritt 2: pip-Pakete einzeln
    print("Schritt 2/2:  Benoetigte Pakete pruefen")
    print()
    fehlend = []
    for modul, paket, zweck in ABHAENGIGKEITEN:
        da = modul_vorhanden(modul)
        status = "vorhanden" if da else "FEHLT"
        print(f"  [{status:>9}]  {paket:<12} -- {zweck}")
        if not da:
            fehlend.append((modul, paket, zweck))
    print()

    if not fehlend:
        linie()
        print("  Alles vorhanden. RAW-NES ist startklar.")
        print("  Starten mit:  python rawnes_gui_v3_final.py")
        linie()
        input("\n  Enter zum Beenden...")
        return

    print(f"Es fehlen {len(fehlend)} Paket(e).")
    if not frage_ja("Jetzt installieren?"):
        print("  Abgebrochen. RAW-NES laeuft erst, wenn die Pakete "
              "installiert sind.")
        input("\n  Enter zum Beenden...")
        return
    print()

    # pip selbst pruefen
    if not modul_vorhanden("pip"):
        print("  FEHLER: pip ist nicht verfuegbar. Bitte Python mit pip "
              "neu installieren.")
        input("\n  Enter zum Beenden...")
        return

    erfolg, gescheitert = [], []
    for modul, paket, zweck in fehlend:
        print(f"\n-- {paket} ({zweck})")
        if paket == "py7zr":
            if not frage_ja(f"  {paket} ist optional. Installieren?"):
                print("  Uebersprungen.")
                continue
        if pip_install(paket):
            erfolg.append(paket)
            print(f"  OK -- {paket} installiert.")
        else:
            gescheitert.append(paket)
            print(f"  FEHLER bei {paket}.")

    print()
    linie()
    if erfolg:
        print("  Installiert:", ", ".join(erfolg))
    if gescheitert:
        print("  Fehlgeschlagen:", ", ".join(gescheitert))
        print("  Diese bitte manuell installieren: "
              "pip install " + " ".join(gescheitert))
    else:
        print("  Fertig. RAW-NES ist startklar.")
        print("  Starten mit:  python rawnes_gui_v3_final.py")
    linie()
    input("\n  Enter zum Beenden...")


if __name__ == "__main__":
    main()
