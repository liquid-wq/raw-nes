# RAW-NES

RetroAchievements on a real NES, over USB from an EverDrive N8 PRO.
No emulator involved.

![RAW-NES in action](docs/rawnes_uebersicht.gif)

A custom FPGA core on the EverDrive snoops the NES CPU bus and mirrors the
console's RAM into block RAM. RAW-NES reads those values live over USB,
evaluates the RetroAchievements condition sets on the PC, and unlocks
achievements while you play on original hardware.

---

## Two versions

| | [**C++ port**](cpp/) | [Python version](python/) |
|---|---|---|
| Install | none, just unpack | needs Python |
| Achievement list with badges | yes | log only |
| Unlock popup | yes | no |
| Leaderboard sidebar | yes | log only |
| Live RAM view | yes | yes |
| Hardcore mode | yes | yes |
| Language switch without restart | yes | no |

**Use the C++ port.** It is the version development continues in. The Python
version still works and stays available, but new features go into the port.

Downloads for both: **[Releases](https://github.com/liquid-wq/raw-nes/releases)**

---

## Requirements

* NES or compatible console
* EverDrive N8 PRO with a USB cable to the PC
* Windows 64-bit
* A RetroAchievements account
* Your NES ROM collection on the PC (`.nes`, `.zip`, `.7z`)

---

## Getting started

Download the release, unpack it, and follow the setup in the README of the
version you picked: [`cpp/README.md`](cpp/README.md) or
[`python/README.md`](python/README.md).

In short: install the FPGA cores onto the EverDrive SD card from inside the
program, log in to RetroAchievements, index your ROM folder once, start a
game on the console, then start the monitor.

---

## Reporting bugs

GitHub Issues only, with the game name, which version you are running, and
the log from the program window.

---

## License

Source-available, not open source. See [`LICENSE`](LICENSE). Third-party
components and their licenses are listed in
[`THIRD-PARTY-NOTICES.txt`](THIRD-PARTY-NOTICES.txt).

Built by Liqui.
