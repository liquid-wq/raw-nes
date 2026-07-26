"""
RetroAchievements Condition Engine v2
rcheevos-Semantik: A (AddSource), B (SubSource), N (AndNext), O (OrNext),
Z (ResetNextIf), P (PauseIf, sticky via Hits), R (ResetIf), T (Trigger),
M (Measured), Hit-Counts (.N.), Alt-Gruppen (S), Delta (d), BCD (b),
Multiplikator (*x), Bit-Groessen.

NES: RA-Byte-Adresse N -> physisch identisch (kein Byteswap, keine
Basis-Verschiebung) -- angenommen, RA bildet NES-Systemspeicher direkt auf
$0000-$07FF ab, wie es beim NES ueblich ist. NICHT gegen echte RA-Daten
verifiziert, nur aus allgemeinem RA/NES-Wissen abgeleitet.
Mehr-Byte-Werte little-endian ueber RA-Bytes = nativ big-endian (verifiziert).

Nicht abbildbare Features (Prior 'p', Float 'f', AddAddress 'I', AddHits 'C/D',
Adressen >= 0x10000) -> Achievement unsupported -> triggert NIE.

Adaptiert von MEGA-RAWs ra_condition.py (github.com/liquid-wq/mega-raw) fuer NES:
nur ra_byte_to_phys()/WORKRAM_BASE geaendert (kein Byteswap noetig), der
restliche rcheevos-Condition-Engine-Code ist unveraendert uebernommen.
Original (c) 2026 Liqui — privates, nicht-kommerzielles Projekt. Nutzung nur unter
Nennung des Urhebers, keine Veroeffentlichung unter fremdem Namen.
"""
import re

ENGINE_VERSION = "v4"

WORKRAM_BASE = 0x0000  # NES: keine Verschiebung noetig

# Groessen: (nbytes, special, big_endian)
_SIZE = {
    '':  (2, None, False), ' ': (2, None, False),
    'h': (1, None, False),
    'x': (4, None, False),
    'w': (3, None, False),
    'i': (2, None, True),
    'j': (3, None, True),
    'g': (4, None, True),
    'm': (1, ('bit', 0), False), 'n': (1, ('bit', 1), False),
    'o': (1, ('bit', 2), False), 'p': (1, ('bit', 3), False),
    'q': (1, ('bit', 4), False), 'r': (1, ('bit', 5), False),
    's': (1, ('bit', 6), False), 't': (1, ('bit', 7), False),
    'l': (1, ('low4',), False), 'u': (1, ('up4',), False),
    'k': (1, ('cnt',), False),
}

# Size-Zeichenklasse explizit (keine Hex-Ziffern a-f!)
_OP_RE = re.compile(
    r'^([dpb]?)'
    r'(?:0x([hHxXwWiIjJgGmMnNoOpPqQrRsStTlLuUkK ]?)([0-9a-fA-F]+)'
    r'|h([0-9a-fA-F]+)'
    r'|(\d+(?:\.\d+)?)'
    r'|f([0-9.]+))'
    r'(?:([*/&^%])(?:0x([0-9a-fA-F]+)|([0-9.]+)))?$'
)

_CMP_RE = re.compile(r'(!=|<=|>=|<|>|=)')

SUPPORTED_FLAGS = {None, 'A', 'B', 'N', 'O', 'Z', 'P', 'R', 'T', 'M', 'C', 'D'}


def _parse_operand(token):
    token = token.strip()
    if not token:
        return None
    m = _OP_RE.match(token)
    if not m:
        return None
    (mod, size_c, addr_hex, const_hex, const_dec, const_f,
     mod_op, mod_hex, mod_dec) = m.groups()
    # Operanden-Modifier (rcheevos): *  /  &  ^  %
    # Bisher wurde nur '*' erkannt; '&' (Bitmaske) kommt in vielen Sets
    # vor (z.B. Metroid: 'A:0xH6990&15') und liess das ganze Achievement
    # als nicht parsebar durchfallen.
    if mod_hex is not None:
        mod_val = int(mod_hex, 16)
    elif mod_dec is not None:
        mod_val = float(mod_dec) if '.' in mod_dec else int(mod_dec)
    else:
        mod_op, mod_val = None, 1
    mult = mod_val if mod_op is None else 1

    if const_hex is not None:
        return {'type': 'const', 'value': int(const_hex, 16), 'mult': mult,
                'mod': mod, 'modop': mod_op, 'modval': mod_val}
    if const_dec is not None:
        v = float(const_dec) if '.' in const_dec else int(const_dec)
        return {'type': 'const', 'value': v, 'mult': mult,
                'mod': mod, 'modop': mod_op, 'modval': mod_val}
    if const_f is not None:
        return {'type': 'const', 'value': 0, 'mult': mult, 'mod': mod,
                'modop': mod_op, 'modval': mod_val, 'unsupported': True}

    addr = int(addr_hex, 16)
    size_key = size_c.lower() if size_c and size_c != ' ' else (size_c or '')
    nbytes, special, be = _SIZE.get(size_key, (1, None, False))
    op = {'type': 'mem', 'ra': addr, 'nbytes': nbytes, 'special': special,
          'be': be, 'mod': mod, 'mult': mult,
          'modop': mod_op, 'modval': mod_val}
    if addr >= 0x10000:
        op['unsupported'] = True
    return op


