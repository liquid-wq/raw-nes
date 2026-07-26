"""
ed_serial_nes.py -- Direkte serielle Verbindung zum EverDrive N8 PRO

Adaptiert von MEGA-RAWs ed_serial.py (github.com/liquid-wq/mega-raw), das
bei kontinuierlichem Live-Polling auf echter Hardware bestaetigt
funktioniert. Der Grund fuer den Wechsel weg von wiederholten
edlink.exe-Prozessaufrufen: jeder Aufruf oeffnet die COM-Verbindung neu
(Handshake, Enumeration) -- bei Dauerabfrage fuehrt das zu Instabilitaet
bis hin zu Haengern. Eine offen gehaltene serielle Verbindung mit eigener
Fehlerbehandlung ist die von MEGA-RAW erprobte Loesung.

BESTAETIGT gegen den Quellcode github.com/krikzz/edlink (DEV_EDN8/DeviceIO.cs,
Device/DeviceIO_V1.cs, Device/Link.cs):
  - Befehlscodes (CMD_STATUS=0x10, CMD_MEM_RD=0x19, CMD_MEM_WR=0x1A) und
    das Rahmenformat (2B D4 <code> <code^0xFF> ...) sind geraeteuebergreifend
    identisch (Basisklasse DeviceIO_V1) -- nicht MEGA-spezifisch.
  - ADDR_FCI_SRM = 0x1000000 fuer N8 PRO ist exakt der Wert aus
    DEV_EDN8/DeviceIO.cs (dort so benannt), stimmt mit unserem ADDR_SRM ueberein.
  - Byte-Reihenfolge: DEV_EDN8 setzt 'link.SwapEndians = false' (Little-Endian),
    im Unterschied zu DEV_MEGA ('SwapEndians = true', Big-Endian) -- MEGA-RAWs
    ed_serial.py war dafuer bereits korrekt auf big-endian eingestellt, das
    N8-PRO-Aequivalent hier braucht little-endian (siehe BYTE_ORDER unten).

Benutzung:
    python ed_serial_nes.py COM10 --addr 0x1000000 --marker-offset 2
"""
import time
import threading

RAWNES_VERSION = "v20"  # Versionsstempel: GUI zeigt beim Start alle Modulversionen


try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

CMD_STATUS = 0x10
CMD_MEM_RD = 0x19
CMD_MEM_WR = 0x1A

# SD-Datei-Kommandos (krikzz/edlink Device/DeviceIO_V1.cs) -- gehen ueber
# MCU<->SD (SPI), NICHT ueber den Cart-Bus. Sicher auch waehrend ein Spiel
# laeuft (im Unterschied zu memrd/memwr auf PRG/CHR/SRAM).
CMD_F_FOPN   = 0xC9
CMD_F_FRD    = 0xCA
CMD_F_FCLOSE = 0xCE
CMD_F_AVB    = 0xD5
FA_READ = 0x01

# FIFO-Puffer im EverDrive-Adressraum (DEV_EDN8/DeviceIO.cs: ADDR_FCI_FIFO
# = 0x1810000). Liegt OBERHALB von ADDR_CFG (0x1800000) -- Zugriffe hierauf
# brauchen KEINEN Cart-Bus-DMA und sind darum jederzeit sicher, auch bei
# laufendem Spiel. edlinks FifoWR() ist exakt ein MemWR auf diese Adresse
# (Device/DeviceIO_V1.cs) -- so werden auch die Menue-Kommandos gesendet.
ADDR_FIFO = 0x1810000

# Cart-Speicherraeume (edn8-pro-pub edio/everdrive.h): PRG-PSRAM ab 0,
# SRAM/Battery-RAM ab 0x1000000. Zugriffe hierauf loesen MCU-DMA aus --
# NUR im Menue sicher, NIE waehrend ein Spiel laeuft.
ADDR_PRG = 0x0000000
ADDR_SRM = 0x1000000

BYTE_ORDER = "little"  # DEV_EDN8/DeviceIO.cs: link.SwapEndians = false -> little-endian
                        # (bestaetigt aus github.com/krikzz/edlink Quellcode, nicht mehr nur Annahme)


def _cmd(code):
    return bytes([0x2B, 0xD4, code, code ^ 0xFF])


