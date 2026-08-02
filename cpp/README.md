# RAW-NES (C++ / Qt6)

RetroAchievements on a real NES, over USB from an EverDrive N8 PRO.
No emulator involved.

![RAW-NES in action](../docs/rawnes_uebersicht.gif)

This is the C++ port of RAW-NES and the version development continues in.
Same approach and same FPGA cores as the Python version, native executable,
no Python install needed.

For an overview of both versions see the [main README](../README.md).

---

## What it does

A custom FPGA core on the EverDrive snoops the NES CPU bus and mirrors the
console's RAM into block RAM. RAW-NES reads those values live over USB,
evaluates the RetroAchievements condition sets on the PC, and unlocks
achievements against the RetroAchievements servers while you play on
original hardware.

* Live achievement tracking, including hit-count conditions
* Leaderboards
* Hardcore mode, with a check that savestates and cheats are switched off
* Automatic game detection from the ROM's vector fingerprint
* German and English, switchable without restarting

---

## Requirements

* NES or compatible console
* EverDrive N8 PRO with a USB cable to the PC
* Windows 64-bit
* A RetroAchievements account
* Your NES ROM collection on the PC (`.nes`, `.zip`, `.7z`)

---

## Setup

**1. Unpack** the release archive into a folder of your choice. Keep the
`cores` folder next to `rawnes_gui.exe`.

**2. Install the FPGA cores.** Put the EverDrive SD card in the PC, start
RAW-NES, then *Options -> Set up FPGA mappers*. This replaces the mapper
cores in `EDN8\MAPS\` with RAW-NES versions and backs up the originals to
`EDN8\MAPS\ORIG_BACKUP\`. *Options -> Restore original mappers* undoes it at
any time.

**3. Log in** with your RetroAchievements username and password (*Log in*).

**4. Index your ROM collection.** *Index ROM collection...*, then pick the
folder. This reads the interrupt vectors of every ROM once so the running
game can be identified without a file dialog. Only needed again when the
collection changes.

**5. Start a game** on the console, then *Load game & start monitor*.

---

## Hardcore mode

Leaderboards and hardcore unlocks only count when savestates and cheats are
switched off on the EverDrive itself. Switch them off in the EverDrive system
menu: *Filebrowser -> \[SELECT] -> Options -> "In Game Menu" off, "Cheats"
off*. The in-game menu is not the same thing.

RAW-NES reads the EverDrive control byte and falls back to softcore if either
is active — at session start, before polling, and after every console reset.
It cannot switch them off for you.

---

## Troubleshooting

**Not found — console on? game started?**
The EverDrive only answers over USB while a game is running. Start the game
first, then connect.

**FPGA build mismatch**
The mapper cores on the SD card are not the RAW-NES ones. Run
*Options -> Set up FPGA mappers* again.

**All values read as FF**
Same cause: the original Krikzz cores are on the card.

**Unsupported game version**
The running ROM has no achievement set on RetroAchievements, or a different
regional version was picked. Stop, start again and choose the right one when
asked.

---

## Reporting bugs

GitHub Issues only: https://github.com/liquid-wq/raw-nes/issues

Please include the log from the program window and your build number, shown
under the title.

---

## License

Source-available, not open source. See `LICENSE`. Third-party components and
their licenses are listed in `THIRD-PARTY-NOTICES.txt`.

Built by Liqui — https://github.com/liquid-wq
