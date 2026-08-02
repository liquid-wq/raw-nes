#!/usr/bin/env python3
"""
rawnes_gui_v2.py -- RAW-NES Play-Monitor (Weg B: FPGA-Snooper + memrd-Polling)

Basiert auf dem hardware-bestaetigten Stand:
  - Polling-Logik 1:1 aus poll_snoop.py (set_watch / read_slot, ADDR_SNOOP)
  - Achievement-/Award-Logik 1:1 aus der alten rawnes_gui.py (Z. 995-1045)
Kein FIFO, kein Stub, kein ROM-Patch.

Ablauf:
  1. GUI startet -> Autoconnect zum EverDrive, RA-Login (falls gespeichert)
  2. Spiel AM GERAET starten (wichtig: sys_rst loescht Watch-Flags)
  3. "Spiel laden & Monitor starten" -> ROM waehlen -> RA-Set laden ->
     Watches setzen -> Polling + Achievement-Tracking

Dependencies: pyserial
Erwartet im selben Ordner: ed_serial_nes.py, ra_client_nes.py, ra_condition_nes.py
"""

import base64
import json
import os
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import webbrowser

GUI_BUILD = 22          # bei jeder Aenderung hochzaehlen
FPGA_BUILD_EXPECTED = 11  # dazu passende Version im FPGA
RAWNES_VERSION = f"v3.0 build {GUI_BUILD}"
# Update-Pruefung: einfache Zahl in der Datei, hoeher = neuere Version.
UPDATE_URL = ("https://raw.githubusercontent.com/liquid-wq/data/main/"
              "version_rawnes_python.txt")
UPDATE_PAGE = "https://github.com/liquid-wq/raw-nes/releases/latest"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(SCRIPT_DIR, "rawnes_gui_config.json")
ROM_INDEX_PATH = os.path.join(SCRIPT_DIR, "rawnes_rom_index.json")
# Snooper-FPGA-Kerne, die auf die SD ins EDN8/MAPS/ gehoeren. Liegen im
# Unterordner "cores" neben der GUI und sind Teil des Release-Pakets.
CORES_DIR = os.path.join(SCRIPT_DIR, "cores")
CORE_FILES = ("000.RBF", "001.RBF", "004.RBF", "005.RBF", "021.RBF")
# Vor dem Ersetzen werden die Krikzz-Original-Mapper hierhin gesichert,
# damit der Nutzer sie jederzeit wiederherstellen kann.
BACKUP_SUBDIR = "ORIG_BACKUP"

# ================= NES-Look (Orientierung: MEGA-RAW) =================
# Schwarzer Grund, graue Flaechen, gruene Akzentschrift -- Terminal-Stil.
COL_BG      = "#000000"   # Fenster-Hintergrund (schwarz)
COL_PANEL   = "#1a1a1a"   # Flaechen/Rahmen (dunkelgrau)
COL_PANEL2  = "#2a2a2a"   # abgesetzte Flaechen (Log, Listen)
COL_GREEN   = "#3ad13a"   # Akzent-/Titelgruen
COL_GREEN_D = "#2a9a2a"   # gedaempftes Gruen
COL_TEXT    = "#c8c8c8"   # normale Schrift (hellgrau)
COL_DIM     = "#808080"   # Nebentext (grau)
COL_RED     = "#d15a5a"   # Fehler/Getrennt
COL_BTN     = "#242424"   # Button-Flaeche
COL_BTN_H   = "#333333"   # Button Hover
COL_GREEN_H = "#5ae85a"   # Gruen Hover (heller)
COL_RED_H   = "#e87a7a"   # Rot Hover (heller)
COL_SEP     = "#3a3a3a"   # Trennlinien-Grau (NES-Gehaeuse)
# Gold-Blitz fuer geaenderte Werte, verblasst ueber Stufen zurueck zu Gruen.
GOLD_FADE = ["#ffe14d", "#f5d13a", "#d4b02a", "#a89020",
             "#7a7a2a", "#5a8a2a", "#3ad13a"]

# Jason-Pixel-Portrait, direkt eingebettet (kein assets-Ordner noetig).
JASON_PNG_B64 = (
    "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"
    "AElFTkSuQmCC"
)
sys.path.insert(0, SCRIPT_DIR)

# ce_snoop = 32 KB Fenster @ 0x1808000 (pi.sv: addr[15] == 1)
#   0x0000-0x07FF  interner RAM       Werte
#   0x0800-0x0807  Vektoren $FFF8-$FFFF
#   0x0810         Build-Nummer
#   0x0811         Reset-Zaehler
#   0x0900-0x09FF  letzte PRG-Page    Werte
#   0x0A00-0x0AFF  dito               gelesen? (Bit 0)
#   0x1000-0x17FF  interner RAM       beschrieben? (Bit 0)
#   0x2000-0x3FFF  WRAM               Werte
#   0x4000-0x5FFF  WRAM               beschrieben? (Bit 0)
ADDR_SNOOP = 0x1808000
WRAM_OFFSET = 0x2000
VEC_OFFSET = 0x0800
BUILD_OFFSET = 0x0810
RST_OFFSET = 0x0811
PAGE_OFFSET = 0x0900
SEEN_OFFSET = 0x0A00
RAM_MARK_OFFSET = 0x1000
WRAM_MARK_OFFSET = 0x4000
ADDR_CFG = 0x1800020     # Mapper-Konfiguration des laufenden Spiels (sys_cfg.sv)
POLL_INTERVAL = 0.1
PING_INTERVAL = 120.0

# Modus-Standard: Softcore. Der EverDrive N8 PRO bietet Savestates, die
# Hardcore-Regeln verletzen wuerden. Echtes Hardcore erfordert eine
# Savestate-Erkennung (geplant: Erkennung des abrupten RAM-Sprungs beim
# Laden eines Savestates). Bis dahin ist Hardcore in der GUI deaktiviert.
HARDCORE_DEFAULT = False

# Vereinzelte Lesefehler sind im Betrieb normal. Erst nach mehreren in
# Folge wird aufgegeben.
LESEFEHLER_MAX = 10
LESEFEHLER_PAUSE = 0.25

JASON_ABOUT_DE = (
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
    "Das Software-Logo ist ein Pixel-Portrait zur Erinnerung an "
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
    "In memory of Jason — 2010–2025.")

JASON_ABOUT_EN = (
    "I chose my cat as the program's logo. The following lines are "
    "dedicated to him — a small homage and a remembrance.\n\n"
    "One in three cats aged 10–12 or older suffers from CKD (chronic "
    "kidney disease). Sadly, the illness often only becomes visible "
    "once it is already nearing its final stage. Many vets do not "
    "point this out.\n\n"
    "So do not feel safe just because you go to the vet regularly and "
    "responsibly and are told everything is fine. Please ask for a "
    "blood test regularly. Only that can save your cat and "
    "demonstrably extend its life. Early detection makes the "
    "difference.\n\n"
    "My cat was diagnosed in early 2025 after changes in his "
    "behaviour: more thirst, the urge to seek out unusual sources of "
    "water. We chose treatment at an animal clinic, and during that "
    "time several vets examined him — and each had a different "
    "opinion. It is important to trust your own senses too, since you "
    "know your cat best.\n\n"
    "He passed away on June 11, 2025.\n\n"
    "The Software Logo is a pixel portrait in memory of my cat, "
    "derived from a real photo from 2010 — when we brought him home "
    "from the shelter.\n\n"
    "Even though this project has a donation button: the greatest joy "
    "you can give me is to have your cat checked for this disease.\n\n"
    "* CKD affects not only cats, but other animals too.\n\n"
    "And if even one person does that now, this text has fully served "
    "its purpose in a piece of software that actually has nothing to "
    "do with it.\n\n"
    "Thank you for reading.\n\n"
    "In memory of Jason — 2010–2025.")


