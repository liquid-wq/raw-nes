// Selbsttest der Kernschicht. Konsolenprogramm, kein Fenster, kein Geraet,
// kein Netz. Prueft, was ohne EverDrive und ohne RA-Zugang pruefbar ist --
// darunter ausdruecklich Pfade mit Umlauten, die in der Qt-Fassung an
// ifstream/fopen scheiterten.
//
// Aufruf: selbsttest.exe   (Rueckgabe 0 = alles gruen)

#include "ConditionEngine.h"
#include "Md5.h"
#include "RomIndex.h"
#include "ed_protocol.h"
#include "fpga_installer.h"
#include "json.h"
#include "ra_cache.h"
#include "ra_client.h"

#include "miniz.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_fehler = 0;
int g_ok = 0;

void Pruefe(bool bedingung, const char* was) {
    if (bedingung) {
        ++g_ok;
        std::printf("  ok    %s\n", was);
    } else {
        ++g_fehler;
        std::printf("  FEHLER %s\n", was);
    }
}

std::vector<uint8_t> InesRom(uint8_t mapper, uint8_t prg16k, uint8_t chr8k, uint8_t reset0) {
    std::vector<uint8_t> d(16 + (size_t)prg16k * 16384 + (size_t)chr8k * 8192, 0);
    d[0] = 'N'; d[1] = 'E'; d[2] = 'S'; d[3] = 0x1A;
    d[4] = prg16k;
    d[5] = chr8k;
    d[6] = (uint8_t)((mapper & 0x0F) << 4);
    d[7] = (uint8_t)(mapper & 0xF0);
    const size_t prgEnde = 16 + (size_t)prg16k * 16384;
    // Vektoren NMI/RESET/IRQ am Ende des PRG
    d[prgEnde - 6] = 0x10; d[prgEnde - 5] = 0xC0;
    d[prgEnde - 4] = reset0; d[prgEnde - 3] = 0xC0;
    d[prgEnde - 2] = 0x20; d[prgEnde - 1] = 0xC0;
    for (size_t i = 16; i < prgEnde - 6; ++i) d[i] = (uint8_t)(i * 7);
    return d;
}

void Schreibe(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), (std::streamsize)d.size());
}

}  // namespace

