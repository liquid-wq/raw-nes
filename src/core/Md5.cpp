// Md5.cpp -- Implementierung nach dem oeffentlichen RFC-1321-Algorithmus
// (Pseudocode aus der RFC, kein fremder Quellcode uebernommen).
#include "Md5.h"

#include <cstring>

namespace rawnes {
namespace {

using u32 = uint32_t;
using u64 = uint64_t;

inline u32 leftRotate(u32 x, u32 c) { return (x << c) | (x >> (32 - c)); }

// Sinus-Konstanten K[i] = floor(abs(sin(i+1)) * 2^32), i = 0..63 --
// Standardwerte aus RFC 1321 Anhang.
constexpr u32 K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr u32 S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

} // namespace

std::string md5Hex(const uint8_t* data, size_t len) {
    u32 a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    // Nachricht padden: 0x80, dann Nullen, bis Laenge % 64 == 56, dann
    // Original-Bitlaenge als 64-Bit little-endian.
    std::vector<uint8_t> msg(data, data + len);
    u64 bitLen = static_cast<u64>(len) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<uint8_t>(bitLen >> (8 * i)));

    for (size_t chunkStart = 0; chunkStart < msg.size(); chunkStart += 64) {
        u32 M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = static_cast<u32>(msg[chunkStart + i * 4]) |
                   (static_cast<u32>(msg[chunkStart + i * 4 + 1]) << 8) |
                   (static_cast<u32>(msg[chunkStart + i * 4 + 2]) << 16) |
                   (static_cast<u32>(msg[chunkStart + i * 4 + 3]) << 24);
        }
        u32 A = a0, B = b0, C = c0, D = d0;
        for (u32 i = 0; i < 64; ++i) {
            u32 F, g;
            if (i < 16) { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
            else { F = C ^ (B | ~D); g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + leftRotate(F, S[i]);
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }

    uint8_t digest[16];
    u32 words[4] = {a0, b0, c0, d0};
    for (int i = 0; i < 4; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>(words[i] & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((words[i] >> 8) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((words[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>((words[i] >> 24) & 0xFF);
    }

    static const char* hexChars = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out.push_back(hexChars[digest[i] >> 4]);
        out.push_back(hexChars[digest[i] & 0xF]);
    }
    return out;
}

std::string md5Hex(const std::string& s) {
    return md5Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string md5Hex(const std::vector<uint8_t>& data) {
    return md5Hex(data.data(), data.size());
}

} // namespace rawnes