# ================= Sprachtexte (DE/EN) =================
# Nur die sichtbare Oberflaeche ist zweisprachig. Das Log-Protokoll
# unten bleibt bewusst deutsch (Diagnose-Ausgabe, in der Logik verwoben).
TEXTS = {
    "de": {
        "win_title":      "RAW-NES Monitor",
        "subtitle":       f"— {RAWNES_VERSION} — und immer noch nicht perfekt —",
        "copyright":      "© 2026 Liqui",
        "quickstart":     "Schnellstart:  1. Anmelden   2. Spielordner "
                          "einmal indizieren   3. Spiel am Geraet starten, "
                          "dann Monitor starten",
        "opt_lang":       "🌐 Deutsch / English",
        "opt_menu":       "⚙ Optionen",
        "opt_setup":      "🎮 FPGA-Mapper einrichten",
        "opt_support":    "☕ Unterstuetzen (aber wenn du darueber nachdenkst, "
                          "lies bitte vorher about the cat)",
        "opt_about":      "🐾 about the cat",
        "ra_frame":       "RetroAchievements",
        "ra_user":        "Benutzer:",
        "ra_pass":        "Passwort:",
        "ra_login":       "Anmelden",
        "ra_pw_save":     "Passwort speichern",
        "ra_pw_saved":    "Passwort gespeichert.",
        "ra_login_busy":  "...",
        "ra_notloggedin": "nicht angemeldet",
        "ra_loggedin":    "angemeldet als {user}",
        "ra_loginfail":   "Login fehlgeschlagen",
        "ed_frame":       "EverDrive",
        "ed_connecting":  "verbinde...",
        "ed_connected":   "verbunden ({port})",
        "ed_notfound":    "nicht gefunden",
        "ed_error":       "Fehler",
        "ed_lost":        "Verbindung verloren",
        "ed_retry":       "Erneut verbinden",
        "game_label":     "Spiel:",
        "game_none":      "kein Spiel geladen",
        "index_btn":      "ROM-Sammlung indizieren...",
        "index_none":     "(nicht indiziert — ROM-Auswahl per Dialog)",
        "index_count":    "{n} ROMs indiziert",
        "index_busy":     "indiziere...",
        "start_btn":      "Spiel laden & Monitor starten",
        "start_busy":     "lade...",
        "start_running":  "Monitor laeuft...",
        "stop_btn":       "Stoppen",
        "start_hint":     "(Spiel am Geraet starten)",
        "hardcore_cb":    "Hardcore-Modus",
        "hardcore_hint":  "(deaktiviert -- Savestate-Erkennung folgt; "
                          "Softcore ist aktiv)",
        "ach_frame":      "Achievements  (Klick = Diagnose, "
                          "Doppelklick = auf RetroAchievements oeffnen)",
        "live_frame":     "Live-Werte",
        "col_slot":       "Slot",
        "col_addr":       "Adresse",
        "col_dec":        "Dez",
        "col_hex":        "Hex",
        "col_chg":        "Aenderungen",
        "log_frame":      "Log",
        "status_ready":   "Bereit",
        "status_running": "Monitor laeuft — {game}",
        "status_ident":   "Identifiziere Spiel...",
        "about_title":    "about the cat",
        "err_title":      "Fehler",
        "err_login1st":   "Erst verbinden und bei RA anmelden.",
        "err_missing":    "Benutzer und Passwort eingeben.",
        "err_missing_t":  "Fehlt",
        "upd_title":      "Update verfuegbar",
        "upd_current":    "Version ist aktuell.",
        "upd_available":  "Update verfuegbar: Version {n} (installiert: "
                          + str(GUI_BUILD) + ").",
        "upd_ask":        "Eine neuere Version ist verfuegbar.\n\n"
                          "Installiert: Build {cur}\nVerfuegbar: Build {n}\n\n"
                          "Zur Download-Seite gehen?",
        "cores_title":    "RAW-NES Einrichtung",
        "cores_ask_setup": "Damit RetroAchievements auf echter Hardware "
                          "funktioniert, muss RAW-NES seine FPGA-Mapper "
                          "einmalig auf die EverDrive-SD-Karte installieren.\n\n"
                          "Die vorhandenen Original-Mapper werden vorher "
                          "automatisch gesichert.\n\n"
                          "Wenn du auf JA klickst, waehlst du als naechstes "
                          "das Laufwerk deiner SD-Karte aus.\n\n"
                          "Jetzt einrichten? (SD-Karte muss im PC stecken)",
        "cores_pick_sd":  "SD-Karte auswaehlen, um FPGA-Mapper zu installieren "
                          "(Laufwerk mit EDN8-Ordner)",
        "cores_no_edn8":  "In diesem Ordner ist kein EDN8-Verzeichnis. Bitte "
                          "das Laufwerk der EverDrive-SD-Karte waehlen.",
        "cores_ask_install": "{n} FPGA-Mapper fehlen oder sind veraltet.\n\n"
                          "Jetzt installieren? (Originale werden vorher "
                          "gesichert)",
        "cores_confirm_head": "FPGA-Mapper installieren",
        "cores_confirm_target": "Ziel: {path}",
        "cores_confirm_install": "Diese {n} FPGA-Mapper werden installiert:",
        "cores_confirm_backup": "Diese {n} Original-Mapper werden vorher "
                          "gesichert (nach ORIG_BACKUP):",
        "cores_confirm_restorehint": "Du kannst die Originale jederzeit ueber "
                          "\"Originale wiederherstellen\" zuruecksetzen.",
        "cores_confirm_nobackup": "Kein Backup noetig (Originale sind bereits "
                          "gesichert oder nicht vorhanden).",
        "cores_confirm_yes": "Installieren",
        "cores_confirm_no": "Abbrechen",
        "cores_ok":       "FPGA-Mapper sind aktuell.",
        "cores_already_ok": "Die FPGA-Mapper auf der SD-Karte sind bereits "
                          "aktuell -- es ist nichts zu tun. RAW-NES ist "
                          "startklar.",
        "cores_cancelled": "Einrichtung abgebrochen -- es wurde keine "
                          "SD-Karte gewaehlt.",
        "cores_backup_done": "{n} Original-Mapper gesichert (ORIG_BACKUP).",
        "cores_installed": "{n} FPGA-Mapper installiert. SD sicher auswerfen, "
                          "in den EverDrive, Kaltstart.",
        "cores_install_ok": "{n} FPGA-Mapper erfolgreich installiert.\n\n"
                          "SD-Karte sicher auswerfen, in den EverDrive stecken "
                          "und die Konsole neu starten (Kaltstart).",
        "cores_install_err": "Fehler beim Kopieren:\n{errs}",
        "cores_restore_btn": "Originale wiederherstellen",
        "cores_ask_restore": "{n} Original-Mapper aus dem Backup "
                          "wiederherstellen?\n\nDamit laeuft die EverDrive "
                          "wieder wie ohne RAW-NES (keine Achievement-"
                          "Ueberwachung mehr).",
        "cores_restore_ok": "{n} Original-Mapper wiederhergestellt.\n\nSD "
                          "sicher auswerfen, in den EverDrive, Kaltstart.",
        "cores_restored": "{n} Original-Mapper wiederhergestellt.",
        "cores_no_backup": "Kein Backup gefunden. Es wurden noch keine "
                          "Original-Mapper gesichert.",
    },
    "en": {
        "win_title":      "RAW-NES Monitor",
        "subtitle":       f"— {RAWNES_VERSION} — and still not perfect —",
        "copyright":      "© 2026 Liqui",
        "quickstart":     "Quick start:  1. Log in   2. Index game folder "
                          "once   3. Start game on device, then start monitor",
        "opt_lang":       "🌐 Deutsch / English",
        "opt_menu":       "⚙ Options",
        "opt_setup":      "🎮 Set up FPGA mappers",
        "opt_support":    "☕ Support (but if you are thinking about it, "
                          "please read about the cat before)",
        "opt_about":      "🐾 about the cat",
        "ra_frame":       "RetroAchievements",
        "ra_user":        "User:",
        "ra_pass":        "Password:",
        "ra_login":       "Log in",
        "ra_pw_save":     "Save password",
        "ra_pw_saved":    "Password saved.",
        "ra_login_busy":  "...",
        "ra_notloggedin": "not logged in",
        "ra_loggedin":    "logged in as {user}",
        "ra_loginfail":   "login failed",
        "ed_frame":       "EverDrive",
        "ed_connecting":  "connecting...",
        "ed_connected":   "connected ({port})",
        "ed_notfound":    "not found",
        "ed_error":       "error",
        "ed_lost":        "connection lost",
        "ed_retry":       "Reconnect",
        "game_label":     "Game:",
        "game_none":      "no game loaded",
        "index_btn":      "Index ROM collection...",
        "index_none":     "(not indexed — pick ROM via dialog)",
        "index_count":    "{n} ROMs indexed",
        "index_busy":     "indexing...",
        "start_btn":      "Load game & start monitor",
        "start_busy":     "loading...",
        "start_running":  "Monitor running...",
        "stop_btn":       "Stop",
        "start_hint":     "(start game on device)",
        "hardcore_cb":    "Hardcore mode",
        "hardcore_hint":  "(disabled -- savestate detection pending; "
                          "softcore is active)",
        "ach_frame":      "Achievements  (click = diagnose, "
                          "double-click = open on RetroAchievements)",
        "live_frame":     "Live values",
        "col_slot":       "Slot",
        "col_addr":       "Address",
        "col_dec":        "Dec",
        "col_hex":        "Hex",
        "col_chg":        "Changes",
        "log_frame":      "Log",
        "status_ready":   "Ready",
        "status_running": "Monitor running — {game}",
        "status_ident":   "Identifying game...",
        "about_title":    "about the cat",
        "err_title":      "Error",
        "err_login1st":   "Connect and log in to RA first.",
        "err_missing":    "Enter user and password.",
        "err_missing_t":  "Missing",
        "upd_title":      "Update available",
        "upd_current":    "Version is up to date.",
        "upd_available":  "Update available: version {n} (installed: "
                          + str(GUI_BUILD) + ").",
        "upd_ask":        "A newer version is available.\n\n"
                          "Installed: build {cur}\nAvailable: build {n}\n\n"
                          "Go to the download page?",
        "cores_title":    "RAW-NES Setup",
        "cores_ask_setup": "For RetroAchievements to work on real hardware, "
                          "RAW-NES needs to install its FPGA mappers onto the "
                          "EverDrive SD card once.\n\n"
                          "The existing original mappers are backed up "
                          "automatically first.\n\n"
                          "Set up now? (SD card must be in the PC)",
        "cores_pick_sd":  "Select SD card to install FPGA mappers "
                          "(drive with EDN8 folder)",
        "cores_no_edn8":  "This folder has no EDN8 directory. Please select "
                          "the drive of the EverDrive SD card.",
        "cores_ask_install": "{n} FPGA mapper(s) are missing or outdated.\n\n"
                          "Install now? (originals are backed up first)",
        "cores_confirm_head": "Install FPGA mappers",
        "cores_confirm_target": "Target: {path}",
        "cores_confirm_install": "These {n} FPGA mapper(s) will be installed:",
        "cores_confirm_backup": "These {n} original mapper(s) will be backed "
                          "up first (to ORIG_BACKUP):",
        "cores_confirm_restorehint": "You can restore the originals any time "
                          "via \"Restore originals\".",
        "cores_confirm_nobackup": "No backup needed (originals are already "
                          "backed up or not present).",
        "cores_confirm_yes": "Install",
        "cores_confirm_no": "Cancel",
        "cores_ok":       "FPGA mappers are up to date.",
        "cores_already_ok": "The FPGA mappers on the SD card are already up "
                          "to date -- nothing to do. RAW-NES is ready.",
        "cores_cancelled": "Setup cancelled -- no SD card was selected.",
        "cores_backup_done": "{n} original mapper(s) backed up (ORIG_BACKUP).",
        "cores_installed": "{n} FPGA mapper(s) installed. Safely eject the SD, "
                          "insert into the EverDrive, cold start.",
        "cores_install_ok": "{n} FPGA mapper(s) installed successfully.\n\n"
                          "Safely eject the SD card, insert it into the "
                          "EverDrive and restart the console (cold start).",
        "cores_install_err": "Error while copying:\n{errs}",
        "cores_restore_btn": "Restore originals",
        "cores_ask_restore": "Restore {n} original mapper(s) from backup?\n\n"
                          "This makes the EverDrive work like without RAW-NES "
                          "(no more achievement monitoring).",
        "cores_restore_ok": "{n} original mapper(s) restored.\n\nSafely eject "
                          "the SD, insert into the EverDrive, cold start.",
        "cores_restored": "{n} original mapper(s) restored.",
        "cores_no_backup": "No backup found. No original mappers have been "
                          "saved yet.",
    },
}


def snoop_offset(nes_addr):
    """NES-Adresse -> Offset im Spiegel. None, wenn nicht snoopbar."""
    if nes_addr <= 0x07FF:
        return nes_addr
    if 0x6000 <= nes_addr <= 0x7FFF:
        return WRAM_OFFSET + (nes_addr - 0x6000)
    return None


class RawNesGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.lang = "en"                # aktuelle Sprache
        self._i18n = {}                 # widget -> (methode, textschluessel)
        self._connected = False         # fuer Verbindungsstatus-Anzeige
        self.hardcore = tk.BooleanVar(value=HARDCORE_DEFAULT)

        self.configure(bg=COL_BG)
        self.title(f"{self.t('win_title')}")
        self.geometry("900x880")
        self.minsize(820, 700)

        self.ra_user = tk.StringVar()
        self.ra_pass = tk.StringVar()
        self.ra_token = None
        self.ra_game = None
        self.ra_runtimes = {}
        self.ra_list_index = {}
        self.watch_addrs = []
        self._unlocked_ref = set()
        self.lb_runtimes = []
        self.rp_script = None
        self._letztes_ram = None
        self._vorheriges_ram = None

        self.port = tk.StringVar(value="COM10")
        self._remembered = {}   # Fingerabdruck -> gewaehlte ROM-Quelle
        self.es = None
        self._stop_event = threading.Event()

        self.status_var = tk.StringVar(value=self.t("status_ready"))
        self.game_var = tk.StringVar(value=self.t("game_none"))

        self._jason_img = self._load_jason()   # PhotoImage oder None
        self._build_widgets()
        self._apply_language()   # initiale Beschriftung aller Widgets
        self._load_config()
        self.log(f"RAW-NES Monitor {RAWNES_VERSION}\n")
        self._update_index_label()
        self.after(200, self._connect)
        # Kern-Installer erst nach dem Fenster-Aufbau, damit Dialoge sitzen.
        self.after(600, self._check_cores_on_sd)
        self.after(650, self._update_restore_btn)
        self.after(1200, self._check_update)

    # ================= Sprache =================

    def t(self, key, **kw):
        """Text in der aktuellen Sprache, mit optionalem format()."""
        s = TEXTS.get(self.lang, TEXTS["de"]).get(key, key)
        return s.format(**kw) if kw else s

    def _reg(self, widget, method, key):
        """Widget fuer Sprachumschaltung registrieren.
        method: 'text' (Button/Label) oder 'title' (LabelFrame) oder
        'heading:<col>' (Treeview-Spalte)."""
        self._i18n[widget] = (method, key)

    def _apply_language(self):
        """Alle registrierten Widgets auf die aktuelle Sprache setzen."""
        for widget, (method, key) in list(self._i18n.items()):
            try:
                if method.startswith("heading:"):
                    col = method.split(":", 1)[1]
                    widget.heading(col, text=self.t(key))
                else:
                    widget.configure(text=self.t(key))
            except Exception:
                pass
        self.title(self.t("win_title"))
        # Optionen-Menue-Eintraege (Index-basiert: 0 Sprache, 1 Setup,
        # 2 Separator, 3 Support, 4 About the cat)
        if getattr(self, "_opt_menu", None) is not None:
            try:
                self._opt_menu.entryconfigure(0, label=self.t("opt_lang"))
                self._opt_menu.entryconfigure(1, label=self.t("opt_setup"))
                self._opt_menu.entryconfigure(3, label=self.t("opt_support"))
                self._opt_menu.entryconfigure(4, label=self.t("opt_about"))
            except Exception:
                pass
        # dynamische Labels, die nicht statisch sind
        if not self.ra_token:
            self.ra_status_lbl.configure(text=self.t("ra_notloggedin"))
        if not self._connected and self.es is None:
            self.conn_lbl.configure(text=self.t("ed_connecting"))
        if self.game_var.get() in (TEXTS["de"]["game_none"],
                                   TEXTS["en"]["game_none"]):
            self.game_var.set(self.t("game_none"))
        if self.status_var.get() in (TEXTS["de"]["status_ready"],
                                     TEXTS["en"]["status_ready"]):
            self.status_var.set(self.t("status_ready"))
        self._update_index_label()

    def _load_jason(self):
        """Eingebettetes Jason-PNG als PhotoImage. None bei Fehler.
        Wird in Originalgroesse angezeigt -- das PNG ist bereits passend
        skaliert, ein Hochzoomen wuerde es unscharf machen."""
        try:
            return tk.PhotoImage(data=JASON_PNG_B64)
        except Exception:
            return None

    # ================= Kern-Installer (SD einrichten) =================

    def _file_hash(self, path):
        import hashlib
        try:
            with open(path, "rb") as f:
                return hashlib.md5(f.read()).hexdigest()
        except Exception:
            return None

    def _cores_available(self):
        """Sind die Quell-Kerne im cores-Ordner vorhanden?"""
        if not os.path.isdir(CORES_DIR):
            return False
        return all(os.path.isfile(os.path.join(CORES_DIR, f))
                   for f in CORE_FILES)

    def _cores_need_install(self, maps_dir):
        """Liste der Kerne, die auf der SD fehlen oder veraltet sind
        (Hash weicht vom cores-Ordner ab)."""
        noetig = []
        for f in CORE_FILES:
            quelle = os.path.join(CORES_DIR, f)
            ziel = os.path.join(maps_dir, f)
            if not os.path.isfile(ziel):
                noetig.append(f)
            elif self._file_hash(quelle) != self._file_hash(ziel):
                noetig.append(f)
        return noetig

    def _maps_dir_from_sd(self, sd_root):
        """EDN8/MAPS-Pfad aus einem SD-Wurzelpfad. None wenn kein EDN8."""
        edn8 = os.path.join(sd_root, "EDN8")
        if not os.path.isdir(edn8):
            return None
        maps = os.path.join(edn8, "MAPS")
        return maps if os.path.isdir(maps) else None

    def _check_cores_on_sd(self, aktiv=False):
        """Prueft, ob die Snooper-Mapper auf der SD aktuell sind.

        Grundidee:
        - Erster Start (noch nie eingerichtet): einmal anbieten einzurichten.
        - Jeder weitere Start: STILL pruefen, sofern die SD im PC steckt.
          Nur melden, wenn etwas NICHT stimmt (Mapper fehlen/veraltet).
          Steckt die SD nicht im PC (Normalfall beim Spielen), passiert
          nichts -- es gibt nichts zu pruefen.
        - aktiv=True (Nutzer waehlt "FPGA-Mapper einrichten" im Menue):
          immer mit vollem Feedback, auch wenn schon alles stimmt.
        """
        if not self._cores_available():
            return

        sd_root = self._remembered_sd()
        maps = self._maps_dir_from_sd(sd_root) if sd_root else None

        if maps is None:
            # SD nicht erreichbar (nicht im PC oder Pfad ungueltig).
            if not aktiv:
                # Beim Start: nur beim allerersten Mal einrichten anbieten.
                if sd_root or getattr(self, "_setup_gefragt", False):
                    return  # schon eingerichtet oder schon gefragt -> still
                self._setup_gefragt = True
                self._save_config()
            # Einrichtung anbieten (erster Start) bzw. aktiv angestossen.
            if not messagebox.askyesno(
                    self.t("cores_title"), self.t("cores_ask_setup")):
                self.log("Einrichtung uebersprungen.\n")
                return
            sd_root = filedialog.askdirectory(title=self.t("cores_pick_sd"))
            if not sd_root:
                self.log("Keine SD-Karte gewaehlt -- abgebrochen.\n")
                if aktiv:
                    messagebox.showinfo(self.t("cores_title"),
                                        self.t("cores_cancelled"))
                return
            maps = self._maps_dir_from_sd(sd_root)
            if maps is None:
                self.log(f"Kein EDN8-Ordner in {sd_root} gefunden.\n")
                messagebox.showwarning(self.t("cores_title"),
                                       self.t("cores_no_edn8"))
                return
            self._sd_root = sd_root
            self._save_config()
            self.log(f"SD-Karte erkannt: {sd_root}\n")

        # Ab hier: SD ist erreichbar, Mapper pruefen.
        noetig = self._cores_need_install(maps)
        if not noetig:
            self.log(self.t("cores_ok") + "\n")
            if aktiv:
                # Nur wenn der Nutzer aktiv nachgeschaut hat: bestaetigen.
                messagebox.showinfo(self.t("cores_title"),
                                    self.t("cores_already_ok"))
            self._update_restore_btn()
            return

        # Etwas stimmt nicht -> immer melden (auch beim stillen Start-Check),
        # denn hier gibt es echten Handlungsbedarf.
        if self._confirm_install(maps, noetig):
            self._install_cores(maps, noetig)
        else:
            self.log("FPGA-Mapper-Installation abgebrochen.\n")

    def _confirm_install(self, maps_dir, dateien):
        """Detail-Dialog: zeigt genau, welche Mapper gesichert und welche
        installiert werden. Gibt True zurueck, wenn der Nutzer bestaetigt."""
        # Welche werden gesichert (existieren auf SD, noch kein Backup)?
        backup = os.path.join(maps_dir, BACKUP_SUBDIR)
        wird_gesichert = [
            f for f in dateien
            if os.path.isfile(os.path.join(maps_dir, f))
            and not os.path.isfile(os.path.join(backup, f))]

        win = tk.Toplevel(self)
        win.title(self.t("cores_title"))
        win.configure(bg=COL_BG)
        win.geometry("560x520")
        win.transient(self)
        win.grab_set()

        tk.Label(win, text=self.t("cores_confirm_head"),
                 font=("Consolas", 12, "bold"), fg=COL_GREEN,
                 bg=COL_BG, wraplength=520, justify="left").pack(
                     anchor="w", padx=16, pady=(14, 8))

        box = tk.Text(win, wrap="word", bg=COL_PANEL2, fg=COL_TEXT,
                      font=("Consolas", 9), relief=tk.FLAT, height=18,
                      padx=12, pady=10)
        box.pack(fill="both", expand=True, padx=16, pady=(0, 8))

        box.insert("end", self.t("cores_confirm_target",
                                 path=maps_dir) + "\n\n")
        box.insert("end", self.t("cores_confirm_install",
                                 n=len(dateien)) + "\n")
        for f in dateien:
            box.insert("end", f"    {f}\n")
        box.insert("end", "\n")
        if wird_gesichert:
            box.insert("end", self.t("cores_confirm_backup",
                                     n=len(wird_gesichert)) + "\n")
            for f in wird_gesichert:
                box.insert("end", f"    {f}\n")
            box.insert("end", "\n" + self.t("cores_confirm_restorehint")
                       + "\n")
        else:
            box.insert("end", self.t("cores_confirm_nobackup") + "\n")
        box.configure(state="disabled")

        ergebnis = {"ok": False}
        br = tk.Frame(win, bg=COL_BG)
        br.pack(fill="x", padx=16, pady=(0, 14))

        def ja():
            ergebnis["ok"] = True
            win.destroy()

        def nein():
            win.destroy()

        b_ok = self._mkbtn(br, "cores_confirm_yes", ja, kind="primary")
        b_ok.pack(side="right")
        b_no = self._mkbtn(br, "cores_confirm_no", nein)
        b_no.pack(side="right", padx=(0, 8))

        win.wait_window()
        return ergebnis["ok"]

    def _install_cores(self, maps_dir, dateien):
        import shutil
        # Vor dem Ersetzen die vorhandenen (Original-)Mapper sichern.
        backup = os.path.join(maps_dir, BACKUP_SUBDIR)
        os.makedirs(backup, exist_ok=True)
        gesichert = 0
        for f in dateien:
            ziel_orig = os.path.join(maps_dir, f)
            backup_ziel = os.path.join(backup, f)
            # Nur sichern, wenn eine Datei da ist und noch kein Backup existiert
            # (das erste Backup ist das echte Krikzz-Original -- nicht mit einer
            # bereits installierten Snooper-Version ueberschreiben).
            if os.path.isfile(ziel_orig) and not os.path.isfile(backup_ziel):
                try:
                    shutil.copy2(ziel_orig, backup_ziel)
                    gesichert += 1
                except Exception:
                    pass
        if gesichert:
            self.log(self.t("cores_backup_done", n=gesichert) + "\n")

        ok, fehler = 0, []
        for f in dateien:
            try:
                shutil.copy2(os.path.join(CORES_DIR, f),
                             os.path.join(maps_dir, f))
                ok += 1
                self.log(f"  FPGA-Mapper installiert: {f}\n")
            except Exception as e:
                fehler.append(f"{f}: {e}")
        if fehler:
            messagebox.showerror(
                self.t("cores_title"),
                self.t("cores_install_err", errs="\n".join(fehler)))
        else:
            messagebox.showinfo(
                self.t("cores_title"),
                self.t("cores_install_ok", n=ok))
        self.log(self.t("cores_installed", n=ok) + "\n")
        self._update_restore_btn()

    def _restore_originals(self):
        """Krikzz-Original-Mapper aus dem Backup zuruecksichern."""
        sd_root = self._remembered_sd()
        maps = self._maps_dir_from_sd(sd_root) if sd_root else None
        if maps is None:
            sd_root = filedialog.askdirectory(title=self.t("cores_pick_sd"))
            if not sd_root:
                return
            maps = self._maps_dir_from_sd(sd_root)
            if maps is None:
                messagebox.showwarning(self.t("cores_title"),
                                       self.t("cores_no_edn8"))
                return
            self._sd_root = sd_root
            self._save_config()

        backup = os.path.join(maps, BACKUP_SUBDIR)
        if not os.path.isdir(backup):
            messagebox.showinfo(self.t("cores_title"),
                                self.t("cores_no_backup"))
            return
        vorhanden = [f for f in CORE_FILES
                     if os.path.isfile(os.path.join(backup, f))]
        if not vorhanden:
            messagebox.showinfo(self.t("cores_title"),
                                self.t("cores_no_backup"))
            return
        if not messagebox.askyesno(
                self.t("cores_title"),
                self.t("cores_ask_restore", n=len(vorhanden))):
            return

        import shutil
        ok, fehler = 0, []
        for f in vorhanden:
            try:
                shutil.copy2(os.path.join(backup, f),
                             os.path.join(maps, f))
                ok += 1
                self.log(f"  Original wiederhergestellt: {f}\n")
            except Exception as e:
                fehler.append(f"{f}: {e}")
        if fehler:
            messagebox.showerror(
                self.t("cores_title"),
                self.t("cores_install_err", errs="\n".join(fehler)))
        else:
            messagebox.showinfo(self.t("cores_title"),
                                self.t("cores_restore_ok", n=ok))
        self.log(self.t("cores_restored", n=ok) + "\n")

    def _update_restore_btn(self):
        """Wiederherstellen-Button nur aktiv, wenn ein Backup existiert."""
        btn = getattr(self, "restore_btn", None)
        if btn is None:
            return
        sd_root = self._remembered_sd()
        maps = self._maps_dir_from_sd(sd_root) if sd_root else None
        hat_backup = False
        if maps:
            backup = os.path.join(maps, BACKUP_SUBDIR)
            if os.path.isdir(backup):
                hat_backup = any(
                    os.path.isfile(os.path.join(backup, f))
                    for f in CORE_FILES)
        btn.configure(state="normal" if hat_backup else "disabled")

    def _remembered_sd(self):
        return getattr(self, "_sd_root", None)

    # ================= UI =================

    def _mkbtn(self, parent, key, command, kind="normal"):
        """Konsistenter Button mit Rand, Hoehe und Hover-Effekt.
        kind: 'normal' (grau/gruen), 'primary' (gruen gefuellt),
        'danger' (rot gefuellt), 'ghost' (dezent, fuer Optionen-Leiste)."""
        if kind == "primary":
            bg, fg, hover = COL_GREEN, "#08140a", COL_GREEN_H
        elif kind == "danger":
            bg, fg, hover = COL_RED, "#160808", COL_RED_H
        elif kind == "ghost":
            bg, fg, hover = COL_BG, COL_GREEN, COL_PANEL
        else:
            bg, fg, hover = COL_BTN, COL_GREEN, COL_BTN_H
        b = tk.Button(parent, command=command, bg=bg, fg=fg,
                      text=self.t(key),
                      activebackground=hover, activeforeground=fg,
                      font=("Consolas", 10, "bold" if kind in
                            ("primary", "danger") else "normal"),
                      relief=tk.SOLID, bd=1,
                      highlightthickness=0,
                      cursor="hand2", padx=12, pady=4,
                      disabledforeground=COL_DIM)
        b.configure(highlightbackground=COL_SEP)
        # Hover: Hintergrund aufhellen
        b.bind("<Enter>", lambda e, w=b, h=hover: w.configure(bg=h)
               if str(w["state"]) != "disabled" else None)
        b.bind("<Leave>", lambda e, w=b, o=bg: w.configure(bg=o)
               if str(w["state"]) != "disabled" else None)
        self._reg(b, "text", key)
        return b

    def _build_widgets(self):
        pad = {"padx": 8, "pady": 5}

        # ========== KOPF: Jason-Logo mittig, Titel, Optionen (MEGA-RAW-Stil)
        header = tk.Frame(self, bg=COL_BG)
        header.pack(side=tk.TOP, fill=tk.X, pady=(8, 0))

        # Optionen-Zeile oben rechts: about-the-cat Button + Menue-Button
        opt = tk.Frame(header, bg=COL_BG)
        opt.pack(side=tk.TOP, fill=tk.X)
        self.opt_btn = tk.Menubutton(
            opt, bg=COL_BG, fg=COL_GREEN, relief=tk.SOLID, bd=1,
            activebackground=COL_PANEL, activeforeground=COL_GREEN,
            font=("Consolas", 10), cursor="hand2", padx=12, pady=4,
            highlightbackground=COL_SEP, highlightthickness=0)
        self.opt_btn.pack(side=tk.RIGHT, padx=(2, 12))
        self._reg(self.opt_btn, "text", "opt_menu")
        menu = tk.Menu(self.opt_btn, tearoff=0, bg=COL_PANEL2,
                       fg=COL_TEXT, activebackground=COL_GREEN_D,
                       activeforeground="#000000", relief=tk.FLAT,
                       font=("Consolas", 10))
        self.opt_btn.configure(menu=menu)
        self._opt_menu = menu
        # Eintraege werden in _apply_language() beschriftet (Index-basiert)
        menu.add_command(command=self._toggle_language)          # 0
        menu.add_command(                                        # 1
            command=lambda: self._check_cores_on_sd(aktiv=True))
        menu.add_separator()                                     # 2
        menu.add_command(command=self._open_support)              # 3
        menu.add_command(command=self._show_about_cat)            # 4
        self.opt_btn.bind("<Enter>",
                          lambda e: self.opt_btn.configure(bg=COL_PANEL))
        self.opt_btn.bind("<Leave>",
                          lambda e: self.opt_btn.configure(bg=COL_BG))

        # Jason-Logo mittig
        if self._jason_img is not None:
            tk.Label(header, image=self._jason_img,
                     bg=COL_BG).pack(side=tk.TOP, pady=(2, 0))
        else:
            tk.Label(header, text="\U0001F431", font=("Arial", 34),
                     bg=COL_BG).pack(side=tk.TOP)

        tk.Label(header, text="RAW-NES", font=("Consolas", 20, "bold"),
                 fg=COL_GREEN, bg=COL_BG).pack(side=tk.TOP)
        self.subtitle_lbl = tk.Label(header, text=self.t("subtitle"),
                                     font=("Consolas", 10, "bold"),
                                     fg=COL_GREEN_D, bg=COL_BG)
        self.subtitle_lbl.pack(side=tk.TOP)
        self._reg(self.subtitle_lbl, "text", "subtitle")
        tk.Label(header, text=self.t("copyright"), font=("Consolas", 8),
                 fg=COL_DIM, bg=COL_BG).pack(side=tk.TOP, pady=(0, 2))

        self.quick_lbl = tk.Label(header, text=self.t("quickstart"),
                                  font=("Consolas", 8), fg=COL_GREEN_D,
                                  bg=COL_BG)
        self.quick_lbl.pack(side=tk.TOP, pady=(2, 4))
        self._reg(self.quick_lbl, "text", "quickstart")

        # Trennlinie (NES-Gehaeuse-Grau)
        tk.Frame(self, height=1, bg=COL_SEP).pack(fill=tk.X)

        # Statusbalken
        sf = tk.Frame(self, bg=COL_PANEL)
        sf.pack(fill="x")
        tk.Label(sf, textvariable=self.status_var, font=("Consolas", 11, "bold"),
                 fg=COL_GREEN, bg=COL_PANEL, anchor="w",
                 padx=14, pady=8).pack(fill="x")

        # --- RetroAchievements ---
        raf = tk.LabelFrame(self, font=("Consolas", 10, "bold"),
                            bg=COL_BG, fg=COL_GREEN,
                            labelanchor="nw")
        raf.pack(fill="x", **pad)
        self._reg(raf, "text", "ra_frame")
        r = tk.Frame(raf, bg=COL_BG); r.pack(fill="x", padx=6, pady=5)
        lu = tk.Label(r, bg=COL_BG, fg=COL_TEXT); lu.pack(side="left")
        self._reg(lu, "text", "ra_user")
        tk.Entry(r, textvariable=self.ra_user, width=16, bg=COL_PANEL2,
                 fg=COL_TEXT, insertbackground=COL_TEXT,
                 relief=tk.FLAT).pack(side="left", padx=(4, 10))
        lp = tk.Label(r, bg=COL_BG, fg=COL_TEXT); lp.pack(side="left")
        self._reg(lp, "text", "ra_pass")
        tk.Entry(r, textvariable=self.ra_pass, width=16, show="*",
                 bg=COL_PANEL2, fg=COL_TEXT, insertbackground=COL_TEXT,
                 relief=tk.FLAT).pack(side="left", padx=(4, 10))
        self.ra_login_btn = self._mkbtn(r, "ra_login", self._ra_login)
        self.ra_login_btn.pack(side="left", padx=(0, 10))
        self.ra_pw_save_btn = self._mkbtn(r, "ra_pw_save", self._save_password,
                                          kind="ghost")
        self.ra_pw_save_btn.pack(side="left", padx=(0, 10))
        self.ra_status_lbl = tk.Label(r, text=self.t("ra_notloggedin"),
                                      fg=COL_DIM, bg=COL_BG)
        self.ra_status_lbl.pack(side="left")

        # --- EverDrive ---
        hwf = tk.LabelFrame(self, font=("Consolas", 10, "bold"),
                            bg=COL_BG, fg=COL_GREEN, labelanchor="nw")
        hwf.pack(fill="x", **pad)
        self._reg(hwf, "text", "ed_frame")
        r = tk.Frame(hwf, bg=COL_BG); r.pack(fill="x", padx=6, pady=5)
        self.conn_lbl = tk.Label(r, text=self.t("ed_connecting"), fg=COL_DIM,
                                 bg=COL_BG, font=("Consolas", 10, "bold"))
        self.conn_lbl.pack(side="left")
        self.retry_btn = self._mkbtn(r, "ed_retry", self._connect)
        self.retry_btn.pack(side="left", padx=(10, 0))

        r = tk.Frame(hwf, bg=COL_BG); r.pack(fill="x", padx=6, pady=(0, 6))
        lg = tk.Label(r, bg=COL_BG, fg=COL_TEXT); lg.pack(side="left")
        self._reg(lg, "text", "game_label")
        tk.Label(r, textvariable=self.game_var, fg=COL_GREEN,
                 bg=COL_BG).pack(side="left", padx=(4, 0))

        r = tk.Frame(hwf, bg=COL_BG); r.pack(fill="x", padx=6, pady=(0, 4))
        ib = self._mkbtn(r, "index_btn", self._index_roms)
        ib.pack(side="left")
        self.index_lbl = tk.Label(r, text="", fg=COL_DIM, bg=COL_BG)
        self.index_lbl.pack(side="left", padx=(8, 0))
        self.restore_btn = self._mkbtn(r, "cores_restore_btn",
                                       self._restore_originals)
        self.restore_btn.configure(state="disabled")
        self.restore_btn.pack(side="right")

        r = tk.Frame(hwf, bg=COL_BG); r.pack(fill="x", padx=6, pady=(0, 8))
        self.start_btn = self._mkbtn(r, "start_btn", self._start,
                                     kind="primary")
        self.start_btn.configure(state="disabled")
        self.start_btn.pack(side="left")
        self.stop_btn = self._mkbtn(r, "stop_btn", self._stop, kind="danger")
        self.stop_btn.configure(state="disabled")
        self.stop_btn.pack(side="left", padx=(8, 0))
        sh = tk.Label(r, fg=COL_DIM, bg=COL_BG); sh.pack(side="left", padx=(10, 0))
        self._reg(sh, "text", "start_hint")

        # Modus: Softcore (Standard). Hardcore ist deaktiviert, bis die
        # Savestate-Erkennung fertig ist -- der N8 bietet Savestates, die
        # ungesichertes Hardcore zu Betrug einladen wuerden.
        r = tk.Frame(hwf, bg=COL_BG); r.pack(fill="x", padx=6, pady=(0, 8))
        self.hardcore_cb = tk.Checkbutton(
            r, variable=self.hardcore, bg=COL_BG, fg=COL_TEXT,
            selectcolor=COL_PANEL2, activebackground=COL_BG,
            activeforeground=COL_TEXT, font=("Consolas", 9),
            state="disabled", disabledforeground=COL_DIM)
        self.hardcore_cb.pack(side="left")
        self._reg(self.hardcore_cb, "text", "hardcore_cb")
        hh = tk.Label(r, fg=COL_DIM, bg=COL_BG, font=("Consolas", 8))
        hh.pack(side="left", padx=(8, 0))
        self._reg(hh, "text", "hardcore_hint")

        # --- Achievements --- (feste Hoehe, damit Live-Werte expandieren)
        af = tk.LabelFrame(self, font=("Consolas", 10, "bold"),
                           bg=COL_BG, fg=COL_GREEN, labelanchor="nw")
        af.pack(fill="x", **pad)
        self._reg(af, "text", "ach_frame")
        self.ra_list = tk.Listbox(af, bg=COL_PANEL2, fg=COL_TEXT,
                                  font=("Consolas", 9), height=6,
                                  relief=tk.FLAT,
                                  selectbackground=COL_GREEN_D)
        sb = ttk.Scrollbar(af, orient="vertical", command=self.ra_list.yview)
        self.ra_list.configure(yscrollcommand=sb.set)
        self.ra_list.bind("<Double-Button-1>", self._open_achievement_ra)
        self.ra_list.bind("<Button-1>", self._erklaere_achievement_click)
        self.ra_list.pack(side="left", fill="both", expand=True,
                          padx=(6, 0), pady=6)
        sb.pack(side="right", fill="y", padx=(0, 6), pady=6)

        # --- Live-Werte (vertikale Zellen mit Gold-Blitz bei Aenderung) ---
        wf = tk.LabelFrame(self, font=("Consolas", 10, "bold"),
                           bg=COL_BG, fg=COL_GREEN, labelanchor="nw")
        wf.pack(fill="both", expand=True, **pad)
        self._reg(wf, "text", "live_frame")

        # Spaltenkopf
        head = tk.Frame(wf, bg=COL_PANEL)
        head.pack(fill="x", padx=6, pady=(6, 0))
        self._live_head = {}
        for key, w, anchor in (("col_addr", 10, "w"), ("col_dec", 8, "e"),
                               ("col_hex", 8, "e"), ("col_chg", 10, "e")):
            l = tk.Label(head, text=self.t(key), width=w, anchor=anchor,
                         font=("Consolas", 9, "bold"), fg=COL_DIM,
                         bg=COL_PANEL, padx=6, pady=3)
            l.pack(side="left")
            self._reg(l, "text", key)

        # Scrollbarer Bereich fuer die Zellen. Mindesthoehe, damit beim
        # Start mehrere Zeilen sichtbar sind, ohne das Fenster zu vergroessern.
        body = tk.Frame(wf, bg=COL_BG)
        body.pack(fill="both", expand=True, padx=6, pady=(0, 6))
        self._live_canvas = tk.Canvas(body, bg=COL_BG, highlightthickness=0,
                                      height=220)
        vsb = ttk.Scrollbar(body, orient="vertical",
                            command=self._live_canvas.yview)
        self._live_canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        self._live_canvas.pack(side="left", fill="both", expand=True)
        self._live_inner = tk.Frame(self._live_canvas, bg=COL_BG)
        self._live_win = self._live_canvas.create_window(
            (0, 0), window=self._live_inner, anchor="nw")
        self._live_inner.bind(
            "<Configure>",
            lambda e: self._live_canvas.configure(
                scrollregion=self._live_canvas.bbox("all")))
        self._live_canvas.bind(
            "<Configure>",
            lambda e: self._live_canvas.itemconfigure(self._live_win,
                                                      width=e.width))
        # Mausrad
        self._live_canvas.bind_all(
            "<MouseWheel>",
            lambda e: self._live_canvas.yview_scroll(
                int(-e.delta / 120), "units"))

        self._live_rows = {}   # slot -> dict(frame,addr,dec,hex,chg,fade)

        # --- Log --- (feste, kleinere Hoehe: Diagnose, kein expand,
        # damit die Live-Werte den freien Platz bekommen)
        lf = tk.LabelFrame(self, font=("Consolas", 9, "bold"),
                           bg=COL_BG, fg=COL_GREEN, labelanchor="nw")
        lf.pack(fill="x", **pad)
        self._reg(lf, "text", "log_frame")
        self.log_text = tk.Text(lf, wrap="word", bg=COL_PANEL2, fg=COL_TEXT,
                                height=6, font=("Consolas", 9), relief=tk.FLAT)
        self.log_text.pack(fill="both", expand=True, padx=6, pady=6)
        self.log_text.configure(state="disabled")

    # ================= Live-Werte-Zellen =================

    def _live_clear(self):
        """Alle Zeilen entfernen (bei neuem Spiel)."""
        for row in self._live_rows.values():
            row["frame"].destroy()
        self._live_rows.clear()

    def _live_add(self, slot, addr):
        """Eine Zeile fuer eine Adresse anlegen."""
        fr = tk.Frame(self._live_inner, bg=COL_BG)
        fr.pack(fill="x", pady=1)
        a = tk.Label(fr, text=f"${addr:04X}", width=10, anchor="w",
                     font=("Consolas", 10), fg=COL_DIM, bg=COL_BG, padx=6)
        d = tk.Label(fr, text="—", width=8, anchor="e",
                     font=("Consolas", 10, "bold"), fg=COL_DIM, bg=COL_BG,
                     padx=6)
        h = tk.Label(fr, text="—", width=8, anchor="e",
                     font=("Consolas", 10), fg=COL_DIM, bg=COL_BG, padx=6)
        c = tk.Label(fr, text="0", width=10, anchor="e",
                     font=("Consolas", 9), fg=COL_DIM, bg=COL_BG, padx=6)
        for w in (a, d, h, c):
            w.pack(side="left")
        self._live_rows[slot] = {"frame": fr, "addr_lbl": a, "dec": d,
                                 "hex": h, "chg": c, "fade": None,
                                 "value": None}

    def _live_update(self, slot, addr, value, n_changes):
        """Wert setzen. Bei Aenderung Gold-Blitz ausloesen."""
        row = self._live_rows.get(slot)
        if row is None:
            return
        geaendert = (row["value"] is not None and row["value"] != value)
        row["value"] = value
        row["dec"].configure(text=str(value))
        row["hex"].configure(text=f"${value:02X}")
        row["chg"].configure(text=str(n_changes))
        if n_changes:
            row["addr_lbl"].configure(fg=COL_TEXT)
            row["hex"].configure(fg=COL_TEXT)
        if geaendert:
            # laufende Animation abbrechen und neu starten
            if row["fade"] is not None:
                try:
                    self.after_cancel(row["fade"])
                except Exception:
                    pass
            self._live_fade(slot, 0)
        elif row["fade"] is None:
            # kein Blitz: Grundfarbe je nach Aktivitaet
            row["dec"].configure(fg=COL_GREEN if n_changes else COL_DIM)

    def _live_fade(self, slot, step):
        """Wert-Label von Gold ueber Stufen zurueck zu Gruen faerben."""
        row = self._live_rows.get(slot)
        if row is None:
            return
        if step >= len(GOLD_FADE):
            row["fade"] = None
            row["dec"].configure(fg=COL_GREEN)
            return
        row["dec"].configure(fg=GOLD_FADE[step])
        row["fade"] = self.after(70, lambda: self._live_fade(slot, step + 1))

    def log(self, msg):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", msg)
        self.log_text.see("end")
        self.log_text.configure(state="disabled")
        self.update_idletasks()

    def _status(self, text):
        self.status_var.set(text)

    # ================= Config =================

    def _load_config(self):
        if not os.path.isfile(CONFIG_PATH):
            return
        try:
            with open(CONFIG_PATH, encoding="utf-8") as f:
                cfg = json.load(f)
            self.ra_user.set(cfg.get("ra_user", ""))
            self._saved_pw = cfg.get("ra_pass", "")
            self.ra_pass.set(self._saved_pw)
            if cfg.get("port"):
                self.port.set(cfg["port"])
            self._remembered = cfg.get("remembered", {}) or {}
            self._sd_root = cfg.get("sd_root")
            self._setup_gefragt = cfg.get("setup_gefragt", False)
            lang = cfg.get("lang")
            if lang in TEXTS and lang != self.lang:
                self.lang = lang
                self._apply_language()
            if self.ra_user.get() and self.ra_pass.get():
                self._ra_login(silent=True)
        except Exception:
            pass

    def _save_config(self):
        try:
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump({"ra_user": self.ra_user.get().strip(),
                           "ra_pass": getattr(self, "_saved_pw", ""),
                           "port": self.port.get().strip(),
                           "lang": self.lang,
                           "sd_root": getattr(self, "_sd_root", None),
                           "setup_gefragt": getattr(self, "_setup_gefragt", False),
                           "remembered": self._remembered}, f)
        except Exception:
            pass

    def _save_password(self):
        """Speichert das aktuell eingegebene Passwort explizit in der Config.
        Wird NICHT automatisch bei jedem _save_config()-Aufruf mitgespeichert
        -- nur wenn der Nutzer diesen Button bewusst klickt."""
        self._saved_pw = self.ra_pass.get().strip()
        self._save_config()
        self.log(self.t("ra_pw_saved") + "\n")

    # ================= RA-Login =================

    def _ra_login(self, silent=False):
        user, pw = self.ra_user.get().strip(), self.ra_pass.get().strip()
        if not user or not pw:
            if not silent:
                messagebox.showerror(self.t("err_missing_t"),
                                     self.t("err_missing"))
            return
        self.ra_login_btn.configure(state="disabled", text=self.t("ra_login_busy"))

        def worker():
            try:
                import ra_client_nes as rac
                token = rac.ra_login(user, pw)
                if not token:
                    raise RuntimeError("Login abgelehnt (Benutzer/Passwort?)")
                self.ra_token = token
                self.ra_status_lbl.configure(text=self.t("ra_loggedin", user=user),
                                             fg=COL_GREEN)
                self.log(f"RA-Login erfolgreich ({user}).\n")
                self._save_config()
                self._maybe_enable_start()
            except Exception as e:
                self.ra_token = None
                self.ra_status_lbl.configure(text=self.t("ra_loginfail"),
                                             fg=COL_RED)
                self.log(f"RA-Login-Fehler: {e}\n")
            finally:
                self.ra_login_btn.configure(state="normal",
                                            text=self.t("ra_login"))

        threading.Thread(target=worker, daemon=True).start()

    # ================= Verbindung =================

    def _connect(self):
        self.retry_btn.configure(state="disabled")
        self._connected = False
        self.conn_lbl.configure(text=self.t("ed_connecting"), fg=COL_DIM)

        def worker():
            try:
                from ed_serial_nes import find_everdrive
                es, port = find_everdrive(self.port.get().strip() or None)
                if not es:
                    self._connected = False
                    self.conn_lbl.configure(text=self.t("ed_notfound"),
                                            fg=COL_RED)
                    self.log("EverDrive nicht gefunden -- angeschlossen? "
                             "Anderes Programm auf dem COM-Port?\n")
                    return
                self.es = es
                self.port.set(port)
                self._connected = True
                self.conn_lbl.configure(text=self.t("ed_connected", port=port),
                                        fg=COL_GREEN)
                self.log(f"Verbunden auf {port}.\n")
                self._check_builds()
                self._save_config()
                self._maybe_enable_start()
            except Exception as e:
                self._connected = False
                self.conn_lbl.configure(text=self.t("ed_error"), fg=COL_RED)
                self.log(f"Verbindungsfehler: {e}\n")
            finally:
                self.retry_btn.configure(state="normal")

        threading.Thread(target=worker, daemon=True).start()

    def _aid_from_selection(self):
        """Achievement-ID aus der aktuellen Listenauswahl. None wenn keine."""
        sel = self.ra_list.curselection()
        if not sel:
            return None
        idx = sel[0]
        for k, v in self.ra_list_index.items():
            if v == idx:
                return k
        return None

    def _erklaere_achievement_click(self, event=None):
        """Einfachklick: Diagnose. Verzoegert, damit bei einem Doppelklick
        nur der Browser oeffnet und nicht zusaetzlich die Diagnose feuert."""
        # kurze Verzoegerung; ein Doppelklick setzt _dblclick und unterdrueckt
        self._dblclick = False
        self.after(250, self._maybe_erklaere)

    def _maybe_erklaere(self):
        if getattr(self, "_dblclick", False):
            self._dblclick = False
            return
        self._erklaere_achievement()

    def _open_achievement_ra(self, event=None):
        """Doppelklick: Achievement auf RetroAchievements im Browser oeffnen."""
        self._dblclick = True   # unterdrueckt die verzoegerte Diagnose
        aid = self._aid_from_selection()
        if aid is None:
            return
        # RA-Achievement-Seite. aid ist die RetroAchievements-Achievement-ID.
        webbrowser.open(f"https://retroachievements.org/achievement/{aid}")

    def _play_unlock_sound(self):
        """Kurze aufsteigende Retro-Fanfare beim Freischalten (nur Windows,
        ueber winsound -- keine Datei, keine Dependency). Laeuft im Thread,
        damit die GUI nicht blockiert."""
        def worker():
            try:
                import winsound
                # aufsteigende Tonfolge, retro-typisch
                for freq, dur in ((660, 90), (880, 90), (1175, 140),
                                  (1568, 220)):
                    winsound.Beep(freq, dur)
            except Exception:
                # winsound fehlt (Nicht-Windows) oder Audio nicht verfuegbar
                pass
        threading.Thread(target=worker, daemon=True).start()

    def _flash_unlock(self, idx, step=0):
        """Goldener Aufblitz-Effekt auf einer freigeschalteten Zeile."""
        if idx is None:
            return
        try:
            if step >= len(GOLD_FADE):
                self.ra_list.itemconfig(idx, fg=COL_GREEN)
                return
            self.ra_list.itemconfig(idx, fg=GOLD_FADE[step])
            self.after(90, lambda: self._flash_unlock(idx, step + 1))
        except Exception:
            pass

    def _erklaere_achievement(self, _event=None):
        """Einfachklick auf ein Achievement: zeigt Bedingung fuer Bedingung,
        warum es aktuell nicht ausloest.

        Fuer die Fehlersuche an Sets, die sich unerwartet verhalten --
        ohne das bleibt nur Raten, ob es an der Adresse, der Bedingung
        oder einem Treffer-Zaehler liegt.
        """
        sel = self.ra_list.curselection()
        if not sel:
            return
        idx = sel[0]
        aid = None
        for k, v in self.ra_list_index.items():
            if v == idx:
                aid = k
                break
        if aid is None or aid not in self.ra_runtimes:
            return
        rt, ac, need = self.ra_runtimes[aid]

        self.log(f"\n--- {ac['title']} ({ac['points']}P) ---\n")
        self.log(f"MemAddr: {ac['mem']}\n")

        fehlend = sorted(a for a in need if snoop_offset(a) is None)
        if fehlend:
            self.log(f"nicht snoopbare Adressen: "
                     f"{', '.join(f'${a:04X}' for a in fehlend)}\n")
        if rt.unsupported:
            self.log("Bedingung wird nicht unterstuetzt -- feuert nie.\n")
            return
        if not self._letztes_ram:
            self.log("noch keine Messwerte -- Monitor starten.\n")
            return

        werte = ", ".join(f"${a:04X}={self._letztes_ram.get(a, '?')}"
                          for a in sorted(need))
        self.log(f"aktuelle Werte: {werte}\n")
        try:
            import ra_condition_nes as rcond
            for zeile in rcond.erklaere(rt, self._letztes_ram,
                                        self._vorheriges_ram):
                self.log("  " + zeile + "\n")
        except Exception as e:
            self.log(f"Diagnose fehlgeschlagen: {e}\n")

    def _check_builds(self):
        """Versionen von GUI, Index-Modul und FPGA gegenpruefen.

        Ohne diesen Abgleich ist bei Fehlern nicht erkennbar, ob eine
        Datei nicht ausgetauscht oder die top.rbf nicht neu geladen wurde.
        """
        try:
            import rom_index
            idx_build = getattr(rom_index, "INDEX_BUILD", "?")
        except Exception:
            idx_build = "?"
        # Beide moeglichen Basisadressen probieren. Die 16-KB-Region lief
        # nachweislich, die 32-KB-Region lieferte nur den Standardwert --
        # welche die MCU erreicht, stellt sich hier heraus.
        global ADDR_SNOOP
        fpga = None
        for basis in (0x1808000, 0x1804000):
            try:
                wert = self.es.memrd(basis + BUILD_OFFSET, 1)[0]
            except Exception:
                continue
            if wert == FPGA_BUILD_EXPECTED:
                fpga = wert
                if basis != ADDR_SNOOP:
                    self.log(f"Snoop-Region liegt bei 0x{basis:07X} "
                             f"(nicht 0x{ADDR_SNOOP:07X}).\n")
                    ADDR_SNOOP = basis
                break
            if fpga is None:
                fpga = wert

        teile = [f"GUI {GUI_BUILD}", f"Index {idx_build}"]
        teile.append(f"FPGA {fpga}" if fpga is not None else "FPGA n/v")
        self.log("Builds: " + ", ".join(teile) + "\n")

        # Am Menue ist der Snoop-Bereich wegen der MCU-DMA-Arbitration oft
        # nicht lesbar; memrd liefert dann 0xFF (255). Das ist KEIN
        # Versionsfehler -- die echte Pruefung passiert beim Monitor-Start,
        # wenn ein Spiel laeuft und der Snooper zuverlaessig antwortet.
        # Daher hier nur bei einem tatsaechlich gelesenen, abweichenden
        # Build warnen; 255/n/v wird stillschweigend akzeptiert.
        if fpga is not None and fpga != 0xFF and fpga != FPGA_BUILD_EXPECTED:
            self.log(f"  ACHTUNG: FPGA-Build {fpga}, erwartet "
                     f"{FPGA_BUILD_EXPECTED} -- top.rbf auf der SD ist eine "
                     f"andere Version.\n")
        elif fpga is None or fpga == 0xFF:
            self.log("  (FPGA-Build am Menue nicht lesbar -- normal; "
                     "wird beim Spielstart geprueft.)\n")
        if str(idx_build) != str(GUI_BUILD):
            self.log(f"  ACHTUNG: rom_index.py ist Build {idx_build}, GUI ist "
                     f"{GUI_BUILD} -- Dateien passen nicht zusammen.\n")

    def _maybe_enable_start(self):
        if self.es and self.ra_token:
            self.start_btn.configure(state="normal")

    # ================= Start: Spiel laden + Monitor =================

    def _resolve_rom(self, path):
        """Liefert einen Pfad auf echte ROM-Daten. Archive (.zip/.7z) werden
        entpackt -- sonst wuerde nes_hash() das ARCHIV hashen (Archiv beginnt
        mit 'PK..' bzw. '7z..', nicht mit 'NES\\x1a', also wird der iNES-Header
        nicht abgeschnitten und der Hash ist garantiert falsch).
        Rueckgabe: (pfad, tempfile_oder_None)."""
        import tempfile, zipfile
        ext = os.path.splitext(path)[1].lower()

        if ext == ".zip":
            with zipfile.ZipFile(path) as zf:
                names = [n for n in zf.namelist() if n.lower().endswith(".nes")]
                if not names:
                    raise RuntimeError("Im ZIP ist keine .nes-Datei.")
                if len(names) > 1:
                    self.log(f"ZIP enthaelt {len(names)} ROMs, nehme '{names[0]}'.\n")
                data = zf.read(names[0])
                self.log(f"Aus ZIP entpackt: {names[0]} ({len(data)} Byte)\n")
        elif ext == ".7z":
            try:
                import py7zr
            except ImportError:
                raise RuntimeError("7z-Archiv: 'pip install py7zr' noetig "
                                   "(oder ROM vorher entpacken).")
            with py7zr.SevenZipFile(path) as zf:
                names = [n for n in zf.getnames() if n.lower().endswith(".nes")]
            if not names:
                raise RuntimeError("Im 7z ist keine .nes-Datei.")
            if len(names) > 1:
                self.log(f"7z enthaelt {len(names)} ROMs, nehme '{names[0]}'.\n")
            with tempfile.TemporaryDirectory() as td:
                with py7zr.SevenZipFile(path) as zf:
                    zf.extract(path=td, targets=[names[0]])
                with open(os.path.join(td, names[0]), "rb") as f:
                    data = f.read()
            self.log(f"Aus 7z entpackt: {names[0]} ({len(data)} Byte)\n")
        else:
            with open(path, "rb") as f:
                data = f.read()

        # Gegenpruefung: ohne iNES-Header wird der RA-Hash falsch berechnet
        if data[:4] != b"NES\x1a":
            self.log(f"WARNUNG: Datei beginnt mit {data[:4]!r} statt b'NES\\x1a' "
                     f"-- kein iNES-Header. RA hasht dann die GANZE Datei, was "
                     f"nur bei headerlosen ROMs stimmt.\n")

        if ext in (".zip", ".7z"):
            fd, tmp = tempfile.mkstemp(suffix=".nes")
            with os.fdopen(fd, "wb") as f:
                f.write(data)
            return tmp, tmp
        return path, None

    # ============ Automatische Spielerkennung ============

    def _update_index_label(self):
        try:
            import rom_index
            idx = rom_index.load_index(ROM_INDEX_PATH)
        except Exception:
            idx = {}
        n = sum(len(v) for v in idx.values())
        if n:
            self.index_lbl.configure(text=self.t("index_count", n=n),
                                     fg=COL_GREEN)
        else:
            self.index_lbl.configure(text=self.t("index_none"), fg=COL_DIM)

    def _index_roms(self):
        from tkinter import filedialog
        base = filedialog.askdirectory(title="ROM-Ordner waehlen "
                                             "(wird einmalig indiziert)")
        if not base:
            return
        self.index_lbl.configure(text="indiziere...", fg="gray")

        def worker():
            try:
                import rom_index
                last = [0.0]

                def fortschritt(c):
                    now = time.time()
                    if now - last[0] > 0.3:      # UI nicht ueberfluten
                        last[0] = now
                        self.index_lbl.configure(text=f"indiziere... {c} ROMs",
                                                 fg="gray")

                idx, n = rom_index.build_index(base, progress=fortschritt)
                rom_index.save_index(idx, ROM_INDEX_PATH)
                koll = sum(1 for v in idx.values() if len(v) > 1)
                self.log(f"{n} ROMs indiziert, {len(idx)} Fingerabdruecke"
                         f"{f', {koll} mehrdeutig' if koll else ''}.\n")
                self._update_index_label()
            except Exception as e:
                self.log(f"Indizieren fehlgeschlagen: {e}\n")
                self._update_index_label()

        threading.Thread(target=worker, daemon=True).start()

    def _read_vectors(self):
        """Die 6 Vektorbytes $FFFA-$FFFF vom FPGA lesen."""
        vec = bytearray()
        for i in range(2, 8):          # Offset 2..7 = $FFFA..$FFFF
            vec.append(self.es.memrd(ADDR_SNOOP + VEC_OFFSET + i, 1)[0])
        return bytes(vec)

    def _adresse_beschrieben(self, nes_addr):
        """Hat das Spiel jemals an diese Adresse geschrieben?

        Der FPGA fuehrt pro Adresse ein Bit. Das ersetzt die frueher
        noetige Heuristik (Warmlaufphase): dort galt eine Adresse erst
        nach einer Wertaenderung als echt, wodurch Achievements mit
        konstant bleibenden Adressen nie ausgeloest wurden.
        """
        if nes_addr <= 0x07FF:
            off = RAM_MARK_OFFSET + nes_addr
        elif 0x6000 <= nes_addr <= 0x7FFF:
            off = WRAM_MARK_OFFSET + (nes_addr - 0x6000)
        else:
            return False
        try:
            return bool(self.es.memrd(ADDR_SNOOP + off, 1)[0] & 1)
        except Exception:
            return False

    def _read_page(self):
        """Erfasste PRG-Page $FF00-$FFFF plus Gueltigkeitsbits lesen.

        Byteweise, nicht als Block: der Lesepfad des Spiegels ist getaktet
        (ein Takt Latenz), ein Blocktransfer ueber memrd(..., n) liefert
        dabei nicht zuverlaessig die richtigen Werte. Alle bisher
        funktionierenden Lesezugriffe im Projekt sind ebenfalls byteweise.
        """
        try:
            seen = bytes(self.es.memrd(ADDR_SNOOP + SEEN_OFFSET + i, 1)[0] & 1
                         for i in range(256))
        except Exception as e:
            self.log(f"  Gueltigkeitsbits nicht lesbar: {e}\n")
            return None, None

        n_pos = sum(seen)
        if n_pos == 0:
            self.log("  PRG-Page: keine Position markiert. Die Vektoren "
                     "liegen ebenfalls in $FF00-$FFFF und werden erfasst -- "
                     "laeuft die neu kompilierte top.rbf?\n")
            return None, None

        # Nur die markierten Bytes holen -- spart Zeit gegenueber allen 256
        page = bytearray(256)
        try:
            for i in range(256):
                if seen[i]:
                    page[i] = self.es.memrd(ADDR_SNOOP + PAGE_OFFSET + i, 1)[0]
        except Exception as e:
            self.log(f"  PRG-Page nicht lesbar: {e}\n")
            return None, None
        return bytes(page), seen

    def _read_map_cfg(self):
        """Mapper-Konfiguration des LAUFENDEN Spiels lesen.

        Layout aus krikzz base_sv/sys_cfg.sv:
          scfg[0]      map_idx low
          scfg[1]      prg_mask (low nibble) / srm_mask (high nibble)
          scfg[2]      map_idx high (bits 7-4) / chr_mask (bits 3-0)
          scfg[4]      map_cfg: mirroring (bits 1-0), chr_ram (bit 2)
        Das EverDrive fuellt diese Register beim Spielstart aus dem
        iNES-Header -- damit laesst sich das laufende Spiel gegen die
        Header-Daten der Kandidaten abgleichen.
        """
        try:
            raw = self.es.memrd(ADDR_CFG, 16)
        except Exception:
            return None
        if len(raw) < 5:
            return None
        return {
            "mapper": raw[0] | ((raw[2] >> 4) << 8),
            "prg_shift": raw[1] & 0x0F,
            "chr_shift": raw[2] & 0x0F,
            "chr_ram": bool(raw[4] & 0x04),
            "mirror": raw[4] & 0x03,
        }

    @staticmethod
    def _shift_for(units_16k, unit_kb=16):
        """Shift-Wert, den das EverDrive fuer eine ROM-Groesse fuehrt.

        sys_cfg.sv rechnet: maske = (1 << shift) - 1, wobei die Maske die
        Bankanzahl abdeckt. PRG wird in 8-KB-Baenken adressiert, CHR in
        1-KB-Baenken. Bei nicht-Zweierpotenzen wird aufgerundet.
        """
        if not units_16k:
            return None
        banks = units_16k * (unit_kb // 8 if unit_kb == 16 else unit_kb)
        shift = 0
        while (1 << shift) < banks:
            shift += 1
        return shift

    def _filter_by_mapper(self, treffer):
        """Kandidaten aussortieren, die nicht zum laufenden Spiel passen.

        Verglichen werden Mapper-Nummer und ROM-Groessen aus der
        EverDrive-Konfiguration. Entscheidend bei Spielen mit gleicher
        Engine und damit identischer Vektortabelle (z.B. Ocean-Titel).
        Es wird nur gefiltert, wenn danach noch Kandidaten uebrig sind --
        so kann eine unerwartete Kodierung nichts kaputtmachen.
        """
        cfg = self._read_map_cfg()
        if not cfg:
            self.log("  Mapper-Konfiguration nicht lesbar -- Filter uebersprungen.\n")
            return treffer, None

        ohne_hdr = [t for t in treffer if t.get("mapper") is None]
        if ohne_hdr:
            self.log(f"  {len(ohne_hdr)} Kandidat(en) ohne Mapper-Angabe im "
                     f"Index -- bitte ROM-Sammlung NEU INDIZIEREN "
                     f"(der alte Index hat die Daten nicht).\n")

        self.log(f"  laufendes Spiel: Mapper {cfg['mapper']}, "
                 f"PRG-Shift {cfg['prg_shift']}, CHR-Shift {cfg['chr_shift']}, "
                 f"CHR-RAM {cfg['chr_ram']}\n")
        gefunden = sorted({t.get("mapper") for t in treffer
                           if t.get("mapper") is not None})
        if gefunden:
            self.log(f"  Mapper der Kandidaten: {gefunden}\n")

        passend = [t for t in treffer
                   if t.get("mapper") is not None
                   and t["mapper"] == cfg["mapper"]]
        if passend and len(passend) < len(treffer):
            treffer = passend

        # Weiter ueber die PRG-Groesse eingrenzen
        if len(treffer) > 1:
            enger = [t for t in treffer
                     if self._shift_for(t.get("prg")) == cfg["prg_shift"]]
            if enger and len(enger) < len(treffer):
                self.log(f"  PRG-Groesse (Shift {cfg['prg_shift']}) grenzt "
                         f"auf {len(enger)} ein.\n")
                treffer = enger

        # ... und ueber CHR (0 = CHR-RAM, dann greift chr_ram-Flag)
        if len(treffer) > 1:
            enger = [t for t in treffer
                     if bool(t.get("chr", 0) == 0) == cfg["chr_ram"]]
            if enger and len(enger) < len(treffer):
                self.log(f"  CHR-Typ grenzt auf {len(enger)} ein.\n")
                treffer = enger

        return treffer, cfg

    def _detect_rom(self):
        """Laufendes Spiel ueber den Vektor-Fingerabdruck bestimmen.
        Gibt einen Quellpfad zurueck oder None."""
        try:
            import rom_index
        except ImportError:
            return None
        idx = rom_index.load_index(ROM_INDEX_PATH)
        if not idx:
            return None
        try:
            vec = self._read_vectors()
        except Exception as e:
            self.log(f"Vektoren nicht lesbar: {e}\n")
            return None
        if vec == b"\x00" * 6 or vec == b"\xff" * 6:
            self.log(f"Vektoren leer ({vec.hex(' ')}) -- laeuft ein Spiel?\n")
            return None

        treffer = rom_index.lookup(idx, vec)
        hexs = vec.hex(" ")
        if not treffer:
            self.log(f"Fingerabdruck [{hexs}] nicht in der Sammlung.\n")
            return None
        if len(treffer) == 1:
            self.log(f"Erkannt: {treffer[0]['name']}  [{hexs}]\n")
            return treffer[0]["source"]

        # Mehrdeutig: meist sind es nur Regions-/Dump-Varianten desselben
        # Spiels. Erst pruefen, ob alle Kandidaten bei RA zur gleichen
        # GameID gehoeren -- dann ist die Auswahl egal und wir nehmen
        # einfach die erste. ra_gameid() cached, kostet also nur einmal Zeit.
        self.log(f"Fingerabdruck [{hexs}] passt auf {len(treffer)} ROMs.\n")

        # Erst ueber den Mapper des laufenden Spiels eingrenzen
        vorher = len(treffer)
        treffer, cfg = self._filter_by_mapper(treffer)
        if cfg:
            if len(treffer) < vorher:
                self.log(f"  Mapper {cfg['mapper']} laeuft -- "
                         f"{vorher - len(treffer)} Kandidaten passen nicht, "
                         f"{len(treffer)} verbleiben.\n")
            if len(treffer) == 1:
                self.log(f"Eindeutig ueber Mapper: {treffer[0]['name']}\n")
                return treffer[0]["source"]

        # Ueber den tatsaechlich gelesenen ROM-Code eingrenzen. Trennt
        # Spiele derselben Engine, die in Vektoren, Mapper und Groesse
        # uebereinstimmen (Ocean-Titel).
        if len(treffer) > 1:
            page, seen = self._read_page()
            if page:
                try:
                    import rom_index
                    vorher = len(treffer)
                    treffer, n_pos = rom_index.match_page(treffer, page, seen)
                    if n_pos < 8:
                        self.log(f"  PRG-Page noch zu wenig erfasst "
                                 f"({n_pos} Bytes) -- kurz spielen und "
                                 f"erneut versuchen.\n")
                    elif len(treffer) < vorher:
                        self.log(f"  ROM-Code ({n_pos} Bytes) grenzt von "
                                 f"{vorher} auf {len(treffer)} ein.\n")
                    if len(treffer) == 1:
                        self.log(f"Eindeutig ueber ROM-Code: "
                                 f"{treffer[0]['name']}\n")
                        return treffer[0]["source"]
                except Exception as e:
                    self.log(f"  Page-Vergleich fehlgeschlagen: {e}\n")

        self.log(f"  pruefe RA-Zuordnung...\n")
        gids = {}
        try:
            import ra_client_nes as rac
            for t in treffer:
                md5 = t.get("md5")
                if not md5:
                    gids = {}
                    break
                gids[t["name"]] = rac.ra_gameid(md5)
        except Exception as e:
            self.log(f"  (RA-Abfrage fehlgeschlagen: {e})\n")
            gids = {}

        # Kandidaten ohne RA-Eintrag aussortieren: verschiedene Spiele mit
        # gleicher Engine teilen oft die Vektortabelle, aber nur wenige
        # davon haben ueberhaupt ein Achievement-Set. Bleibt danach genau
        # einer uebrig, ist die Erkennung eindeutig.
        mit_set = [t for t in treffer if gids.get(t["name"])]
        if len(mit_set) < len(treffer) and mit_set:
            self.log(f"  {len(treffer) - len(mit_set)} ohne RA-Eintrag "
                     f"verworfen, {len(mit_set)} verbleiben.\n")
            treffer = mit_set

        eindeutige = {g for g in gids.values() if g}
        if gids and len(eindeutige) == 1:
            gid = eindeutige.pop()
            passend = [t for t in treffer if gids.get(t["name"])]
            self.log(f"Alle {len(passend)} Varianten gehoeren zu RA-Spiel "
                     f"#{gid} -- nehme '{passend[0]['name']}'.\n")
            return passend[0]["source"]

        if len(treffer) == 1:
            self.log(f"Eindeutig nach RA-Abgleich: {treffer[0]['name']}\n")
            return treffer[0]["source"]

        # Fruehere Wahl NUR uebernehmen, wenn NICHT feststeht, dass die
        # Kandidaten zu verschiedenen RA-Spielen gehoeren. Sonst wird eine
        # alte Wahl (z.B. die Europe-Fassung) stillschweigend auf ein anderes
        # gestartetes ROM (z.B. USA) angewendet -- Folge: falsches oder gar
        # kein Achievement-Set, ohne dass je gefragt wurde. Der Fingerabdruck
        # allein kann die Varianten nicht trennen, also darf er die
        # Entscheidung auch nicht ersetzen.
        verschiedene_ra_spiele = len(eindeutige) > 1
        gemerkt = self._remembered.get(hexs)
        if gemerkt and not verschiedene_ra_spiele:
            for t in treffer:
                if t["source"] == gemerkt:
                    self.log(f"Fruehere Wahl uebernommen: {t['name']}\n")
                    return gemerkt
        elif gemerkt:
            self.log("Fruehere Wahl NICHT uebernommen -- die Kandidaten "
                     "gehoeren zu verschiedenen RA-Spielen. Bitte bewusst "
                     "auswaehlen.\n")

        zeilen = []
        for i, t in enumerate(treffer[:9]):
            g = gids.get(t["name"])
            zeilen.append(f"  {i+1}. {t['name']}" + (f"   (RA #{g})" if g else ""))
        namen = "\n".join(zeilen)
        self.log(f"{namen}\n")
        from tkinter import simpledialog
        w = simpledialog.askinteger(
            "Mehrere Treffer",
            f"Diese ROMs haben denselben Fingerabdruck, gehoeren aber zu "
            f"verschiedenen RA-Spielen:\n\n{namen}\n\n"
            f"Nummer waehlen (bei verschiedenen RA-Spielen wird die Wahl "
            f"bewusst NICHT automatisch wiederverwendet):",
            parent=self, minvalue=1, maxvalue=min(9, len(treffer)))
        if not w:
            return None
        wahl = treffer[w - 1]
        self._remembered[hexs] = wahl["source"]
        self._save_config()
        return wahl["source"]

    def _start(self):
        if not self.es or not self.ra_token:
            messagebox.showerror(self.t("err_title"), self.t("err_login1st"))
            return
        rom = self._detect_rom()
        if rom:
            self.log("Automatisch erkannt -- kein Dateidialog noetig.\n")
        else:
            rom = filedialog.askopenfilename(
                title="ROM oder Archiv waehlen",
                filetypes=[("NES ROMs / Archive", ("*.nes", "*.zip", "*.7z")),
                           ("NES ROMs", "*.nes"),
                           ("ZIP-Archive", "*.zip"),
                           ("7z-Archive", "*.7z"),
                           ("Alle Dateien", "*.*")])
        if not rom:
            return

        self.start_btn.configure(state="disabled", text=self.t("start_busy"))
        self._status(self.t("status_ident"))

        def worker():
            tmp = None
            try:
                import ra_client_nes as rac
                import ra_condition_nes as racond

                rom_path, tmp = self._resolve_rom(rom)
                game, err = rac.identify_and_load(rom_path, self.ra_user.get().strip(),
                                                  self.ra_token)
                if not game:
                    self.log(f"Fehler: {err}\n")
                    self._reset_ui(); return
                self.ra_game = game
                self.game_var.set(game["name"])
                if game.get("no_set") or not game["achievements"]:
                    self.log(f"{game['name']}: kein Achievement-Set.\n")
                    self._reset_ui(); return

                addrs_all = racond.collect_addresses(game["achievements"])
                if not addrs_all:
                    self.log("Keine unterstuetzten Bedingungen.\n")
                    self._reset_ui(); return

                # Snoopbar = im Spiegel vorhanden (siehe snoop_offset).
                def _snoopable(a):
                    return snoop_offset(a) is not None

                addrs_ram = {a for a in addrs_all if _snoopable(a)}
                addrs_ext = {a for a in addrs_all if not _snoopable(a)}
                if addrs_ext:
                    self.log(f"{len(addrs_ext)} Adresse(n) ausserhalb RAM/WRAM "
                             f"(z.B. ${min(addrs_ext):04X}) -- nicht snoopbar.\n")
                if not addrs_ram:
                    self.log("Keine snoopbare Adresse ($0000-$07FF oder "
                             "$6000-$7FFF) -- Set nicht trackbar.\n")
                    self._reset_ui(); return

                # Adressen der Leaderboards mit beruecksichtigen, damit sie
                # nicht durch die Snoop-Pruefung fallen
                try:
                    import ra_leaderboard_nes as _lbm
                    for _lb in game.get("leaderboards", []):
                        _rt = _lbm.LeaderboardRuntime(_lb)
                        if not _rt.unsupported:
                            for _a in _rt.adressen():
                                if _snoopable(_a):
                                    addrs_ram.add(_a)
                except Exception:
                    pass

                # Bedarf pro Achievement ermitteln
                per_ach = []
                for ac in game["achievements"]:
                    rt = racond.AchievementRuntime(ac["mem"])
                    need = set(racond.collect_addresses([ac]))
                    per_ach.append((ac["id"], rt, ac, need))

                # Voller Spiegel: KEIN Slot-Limit mehr, alle Adressen lesbar.
                addrs = sorted(addrs_ram)
                self.watch_addrs = addrs

                # Achievements aufbauen -- Ausfallgrund unterscheiden
                addr_set = set(addrs)
                self.ra_runtimes = {}
                self.ra_list_index = {}
                self.ra_list.delete(0, "end")
                n_parse, n_missing, n_ext, n_ok = 0, 0, 0, 0
                for idx, (aid, rt, ac, need) in enumerate(per_ach):
                    ext = need & addrs_ext
                    missing = (need - addr_set) - ext
                    if rt.unsupported:
                        tag, col = "  [Bedingung nicht parsebar]", "#c07070"
                        n_parse += 1
                    elif ext:
                        rt.unsupported = True
                        tag, col = f"  [${min(ext):04X} nicht snoopbar]", "#b08050"
                        n_ext += 1
                    elif missing:
                        rt.unsupported = True
                        tag, col = f"  [{len(missing)} Adresse(n) fehlen]", "#909090"
                        n_missing += 1
                    else:
                        tag, col = "", "#d4d4d4"
                        n_ok += 1
                    self.ra_runtimes[aid] = (rt, ac, need)
                    self.ra_list_index[aid] = idx
                    self.ra_list.insert("end", f"{ac['title']} ({ac['points']}P){tag}")
                    self.ra_list.itemconfig(idx, fg=col)

                # Rich-Presence-Script des Spiels uebernehmen. RA liefert
                # es im Patch mit; bisher wurde stattdessen ein
                # selbstgebauter Text gesendet.
                self.rp_script = None
                try:
                    import ra_richpresence_nes as rpm
                    self.rp_script = rpm.parse_script(game.get("rich_presence"))
                    if self.rp_script:
                        self.log("Rich-Presence-Script des Spiels geladen.\n")
                except Exception as e:
                    self.log(f"Rich Presence uebersprungen: {e}\n")

                # Leaderboards aufbauen (nutzen dieselbe Bedingungssyntax)
                self.lb_runtimes = []
                try:
                    import ra_leaderboard_nes as lbm
                    for lb in game.get("leaderboards", []):
                        rt = lbm.LeaderboardRuntime(lb)
                        if rt.unsupported:
                            continue
                        if not rt.adressen().issubset(addrs_ram):
                            continue          # braucht nicht-snoopbare Adressen
                        self.lb_runtimes.append(rt)
                    if game.get("leaderboards"):
                        self.log(f"Leaderboards: {len(self.lb_runtimes)} von "
                                 f"{len(game['leaderboards'])} auswertbar.\n")
                except Exception as e:
                    self.log(f"Leaderboards uebersprungen: {e}\n")

                self.log(f"{game['name']}: {n_ok} trackbar | {n_missing} Adressen "
                         f"fehlen | {n_ext} ausserhalb RAM | {n_parse} nicht "
                         f"parsebar ({len(addrs)} Watches).\n")
                if n_ok == 0 and n_parse:
                    self.log("HINWEIS: Kein einziges Achievement parsebar -- das "
                             "liegt an ra_condition_nes.py (z.B. AddAddress/Prior/"
                             "Float-Bedingungen), nicht an der Slot-Zahl.\n")

                # RA-Session oeffnen (holt bereits Freigeschaltete)
                already = set()
                try:
                    gueltig = {ac["id"] for ac in game["achievements"]}
                    hc = self.hardcore.get()
                    un, info = rac.ra_unlocks(
                        game["gameid"], self.ra_user.get().strip(),
                        self.ra_token, gueltige_ids=gueltig, hardcore=hc)
                    already.update(un)
                    modus = "Hardcore" if hc else "Softcore"
                    self.log(f"RA-Session gestartet, {modus}: "
                             f"{len(un)} bereits freigeschaltet ({info}).\n")
                    for aid in un:
                        idx = self.ra_list_index.get(aid)
                        if idx is not None:
                            self.ra_list.itemconfig(idx, fg="#40c060")
                except Exception as e:
                    self.log(f"RA-Session-Warnung: {e}\n")

                # Voller Spiegel: nichts zu konfigurieren, direkt lesen.
                self._live_clear()
                self._chg = {}
                for slot, a in enumerate(addrs):
                    self._live_add(slot, a)
                    self._chg[slot] = 0
                self.log(f"{len(addrs)} Adressen werden gelesen. Polling laeuft.\n\n")

                self._stop_event.clear()
                self.stop_btn.configure(state="normal")
                self.start_btn.configure(text=self.t("start_running"))
                self._status(self.t("status_running", game=game['name']))
                self._poll_loop(already)

            except Exception as e:
                self.log(f"Fehler: {e}\n")
                self._reset_ui()
            finally:
                if tmp and os.path.exists(tmp):
                    try:
                        os.remove(tmp)
                    except Exception:
                        pass

        threading.Thread(target=worker, daemon=True).start()

    def _start_ping(self, gameid, spielname):
        """Aktivitaets-Ping an RA in eigenem Thread.

        Muss nebenher laufen: ra_request() legt vor jeder Anfrage eine
        feste Pause ein, im Polling-Loop wuerde das die Live-Werte
        stocken lassen.
        """
        def worker():
            import ra_client_nes as rac
            erster = True
            while not self._stop_event.is_set():
                gesamt = len(self.ra_runtimes)
                offen = sum(1 for rt, ac, need in self.ra_runtimes.values()
                            if not rt.unsupported)
                erledigt = len(self._unlocked_ref)
                rp = None
                if getattr(self, "rp_script", None) and self._letztes_ram:
                    try:
                        import ra_richpresence_nes as rpm
                        rp = rpm.evaluate(self.rp_script, self._letztes_ram,
                                          self._letztes_ram)
                    except Exception:
                        rp = None
                if not rp:
                    rp = f"{spielname} -- {erledigt}/{gesamt} Achievements"
                ok, info = rac.ra_ping(gameid, self.ra_user.get().strip(),
                                       self.ra_token, rich_presence=rp)
                if erster:
                    self.log(f"RA-Aktivitaet: {'gemeldet' if ok else info}"
                             f" (alle {int(PING_INTERVAL)}s)\n")
                    erster = False
                elif not ok:
                    self.log(f"RA-Ping fehlgeschlagen: {info}\n")
                # unterbrechbar warten, damit "Stoppen" sofort greift
                self._stop_event.wait(PING_INTERVAL)

        threading.Thread(target=worker, daemon=True).start()

    def _poll_loop(self, already_unlocked):
        """Polling + Achievement-Auswertung. Award-Logik 1:1 aus der alten
        GUI (Z. 1009-1039), Werte-Quelle statt FIFO jetzt read_slot()."""
        import ra_client_nes as rac
        self._unlocked_ref = already_unlocked
        if self.ra_game:
            self._start_ping(self.ra_game["gameid"], self.ra_game["name"])
        prev_ram = None
        t0 = time.time()
        last_val = {}
        warned = False
        try:
            rst_ctr = self.es.memrd(ADDR_SNOOP + RST_OFFSET, 1)[0]
        except Exception:
            rst_ctr = None
        confirmed = set()      # Adressen mit nachweislich echten Daten
        waiting_logged = set() # Achievements, die auf Daten warten
        warmed_up = False      # Spiel laeuft nachweislich und ist initialisiert
        # Ab FPGA-Build 9 liefert der Snooper pro Adresse ein
        # "wurde beschrieben"-Bit. Dann ist die Heuristik unnoetig.
        fehler_folge = 0
        wr_bits_da = False
        try:
            fb = self.es.memrd(ADDR_SNOOP + BUILD_OFFSET, 1)[0]
            wr_bits_da = fb >= 9
        except Exception:
            fb = None
        # Jetzt laeuft ein Spiel -- hier ist der Build zuverlaessig lesbar.
        # Ist er trotzdem 255/nicht der erwartete, laeuft wirklich die alte
        # .rbf: dann hilft ein Kaltstart (SD raus/rein), NICHT neu bauen.
        if fb == 0xFF or fb is None:
            self.log("  ACHTUNG: FPGA-Build trotz laufendem Spiel nicht "
                     "lesbar -- die Konsole haelt vermutlich die alte .rbf "
                     "im Cache. Konsole ausschalten, SD herausnehmen, wieder "
                     "einstecken, einschalten (Kaltstart).\n")
        elif fb != FPGA_BUILD_EXPECTED:
            self.log(f"  ACHTUNG: FPGA-Build {fb}, erwartet "
                     f"{FPGA_BUILD_EXPECTED} -- andere .rbf-Version auf der "
                     f"SD. Passendes Pack neu bauen und deployen.\n")
        if wr_bits_da:
            self.log("Schreibmarkierungen des FPGA verfuegbar -- "
                     "Achievements werden exakt freigegeben.\n")
        else:
            self.log("FPGA ohne Schreibmarkierungen -- Warmlauf-Heuristik "
                     "aktiv (Achievements mit konstanten Adressen koennen "
                     "ausbleiben).\n")
        warm_since = None      # Zeitpunkt, ab dem genug Aktivitaet da war
        WARMUP_MIN_ACTIVE = 3  # so viele Adressen muessen sich bewegt haben
        WARMUP_SECONDS = 5.0   # ... und so lange muss das anhalten
        try:
            while not self._stop_event.is_set():
                ram = {}
                lesefehler = None
                for slot, a in enumerate(self.watch_addrs):
                    try:
                        v = self.es.memrd(ADDR_SNOOP + snoop_offset(a), 1)[0]
                    except Exception as e:
                        lesefehler = e
                        break
                    ram[a] = v

                if lesefehler is not None:
                    fehler_folge += 1
                    if fehler_folge == 1:
                        # Verbindungsanzeige sofort auf "verloren" (rot):
                        # sonst steht oben "verbunden", waehrend unten die
                        # Lesefehler laufen -- das war der gemeldete Fehler.
                        self._connected = False
                        self.conn_lbl.configure(text=self.t("ed_lost"),
                                                fg=COL_RED)
                        self.log(f"Lesefehler ({lesefehler}) -- versuche weiter.\n")
                    if fehler_folge >= LESEFEHLER_MAX:
                        self.log(f"{fehler_folge} Lesefehler in Folge -- "
                                 f"Monitor gestoppt. Verbindung pruefen, "
                                 f"dann neu starten.\n")
                        break
                    self._stop_event.wait(LESEFEHLER_PAUSE)
                    continue

                if fehler_folge:
                    self.log(f"Verbindung wieder stabil (nach "
                             f"{fehler_folge} Fehlversuchen).\n")
                    fehler_folge = 0
                    # Anzeige zurueck auf "verbunden" (gruen)
                    self._connected = True
                    self.conn_lbl.configure(
                        text=self.t("ed_connected", port=self.port.get()),
                        fg=COL_GREEN)

                for slot, a in enumerate(self.watch_addrs):
                    v = ram[a]
                    if slot in last_val and last_val[slot] != v:
                        self._chg[slot] = self._chg.get(slot, 0) + 1
                        confirmed.add(a)   # hat sich geaendert -> kein Init-Wert
                    last_val[slot] = v
                    n = self._chg.get(slot, 0)
                    # Anzeige im Hauptthread aktualisieren (Fade nutzt after)
                    self.after(0, self._live_update, slot, a, v, n)

                # Reset erkennen: der FPGA zaehlt jeden sys_rst. Ohne diese
                # Pruefung laufen die Achievement-Runtimes nach einem
                # Spielneustart mit Hit-Countern aus dem vorherigen Durchlauf
                # weiter -- das kann Achievements faelschlich ausloesen oder
                # verhindern, dass sie nochmal zaehlen.
                if rst_ctr is not None:
                    try:
                        jetzt = self.es.memrd(ADDR_SNOOP + RST_OFFSET, 1)[0]
                    except Exception:
                        jetzt = rst_ctr
                    if jetzt != rst_ctr:
                        rst_ctr = jetzt
                        for rt, ac, need in self.ra_runtimes.values():
                            rt.reset()
                            rt.prior = {}
                            rt._last = {}
                        for lb in getattr(self, "lb_runtimes", []):
                            lb.reset()
                        confirmed.clear()
                        waiting_logged.clear()
                        warmed_up = False
                        warm_since = None
                        last_val.clear()
                        prev_ram = None
                        self._chg = {s: 0 for s in self._chg}
                        self.log("Spiel wurde neu gestartet -- "
                                 "Achievement-Zustand zurueckgesetzt.\n")
                        continue

                # Warmlauf: sobald sich mehrere Adressen bewegt haben und
                # das eine Weile anhaelt, gilt der Spiegel als initialisiert.
                if not warmed_up:
                    if len(confirmed) >= WARMUP_MIN_ACTIVE:
                        if warm_since is None:
                            warm_since = time.time()
                        elif time.time() - warm_since >= WARMUP_SECONDS:
                            warmed_up = True
                            self.log(f"Spiel laeuft ({len(confirmed)} Adressen "
                                     f"aktiv) -- alle Achievements scharf.\n")
                            for aid in list(waiting_logged):
                                idx = self.ra_list_index.get(aid)
                                if idx is not None:
                                    self.ra_list.itemconfig(idx, fg="#d4d4d4")
                            waiting_logged.clear()
                    else:
                        warm_since = None

                # Nach 3s einmalig Bilanz ziehen -- sonst sitzt man vor einer
                # Tabelle voller $FF und weiss nicht, ob das normal ist.
                if not warned and time.time() - t0 > 3.0:
                    warned = True
                    total = sum(self._chg.values())
                    if total == 0:
                        allff = all(v == 0xFF for v in last_val.values())
                        self.log(
                            f"WARNUNG: nach 3s keine einzige Wertaenderung"
                            f"{' -- alle Slots stehen auf $FF' if allff else ''}.\n"
                            f"  Moegliche Gruende:\n"
                            f"  1. Laeuft am Geraet die aktuelle top.rbf mit dem "
                            f"Spiegel-Snooper?\n"
                            f"  2. Laeuft ueberhaupt ein Spiel (nicht das Menue)?\n"
                            f"  3. Beschreibt dieses Spiel die Adressen ueberhaupt?\n")
                    else:
                        aktiv = sum(1 for n in self._chg.values() if n)
                        wartet = sum(1 for rt, ac, need in self.ra_runtimes.values()
                                     if not rt.unsupported and not need.issubset(confirmed))
                        self.log(f"Snooper liefert Daten: {aktiv}/"
                                 f"{len(self.watch_addrs)} Adressen aendern sich.\n")
                        if wartet:
                            self.log(f"{wartet} Achievement(s) warten noch auf echte "
                                     f"Daten (blau markiert) -- werden erst geprueft, "
                                     f"wenn ihre Adressen bestaetigt sind. Schutz "
                                     f"gegen Fehlbuchungen an RA.\n")

                for aid, (rt, ac, need) in self.ra_runtimes.items():
                    if aid in already_unlocked or rt.unsupported:
                        continue

                    # SICHERUNG GEGEN FEHLBUCHUNGEN:
                    # Der Spiegel im FPGA hat einen Init-Wert ($00), bis das
                    # Spiel die Adresse zum ersten Mal beschreibt. Eine
                    # Bedingung wie "$6877 == 0" waere sonst sofort beim ersten
                    # Poll wahr und wuerde ungerechtfertigt an RA gebucht.
                    #
                    # Zwei Wege, eine Adresse als echt zu bestaetigen:
                    #  a) ihr Wert hat sich mindestens einmal geaendert
                    #  b) das Spiel laeuft nachweislich (genug andere Adressen
                    #     haben sich bewegt) UND es ist genug Zeit vergangen,
                    #     dass das Spiel seine Variablen initialisiert hat
                    # Ohne (b) wuerden Achievements nie feuern, deren Adressen
                    # waehrend der Sitzung konstant bleiben -- z.B. Item-Flags,
                    # die sich erst beim Einsammeln aendern.
                    if wr_bits_da:
                        # harte Auskunft vom FPGA
                        offen = need - confirmed
                        for a in list(offen):
                            if self._adresse_beschrieben(a):
                                confirmed.add(a)
                        frei = need.issubset(confirmed)
                    else:
                        frei = warmed_up or need.issubset(confirmed)
                    if not frei:
                        if aid not in waiting_logged:
                            waiting_logged.add(aid)
                            idx = self.ra_list_index.get(aid)
                            if idx is not None:
                                self.ra_list.itemconfig(idx, fg="#6080a0")
                        continue
                    if aid in waiting_logged:
                        waiting_logged.discard(aid)
                        idx = self.ra_list_index.get(aid)
                        if idx is not None:
                            self.ra_list.itemconfig(idx, fg="#d4d4d4")

                    if rt.update(ram, prev_ram):
                        already_unlocked.add(aid)
                        self.log(f"  *** ACHIEVEMENT: {ac['title']} "
                                 f"({ac['points']} Punkte) ***\n")
                        idx = self.ra_list_index.get(aid)
                        if idx is not None:
                            text = self.ra_list.get(idx)
                            if not text.startswith("\u2713 "):
                                self.ra_list.delete(idx)
                                self.ra_list.insert(idx, f"\u2713 {text}")
                            # Freischalt-Effekt: goldener Blitz + Retro-Sound.
                            # Im Hauptthread, da die Animation after() nutzt.
                            self.after(0, self._flash_unlock, idx, 0)
                        self._play_unlock_sound()
                        try:
                            ok, info = rac.ra_award(
                                aid, self.ra_user.get().strip(),
                                self.ra_token,
                                hardcore=1 if self.hardcore.get() else 0)
                            self.log(f"  RA-Buchung: "
                                     f"{'gebucht' if ok else 'NICHT gebucht -- ' + info}\n")
                        except Exception as e:
                            self.log(f"  (Melden an RA fehlgeschlagen: {e})\n")

                # Leaderboards auswerten
                for lb in getattr(self, "lb_runtimes", []):
                    ereignis = lb.update(ram, prev_ram)
                    if ereignis == "start":
                        self.log(f"  Leaderboard laeuft: {lb.title}\n")
                    elif ereignis == "cancel":
                        self.log(f"  Leaderboard abgebrochen: {lb.title}\n")
                    elif isinstance(ereignis, tuple):
                        wert = ereignis[1]
                        import ra_leaderboard_nes as _lbm
                        anzeige = _lbm.format_value(wert, lb.format)
                        self.log(f"  *** Leaderboard {lb.title}: "
                                 f"{anzeige} ***\n")
                        try:
                            ok, info = rac.ra_submit_lb(
                                lb.id, self.ra_user.get().strip(),
                                self.ra_token, wert)
                            self.log(f"  RA-Abgabe: {info}\n")
                        except Exception as e:
                            self.log(f"  Abgabe fehlgeschlagen: {e}\n")

                self._vorheriges_ram = prev_ram
                prev_ram = ram
                self._letztes_ram = ram
                time.sleep(POLL_INTERVAL)
        except Exception as e:
            self.log(f"Polling-Fehler: {e}\n")
        finally:
            self.log(f"\n(Monitor gestoppt nach {time.time()-t0:.0f}s, "
                     f"{len(already_unlocked)}/{len(self.ra_runtimes)} freigeschaltet)\n")
            self._reset_ui()

    def _reset_ui(self):
        self._status(self.t("status_ready"))
        self.start_btn.configure(state="normal")
        self.start_btn.configure(text=self.t("start_btn"))
        self.stop_btn.configure(state="disabled")

    def _stop(self):
        self._stop_event.set()

    def _open_support(self):
        # Direkt zu Ko-fi. Frueher zeigte das auf liquid-wq.github.io/data/ --
        # das war eine Weiterleitung dorthin, ist inzwischen aber die
        # Downloadseite.
        webbrowser.open("https://ko-fi.com/liqui69747")

    def _check_update(self):
        """Beim Start pruefen, ob eine neuere Version verfuegbar ist.
        Laeuft im Thread, meldet nur bei neuerer Version -- still bei
        Netzwerkfehlern (Update-Pruefung darf den Start nie blockieren)."""
        def worker():
            try:
                import urllib.request
                with urllib.request.urlopen(UPDATE_URL, timeout=6) as r:
                    txt = r.read().decode("utf-8", "ignore").strip()
                neueste = int("".join(c for c in txt if c.isdigit()))
                if neueste > GUI_BUILD:
                    self.after(0, self._show_update, neueste)
                else:
                    self.log(self.t("upd_current") + "\n")
            except Exception:
                # still: kein Internet, Datei fehlt o.ae. -> kein Stoerer
                pass
        threading.Thread(target=worker, daemon=True).start()

    def _show_update(self, neueste):
        self.log(self.t("upd_available", n=neueste) + "\n")
        if messagebox.askyesno(self.t("upd_title"),
                               self.t("upd_ask", n=neueste, cur=GUI_BUILD)):
            webbrowser.open(UPDATE_PAGE)

    def _toggle_language(self):
        """Sprache zwischen DE und EN umschalten und ganze Oberflaeche neu
        beschriften. Betrifft nur die sichtbaren UI-Texte -- das
        Log-Protokoll bleibt deutsch."""
        self.lang = "en" if self.lang == "de" else "de"
        self._apply_language()
        self._save_config()

    def _show_about_cat(self):
        """Ueber-Jason-Dialog: Pixel-Portrait + vollstaendiger Text in der
        aktuellen Sprache."""
        win = tk.Toplevel(self)
        win.title(self.t("about_title"))
        win.geometry("660x780")
        win.configure(bg=COL_BG)

        if self._jason_img is not None:
            tk.Label(win, image=self._jason_img,
                     bg=COL_BG).pack(pady=(12, 4))

        tk.Label(win, text="In memory of Jason", font=("Consolas", 13, "bold"),
                 fg=COL_GREEN, bg=COL_BG).pack(pady=(0, 2))
        tk.Label(win, text="2010 – 2025", font=("Consolas", 10),
                 fg=COL_DIM, bg=COL_BG).pack(pady=(0, 8))

        frame = tk.Frame(win, bg=COL_BG)
        frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))
        text = tk.Text(frame, wrap=tk.WORD, font=("Consolas", 10),
                       bg=COL_PANEL2, fg=COL_TEXT, padx=16, pady=16,
                       relief=tk.FLAT)
        scr = ttk.Scrollbar(frame, orient="vertical", command=text.yview)
        text.configure(yscrollcommand=scr.set)
        text.pack(side="left", fill=tk.BOTH, expand=True)
        scr.pack(side="right", fill="y")

        text.insert(tk.END, JASON_ABOUT_DE if self.lang == "de"
                    else JASON_ABOUT_EN)
        text.config(state=tk.DISABLED)


