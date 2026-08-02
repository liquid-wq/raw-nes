#pragma once
#include <cstdint>
#include <optional>
#include <vector>

// Der eigentliche serielle I/O (open/close/read/write, Timing, Recovery)
// ist in ed_serial_qt.h (QSerialPort) -- hier nur die Byte-Frames, die auf
// die Leitung gehen. Getrennt gehalten wie bei MEGA-RAW, damit dieser Teil
// ohne Qt kompilier- und testbar bleibt.
//
// NES/EDN8-PRO-spezifisch: LITTLE-ENDIAN (im Unterschied zu MEGA-RAW/
// Genesis, das big-endian ist) -- bestaetigt aus github.com/krikzz/edlink
// Quellcode (DEV_EDN8/DeviceIO.cs: link.SwapEndians = false).

constexpr uint8_t CMD_STATUS = 0x10;
constexpr uint8_t CMD_MEM_RD = 0x19;
constexpr uint8_t CMD_MEM_WR = 0x1A;

std::vector<uint8_t> ed_cmd(uint8_t code, std::optional<uint8_t> sub = std::nullopt);

// Paket fuer memrd: CMD_MEM_RD-Header + addr(4 little-endian) + length(4 LE) + 0x00
std::vector<uint8_t> ed_build_memrd_packet(uint32_t addr, uint32_t length);

// Paket fuer memwr: CMD_MEM_WR-Header + addr(4 LE) + len(4 LE) + 0x00 + data
std::vector<uint8_t> ed_build_memwr_packet(uint32_t addr, const std::vector<uint8_t>& data);
