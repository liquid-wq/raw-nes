#include "ed_protocol.h"

std::vector<uint8_t> ed_cmd(uint8_t code, std::optional<uint8_t> sub) {
    std::vector<uint8_t> pkt = {0x2B, 0xD4, code, static_cast<uint8_t>(code ^ 0xFF)};
    if (sub.has_value()) pkt.push_back(*sub);
    return pkt;
}

namespace {
void append_u32_le(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 24) & 0xFF);
}
}

std::vector<uint8_t> ed_build_memrd_packet(uint32_t addr, uint32_t length) {
    std::vector<uint8_t> pkt = ed_cmd(CMD_MEM_RD);
    append_u32_le(pkt, addr);
    append_u32_le(pkt, length);
    pkt.push_back(0x00);
    return pkt;
}

std::vector<uint8_t> ed_build_memwr_packet(uint32_t addr, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> pkt = ed_cmd(CMD_MEM_WR);
    append_u32_le(pkt, addr);
    append_u32_le(pkt, static_cast<uint32_t>(data.size()));
    pkt.push_back(0x00);
    pkt.insert(pkt.end(), data.begin(), data.end());
    return pkt;
}