def _parse_condition(cond):
    flag = None
    m = re.match(r'^([A-Za-z]):(.*)$', cond)
    if m:
        flag = m.group(1)
        if flag == 'Q':   # MeasuredIf: im Trigger-Pfad wie normale Bedingung
            flag = None
        cond = m.group(2)

    target = 0
    m = re.search(r'\.(\d+)\.$', cond)   # strikt .N. mit Schlusspunkt
    if m:
        target = int(m.group(1))
        cond = cond[:m.start()]

    m = _CMP_RE.search(cond)
    if m:
        op = m.group(1)
        left = _parse_operand(cond[:m.start()])
        right = _parse_operand(cond[m.end():])
    else:
        op = None
        left = _parse_operand(cond)
        right = None

    return {'flag': flag, 'left': left, 'op': op, 'right': right,
            'target': target, 'hits': 0}


def parse_memaddr(memaddr):
    groups = []
    for grp in re.split(r'(?<!0x)S', memaddr):  # 'S' in 0xS.. = Bit6-Operand, kein Separator
        conds = []
        for c in grp.split('_'):
            c = c.strip()
            if c:
                conds.append(_parse_condition(c))
        if conds:
            groups.append(conds)
    return groups


def _is_unsupported(groups):
    if not groups:
        return True
    for g in groups:
        for c in g:
            if c['flag'] not in SUPPORTED_FLAGS:
                return True
            if c['left'] is None:
                return True
            if c['op'] is not None and c['right'] is None:
                return True
            for side in (c['left'], c['right']):
                if side and side.get('unsupported'):
                    return True
    return False


def ra_byte_to_phys(ra_addr):
    # NES: direkte 1:1-Abbildung, kein Byteswap wie bei Genesis noetig.
    return WORKRAM_BASE + (ra_addr & 0xFFFF)


def collect_addresses(achievements):
    """Alle physischen Byte-Adressen aus allen Achievements (sortierte Liste)."""
    addrs = set()
    for ac in achievements:
        mem = ac.get('mem') or ac.get('MemAddr', '')
        for grp in parse_memaddr(mem):
            for c in grp:
                for side in (c['left'], c['right']):
                    if side and side.get('type') == 'mem' \
                            and not side.get('unsupported'):
                        for i in range(side['nbytes']):
                            addrs.add(ra_byte_to_phys(side['ra'] + i))
    return sorted(addrs)


def _bcd(v, nbytes):
    total = 0
    for shift in range((nbytes * 2 - 1) * 4, -1, -4):
        total = total * 10 + ((v >> shift) & 0xF)
    return total


def _apply_mod(v, op):
    """Operanden-Modifier anwenden (rcheevos: *  /  &  ^  %)."""
    o, mv = op.get('modop'), op.get('modval', 1)
    if o is None:
        return v * op.get('mult', 1)
    if o == '*':
        return v * mv
    if o == '/':
        return v / mv if mv else 0
    if o == '&':
        return int(v) & int(mv)
    if o == '^':
        return int(v) ^ int(mv)
    if o == '%':
        return int(v) % int(mv) if mv else 0
    return v


def _read(op, ram, prev, prior=None):
    if op['type'] == 'const':
        return _apply_mod(op['value'], op)
    if op['mod'] == 'd':
        src = prev
    elif op['mod'] == 'p':
        src = prior if prior is not None else prev   # Prior = Wert vor letzter Aenderung
    else:
        src = ram
    v = 0
    if op['be']:
        for i in range(op['nbytes']):
            v = (v << 8) | src.get(ra_byte_to_phys(op['ra'] + i), 0)
    else:
        for i in range(op['nbytes']):
            v |= src.get(ra_byte_to_phys(op['ra'] + i), 0) << (8 * i)
    sp = op['special']
    if sp:
        if sp[0] == 'bit':
            v = (v >> sp[1]) & 1
        elif sp[0] == 'low4':
            v = v & 0xF
        elif sp[0] == 'up4':
            v = (v >> 4) & 0xF
        elif sp[0] == 'cnt':
            v = bin(v & 0xFF).count('1')
    if op['mod'] == 'b':
        v = _bcd(v, op['nbytes'])
    return _apply_mod(v, op)


def _cmp(a, op, b):
    if op == '=':  return a == b
    if op == '!=': return a != b
    if op == '>':  return a > b
    if op == '>=': return a >= b
    if op == '<':  return a < b
    if op == '<=': return a <= b
    return False


