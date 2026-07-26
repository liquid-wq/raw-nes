"""
ra_richpresence_nes.py -- minimale Rich-Presence-Unterstuetzung.

RAs Rich-Presence-Scripte sind eine eigene, umfangreiche Sprache
(Lookup-Tabellen, Format-Funktionen, bedingte Anzeige, Speicherzugriffe).
Ein vollstaendiger Interpreter waere ein eigenes Projekt und traegt zur
Kernfunktion (Achievements) nichts bei -- Rich Presence steuert nur den
"Last Seen In"-Text im RA-Profil.

Dieses Modul haelt es daher bewusst einfach: statt das Script auszuwerten,
liefert es einen statischen Aktivitaetstext. Damit ist der Ping nicht mehr
leer (das Spiel taucht im Profil auf), ohne den vollen Parser zu bauen.

Wird der volle Funktionsumfang spaeter gebraucht, ist hier die Stelle, an
der ein echter Interpreter andocken wuerde -- die GUI ruft nur parse_script
und evaluate auf.
"""


def parse_script(script):
    """Nimmt das Rich-Presence-Script aus dem RA-Patch entgegen.

    Rueckgabe: ein "kompiliertes" Objekt, das evaluate() versteht.
    Hier: das rohe Script (oder None), evaluate ignoriert die Details.
    """
    if not script:
        return None
    # Nur merken, dass ein Script existiert. Kein echtes Parsen.
    return {"raw": script}


def evaluate(compiled, ram, prev_ram=None):
    """Erzeugt den Aktivitaetstext fuer den RA-Ping.

    compiled: Rueckgabe von parse_script
    ram:      aktueller RAM-Spiegel (bytes) -- hier ungenutzt, waere aber
              der Anknuepfpunkt fuer einen echten Interpreter
    Rueckgabe: kurzer Text (max. 120 Zeichen, RA-Limit) oder None.
    """
    if not compiled:
        return None
    # Platzhalter-Aktivitaet. Ein echter Interpreter wuerde hier das
    # Script gegen ram auswerten und z.B. "World 1-1, 3 Leben" liefern.
    return "Spielt (RAW-NES)"
