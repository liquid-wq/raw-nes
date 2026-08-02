"""
rom_index.py -- Vektor-Fingerabdruecke einer ROM-Sammlung indizieren.

Fuer die automatische Spielerkennung: Der FPGA-Snooper schneidet die
CPU-Reads bei $FFFA-$FFFF mit (NMI/IRQ-Vektoren, werden ~60x/s gelesen).
Dieselben 6 Bytes stehen in jeder .nes-Datei am Ende des letzten
PRG-Banks. Damit laesst sich das laufende Spiel ohne Dateidialog
bestimmen.

Der Index wird einmal erstellt und in rawnes_rom_index.json abgelegt.
"""
INDEX_BUILD = 20   # muss zu GUI_BUILD passen

import hashlib
import json
import os
import zipfile

ROM_EXTS = (".nes",)
ARCHIVE_EXTS = (".zip", ".7z")


def vectors_from_ines(data):
    """Die 6 Vektorbytes $FFFA-$FFFF aus einer iNES-Datei.

    Sie liegen am Ende des letzten PRG-Banks. Bei Mappern mit
    Bank-Switching ist der obere Bank ($C000-$FFFF) praktisch immer auf
    den letzten PRG-Bank fixiert -- genau dort stehen die Vektoren.
    """
    if len(data) < 16 or data[:4] != b"NES\x1a":
        return None
    prg_size = data[4] * 16384
    if prg_size == 0:
        return None
    prg_end = 16 + prg_size
    if prg_end > len(data):
        return None
    return data[prg_end - 6:prg_end]


def iter_roms(base):
    """(anzeigename, quelle, rohdaten) fuer alle ROMs unter base."""
    for root, _dirs, files in os.walk(base):
        for fn in files:
            path = os.path.join(root, fn)
            ext = os.path.splitext(fn)[1].lower()
            if ext in ROM_EXTS:
                try:
                    with open(path, "rb") as f:
                        yield fn, path, f.read()
                except Exception:
                    pass
            elif ext == ".zip":
                try:
                    with zipfile.ZipFile(path) as zf:
                        for n in zf.namelist():
                            if n.lower().endswith(".nes"):
                                yield os.path.basename(n), path, zf.read(n)
                except Exception:
                    pass
            elif ext == ".7z":
                try:
                    import py7zr, tempfile
                    with py7zr.SevenZipFile(path) as zf:
                        names = [n for n in zf.getnames()
                                 if n.lower().endswith(".nes")]
                    for n in names:
                        with tempfile.TemporaryDirectory() as td:
                            with py7zr.SevenZipFile(path) as zf:
                                zf.extract(path=td, targets=[n])
                            with open(os.path.join(td, n), "rb") as f:
                                yield os.path.basename(n), path, f.read()
                except Exception:
                    pass


def last_page_from_ines(data):
    """Die letzten 256 PRG-Bytes ($FF00-$FFFF im eingeblendeten Zustand).

    Bei MMC1/MMC3 liegt der letzte PRG-Bank fest bei $C000-$FFFF, dort
    laeuft Handler-Code, den der Snooper zur Laufzeit mitliest. Damit
    lassen sich Spiele derselben Engine unterscheiden, die in Vektoren,
    Mapper und ROM-Groesse identisch sind.
    """
    if len(data) < 16 or data[:4] != b"NES\x1a":
        return None
    prg_size = data[4] * 16384
    if prg_size < 256:
        return None
    end = 16 + prg_size
    if end > len(data):
        return None
    return data[end - 256:end]


def ines_header_info(data):
    """Mapper, PRG-/CHR-Groesse und Mirroring aus dem iNES-Header.

    Diese Werte stehen dem EverDrive auch zur Laufzeit zur Verfuegung
    (Mapper-Konfiguration @ 0x1800020), koennen also mit dem laufenden
    Spiel abgeglichen werden -- entscheidend, wenn mehrere Spiele
    dieselbe Vektortabelle haben (gleiche Engine, z.B. Ocean-Titel).
    """
    if len(data) < 16 or data[:4] != b"NES\x1a":
        return None
    b6, b7 = data[6], data[7]
    mapper = (b6 >> 4) | (b7 & 0xF0)
    # NES 2.0: weitere 4 Mapper-Bits in Byte 8
    if (b7 & 0x0C) == 0x08 and len(data) > 8:
        mapper |= (data[8] & 0x0F) << 8
    return {
        "mapper": mapper,
        "prg": data[4],           # 16-KB-Einheiten
        "chr": data[5],           # 8-KB-Einheiten (0 = CHR-RAM)
        "mirror": b6 & 0x01,
        "four": (b6 >> 3) & 0x01,
    }