class EdSerialNes:
    def __init__(self, port="COM10"):
        if serial is None:
            raise RuntimeError("pyserial fehlt: pip install pyserial")
        self.portname = port
        self.ser = None
        self._lock = threading.RLock()
        self._last_counter = None

    def open(self):
        self.ser = serial.Serial(self.portname, baudrate=921600, timeout=0.5,
                                  write_timeout=0.5)
        self.ser.write(bytes(66))
        self.ser.flush()
        self.ser.timeout = 0.05
        quiet, t_start = 0.0, time.time()
        while quiet < 0.3 and time.time() - t_start < 3.0:
            chunk = self.ser.read(4096)
            quiet = 0.0 if chunk else quiet + 0.05
        self.ser.reset_input_buffer()
        self.ser.timeout = 0.5
        self.ser.write(_cmd(CMD_STATUS))
        self.ser.flush()
        time.sleep(0.05)
        resp = self.ser.read(4)
        if not resp or (0x5A not in resp and 0xA5 not in resp):
            self.close()
            raise RuntimeError(f"EverDrive antwortet nicht (Status: {resp.hex() if resp else 'leer'})")
        self.ser.timeout = 2.0
        return True

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None

    def _recover(self):
        try:
            self.close()
        except Exception:
            pass
        time.sleep(0.8)
        try:
            self.open()
            return True
        except Exception:
            pass
        for p in (list_ports.comports() if list_ports else []):
            try:
                self.portname = p.device
                self.open()
                return True
            except Exception:
                try:
                    self.close()
                except Exception:
                    pass
        return False

    # ---- FIFO-Streaming (fuer den 'patch'/'inject'-Weg mit build_stub) ----
    # Der 6502-Stub schickt Frames AKTIV ueber $40F0 -> FIFO -> Serial.
    # Der PC muss dafuer NICHTS anfragen (kein memrd/CMD_MEM_RD), sondern
    # nur passiv die ankommenden Roh-Bytes lesen -- kein Cart-Bus-DMA,
    # also auch waehrend das Spiel laeuft sicher (siehe Modul-Docstring in
    # rawnes_patch.py ueber ADDR_SRM: nur memrd/memwr mit addr < ADDR_CFG
    # loesen den DMA aus, reines Passiv-Lesen des seriellen Puffers tut
    # das nicht, weil dabei ueberhaupt kein Kommando an die MCU geht).
    #
    # Frame-Format identisch zu rawnes_patch.py FRAME_HDR/build_stub:
    #   2B D4 22 DD <len_lo> <len_hi> <sync_byte> <watch_werte...>

    FRAME_HDR = bytes([0x2B, 0xD4, 0x22, 0xDD])  # Konsole->MCU-Framing; kommt am PC NIE an (MCU konsumiert es)
    _fifo_buf = bytearray()

    def read_fifo_frame(self, payload_len, sync_byte=0xA5, timeout=2.0):
        """Sucht im passiv ankommenden Serial-Strom den naechsten
        vollstaendigen Frame und gibt die Watch-Werte zurueck (ohne
        Sync-Byte). Gibt None bei Timeout zurueck.

        WICHTIG (Fix Handoff 9): am PC kommt NUR die Nutzlast an --
        die MCU KONSUMIERT das Konsole->MCU-Framing (2B D4 22 DD + Laenge,
        edio ed_cmd_usb_wr) und leitet ausschliesslich die Datenbytes an
        USB weiter. Die alte Suche nach FRAME_HDR im USB-Strom konnte
        deshalb PRINZIPIELL nie treffen. Neue Erkennung direkt auf dem
        Nutzlast-Strom: Frames liegen als (sync + payload_len Bytes)
        Ruecken an Ruecken; ein Kandidat gilt als Frame, wenn im Puffer
        an der naechsten Stride-Position wieder das Sync-Byte steht
        (oder er das letzte vollstaendige Frame im Puffer ist)."""
        stride = 1 + payload_len
        deadline = time.time() + timeout
        buf = self._fifo_buf
        with self._lock:
            while time.time() < deadline:
                chunk = self.ser.read(64)
                if chunk:
                    buf += chunk
                i = 0
                while i + stride <= len(buf):
                    if buf[i] != sync_byte:
                        i += 1
                        continue
                    # Verifikation ueber den Folge-Frame, falls vorhanden
                    if i + stride < len(buf) and buf[i + stride] != sync_byte:
                        i += 1
                        continue
                    payload = bytes(buf[i + 1:i + stride])
                    del buf[:i + stride]
                    return payload
                if len(buf) > 4096:
                    del buf[:-stride]
        return None

    def detect_frame_layout(self, sync_byte=0xA5, listen=1.5):
        """Bestimmt die Frame-Laenge automatisch aus dem laufenden Stream
        (Abstands-Analyse der Sync-Bytes, wie --raw-dump) -- damit muss
        NIEMAND mehr die Watch-Anzahl von Hand wissen/eintragen. Gibt
        (payload_len, sample_payload) zurueck, (None, None) wenn kein
        stabiles Raster gefunden wurde. Gelesene Bytes gehen NICHT
        verloren (werden an _fifo_buf angehaengt).
        Adaptiv: 'listen' ist nur die MINDEST-Lauschzeit. Langsame Streams
        (SRAMEXEC-Faelle mit ~1 Frame/s, Xevious-Befund) liefern in 1.5s
        keine 4 Sync-Bytes -- darum wird bis max_listen weitergelauscht,
        bis genug Syncs fuer eine Abstands-Analyse da sind."""
        max_listen = max(listen, 8.0)
        t0 = time.time()
        buf = bytearray()
        with self._lock:
            while True:
                elapsed = time.time() - t0
                if elapsed >= max_listen:
                    break
                if elapsed >= listen and sum(1 for b in buf if b == sync_byte) >= 5:
                    break
                chunk = self.ser.read(4096)
                if chunk:
                    buf += chunk
            self._fifo_buf += buf
        pos = [i for i, b in enumerate(buf) if b == sync_byte]
        if len(pos) < 4:
            return None, None
        from collections import Counter
        gaps = Counter(b - a for a, b in zip(pos, pos[1:]))
        stride, n = gaps.most_common(1)[0]
        # Raster gilt nur als stabil, wenn der haeufigste Abstand klar
        # dominiert (Sync-Byte kann zufaellig auch als Wert vorkommen).
        if stride < 2 or n < max(3, (len(pos) - 1) // 2):
            return None, None
        payload_len = stride - 1
        sample = None
        for i in pos:
            if i + stride < len(buf) and buf[i + stride] == sync_byte:
                sample = bytes(buf[i + 1:i + stride])
                break
        return payload_len, sample

    def memrd(self, addr, length):
        try:
            return self._memrd_raw(addr, length)
        except Exception:
            if self._recover():
                return self._memrd_raw(addr, length)
            raise

    def memwr(self, addr, data):
        try:
            return self._memwr_raw(addr, data)
        except Exception:
            if self._recover():
                return self._memwr_raw(addr, data)
            raise

    def _memrd_raw(self, addr, length):
        with self._lock:
            self.ser.reset_input_buffer()
            pkt = _cmd(CMD_MEM_RD)
            pkt += addr.to_bytes(4, BYTE_ORDER)
            pkt += length.to_bytes(4, BYTE_ORDER)
            pkt += b"\x00"
            self.ser.write(pkt)
            self.ser.flush()
            data = self.ser.read(length)
            if len(data) != length:
                raise RuntimeError(f"memrd: {len(data)}/{length} Bytes erhalten")
            return data

    def _memwr_raw(self, addr, data):
        with self._lock:
            pkt = _cmd(CMD_MEM_WR)
            pkt += addr.to_bytes(4, BYTE_ORDER)
            pkt += len(data).to_bytes(4, BYTE_ORDER)
            pkt += b"\x00"
            pkt += bytes(data)
            self.ser.write(pkt)
            self.ser.flush()

    # ---- SD-Datei-Lesen (Option 1: GUI startet Spiel selbst, liest ROM
    # direkt von der SD -- keine lokale PC-Kopie noetig). Quelle: krikzz/
    # edlink Device/DeviceIO_V1.cs (FileOpen/FileRead/FileAvailable/
    # FileClose), MIT-lizenziert, als Code-Basis freigegeben. Geht ueber
    # MCU<->SD (SPI), KEIN Cart-Bus-DMA -- sicher auch waehrend ein Spiel
    # laeuft. UNGETESTET auf echter Hardware (siehe _get_op_status).

    def _get_op_status(self):
        """Fragt den Status der letzten Datei-Operation ab (CMD_STATUS,
        separater Poll -- Muster aus edlink Device/DeviceIO_V1.cs:
        CheckStatus() -> GetStatus() -> Link.GetID()). Gibt das Status-Byte
        zurueck (0 = OK).

        UNGETESTET: Byte-Reihenfolge der Legacy-Antwort ist aus dem
        C#-Quellcode abgeleitet (Link.cs GetID(): fuer PROTOCOL_ID_N8 zuerst
        Status-Byte, dann Key 0xA5), aber noch nicht gegen echte Hardware
        verifiziert. Bei Fehlern hier zuerst mit einem Rohbyte-Dump der
        Antwort nachmessen (wie --raw-dump), nicht weiter raten."""
        with self._lock:
            self.ser.reset_input_buffer()
            self.ser.write(_cmd(CMD_STATUS))
            self.ser.flush()
            resp = self.ser.read(4)
        if not resp:
            raise RuntimeError("Status-Abfrage: keine Antwort")
        if resp[0] in (0x5A, 0xA5):
            return resp[-1]  # Key zuerst -> Status ist letztes Byte
        if resp[-1] in (0x5A, 0xA5):
            return resp[0]   # Status zuerst, Key danach (N8-Legacy)
        raise RuntimeError(f"Status-Abfrage: unbekanntes Antwortformat {resp.hex()}")

    def file_open(self, dev_path, mode=FA_READ):
        """Oeffnet eine Datei auf der SD-Karte (Pfad relativ zur SD-Wurzel,
        z.B. 'games/Metroid.nes' -- OHNE 'sd:'-Praefix)."""
        with self._lock:
            self.ser.reset_input_buffer()
            pkt = _cmd(CMD_F_FOPN)
            pkt += bytes([mode])
            path_bytes = dev_path.encode("ascii")
            pkt += len(path_bytes).to_bytes(2, BYTE_ORDER) + path_bytes
            self.ser.write(pkt)
            self.ser.flush()
        status = self._get_op_status()
        if status != 0:
            raise RuntimeError(f"file_open('{dev_path}') fehlgeschlagen: "
                               f"Status 0x{status:02X} (Pfad falsch/nicht auf SD?)")

    def file_available(self):
        """Restliche/gesamte Dateigroesse nach file_open() (CMD_F_AVB)."""
        with self._lock:
            self.ser.reset_input_buffer()
            self.ser.write(_cmd(CMD_F_AVB))
            self.ser.flush()
            hi = int.from_bytes(self.ser.read(4), BYTE_ORDER)
            lo = int.from_bytes(self.ser.read(4), BYTE_ORDER)
        return lo | (hi << 32)

    def file_read(self, length):
        """Liest 'length' Byte aus der offenen Datei (CMD_F_FRD), blockweise
        zu je max. 4096 Byte mit Status-Byte VOR jedem Block."""
        buf = bytearray()
        with self._lock:
            self.ser.reset_input_buffer()
            pkt = _cmd(CMD_F_FRD) + length.to_bytes(4, BYTE_ORDER)
            self.ser.write(pkt)
            self.ser.flush()
            remaining = length
            while remaining > 0:
                block = min(4096, remaining)
                resp = self.ser.read(1)
                if not resp or resp[0] != 0:
                    raise RuntimeError(f"file_read: Block-Fehler "
                                       f"(Status {resp.hex() if resp else 'leer'})")
                data = self.ser.read(block)
                if len(data) != block:
                    raise RuntimeError(f"file_read: {len(data)}/{block} Byte erhalten")
                buf += data
                remaining -= block
        return bytes(buf)

    def file_close(self):
        """Schliesst die offene Datei (CMD_F_FCLOSE + Status-Poll)."""
        with self._lock:
            self.ser.reset_input_buffer()
            self.ser.write(_cmd(CMD_F_FCLOSE))
            self.ser.flush()
        status = self._get_op_status()
        if status != 0:
            raise RuntimeError(f"file_close fehlgeschlagen: Status 0x{status:02X}")

    def read_file_from_sd(self, dev_path):
        """Liest eine komplette Datei von der SD-Karte -- open, available,
        read, close. dev_path relativ zur SD-Wurzel (kein 'sd:'-Praefix).
        Sicher waehrend ein Spiel laeuft (SPI-Bus, kein Cart-Bus-DMA)."""
        self.file_open(dev_path, FA_READ)
        try:
            size = self.file_available()
            data = self.file_read(size)
        finally:
            try:
                self.file_close()
            except Exception:
                pass  # Lesen ist schon durch -- Schliess-Fehler nicht kritisch
        return data

    # ---- Menue-Kommandos (Protokoll aus edlink DEV_EDN8/MenuCmd.cs:
    # '*'+Zeichen per FifoWR; FifoWR = MemWR auf ADDR_FIFO. Antworten
    # kommen als rohe Serial-Bytes, weil ed_cmd_usb_wr auf Konsolenseite
    # nur die Nutzdaten an den PC durchreicht.) Nur solange das MENUE
    # laeuft benutzen -- ein laufendes Spiel beantwortet sie nicht.

    def fifo_wr(self, data):
        """Entspricht edlinks FifoWR: MemWR auf den FIFO-Puffer.
        Kein Cart-Bus-DMA, jederzeit sicher."""
        self._memwr_raw(ADDR_FIFO, data)

    def _menu_cmd(self, ch):
        self.fifo_wr(bytes([ord("*"), ord(ch)]))

    def menu_test(self, retries=6, delay=1.0):
        """'*t' -- Menue antwortet 'k' (edlink MenuCmd.Test).
        Mit Retries: direkt nach einem edlink.exe-Aufruf ist das Geraet/
        der COM-Port unter Windows oft erst nach 1-3 s wieder sauber
        ansprechbar (bekannte Instabilitaet bei Subprozess-Wechsel)."""
        last = b""
        for _ in range(retries):
            with self._lock:
                self.ser.reset_input_buffer()
                self._menu_cmd("t")
                last = self.ser.read(1)
            if last == b"k":
                return
            time.sleep(delay)
        raise RuntimeError(
            f"Menue antwortet nach {retries} Versuchen nicht (zuletzt: "
            f"{last.hex() if last else 'nichts'}). Checkliste: 1) Konsole "
            f"zeigt das EverDrive-MENUE (kein Spiel, kein Schwarzbild -- "
            f"sonst Power-Cycle), 2) nach einem edlink.exe-Aufruf 3 s "
            f"warten, 3) kein anderes Programm haelt den COM-Port offen.")

    def menu_select_game(self, dev_path, timeout=20.0):
        """'*n' + laengen-praefigierter Pfad (edlink MenuCmd.AppInstall /
        DeviceIO.FifoTxString: u16-LE-Laenge + Zeichen). Das OS laedt
        dabei ROM (und ggf. .srm) von der SD -- kann dauern, darum
        eigener Timeout. Gibt den Mapper-Index zurueck."""
        with self._lock:
            self.ser.reset_input_buffer()
            self._menu_cmd("n")
            path_bytes = dev_path.encode("ascii")
            self.fifo_wr(len(path_bytes).to_bytes(2, BYTE_ORDER) + path_bytes)
            old_timeout = self.ser.timeout
            self.ser.timeout = timeout
            try:
                status = self.ser.read(1)
                if not status:
                    raise RuntimeError("Spielauswahl: keine Antwort vom Menue")
                if status[0] != 0:
                    raise RuntimeError(f"Spielauswahl fehlgeschlagen: "
                                       f"Status 0x{status[0]:02X} (Pfad falsch? "
                                       f"'{dev_path}')")
                map_idx = self.ser.read(2)
                if len(map_idx) != 2:
                    raise RuntimeError("Spielauswahl: Mapper-Index unvollstaendig")
                return int.from_bytes(map_idx, BYTE_ORDER)
            finally:
                # Restore darf bei totem Port nicht den echten Fehler
                # ueberdecken (PermissionError-Kaskade, Handoff 5)
                try:
                    self.ser.timeout = old_timeout
                except Exception:
                    pass

    def menu_run_game(self):
        """'*s' -- startet das zuvor selektierte Spiel (keine Antwort)."""
        self._menu_cmd("s")

    # MapConfig-Struct-Layout aus edn8-pro-pub/edio/everdrive.h:
    #   CheatList gg (8 * 4 Byte CheatSlot) = 32 Byte, dann:
    #   map_idx, prg_msk, chr_msk, master_vol, map_cfg, ss_key_save,
    #   ss_key_load, map_ctrl, ss_key_menu, jmp_val = je 1 Byte
    ADDR_CFG = 0x1800000
    MAPCFG_SIZE = 42
    MAPCFG_OFF_MAP_CFG = 36  # Offset von 'map_cfg' innerhalb MapConfig
    MAPCFG_OFF_PRG_MSK = 33  # Offset von 'prg_msk' innerhalb MapConfig

    def force_prg_ram_on(self):
        """Liest die MapConfig, die das OS nach menu_select_game() fuer
        DIESES Spiel gesetzt hat, und loescht darin NUR Bit 3 von
        map_cfg (= cfg.prg_ram_off in fpga/base_sv/sys_cfg.sv: "assign
        cfg.prg_ram_off = map_cfg[3]" -- 1=PRG-RAM AUS, 0=AN). Alle
        anderen vom OS ermittelten Werte (Mapper-Index, Bank-Masken,
        Mirroring in map_cfg[1:0] usw.) bleiben unveraendert, da nur
        gelesen+zurueckgeschrieben, nicht neu zusammengesetzt.
        NUR aufrufen waehrend das Menue noch kooperiert (vor
        menu_run_game())."""
        cfg = bytearray(self.memrd(self.ADDR_CFG, self.MAPCFG_SIZE))
        before = cfg[self.MAPCFG_OFF_MAP_CFG]
        cfg[self.MAPCFG_OFF_MAP_CFG] = before & ~0x08  # Bit 3 loeschen
        if cfg[self.MAPCFG_OFF_MAP_CFG] != before:
            self.memwr(self.ADDR_CFG, bytes(cfg))
        return before, cfg[self.MAPCFG_OFF_MAP_CFG]

    def force_srm_size(self, size_shift=6):
        """Fix fuer die SRAM-SPIEGELUNGS-FALLE (Ursache des WorldRunner-
        Absturzes): die CPU-Sicht auf SRAM wird mit
        (1 << prg_msk[7:4])-1 in 128-Byte-Einheiten maskiert
        (fpga/base_sv/everdrive.sv:106+110, sys_cfg.sv:29). Traegt das OS
        Groesse 0 ein, spiegelt ganz $6000-$7FFF in die untersten 128
        Chip-Bytes -- der Stub ist fuer die CPU unerreichbar, obwohl
        memrd ihn zeigt (DMA-Zugriffe umgehen die Maske:
        "dma.req_srm ? 'h03FFFF").
        Hebt das High-Nibble von prg_msk (MapConfig-Offset 33) auf
        size_shift an (6 = 128<<6 = 8K) -- aber NUR wenn es 0 ist:
        jede echte Groesse >= 1 (128+ Byte) reicht fuer einen Stub an
        Chip-Offset 0 bereits aus, und bei EEPROM-/Sondermappern koennte
        Verstellen einer korrekten kleinen Groesse Verhalten aendern.
        Low-Nibble (PRG-ROM-Maske!) bleibt unveraendert.
        NUR aufrufen waehrend das Menue noch kooperiert (vor
        menu_run_game())."""
        cfg = bytearray(self.memrd(self.ADDR_CFG, self.MAPCFG_SIZE))
        before = cfg[self.MAPCFG_OFF_PRG_MSK]
        if (before >> 4) == 0:
            cfg[self.MAPCFG_OFF_PRG_MSK] = (size_shift << 4) | (before & 0x0F)
            self.memwr(self.ADDR_CFG, bytes(cfg))
        return before, cfg[self.MAPCFG_OFF_PRG_MSK]

    def launch_with_sram_stub(self, dev_path, srm_bytes, sram_offset=0,
                               force_prg_ram=False, force_srm=False,
                               ref_dev_path=None):
        """Weg B des inject-Ablaufs: Spiel selektieren, Stub-SRAM-Inhalt
        per memwr schreiben SOLANGE das Menue noch kooperiert, dann
        starten.

        force_prg_ram/force_srm/ref_dev_path sind seit Handoff 4 per
        Default AUS: im Menue-Zustand liegt bei ADDR_CFG nur die
        SERVICE-Config (map_idx 255); die SPIEL-Config wird erst beim
        Start dorthin geschrieben (ed_start_app, everdrive.c). Alle
        Schreibzugriffe auf den Puffer vor dem Start waren damit
        wirkungslos fuer den Boot (bestaetigte Placebos) und veraendern
        nur die Service-Config -- bestenfalls nutzlos, schlimmstenfalls
        stoerend.

        Nach menu_run_game() darf KEIN memrd/memwr mehr auf PRG/CHR/SRAM
        erfolgen (MCU-DMA nimmt dem laufenden 6502 den Cart-Bus weg ->
        Freeze) -- Werte danach NUR ueber den FIFO-Stream lesen.
        Gibt (map_idx, prg_ram_off_vorher, prg_ram_off_nachher,
        prg_msk_vorher, prg_msk_nachher, ref_map_idx) zurueck;
        ref_map_idx ist None ohne ref_dev_path. Weichen map_idx und
        ref_map_idx voneinander ab, ist die DB-Fallback-Falle fuer
        dieses ROM BEWIESEN (OS waehlt fuer Kopie und Original
        verschiedene Mapper)."""
        self.menu_test()
        ref_map_idx = None
        ref_cfg = None
        if ref_dev_path:
            ref_map_idx = self.menu_select_game(ref_dev_path)
            ref_cfg = self.memrd(self.ADDR_CFG, self.MAPCFG_SIZE)
        map_idx = self.menu_select_game(dev_path)
        if ref_cfg is not None:
            self.memwr(self.ADDR_CFG, bytes(ref_cfg))
        prg_ram_before = prg_ram_after = None
        if force_prg_ram:
            before, after = self.force_prg_ram_on()
            prg_ram_before = bool(before & 0x08)
            prg_ram_after = bool(after & 0x08)
        prg_msk_before = prg_msk_after = None
        if force_srm:
            prg_msk_before, prg_msk_after = self.force_srm_size()
        if srm_bytes is not None:
            self.memwr(ADDR_SRM + sram_offset, srm_bytes)
        self.menu_run_game()
        return (map_idx, prg_ram_before, prg_ram_after,
                prg_msk_before, prg_msk_after, ref_map_idx)

    @staticmethod
    def decode_mapconfig(cfg):
        """Dekodiert die 42-Byte-MapConfig (Layout: edio/everdrive.h,
        Bedeutung der Bits: fpga/base_sv/sys_cfg.sv) in benannte Felder."""
        d = {}
        d["map_idx"] = cfg[32]
        d["prg_msk_rom_nibble (PRG-Groesse 2^n*16K)"] = cfg[33] & 0x0F
        d["prg_msk_srm_nibble (SRAM 2^n*128B)"] = cfg[33] >> 4
        d["chr_msk"] = cfg[34]
        d["master_vol"] = cfg[35]
        mc = cfg[36]
        d["map_cfg.mirroring (0=H 1=V 2=4scr 3=1scr)"] = mc & 0x03
        d["map_cfg.chr_ram (Bit 2)"] = (mc >> 2) & 1
        d["map_cfg.prg_ram_off (Bit 3)"] = (mc >> 3) & 1
        d["map_cfg.map_sub (Bits 7-4)"] = mc >> 4
        d["ss_key_save"] = cfg[37]
        d["ss_key_load"] = cfg[38]
        d["map_ctrl"] = cfg[39]
        d["ss_key_menu"] = cfg[40]
        d["jmp_val"] = cfg[41]
        return d

    def cfg_diff(self, dev_path_a, dev_path_b):
        """ACHTUNG, BEFUND HANDOFF 4: im Menue-Zustand liegt bei ADDR_CFG
        die SERVICE-Config (map_idx 255) -- die SPIEL-Config wird erst
        beim Start dorthin geschrieben (ed_start_app, everdrive.c). Diese
        Diagnose vergleicht daher NICHT die Boot-Configs der Spiele,
        sondern misst zweimal die Service-Config ("identisch" ist dann
        bedeutungslos). Nur noch nuetzlich, um den Pufferzustand selbst
        zu inspizieren. Fuer die echte Boot-Config: --postmortem nach
        einem Start."""
        self.menu_test()
        self.menu_select_game(dev_path_a)
        cfg_a = self.memrd(self.ADDR_CFG, self.MAPCFG_SIZE)
        self.menu_select_game(dev_path_b)
        cfg_b = self.memrd(self.ADDR_CFG, self.MAPCFG_SIZE)
        da, db = self.decode_mapconfig(cfg_a), self.decode_mapconfig(cfg_b)
        diff = {k: (da[k], db[k]) for k in da if da[k] != db[k]}
        return cfg_a, cfg_b, diff

    def launch_patch_in_place(self, orig_dev_path, prg_size, vec_bytes,
                               srm_bytes, sram_offset=0):
        """Weg C -- AUF HARDWARE IN DIESER FORM WIDERLEGT (Handoff 3):
        der eingebaute Verify schlug fehl und bewies, dass das OS das PRG
        NICHT beim select laedt -- im PSRAM lag noch das Abbild des
        zuletzt GESTARTETEN Spiels (die gepatchte Kopie des vorherigen
        Tests, erkennbar am NMI-Vektor $6000 bei sonst identischen
        Bytes). Das PRG-Laden passiert erst bei 'run'; ein Vektor-Patch
        vor dem run wuerde dabei ueberschrieben. Der Code bleibt fuer
        den Fall erhalten, dass sich das Verhalten anders darstellt --
        der Verify schuetzt vor jedem Fehlstart.

        Urspruengliche Idee: das UNVERAENDERTE Original selektieren
        (OS-DB konfiguriert korrekt), dann im noch kooperierenden Menue
        NUR die zwei NMI-Vektor-Bytes im PSRAM (ADDR_PRG + prg_size - 6)
        umbiegen und den Stub ins SRAM schreiben, dann Start.

        vec_bytes: 2 Bytes (lo, hi) der neuen NMI-Adresse.
        Gibt (map_idx, alte_vektor_bytes) zurueck."""
        self.menu_test()
        map_idx = self.menu_select_game(orig_dev_path)
        tail = self.memrd(ADDR_PRG + prg_size - 16, 16)
        if self._verify_tail is not None and bytes(tail) != bytes(self._verify_tail):
            raise RuntimeError(
                "PSRAM-Verify fehlgeschlagen: die letzten 16 PRG-Bytes im "
                "PSRAM entsprechen nicht der ROM-Datei -- das OS laedt das "
                "PRG offenbar nicht (vollstaendig) beim select. Weg C ist "
                "so nicht moeglich.\n"
                f"  PSRAM: {bytes(tail).hex(' ')}\n"
                f"  Datei: {bytes(self._verify_tail).hex(' ')}")
        old_vec = self.memrd(ADDR_PRG + prg_size - 6, 2)
        self.memwr(ADDR_PRG + prg_size - 6, bytes(vec_bytes))
        self.memwr(ADDR_SRM + sram_offset, srm_bytes)
        self.menu_run_game()
        return map_idx, bytes(old_vec)

    _verify_tail = None  # von der CLI aus der ROM-Datei gesetzt

    def postmortem(self, prg_size, stub_len, sram_offset=0):
        """NUR nach einem Absturz/Schwarzbild aufrufen -- memrd waehrend
        eines GESUNDEN Spiels friert die Konsole ein (DMA); eine bereits
        abgestuerzte Konsole kann uns egal sein (danach Power-Cycle).

        Liest den tatsaechlichen Endzustand nach dem fehlgeschlagenen
        Boot -- die einzige Sicht, die wir auf das haben, was 'run'
        wirklich getan hat (Befund Handoff 3: PRG wird erst bei run
        geladen, select-Zeitpunkt-Messungen koennen stale sein):
          - letzte 16 PRG-Bytes im PSRAM (enthalten die Vektoren)
          - Stub-Bereich im SRAM
          - MapConfig
        Gibt (psram_tail, sram_stub, cfg) zurueck."""
        psram_tail = bytes(self.memrd(ADDR_PRG + prg_size - 16, 16))
        sram_stub = bytes(self.memrd(ADDR_SRM + sram_offset, stub_len))
        counter = bytes(self.memrd(ADDR_SRM + 0x1F0, 1))[0]
        cfg = bytes(self.memrd(self.ADDR_CFG, self.MAPCFG_SIZE))
        return psram_tail, sram_stub, cfg, counter

    def read_watch(self, sram_addr, count, timeout=0.3, poll_interval=0.02):
        """SRAM-Polling per memrd -- AUF DEM N8 PRO WAEHREND LAUFENDEM SPIEL
        NICHT NUTZBAR: auch reines Lesen loest einen MCU-DMA aus, der dem
        6502 den Cart-Bus wegnimmt -> Freeze (Beleg: edn8-pro-pub
        edio/everdrive.c ed_cmd_mem_rd, addr < ADDR_CFG erzwingt
        ed_run_dma; fpga/base_sv/everdrive.sv gated prg_ce waehrend
        DMA-SRAM-Zugriff weg). Die fruehere Annahme "nur memwr ist
        gefaehrlich" war falsch -- das erklaert die 3/3 Freezes der
        SRAM-Strategie. Bleibt nur fuer Experimente ausserhalb laufender
        Spiele erhalten. Fuer Live-Werte den FIFO-Weg nutzen.

        Wartet bis sich der Zaehler seit dem letzten Aufruf veraendert
        hat (neuer Frame) oder timeout ablaeuft.
        Gibt (werte, zaehler, frisch) zurueck."""
        t0 = time.time()
        last_counter = self._last_counter
        while True:
            block = self.memrd(sram_addr, count + 1)
            values, counter = block[:count], block[count]
            if last_counter is None or counter != last_counter:
                self._last_counter = counter
                return values, counter, True
            if time.time() - t0 > timeout:
                self._last_counter = counter
                return values, counter, False
            time.sleep(poll_interval)


def find_everdrive(preferred=None):
    candidates = []
    if preferred:
        candidates.append(preferred)
    for p in (list_ports.comports() if list_ports else []):
        if p.device not in candidates:
            candidates.append(p.device)
    for port in candidates:
        ed = None
        try:
            ed = EdSerialNes(port)
            ed.open()
            return ed, port
        except Exception:
            if ed:
                try:
                    ed.close()
                except Exception:
                    pass
    return None, None


if __name__ == "__main__":
    import sys
    import argparse

    ap = argparse.ArgumentParser(description="Direkter Serial-Test fuer EverDrive N8 PRO")
    ap.add_argument("port", nargs="?", default="COM10")
    ap.add_argument("--addr", default="0x1000000", help="SRAM-Basisadresse (memrd)")
    ap.add_argument("--count", type=int, default=2, help="Anzahl Watch-Werte")
    ap.add_argument("--marker", default="0xAA", help="erwarteter Bereit-Marker")
    ap.add_argument("--seconds", type=float, default=5.0, help="wie lange live lesen")
    ap.add_argument("--launch", metavar="DEVPATH",
                    help="Weg B des inject-Ablaufs: Spiel auf der SD selektieren "
                         "(z.B. sd:usb-games/spiel_rawnes.nes), Stub aus --srm-file "
                         "ins SRAM schreiben, starten -- dann beenden")
    ap.add_argument("--srm-file", metavar="DATEI",
                    help="SRAM-Abbild mit Stub (die .srm aus 'rawnes_patch.py inject')")
    ap.add_argument("--launch-ref", metavar="DEVPATH",
                    help="Geraetepfad des UNVERAENDERTEN Original-ROMs: dessen "
                         "OS/DB-MapConfig wird gesichert und der gepatchten Kopie "
                         "transplantiert (Fix fuer kaputte Header, die das OS beim "
                         "Original per Checksummen-DB stillschweigend korrigiert)")
    ap.add_argument("--cfg-diff", nargs=2, metavar=("DEVPATH_A", "DEVPATH_B"),
                    help="Diagnose ohne Boot: MapConfigs zweier ROMs (z.B. Original "
                         "vs. gepatchte Kopie) dumpen, feldweise dekodieren und "
                         "diffen -- zeigt exakt, was das OS unterschiedlich "
                         "konfiguriert")
    ap.add_argument("--launch-inplace", metavar="DEVPATH",
                    help="Weg C: UNVERAENDERTES Original selektieren (DB "
                         "konfiguriert alles, PRG liegt im PSRAM), dann NUR die 2 "
                         "NMI-Vektor-Bytes im PSRAM patchen + Stub ins SRAM, dann "
                         "Start. Braucht --rom-file und --srm-file")
    ap.add_argument("--rom-file", metavar="DATEI",
                    help="lokale Original-ROM-Datei (fuer --launch-inplace: "
                         "PRG-Groesse + PSRAM-Verify)")
    ap.add_argument("--stub-addr", default="0x6000",
                    help="CPU-Adresse des Stubs im SRAM (fuer --launch-inplace, "
                         "Default 0x6000)")
    ap.add_argument("--postmortem", action="store_true",
                    help="NACH einem Schwarzbild/Absturz: PSRAM-Tail, SRAM-Stub "
                         "und MapConfig aus der abgestuerzten Konsole lesen und "
                         "gegen --orig-file/--patched-file/--srm-file bewerten. "
                         "NIE bei laufendem gesunden Spiel (memrd-Freeze); danach "
                         "Konsole aus/an")
    ap.add_argument("--orig-file", metavar="DATEI",
                    help="unveraendertes Original-ROM (fuer --postmortem-Vergleich)")
    ap.add_argument("--patched-file", metavar="DATEI",
                    help="gepatchte Kopie (fuer --postmortem-Vergleich)")
    ap.add_argument("--read-fifo", action="store_true",
                    help="Live per FIFO-Stream lesen statt SRAM/memrd (fuer den "
                         "'patch'/'inject'-Weg mit build_stub -- kein DMA-Risiko, "
                         "passives Lesen, funktioniert waehrend das Spiel laeuft)")
    ap.add_argument("--sync-byte", default="0xA5", help="erwartetes Sync-Byte im FIFO-Frame")
    ap.add_argument("--raw-dump", action="store_true",
                    help="Diagnose: Port KOMPLETT passiv oeffnen (kein Status-"
                         "Handshake, kein einziges gesendetes Byte) und die "
                         "ankommenden Rohbytes unverarbeitet hex-dumpen. Trennt "
                         "'es kommen gar keine Bytes an' (falsches Spiel laeuft/"
                         "COM-Port/Hardware) von 'Bytes kommen an, aber die "
                         "Frame-Erkennung greift nicht' (payload_len/Sync falsch)")
    ap.add_argument("--srm-select-test", metavar="DEVPATH",
                    help="Test: laedt das Menue eine .srm schon beim blossen ANWAEHLEN "
                         "(ohne 'Copy to RAM'-Klick) automatisch ins SRAM? Waehlt "
                         "DEVPATH (z.B. usb-games/spiel_rawnes.srm) im Menue an und "
                         "liest sofort danach das SRAM zurueck -- sicher, da das Menue "
                         "kooperiert. Braucht --srm-file zum Vergleich")
    args = ap.parse_args()

    if args.srm_select_test:
        if not args.srm_file:
            sys.exit("--srm-select-test braucht --srm-file zum Vergleich")
        with open(args.srm_file, "rb") as f:
            expected = f.read()
        stub_len = max(1, len(expected[:128].rstrip(b"\x00")))
        print(f"Verbinde mit {args.port} @ 921600...")
        ed = EdSerialNes(args.port)
        ed.open()
        print(f"Waehle '{args.srm_select_test}' im Menue an (OHNE Copy to RAM)...")
        ed.menu_test()
        ed.menu_select_game(args.srm_select_test)
        sram = bytes(ed.memrd(ADDR_SRM, stub_len))
        ed.close()
        print(f"SRAM danach: {sram.hex(' ')}")
        print(f"Erwartet   : {expected[:stub_len].hex(' ')}")
        if sram == expected[:stub_len]:
            print("=> TREFFER: das Menue laedt die .srm schon beim Anwaehlen "
                  "automatisch -- 'Copy to RAM' per Hand ist NICHT noetig! "
                  "menu_select_game() reicht, das koennen wir automatisieren.")
        elif set(sram) == {0} or set(sram) == {0xFF}:
            print("=> Leer/geloescht: das Menue laedt NICHT automatisch beim "
                  "Anwaehlen. 'Copy to RAM' per Hand bleibt noetig (Befund "
                  "bestaetigt, kein neuer Ansatz).")
        else:
            print("=> Unerwarteter Inhalt (weder erwarteter Stub noch leer) -- "
                  "bitte Ausgabe melden, das ist ein neuer Befund.")
        sys.exit(0)

    if args.postmortem:
        if not (args.orig_file and args.srm_file):
            sys.exit("--postmortem braucht --orig-file und --srm-file "
                     "(--patched-file optional, aber empfohlen)")
        with open(args.orig_file, "rb") as f:
            orig = f.read()
        if orig[:4] != b"NES\x1a":
            sys.exit("--orig-file: kein iNES-Header")
        prg_size, prg_start = orig[4] * 16384, 16
        orig_tail = orig[prg_start + prg_size - 16:prg_start + prg_size]
        patched_tail = None
        if args.patched_file:
            with open(args.patched_file, "rb") as f:
                p = f.read()
            patched_tail = p[prg_start + prg_size - 16:prg_start + prg_size]
        with open(args.srm_file, "rb") as f:
            srm = f.read()
        stub_addr = int(args.stub_addr, 0)
        sram_off = stub_addr - 0x6000
        # Stub-Laenge: Nutzbytes in der .srm ab Offset (bis letztes Nicht-Null-Byte)
        region = srm[sram_off:sram_off + 128]
        stub_len = max(1, len(region.rstrip(b"\x00")))
        expected_stub = region[:stub_len]

        print(f"Verbinde mit {args.port} @ 921600... (Konsole gilt als "
              f"abgestuerzt -- danach Power-Cycle)")
        ed = EdSerialNes(args.port)
        ed.open()
        psram_tail, sram_stub, cfg, counter = ed.postmortem(prg_size, stub_len, sram_off)
        ed.close()

        def vec(b, o): return b[o] | (b[o + 1] << 8)
        print(f"PSRAM-Tail : {psram_tail.hex(' ')}")
        print(f"  Vektoren im PSRAM: NMI=${vec(psram_tail,10):04X} "
              f"RESET=${vec(psram_tail,12):04X} IRQ=${vec(psram_tail,14):04X}")
        if psram_tail == orig_tail:
            print("  = ORIGINAL-Datei -- run hat das Original(-Abbild) geladen; "
                  "der Vektor-Patch war beim Boot NICHT aktiv.")
        elif patched_tail is not None and psram_tail == patched_tail:
            print("  = GEPATCHTE Kopie -- run hat die Kopie geladen; der "
                  "NMI-Vektor zeigte beim Boot auf den Stub.")
        else:
            print("  = WEDER Original noch Kopie -- unerwarteter PSRAM-Inhalt.")
        print(f"SRAM-Stub  : {sram_stub.hex(' ')}")
        if sram_stub == expected_stub:
            print("  = Stub INTAKT -- run hat das SRAM nicht angetastet.")
        elif set(sram_stub) == {0}:
            print("  = GELOESCHT (0x00) -- run initialisiert SRAM NACH unserem "
                  "memwr; Stub war beim Boot weg. -> Weg A (.srm in den "
                  "Save-Ordner) ist der Fix.")
        elif set(sram_stub) == {0xFF}:
            print("  = GELOESCHT (0xFF) -- run initialisiert SRAM NACH unserem "
                  "memwr; Stub war beim Boot weg. -> Weg A (.srm in den "
                  "Save-Ordner) ist der Fix.")
        else:
            print("  = VERAENDERT (weder intakt noch geloescht) -- Inhalt oben "
                  "mit erwartetem Stub vergleichen:")
            print(f"    erwartet: {expected_stub.hex(' ')}")
        print(f"SRAM[$61F0]: {counter:#04x}  (srmprobe-Lebenszeichen: >0 = "
              f"Stub LIEF; bei anderen ROMs bedeutungslos)")
        print(f"MapConfig  : {cfg.hex(' ')}")
        for k, v in EdSerialNes.decode_mapconfig(cfg).items():
            print(f"  {k} = {v}")
        if cfg[32] == 255:
            print("  = SERVICE/MENUE-Config (map_idx 255 + map_ctrl-UNLOCK -- "
                  "exakt was ed_exit_game()/das Menue schreibt, everdrive.c). "
                  "Die SPIEL-Config wird erst beim Start nach ADDR_CFG "
                  "geschrieben (ed_start_app) -- sie fehlt hier, also HING DER "
                  "STARTVORGANG, bevor er die Config uebergab: das Spiel hat "
                  "nie gebootet, der Stub war nie dran. Verdaechtige: "
                  "Unterschiede der Kopie, die den MENUE-Startpfad betreffen "
                  "(v.a. Battery-Flag -> Save-Handling).")
        print()
        print("Kurzlogik: MapConfig=Service (255) -> Start hing, Stub "
              "unschuldig. MapConfig=Spiel (map_idx passend) + Kopie geladen "
              "+ Stub intakt und trotzdem schwarz -> Fehler in der "
              "Stub-AUSFUEHRUNG. Original geladen -> run laedt von SD, Datei "
              "pruefen. Stub geloescht -> SRAM-Init bei run, Weg A.")
        sys.exit(0)

    if args.cfg_diff:
        print(f"Verbinde mit {args.port} @ 921600...")
        ed = EdSerialNes(args.port)
        ed.open()
        a, b = args.cfg_diff
        print(f"Selektiere nacheinander '{a}' und '{b}', dumpe MapConfigs...")
        cfg_a, cfg_b, diff = ed.cfg_diff(a, b)
        print(f"A roh: {bytes(cfg_a).hex(' ')}")
        print(f"B roh: {bytes(cfg_b).hex(' ')}")
        if not diff:
            print("MapConfigs sind IDENTISCH -- das OS konfiguriert beide "
                  "gleich; Config-Unterschiede scheiden als Ursache aus.")
        else:
            print("ABWEICHENDE Felder (A -> B):")
            for k, (va, vb) in diff.items():
                print(f"  {k}: {va} -> {vb}")
        ed.close()
        sys.exit(0)

    if args.launch_inplace:
        if not (args.srm_file and args.rom_file):
            sys.exit("--launch-inplace braucht --srm-file UND --rom-file")
        with open(args.srm_file, "rb") as f:
            srm_bytes = f.read()
        with open(args.rom_file, "rb") as f:
            rom_data = f.read()
        if rom_data[:4] == b"NES\x1a":
            prg_size, prg_start = rom_data[4] * 16384, 16
        else:
            sys.exit("--rom-file: kein iNES-Header -- PRG-Groesse nicht "
                     "bestimmbar (headerlose Dumps hier nicht unterstuetzt)")
        stub_addr = int(args.stub_addr, 0)
        vec = bytes([stub_addr & 0xFF, stub_addr >> 8])
        print(f"Verbinde mit {args.port} @ 921600...")
        ed = EdSerialNes(args.port)
        ed._verify_tail = rom_data[prg_start + prg_size - 16:prg_start + prg_size]
        ed.open()
        print(f"Weg C: selektiere Original '{args.launch_inplace}', verifiziere "
              f"PSRAM, patche NMI-Vektor -> ${stub_addr:04X}, Stub ins SRAM, Start...")
        map_idx, old_vec = ed.launch_patch_in_place(
            args.launch_inplace, prg_size, vec, srm_bytes)
        print(f"Gestartet (Mapper-Index {map_idx}). PSRAM-Verify OK -- PRG lag "
              f"nach select im PSRAM. NMI-Vektor im PSRAM: "
              f"{bytes(old_vec).hex(' ')} -> {vec.hex(' ')}. ROM-Datei, Header "
              f"und CRC blieben unberuehrt; DB-Konfiguration des Originals aktiv.")
        print("Ab jetzt NUR noch FIFO-Stream lesen -- kein memrd/memwr mehr "
              "auf PRG/CHR/SRAM (Freeze-Gefahr).")
        ed.close()
        sys.exit(0)

    if args.launch:
        # --srm-file ist seit Handoff 5 OPTIONAL und sollte weggelassen
        # werden: der memwr des Stubs ins SRAM vor dem run zerschiesst
        # nachweislich den Menue-Startpfad (das Menue-OS nutzt das
        # Cart-SRAM als Arbeitsspeicher) -- Stub-Auslieferung laeuft
        # jetzt ueber die .srm-Datei im Save-Ordner (Weg A).
        srm_bytes = None
        if args.srm_file:
            print("WARNUNG: --srm-file bei --launch = memwr ins SRAM vor dem "
                  "Start -- haengt nachweislich das Menue (Handoff 5). Nur "
                  "fuer Experimente.")
            with open(args.srm_file, "rb") as f:
                srm_bytes = f.read()
        print(f"Verbinde mit {args.port} @ 921600...")
        ed = EdSerialNes(args.port)
        ed.open()
        print(f"Verbunden. Selektiere '{args.launch}' im Menue...")
        (map_idx, prg_ram_before, prg_ram_after,
         prg_msk_before, prg_msk_after, ref_map_idx) = \
            ed.launch_with_sram_stub(args.launch, srm_bytes,
                                     ref_dev_path=args.launch_ref)
        print(f"Spiel gestartet (Mapper-Index {map_idx})."
              + (f" Stub ({len(srm_bytes)} Byte SRAM-Abbild) wurde VOR dem "
                 f"Start per memwr geschrieben." if srm_bytes is not None
                 else " Kein memwr -- Stub-Auslieferung via .srm im "
                      "Save-Ordner (Weg A)."))
        if ref_map_idx is not None:
            print(f"Mapper-Index Original: {ref_map_idx}  vs. Kopie: {map_idx}"
                  + ("  -> ABWEICHUNG: DB-Fallback-Falle fuer dieses ROM "
                     "BEWIESEN (Header liefert anderen Mapper als die "
                     "OS-Datenbank). MapConfig des Originals wurde "
                     "transplantiert."
                     if ref_map_idx != map_idx else
                     "  -> identisch (Header war hier nicht das Problem); "
                     "MapConfig des Originals trotzdem transplantiert."))
        if prg_ram_before is not None:
            print(f"PRG-RAM (map_cfg Bit 3) -- vorher AUS: {prg_ram_before}, "
                  f"nachher AUS: {prg_ram_after}"
                  + (" (erzwungen AN)" if prg_ram_before and not prg_ram_after else ""))
        if prg_msk_before is not None:
            size_b = 128 << (prg_msk_before >> 4) if (prg_msk_before >> 4) else 128
            size_a = 128 << (prg_msk_after >> 4)
            print(f"SRAM-Fenster (prg_msk High-Nibble) -- vorher: "
                  f"{prg_msk_before >> 4} (CPU sieht {size_b} Byte gespiegelt), "
                  f"nachher: {prg_msk_after >> 4} ({size_a} Byte)"
                  + (" (erzwungen)" if prg_msk_before != prg_msk_after else " (war schon ok)"))
            if (prg_msk_before >> 4) == 0:
                print("  -> Groesse war 0: fatal nur fuer Stubs ausserhalb der "
                      "ersten 128 Chip-Bytes (so scheiterte der $7E00-Test); "
                      "der aktuelle $6000-Stub waere auch ohne Erzwingen "
                      "erreichbar gewesen.")
        print("Ab jetzt NUR noch FIFO-Stream lesen -- kein memrd/memwr mehr "
              "auf PRG/CHR/SRAM (Freeze-Gefahr).")
        ed.close()
        sys.exit(0)

    if args.raw_dump:
        sync_byte = int(args.sync_byte, 0)
        print(f"Roh-Dump: oeffne {args.port} @ 921600 -- rein passiv, es wird "
              f"NICHTS gesendet (auch kein Status-Handshake).")
        ser = serial.Serial(args.port, baudrate=921600, timeout=0.1)
        try:
            t0 = time.time()
            buf = bytearray()
            while time.time() - t0 < args.seconds:
                chunk = ser.read(4096)
                if chunk:
                    buf += chunk
        finally:
            ser.close()
        print(f"\n{len(buf)} Byte(s) in {args.seconds:.1f}s empfangen.")
        if not buf:
            print("=> KEINE Bytes. Der Stub sendet nicht: laeuft am Geraet "
                  "wirklich die frisch gepatchte Datei? Bei '1 ACHTUNG'-"
                  "Dateien (SRAMEXEC): wurde 'Copy to RAM' gemacht? Stimmt "
                  "der COM-Port?")
            sys.exit(0)
        show = min(len(buf), 512)
        print(f"Erste {show} Byte(s):")
        for off in range(0, show, 16):
            print(f"  {off:04x}: {buf[off:off+16].hex(' ')}")
        # Sync-Analyse: Abstaende zwischen Sync-Bytes verraten die echte
        # Frame-Laenge (stride = 1 + payload_len), ganz ohne Vorwissen.
        pos = [i for i, b in enumerate(buf) if b == sync_byte]
        print(f"\nSync-Byte ${sync_byte:02X}: {len(pos)}x im Puffer.")
        if len(pos) >= 3:
            from collections import Counter
            gaps = Counter(b - a for a, b in zip(pos, pos[1:]))
            top = gaps.most_common(3)
            print("Haeufigste Abstaende (stride = 1 + Payload-Laenge):")
            for stride, n in top:
                print(f"  Abstand {stride:3d} ({n}x) -> Payload {stride-1} Byte "
                      f"-> im Monitor {stride-1} Watch-Adresse(n) eintragen")
            print("=> Bytes kommen an. Falls der Monitor trotzdem TIMEOUT "
                  "zeigt, ist die Watch-Anzahl im Feld falsch -- auf den "
                  "obigen Payload-Wert setzen.")
        else:
            print("=> Bytes kommen an, aber (fast) ohne Sync-Byte -- der "
                  "laufende Stub sendet Frames OHNE Sync (alter Patch-Stand?) "
                  "oder es ist gar kein RAW-NES-Stream. Datei-Zeitstempel auf "
                  "der SD gegen den Patch-Zeitpunkt pruefen.")
        sys.exit(0)

    if args.read_fifo:
        sync_byte = int(args.sync_byte, 0)
        print(f"Verbinde mit {args.port} @ 921600...")
        ed = EdSerialNes(args.port)
        ed.open()
        print(f"Verbunden. Lese FIFO-Stream ({args.count} Watch-Byte, Sync ${sync_byte:02X}) "
              f"fuer {args.seconds:.1f}s -- rein passiv, kein memrd/DMA:\n")
        t0 = time.time()
        n = 0
        while time.time() - t0 < args.seconds:
            payload = ed.read_fifo_frame(args.count, sync_byte=sync_byte, timeout=1.0)
            n += 1
            if payload is None:
                print(f"  t={time.time()-t0:6.2f}s  TIMEOUT (kein Frame empfangen)")
            else:
                print(f"  t={time.time()-t0:6.2f}s  Werte={payload.hex()}")
        print(f"\n{n} Leseversuche in {args.seconds:.1f}s.")
        ed.close()
        sys.exit(0)

    addr = int(args.addr, 0)
    marker = int(args.marker, 0)

    print(f"Verbinde mit {args.port} @ 921600...")
    ed = EdSerialNes(args.port)
    ed.open()
    print("Verbunden.")
    print(f"\nStatus-Rohantwort war ok. Lese jetzt {args.count} Watch-Byte(s) "
          f"+ Marker ab {addr:#x} fuer {args.seconds:.1f}s live:\n")

    t0 = time.time()
    n = 0
    while time.time() - t0 < args.seconds:
        values, counter, frisch = ed.read_watch(addr, args.count, timeout=0.3)
        n += 1
        flag = "neuer Frame" if frisch else "TIMEOUT (Zaehler unveraendert -- Stub schreibt nicht?)"
        print(f"  t={time.time()-t0:5.2f}s  Werte={values.hex()}  Zaehler={counter:3d}  [{flag}]")
        time.sleep(0.05)

    print(f"\n{n} Leseversuche in {args.seconds:.1f}s. Verbindung wird geschlossen.")
    ed.close()
