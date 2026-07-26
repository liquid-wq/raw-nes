"""
ra_client_nes.py -- RetroAchievements-API-Anbindung fuer NES/RAW-NES.

Adaptiert von MEGA-RAWs md_ra_tool.py (github.com/liquid-wq/mega-raw):
gleiche dorequest.php-API-Endpunkte (r=login2/gameid/patch/startsession/
awardachievement), gleiche Cache-Strategie. Console-spezifisch angepasst:
NES-Hash statt Genesis-Hash (siehe nes_hash()).

UNGETESTET gegen die echte RA-API -- basiert auf MEGA-RAWs bestaetigt
funktionierendem Muster, aber:
  - Der NES-Hash-Algorithmus (Header abschneiden, PRG+CHR hashen) ist aus
    allgemeinem RA/NES-Wissen abgeleitet, NICHT gegen echte RA-Antworten
    verifiziert. Falls 'gameid' konsequent None liefert, ist das der
    erste Verdaechtige.
  - RA_HOST/Endpunkte sind console-unabhaengig (gleiche RAWeb-API fuer
    alle Systeme), daher hohe Zuversicht, dass dieser Teil stimmt.

(c) 2026 Liqui -- MEGA-RAW-Originalcode als Vorlage, siehe dortige Lizenz-
hinweise. Diese Datei: eigenstaendige NES-Adaption fuer RAW-NES.
"""
import hashlib
import json
import os
import time
import urllib.request
import urllib.parse
import urllib.error

RA_HOST = "retroachievements.org"
USER_AGENT = "RAW-NES/0.1"
RA_REQUEST_PAUSE = 0.4  # Pflicht-Pause vor jeder RA-Anfrage (Server-Schonung, fix, wie MEGA-RAW)


class RateLimited(Exception):
    def __init__(self, retry_after=0):
        super().__init__("rate limited")
        self.retry_after = retry_after


def _p(name):
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), name)


_ra_cache = None


def _cache():
    global _ra_cache
    if _ra_cache is None:
        try:
            _ra_cache = json.load(open(_p("ra_cache_nes.json"), encoding="utf-8"))
        except Exception:
            _ra_cache = {"gameid": {}, "patch": {}}
        _ra_cache.setdefault("gameid", {})
        _ra_cache.setdefault("patch", {})
    return _ra_cache


_cache_dirty = 0