def ines_md5(data):
    """RA-Hash einer NES-ROM: iNES-Header abschneiden, Rest hashen.
    Wird beim Indizieren mitgespeichert, damit die GUI bei mehrdeutigen
    Fingerabdruecken pruefen kann, ob die Kandidaten zur selben
    RA-GameID gehoeren -- dann ist die Auswahl egal."""
    if data[:4] == b"NES\x1a":
        data = data[16:]
    return hashlib.md5(data).hexdigest()


def index_key(vec6):
    """Schluessel aus den Vektorbytes.

    Nur NMI ($FFFA/B) + Reset ($FFFC/D) werden verwendet. Grund: der
    IRQ-Vektor ($FFFE/F) wird nur gelesen, wenn das Spiel ueberhaupt
    IRQs nutzt -- MMC1-Titel wie Metroid tun das nicht, dort bleibt er
    im Snooper auf dem Init-Wert und der Vergleich mit der ROM-Datei
    schlaegt fehl. NMI und Reset werden dagegen immer gelesen.
    """
    return bytes(vec6[:4]).hex()


def build_index(base, progress=None):
    """Index bauen: {schluessel: [{name, source, vec6}, ...]}"""
    index = {}
    n = 0
    for name, source, data in iter_roms(base):
        vec = vectors_from_ines(data)
        if not vec:
            continue
        page = last_page_from_ines(data)
        eintrag = {"name": name, "source": source, "vec6": vec.hex(),
                   "md5": ines_md5(data),
                   "page": page.hex() if page else None}
        hdr = ines_header_info(data)
        if hdr:
            eintrag.update(hdr)
        index.setdefault(index_key(vec), []).append(eintrag)
        n += 1
        if progress:
            progress(n)
    return index, n


def save_index(index, path):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(index, f)


def load_index(path):
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}


def match_page(treffer, page_bytes, seen_bits):
    """Kandidaten ueber die erfasste PRG-Page eingrenzen.

    page_bytes: 256 Bytes aus dem Snooper
    seen_bits:  256 Marker, je Position ein Byte (0 oder 1)
    Verglichen werden nur Positionen, die der Snooper tatsaechlich
    gelesen hat -- ungelesene sind im FPGA 0 und wuerden sonst falsche
    Unterschiede erzeugen.
    """
    positionen = [i for i in range(256) if seen_bits[i]]
    if len(positionen) < 8:          # zu wenig Daten fuer eine Aussage
        return treffer, 0
    passend = []
    for t in treffer:
        ph = t.get("page")
        if not ph:
            continue
        roh = bytes.fromhex(ph)
        if all(roh[i] == page_bytes[i] for i in positionen):
            passend.append(t)
    if passend:
        return passend, len(positionen)
    return treffer, len(positionen)


def lookup(index, vec_bytes):
    """Kandidaten zu einem gelesenen Vektor-Fingerabdruck.

    Sucht ueber NMI+Reset. Wurde auch der IRQ-Vektor gelesen (Spiel nutzt
    IRQs, Bytes 4-5 nicht null), wird damit weiter eingegrenzt.
    """
    vec = bytes(vec_bytes)
    treffer = index.get(index_key(vec), [])
    if len(treffer) > 1 and vec[4:6] not in (b"\x00\x00", b"\xff\xff"):
        genau = [t for t in treffer if t.get("vec6") == vec.hex()]
        if genau:
            return genau
    return treffer


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print(__doc__)
        print("Benutzung: python rom_index.py <ROM-Ordner>")
        sys.exit(0)
    base = sys.argv[1]
    print(f"Indiziere '{base}' ...")
    idx, n = build_index(base, progress=lambda c: print(f"  {c} ROMs ..."))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "rawnes_rom_index.json")
    save_index(idx, out)
    kollisionen = sum(1 for v in idx.values() if len(v) > 1)
    print(f"\n{n} ROMs indiziert, {len(idx)} verschiedene Fingerabdruecke.")
    print(f"{kollisionen} Fingerabdruecke mit mehreren ROMs "
          f"(dort fragt die GUI nach).")
    print(f"Gespeichert: {out}")