class AchievementRuntime:
    """Stateful-Auswertung eines Achievements ueber Frames (Hit-Counter)."""

    def __init__(self, memaddr):
        self.groups = parse_memaddr(memaddr)
        self.unsupported = _is_unsupported(self.groups)
        self.prior = {}        # phys-Adresse -> Wert vor der letzten Aenderung
        self._last = {}        # phys-Adresse -> letzter gesehener Wert

    def reset(self):
        for g in self.groups:
            for c in g:
                c['hits'] = 0

    def _walk(self, group, ram, prev, pause_pass):
        """Ein Durchlauf durch eine Gruppe.
        pause_pass=True : nur P-Bedingungen erhalten Hit-Updates; liefert paused.
        pause_pass=False: alle anderen erhalten Hit-Updates; liefert (sat, reset).
        Ketten (A/B/N/O/Z) werden in beiden Paessen identisch verarbeitet.
        """
        accum = 0
        hit_pool = 0           # AddHits/SubHits-Summe fuer die naechste Ziel-Bedingung
        chain_val = None       # bisheriges Ketten-Ergebnis
        chain_op = None        # 'and' / 'or' Verknuepfung zum naechsten Glied
        reset_next = False
        paused = False
        satisfied = True
        any_countable = False
        reset = False

        for c in group:
            flag = c['flag']
            raw_val = _read(c['left'], ram, prev, self.prior)

            # AddSource / SubSource: nur Akkumulator, keine Wahrheit
            if flag == 'A':
                accum += raw_val
                continue
            if flag == 'B':
                accum -= raw_val
                continue

            lval = raw_val + accum
            if c['op'] is not None:
                raw = _cmp(lval, c['op'], _read(c['right'], ram, prev, self.prior))
            else:
                raw = True

            if chain_val is None:
                combined = raw
            elif chain_op == 'or':
                combined = chain_val or raw
            else:
                combined = chain_val and raw

            if flag == 'N':
                chain_val, chain_op = combined, 'and'
                accum = 0
                continue
            if flag == 'O':
                chain_val, chain_op = combined, 'or'
                accum = 0
                continue
            if flag == 'Z':
                if combined:
                    reset_next = True
                chain_val = chain_op = None
                accum = 0
                continue
            if flag in ('C', 'D'):
                # AddHits/SubHits: zaehlt eigene Hits (mit Ketten/Z/Cap) und
                # speist den Pool der naechsten Ziel-Bedingung. Entscheidet nichts.
                if not pause_pass:
                    if reset_next:
                        c['hits'] = 0
                    if combined and (c['target'] == 0 or c['hits'] < c['target']):
                        c['hits'] += 1
                hit_pool += c['hits'] if flag == 'C' else -c['hits']
                reset_next = False
                chain_val = chain_op = None
                accum = 0
                continue

            # reale Bedingung (None / P / R / T / M)
            accum = 0
            chain_val = chain_op = None

            is_pause = (flag == 'P')
            do_hits = (pause_pass and is_pause) or (not pause_pass and not is_pause)

            if do_hits:
                if reset_next:
                    c['hits'] = 0
                if c['target'] > 0:
                    if combined and c['hits'] < c['target']:
                        c['hits'] += 1
                    sat = (c['hits'] + hit_pool) >= c['target']
                else:
                    sat = combined
            else:
                # Zustand nur lesen, nicht veraendern
                if c['target'] > 0:
                    sat = (c['hits'] + hit_pool) >= c['target']
                else:
                    sat = combined
            hit_pool = 0
            reset_next = False

            if is_pause:
                if pause_pass and sat:
                    paused = True
                continue
            if pause_pass:
                continue

            if flag == 'R':
                if sat:
                    reset = True
                continue

            any_countable = True
            if not sat:
                satisfied = False

        if pause_pass:
            return paused
        # rcheevos: eine Gruppe ohne zaehlbare Bedingungen (nur ResetIf/PauseIf,
        # z.B. Reset-Alt-Gruppen) gilt als erfuellt.
        return (satisfied if any_countable else True), reset

    def _track_prior(self, ram):
        # Prior-Wert = letzter Wert, BEVOR sich die Adresse zuletzt aenderte.
        for addr, val in ram.items():
            if addr in self._last and self._last[addr] != val:
                self.prior[addr] = self._last[addr]
            self._last[addr] = val

    def update(self, ram, prev):
        """Pro Frame aufrufen. True wenn das Achievement jetzt feuert."""
        if self.unsupported or prev is None:
            self._track_prior(ram)
            return False
        self._track_prior(ram)

        paused = [self._walk(g, ram, prev, pause_pass=True)
                  for g in self.groups]

        results = []
        reset_all = False
        for g, p in zip(self.groups, paused):
            if p:
                results.append(False)
                continue
            sat, rst = self._walk(g, ram, prev, pause_pass=False)
            results.append(sat)
            if rst:
                reset_all = True

        if reset_all:
            self.reset()
            return False

        core = results[0]
        alts = results[1:]
        return core and (not alts or any(alts))


def evaluate_achievement(memaddr, ram, prev):
    """Stateless-Wrapper fuer Einzelchecks."""
    rt = AchievementRuntime(memaddr)
    return rt.update(ram, prev if prev else ram)