def _cache_save(force=False):
    global _cache_dirty
    _cache_dirty += 1
    if not force and _cache_dirty < 10:
        return
    _cache_dirty = 0
    try:
        tmp = _p("ra_cache_nes.json.tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(_ra_cache, f)
        os.replace(tmp, _p("ra_cache_nes.json"))
    except Exception:
        pass


def ra_request(params):
    time.sleep(RA_REQUEST_PAUSE)  # feste Server-Schonung, nicht abschaltbar (wie MEGA-RAW)
    query = urllib.parse.urlencode(params)
    url = f"https://{RA_HOST}/dorequest.php?{query}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        if e.code == 429:
            ra = e.headers.get("Retry-After") if e.headers else None
            raise RateLimited(int(ra) if (ra and ra.isdigit()) else 0)
        raise


def ra_login(user, pw):
    d = ra_request({"r": "login2", "u": user, "p": pw})
    return d.get("Token") if d.get("Success") else None


def nes_hash(rom_path):
    """RA-Hash fuer NES: iNES-Header (16 Byte) abschneiden, PRG+CHR-Daten
    hashen. Falls kein iNES-Header vorhanden (.unh), die ganze Datei.
    ANNAHME, nicht gegen echte RA-Antworten verifiziert (siehe Docstring)."""
    with open(rom_path, "rb") as f:
        data = f.read()
    if data[:4] == b"NES\x1a":
        data = data[16:]
    return hashlib.md5(data).hexdigest()


def ra_gameid(md5, raise_limit=False):
    cache = _cache()["gameid"]
    if md5 in cache:
        return cache[md5] or None
    try:
        d = ra_request({"r": "gameid", "m": md5})
        gid = d.get("GameID") or None
        cache[md5] = gid
        _cache_save()
        return gid
    except RateLimited:
        if raise_limit:
            raise
        return None
    except Exception:
        return None


def ra_patch(gameid, user, token):
    cache = _cache()["patch"]
    key = str(gameid)
    if key in cache:
        return cache[key]
    try:
        d = ra_request({"r": "patch", "g": gameid, "u": user, "t": token})
        pd = d.get("PatchData")
        if pd:
            cache[key] = pd
            _cache_save()
        return pd
    except RateLimited:
        raise
    except Exception:
        return None


def ra_unlocks(gameid, user, token, gueltige_ids=None, hardcore=0):
    """Bereits freigeschaltete Achievement-IDs vom Account holen.

    gueltige_ids: optionales Set -- nur IDs daraus werden zurueckgegeben
                  (filtert Fremd-/Alt-Achievements aus der Session-Antwort).
    hardcore:     0/1 -- an RA weitergereicht (Hardcore-Session).

    Gibt (ids, info) zurueck: ids ist ein Set der freigeschalteten
    Achievement-IDs, info ein kurzer Statustext.
    """
    try:
        params = {"r": "startsession", "g": gameid, "u": user, "t": token}
        if hardcore:
            params["h"] = 1
        d = ra_request(params) or {}
        ids = set()
        for k, v in d.items():
            if "unlock" in str(k).lower() and isinstance(v, list):
                for e in v:
                    if isinstance(e, dict):
                        i = e.get("ID") or e.get("AchievementID") or e.get("id")
                        if i is not None:
                            ids.add(int(i))
                    else:
                        try:
                            ids.add(int(e))
                        except (TypeError, ValueError):
                            pass
        if gueltige_ids is not None:
            ids &= set(gueltige_ids)
        return ids, "OK"
    except Exception as e:
        return set(), f"Fehler: {e}"


def ra_ping(gameid, user, token, rich_presence=None):
    """Aktivitaets-Ping an RA (r=ping).

    Laut RA-Integrationsdoku senden Emulatoren alle zwei Minuten einen
    Ping. Ohne ihn wird die Spielzeit nicht erfasst, das Spiel taucht
    nicht in der Spielerliste auf und erscheint nicht als "zuletzt
    gespielt" im Profil. Der optionale Text erscheint als Aktivitaet
    ("Last Seen In").

    Gibt (ok, info) zurueck.
    """
    try:
        params = {"r": "ping", "g": gameid, "u": user, "t": token}
        if rich_presence:
            params["m"] = rich_presence[:120]
        d = ra_request(params) or {}
        if d.get("Success", False):
            return True, "OK"
        return False, f"RA lehnte Ping ab: {d.get('Error') or d}"
    except Exception as e:
        return False, f"Netzwerk-/API-Fehler: {e}"


def ra_award(achid, user, token, hardcore=0):
    """Gibt (ok, info) zurueck. info enthaelt die Server-Antwort bzw. den
    Fehler -- ohne das war nicht erkennbar, WARUM ein Award nicht gebucht
    wird (Token abgelaufen, Session fehlt, Achievement-ID unbekannt ...)."""
    try:
        d = ra_request({"r": "awardachievement", "a": achid, "h": hardcore,
                        "u": user, "t": token})
        ok = bool(d.get("Success", False))
        if ok:
            return True, "OK"
        return False, f"RA lehnte ab: {d.get('Error') or d}"
    except Exception as e:
        return False, f"Netzwerk-/API-Fehler: {e}"


def build_game(gid, md5, user, token):
    """RA-Patch holen und ein vollstaendiges game-Dict bauen. Liefert
    game-Dict oder None."""
    patch = ra_patch(gid, user, token)
    if not patch:
        return None
    title = patch.get("Title", f"#{gid}")
    if "unsupported game version" in title.lower():
        return {"name": title, "gameid": gid, "md5": md5, "no_set": True, "achievements": []}

    achs = []
    for ac in patch.get("Achievements", []):
        if ac.get("Flags") != 3 or ac.get("ID", 0) >= 101000000:
            continue
        achs.append({
            "id": ac.get("ID"),
            "title": ac.get("Title", ""),
            "desc": ac.get("Description", ""),
            "mem": ac.get("MemAddr", ""),
            "points": ac.get("Points", 0),
            "badge": ac.get("BadgeName", ""),
        })
    return {"name": title, "gameid": gid, "md5": md5, "no_set": False, "achievements": achs}


def identify_and_load(rom_path, user, token):
    """Kompletter Ablauf: Hash -> GameID -> Achievement-Set.
    Gibt (game_dict, fehler_text) zurueck; game_dict ist None bei Fehler."""
    md5 = nes_hash(rom_path)
    gid = ra_gameid(md5)
    if not gid:
        return None, f"Kein RA-Eintrag fuer diese ROM gefunden (Hash: {md5})."
    game = build_game(gid, md5, user, token)
    if not game:
        return None, f"RA-Patch fuer Spiel #{gid} konnte nicht geladen werden."
    if game.get("no_set"):
        return game, "Spiel erkannt, aber kein Achievement-Set vorhanden."
    return game, None
