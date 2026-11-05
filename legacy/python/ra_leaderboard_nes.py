"""
ra_leaderboard_nes.py -- Leaderboard-Conditions + Value-Formatting.

Analog zu ra_condition_nes.py, aber für Leaderboard-Start/Cancel/Submit-Bedingungen.
Die Bedingungen kommen vom RA-API (als Strings), der rp_script parst sie.
"""


def format_value(wert, fmt):
    """Formatiert einen Leaderboard-Wert gemäss RA-Format.
    
    fmt kann sein: "TIME_FRAMES", "TIME_SECONDS", "TIME_MILLISECONDS",
    "SCORE", "SCORE_SHORTER", "MINUTES", "SECS", ...
    Bei Leaderboards ist fmt typisch "SECS" oder "SCORE".
    """
    if fmt in ("SECS", "TIME_SECONDS"):
        return f"{wert}s"
    elif fmt in ("FRAMES", "TIME_FRAMES"):
        return f"{wert}f"
    elif fmt == "MILLISECONDS" or fmt == "TIME_MILLISECONDS":
        return f"{wert}ms"
    elif fmt in ("SCORE", "SCORE_SHORTER"):
        return f"{wert:,d}"
    else:
        return str(wert)


class LeaderboardRuntime:
    """Runtime für einen einzelnen Leaderboard-Eintrag (vom RA-API).
    
    Das RA-Objekt enthält: id, title, format, start_mem, cancel_mem, submit_mem.
    Diese werden hier geparst (wie in ra_condition_nes.py).
    """
    def __init__(self, lb_obj):
        self.id = lb_obj.get("id")
        self.title = lb_obj.get("title", "")
        self.format = lb_obj.get("format", "SCORE")
        
        self.unsupported = False
        self.started = False
        self.submitted = False
        
        # Bedingungen parsen wie in ra_condition_nes.py
        try:
            import ra_condition_nes as racond
            self.start_cond = racond.parse_condition(lb_obj.get("start_mem", ""))
            self.cancel_cond = racond.parse_condition(lb_obj.get("cancel_mem", ""))
            self.submit_cond = racond.parse_condition(lb_obj.get("submit_mem", ""))
        except Exception:
            self.unsupported = True
            self.start_cond = None
            self.cancel_cond = None
            self.submit_cond = None
    
    def adressen(self):
        """Alle Adressen, die dieses LB braucht."""
        import ra_condition_nes as racond
        adressen = set()
        for cond in (self.start_cond, self.cancel_cond, self.submit_cond):
            if cond:
                adressen.update(racond.collect_addresses_from_runtime(cond))
        return adressen
    
    def update(self, ram, prev_ram):
        """Aktualisiert Zustand und gibt Ereignis zurück.
        
        Rückgabe:
          None — nichts passiert
          "start" — Leaderboard hat gestartet
          "cancel" — wurde abgebrochen
          ("submit", wert) — Submit-Bedingung erfüllt, wert ist der Messwert
        """
        if self.unsupported or self.submitted:
            return None
        
        # start_cond prüfen
        if not self.started:
            if self.start_cond:
                try:
                    ok, _ = self.start_cond(ram, prev_ram)
                    if ok:
                        self.started = True
                        return "start"
                except Exception:
                    pass
            return None
        
        # cancel_cond prüfen
        if self.cancel_cond:
            try:
                ok, _ = self.cancel_cond(ram, prev_ram)
                if ok:
                    self.started = False
                    return "cancel"
            except Exception:
                pass
        
        # submit_cond prüfen
        if self.submit_cond:
            try:
                ok, wert = self.submit_cond(ram, prev_ram)
                if ok:
                    self.submitted = True
                    return ("submit", wert)
            except Exception:
                pass
        
        return None