int main() {
    std::printf("RAW-NES Selbsttest der Kernschicht\n\n");

    std::printf("MD5 (RFC 1321)\n");
    Pruefe(rawnes::md5Hex(std::string("")) == "d41d8cd98f00b204e9800998ecf8427e", "md5(\"\")");
    Pruefe(rawnes::md5Hex(std::string("abc")) == "900150983cd24fb0d6963f7d28e17f72", "md5(\"abc\")");
    Pruefe(rawnes::md5Hex(std::string("The quick brown fox jumps over the lazy dog")) ==
               "9e107d9d372bb6826bd81d3542a419d6", "md5(quick brown fox)");

    std::printf("\nEverDrive-Protokoll (little-endian)\n");
    {
        const auto p = ed_build_memrd_packet(0x1808810, 1);
        const std::vector<uint8_t> soll = {0x2B, 0xD4, 0x19, 0xE6, 0x10, 0x88, 0x80, 0x01,
                                           0x01, 0x00, 0x00, 0x00, 0x00};
        Pruefe(p == soll, "memrd-Paket 0x1808810, 1 Byte");
        const auto s = ed_cmd(CMD_STATUS);
        Pruefe(s.size() == 4 && s[2] == 0x10 && s[3] == 0xEF, "Status-Kommando");
    }

    std::printf("\nBedingungsauswertung\n");
    {
        rawnes::AchievementRuntime rt("0xH0010=5");
        rawnes::RamMap prev{{0x10, 0}}, ram{{0x10, 0}};
        Pruefe(!rt.unsupported(), "0xH0010=5 wird unterstützt");
        Pruefe(!rt.update(ram, &prev), "feuert nicht bei 0");
        ram[0x10] = 5;
        Pruefe(rt.update(ram, &prev), "feuert bei 5");
    }
    {
        rawnes::AchievementRuntime rt("0xH0010=5.3.");
        rawnes::RamMap ram{{0x10, 5}};
        bool f1 = rt.update(ram, &ram), f2 = rt.update(ram, &ram), f3 = rt.update(ram, &ram);
        Pruefe(!f1 && !f2 && f3, "Hit-Count 3 feuert im dritten Bild");
        auto pr = rt.progress();
        Pruefe(pr.hasProgress && pr.target == 3, "Fortschritt meldet Ziel 3");
    }
    {
        rawnes::AchievementRuntime rt("0xH0010>d0xH0010");
        rawnes::RamMap a{{0x10, 3}}, b{{0x10, 4}};
        rt.update(a, &a);
        Pruefe(rt.update(b, &a), "Delta: Wert gestiegen");
    }
    {
        auto adr = rawnes::collectAddresses({{"1", "0xH0010=5_0xH6000=1"}});
        Pruefe(adr.size() == 2, "collectAddresses findet RAM und WRAM");
    }

    std::printf("\nJSON\n");
    {
        Json j = Json::parse("{\"Title\":\"Pok\\u00e9mon\",\"ID\":42,\"x\":[1,2.5,null,true]}");
        Pruefe(j.find("Title") && j.find("Title")->as_string() == "Pok\xC3\xA9mon", "\\u-Escape nach UTF-8");
        Pruefe(j.find("ID") && j.find("ID")->type() == Json::Type::Int && j.find("ID")->as_int() == 42, "Ganzzahl");
        Json r = Json::parse(j.dump());
        Pruefe(r.find("Title") && r.find("Title")->as_string() == "Pok\xC3\xA9mon", "dump/parse-Rundlauf");
    }

    // Pfad mit Umlauten -- daran scheiterte die Qt-Fassung.
    std::error_code ec;
    const fs::path basis = fs::temp_directory_path() / fs::u8path("rawnes_selbsttest_\xC3\xBC\xC3\xA9");
    fs::remove_all(basis, ec);
    fs::create_directories(basis / "roms", ec);

    std::printf("\nROM-Index (Pfad mit Umlauten)\n");
    {
        const auto romA = InesRom(4, 2, 1, 0x00);
        const auto romB = InesRom(1, 1, 0, 0x40);
        Schreibe(basis / "roms" / fs::u8path("Spiel \xC3\x84 (Europe).nes"), romA);

        // ZIP mit der zweiten ROM, ueber den Heap geschrieben (unabhaengig von fopen).
        mz_zip_archive za;
        std::memset(&za, 0, sizeof(za));
        void* puffer = nullptr;
        size_t groesse = 0;
        bool zipOk = mz_zip_writer_init_heap(&za, 0, 0) &&
                     mz_zip_writer_add_mem(&za, "unter/Pok\xC3\xA9mon.nes", romB.data(), romB.size(), MZ_DEFAULT_COMPRESSION) &&
                     mz_zip_writer_finalize_heap_archive(&za, &puffer, &groesse);
        if (zipOk) {
            Schreibe(basis / "roms" / fs::u8path("Archiv \xC3\xB6.zip"),
                     std::vector<uint8_t>((uint8_t*)puffer, (uint8_t*)puffer + groesse));
        }
        mz_zip_writer_end(&za);
        if (puffer) mz_free(puffer);
        Pruefe(zipOk, "Test-ZIP angelegt");

        int skipped = -1;
        const std::string ordner = (basis / "roms").u8string();
        auto idx = rawnes::buildIndex(ordner, &skipped);
        int n = 0;
        for (auto& kv : idx) n += (int)kv.second.size();
        Pruefe(n == 2, ".nes und .nes-in-.zip indexiert");
        Pruefe(skipped == 0, "nichts übersprungen");

        const auto vecA = rawnes::vectorsFromInes(romA);
        auto treffer = vecA ? rawnes::lookup(idx, *vecA) : std::vector<rawnes::RomIndexEntry>{};
        Pruefe(treffer.size() == 1 && treffer[0].mapper == 4 && treffer[0].prg == 2, "Fingerabdruck findet ROM A mit Mapper 4");

        const std::string indexDatei = (basis / "index.json").u8string();
        rawnes::saveIndex(idx, indexDatei);
        auto geladen = rawnes::loadIndex(indexDatei);
        int m = 0;
        for (auto& kv : geladen) m += (int)kv.second.size();
        Pruefe(m == 2, "Index speichern und laden");

        if (!treffer.empty()) {
            const std::string md5 = RaClient::nes_hash(treffer[0].source);
            Pruefe(md5 == rawnes::inesMd5(romA), "nes_hash liest Datei mit Umlaut im Pfad");
        }
        std::string err;
        auto aus = rawnes::extractSingleNesFromArchive((basis / "roms" / fs::u8path("Archiv \xC3\xB6.zip")).u8string(), &err);
        Pruefe(aus == romB, "einzelne ROM aus ZIP mit Umlaut im Namen entpackt");
    }

    std::printf("\nRA-Cache (Pfad mit Umlauten)\n");
    {
        const std::string pfad = (basis / "ra_cache_nes.json").u8string();
        {
            RaCache c(pfad);
            c.gameid_set("abc", Json::integer(7));
            c.save(true);
        }
        RaCache c2(pfad);
        const Json* g = c2.gameid_get("abc");
        Pruefe(g && g->as_int() == 7, "Cache schreiben und wieder lesen");
    }

    std::printf("\nFPGA-Mapper-Installer (Pfad mit Umlauten)\n");
    {
        const fs::path kerne = basis / "cores";
        const fs::path sd = basis / fs::u8path("SD-K\xC3\xA4rte");
        fs::create_directories(kerne, ec);
        fs::create_directories(sd / "EDN8" / "MAPS", ec);
        for (const auto& f : rawnes::FpgaInstaller::coreFiles()) {
            Schreibe(kerne / f, std::vector<uint8_t>(64, (uint8_t)f[1]));
            Schreibe(sd / "EDN8" / "MAPS" / f, std::vector<uint8_t>(32, 0xEE));  // "Original"
        }
        rawnes::FpgaInstaller inst(kerne.u8string());
        Pruefe(inst.coresAvailable(), "Kerne vorhanden");
        const std::string maps = rawnes::FpgaInstaller::mapsDirFromSd(sd.u8string());
        Pruefe(!maps.empty(), "EDN8/MAPS gefunden");
        auto noetig = inst.coresNeedInstall(maps);
        Pruefe(noetig.size() == 5, "alle 5 brauchen Installation");
        auto r = inst.installCores(maps, noetig);
        Pruefe(r.errors.empty() && r.installed == 5 && r.backedUp == 5, "installiert und Originale gesichert");
        Pruefe(inst.coresNeedInstall(maps).empty(), "danach aktuell");
        auto rr = inst.restoreOriginals(maps, inst.backedUpFilesAvailable(maps));
        Pruefe(rr.errors.empty() && rr.restored == 5, "Originale wiederhergestellt");
        Pruefe(inst.coresNeedInstall(maps).size() == 5, "danach wieder Originale auf der Karte");
    }

    // Praxisfall mit den echten Kernen: Auf der Karte liegen bereits
    // RAW-NES-Kerne, nur 004.RBF ist ein aelterer Stand; in ORIG_BACKUP liegen
    // die Krikzz-Originale. Erwartet: nur 004 wird ersetzt, das Backup bleibt
    // unberuehrt (das erste Backup ist das echte Original).
    std::printf("\nFPGA-Mapper, Praxisfall mit den echten Kernen\n");
    {
        const fs::path kerne = fs::u8path(RAWNES_ASSETS) / "cores";
        rawnes::FpgaInstaller inst(kerne.u8string());
        Pruefe(inst.coresAvailable(), "echte Kerne aus assets/cores vorhanden");

        const fs::path maps = basis / "praxis" / "EDN8" / "MAPS";
        fs::create_directories(maps / "ORIG_BACKUP", ec);
        for (const auto& f : rawnes::FpgaInstaller::coreFiles()) {
            fs::copy_file(kerne / f, maps / f, fs::copy_options::overwrite_existing, ec);
            Schreibe(maps / "ORIG_BACKUP" / f, std::vector<uint8_t>(100, 0x4B));  // "Krikzz"
        }
        Schreibe(maps / "004.RBF", std::vector<uint8_t>(157649, 0x11));  // alter Stand

        const std::string m = rawnes::FpgaInstaller::mapsDirFromSd((basis / "praxis").u8string());
        auto noetig = inst.coresNeedInstall(m);
        Pruefe(noetig.size() == 1 && noetig[0] == "004.RBF", "nur 004.RBF muss ersetzt werden");
        Pruefe(inst.filesToBackup(m, noetig).empty(), "kein neues Backup (Original schon gesichert)");
        auto r = inst.installCores(m, noetig);
        Pruefe(r.errors.empty() && r.installed == 1 && r.backedUp == 0, "004.RBF ersetzt, Backup nicht überschrieben");
        std::ifstream b(maps / "ORIG_BACKUP" / "004.RBF", std::ios::binary);
        std::vector<char> inhalt((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
        Pruefe(inhalt.size() == 100 && inhalt[0] == 0x4B, "ORIG_BACKUP/004.RBF ist weiterhin das Original");
        Pruefe(inst.coresNeedInstall(m).empty(), "Karte danach aktuell");
        Pruefe(fs::file_size(maps / "004.RBF", ec) == fs::file_size(fs::path(RAWNES_ASSETS) / "cores" / "004.RBF", ec), "004.RBF auf der Karte ist der eingebaute Stand");
    }

    fs::remove_all(basis, ec);

    std::printf("\n%d ok, %d Fehler\n", g_ok, g_fehler);
    return g_fehler == 0 ? 0 : 1;
}
