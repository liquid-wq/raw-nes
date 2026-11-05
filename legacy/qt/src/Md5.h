// Md5.h -- Eigenstaendige MD5-Implementierung nach dem oeffentlichen
// RFC-1321-Algorithmus. Wird gebraucht fuer nes_hash() (ROM-Identifikation
// gegen die RA-Datenbank) und die Verifizierungshashes bei RA-API-Aufrufen
// (awardachievement, submitlbentry).
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace rawnes {

// Berechnet den MD5-Hash der uebergebenen Bytes, gibt ihn als 32-stelligen
// Hex-String (Kleinbuchstaben) zurueck -- entspricht Python's
// hashlib.md5(data).hexdigest().
std::string md5Hex(const uint8_t* data, size_t len);
std::string md5Hex(const std::string& s);
std::string md5Hex(const std::vector<uint8_t>& data);

} // namespace rawnes