def show_intro_html():
    """Zeigt rawnes_intro.html in einem rahmenlosen Fenster via pywebview.
    Fehlt pywebview oder die HTML, wird das Intro stillschweigend
    uebersprungen -- das Tool startet normal."""
    html = os.path.join(SCRIPT_DIR, "rawnes_intro.html")
    if not os.path.isfile(html):
        return
    try:
        import webview
    except Exception:
        return
    try:
        import threading, tempfile as _tf
        with open(html, "r", encoding="utf-8") as f:
            html_content = f.read()
        state = {"win": None, "closed": False}

        def _close():
            if state["closed"]:
                return
            state["closed"] = True
            try:
                state["win"].destroy()
            except Exception:
                pass

        class _Api:
            def intro_fertig(self):
                _close()

        win = webview.create_window(
            "", html=html_content, width=740, height=840,
            frameless=True, resizable=False, on_top=True, js_api=_Api())
        state["win"] = win

        def _watchdog():
            import time as _t
            _t.sleep(12.0)
            _close()
        threading.Thread(target=_watchdog, daemon=True).start()

        wv_store = os.path.join(_tf.gettempdir(), "rawnes_webview")
        try:
            webview.start(private_mode=False, storage_path=wv_store)
        except TypeError:
            try:
                webview.start()
            except Exception:
                pass
        except Exception:
            pass
    except Exception:
        pass


if __name__ == "__main__":
    # Intro-Modus: nur Animation zeigen, eigener Prozess (WebView2 stoert
    # sonst COM-Zustand fuer tkinter-Dialoge im Hauptprozess).
    if "--intro" in sys.argv:
        show_intro_html()
        sys.exit(0)
    try:
        import subprocess
        _flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        subprocess.run([sys.executable, os.path.abspath(__file__), "--intro"],
                       timeout=20, creationflags=_flags)
    except Exception:
        pass
    RawNesGui().mainloop()
